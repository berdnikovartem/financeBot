#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <curl/curl.h>
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include "ConfigFetcher.h"
#include "MarketAnalyzer.h"
#include "Bitunix.h"

size_t TelegramWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string fetchUrl(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string readBuffer;
    if (!curl) return "";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, TelegramWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return "";
    }
    return readBuffer;
}

void sendTelegramAlert(const std::string& rawMessage) {
    const char* tokenEnv = std::getenv("TELEGRAM_BOT_TOKEN");
    const char* chatIdEnv = std::getenv("MY_CHAT_ID");

    if (!tokenEnv || !chatIdEnv) {
        std::cout << "[Telegram] Error: Missing TELEGRAM_BOT_TOKEN or MY_CHAT_ID environment variables!\n";
        return;
    }

    CURL* curl = curl_easy_init();
    if (!curl) return;

    // Safely encode spaces, colons, and mathematical symbols for URL transmission
    char* encoded = curl_easy_escape(curl, rawMessage.c_str(), static_cast<int>(rawMessage.length()));
    std::string safeMessage = encoded ? encoded : "";
    curl_free(encoded);
    curl_easy_cleanup(curl);

    std::string botToken(tokenEnv);
    std::string chatId(chatIdEnv);
    std::string url = "https://api.telegram.org/bot" + botToken +
                      "/sendMessage?chat_id=" + chatId + "&text=" + safeMessage;

    std::cout << "[Telegram] Sending alert...\n";
    std::string response = fetchUrl(url);
    std::cout << "[Telegram Response]: " << response << std::endl;
}

struct PositionState {
    double quantity = 0.0;
    double average_entry_price = 0.0;
    double last_entry_price = 0.0;
    int entry_count = 0;
};

using PositionBook = std::unordered_map<std::string, PositionState>;

bool persistCycleState(ConfigFetcher& configFetcher, const std::string& email,
                       const std::string& symbol, const PositionBook& positions) {
    const auto longPosition = positions.find(email + ":" + symbol + ":LONG");
    const auto shortPosition = positions.find(email + ":" + symbol + ":SHORT");
    const PositionState empty;
    const PositionState& longState = longPosition == positions.end() ? empty : longPosition->second;
    const PositionState& shortState = shortPosition == positions.end() ? empty : shortPosition->second;
    return configFetcher.persistCycleState(email, symbol,
                                           longState.last_entry_price, longState.entry_count,
                                           shortState.last_entry_price, shortState.entry_count);
}

bool executeOrderAndNotify(BitunixExchange& exchange, const std::string& session_email,
                           const BotParams& bot, const std::string& side,
                           const std::string& signal_type, double rsi, double close_price,
                           PositionBook& positions, ConfigFetcher& configFetcher) {
    if (bot.trading_side != "BOTH" && bot.trading_side != "LONG" && bot.trading_side != "SHORT") {
        std::cerr << " [ERROR] Invalid trading_side for " << bot.symbol
                  << "; refusing entry because allowed side is unknown." << std::endl;
        return false;
    }
    if ((side == "BUY" && bot.trading_side == "SHORT") ||
        (side == "SELL" && bot.trading_side == "LONG")) {
        std::cout << " [DIRECTION FILTER] " << side << " signal ignored for " << bot.symbol
                  << "; strategy is configured for " << bot.trading_side << " only." << std::endl;
        return false;
    }
    if (side != "BUY" && side != "SELL") {
        std::cerr << " [ERROR] Invalid entry direction for " << bot.symbol << std::endl;
        return false;
    }
    std::cout << " [TRADE EXECUTION] Initiating " << side << " order for " << bot.symbol
              << " | Price: " << close_price << std::endl;

    // Define order properties for futures trading
    std::string order_side = side; // "BUY" or "SELL"
    std::string position_side = (side == "BUY") ? "LONG" : "SHORT";
    double quantity = bot.trade_quantity;
    if (!std::isfinite(bot.price_step_percentage) ||
        bot.price_step_percentage < 0.0 || bot.price_step_percentage > 100.0) {
        std::cerr << " [ERROR] Invalid step distance for " << bot.symbol << ": "
                  << bot.price_step_percentage << "%" << std::endl;
        return false;
    }
    const std::string key = session_email + ":" + bot.symbol + ":" + position_side;
    auto& position = positions[key];
    if (bot.price_step_percentage > 0.0 && position.last_entry_price > 0.0) {
        const double step_fraction = bot.price_step_percentage / 100.0;
        const double trigger_price = position_side == "LONG"
                ? position.last_entry_price * (1.0 - step_fraction)
                : position.last_entry_price * (1.0 + step_fraction);
        const bool step_reached = position_side == "LONG"
                ? close_price <= trigger_price
                : close_price >= trigger_price;
        if (!step_reached) {
            std::cout << " [STEP DISTANCE] " << position_side << " add blocked for " << bot.symbol
                      << " | configured: " << bot.price_step_percentage << "%"
                      << " | last entry: " << position.last_entry_price
                      << " | next trigger: " << trigger_price
                      << " | current: " << close_price << std::endl;
            return false;
        }
    }
    if (quantity <= 0.0 || close_price <= 0.0 || bot.take_profit_pct <= 0.0) {
        std::cerr << " [ERROR] Invalid trade quantity or entry price for " << bot.symbol << std::endl;
        return false;
    }

    const double new_quantity = position.quantity + quantity;
    const double average_entry_price =
            ((position.average_entry_price * position.quantity) + (close_price * quantity)) / new_quantity;
    const bool take_profit_per_trade = bot.take_profit_scope == "TRADE";
    const double take_profit_basis = take_profit_per_trade ? close_price : average_entry_price;
    const double target_price = position_side == "LONG"
            ? take_profit_basis * (1.0 + bot.take_profit_pct)
            : take_profit_basis * (1.0 - bot.take_profit_pct);
    const bool success = exchange.placeOrder(bot.symbol, order_side, position_side, "OPEN", quantity);

    if (success) {
        std::cout << " [SUCCESS] " << position_side << " position created for " << bot.symbol << std::endl;
        std::string alert = " POSITION OPENED [" + position_side + "]\n" +
                            "Symbol: " + bot.symbol + "\n" +
                            "Trigger: " + signal_type + "\n" +
                            "Entry Price: $" + std::to_string(close_price) + "\n" +
                            "RSI: " + std::to_string(rsi);
        position.average_entry_price = average_entry_price;
        position.quantity = new_quantity;
        position.last_entry_price = close_price;
        ++position.entry_count;
        if (!persistCycleState(configFetcher, session_email, bot.symbol, positions)) {
            std::cerr << " [WORKER STATE] Trade opened but cycle progress was not persisted for "
                      << bot.symbol << "." << std::endl;
        }
        if (take_profit_per_trade) {
            std::vector<BitunixPosition> open_positions;
            const auto openPositionFound = exchange.getPendingPositions(bot.symbol, open_positions)
                    ? std::find_if(open_positions.begin(), open_positions.end(),
                                   [&position_side](const BitunixPosition& openPosition) {
                                       return openPosition.side == position_side;
                                   })
                    : open_positions.end();
            if (openPositionFound == open_positions.end() ||
                !exchange.placeTradeTakeProfitLimit(bot.symbol, *openPositionFound, quantity,
                                                    target_price, position.entry_count)) {
                std::cerr << " [TP ERROR] Entry opened but standalone LIMIT take-profit could not be "
                          << "confirmed for " << bot.symbol << " " << position_side << "." << std::endl;
                alert += "\nWARNING: standalone LIMIT take-profit placement was not confirmed.";
            }
        } else {
            std::vector<BitunixPosition> open_positions;
            if (!exchange.getPendingPositions(bot.symbol, open_positions) ||
                !exchange.reconcileCycleTakeProfits(bot.symbol, open_positions, bot.take_profit_pct)) {
                std::cerr << " [TP SYNC] Cycle limit TP could not be immediately reconciled for "
                          << bot.symbol << "; worker will retry during its next scan." << std::endl;
            }
        }
        sendTelegramAlert(alert);
        return true;
    } else {
        std::cerr << " [ERROR] Failed to execute " << position_side << " position on exchange!" << std::endl;
        sendTelegramAlert(" EXECUTION FAILED\nFailed to open " + position_side + " position on " + bot.symbol);
        return false;
    }
}

int main() {
    std::cout << "[INFO] Starting Autonomous Database-Driven C++ Crypto Worker..." << std::endl;

    const char* apiBaseUrl = std::getenv("API_BASE_URL");
    const char* workerToken = std::getenv("APP_WORKER_SERVICE_TOKEN");
    if (!apiBaseUrl || std::string(apiBaseUrl).empty()) {
        std::cerr << "[ERROR] API_BASE_URL must be configured." << std::endl;
        return EXIT_FAILURE;
    }
    if (!workerToken || std::string(workerToken).empty()) {
        std::cerr << "[ERROR] APP_WORKER_SERVICE_TOKEN must be configured." << std::endl;
        return EXIT_FAILURE;
    }

    ConfigFetcher config_fetcher(apiBaseUrl);
    MarketAnalyzer market_analyzer;
    std::vector<WorkerSession> sessions;
    PositionBook positions;
    std::unordered_set<std::string> restored_position_keys;

    // Startup retry loop: Wait for Java API & Database readiness
    while (!config_fetcher.fetchWorkerSessions(sessions) || sessions.empty()) {
        std::cout << "[INFO] Waiting for active trading pairs from database..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    std::cout << "[INFO] Initial sync complete. Tracking " << sessions.size() << " user session(s)." << std::endl;

    // Main continuous autonomous loop
    while (true) {
        std::cout << "\n----------------------------------------" << std::endl;
        std::cout << "[INFO] Polling database for latest active configurations..." << std::endl;

        if (config_fetcher.fetchWorkerSessions(sessions)) {
            size_t market_count = 0;
            std::unordered_set<std::string> active_strategy_keys;
            for (const auto& session : sessions) market_count += session.configs.size();
            std::cout << "[INFO] Active markets count: " << market_count
                      << " across " << sessions.size() << " user session(s)" << std::endl;

            for (const auto& session : sessions) {
                BitunixExchange exchange(session.api_key, session.api_secret);
                for (const auto& bot : session.configs) {
                const std::string strategy_key = session.email + ":" + bot.symbol;
                active_strategy_keys.insert(strategy_key);
                const std::string long_key = strategy_key + ":LONG";
                const std::string short_key = strategy_key + ":SHORT";
                if (restored_position_keys.insert(long_key).second) {
                    auto& state = positions[long_key];
                    if (bot.last_long_entry_price > 0.0 && bot.long_trade_count > 0) {
                        state.last_entry_price = bot.last_long_entry_price;
                        state.entry_count = bot.long_trade_count;
                    }
                }
                if (restored_position_keys.insert(short_key).second) {
                    auto& state = positions[short_key];
                    if (bot.last_short_entry_price > 0.0 && bot.short_trade_count > 0) {
                        state.last_entry_price = bot.last_short_entry_price;
                        state.entry_count = bot.short_trade_count;
                    }
                }
                std::cout << "\n[SCAN] Evaluating " << bot.symbol
                          << " | RSI indicators: " << bot.rsi_indicators.size()
                          << " | Hammer indicators: " << bot.hammer_indicators.size()
                          << " | Volume indicators: " << bot.volume_indicators.size()
                          << " | Trading side: " << bot.trading_side
                          << " | Step distance: " << bot.price_step_percentage << "%" << std::endl;

                std::vector<BitunixPosition> open_positions;
                if (!exchange.getPendingPositions(bot.symbol, open_positions)) {
                    std::cerr << " [POSITION MONITOR] Could not verify " << bot.symbol
                              << "; skipping this scan to avoid duplicating an unknown open position."
                              << std::endl;
                    continue;
                }

                bool has_long_position = false;
                bool has_short_position = false;
                bool cycle_state_changed = false;
                for (const auto& open_position : open_positions) {
                    const std::string position_key = strategy_key + ":" + open_position.side;
                    auto& position = positions[position_key];
                    position.quantity = open_position.quantity;
                    position.average_entry_price = open_position.average_open_price;
                    if (position.last_entry_price <= 0.0) {
                        position.last_entry_price = open_position.average_open_price;
                        if (position.entry_count <= 0) position.entry_count = 1;
                        cycle_state_changed = true;
                    }
                    if (open_position.side == "LONG") has_long_position = true;
                    if (open_position.side == "SHORT") has_short_position = true;
                }
                for (const auto& [side, exists] :
                     std::vector<std::pair<std::string, bool>>{{"LONG", has_long_position},
                                                               {"SHORT", has_short_position}}) {
                    const std::string position_key = strategy_key + ":" + side;
                    if (!exists) {
                        auto position = positions.find(position_key);
                        if (position != positions.end() &&
                            (position->second.last_entry_price > 0.0 ||
                             position->second.entry_count > 0 ||
                             position->second.quantity > 0.0)) {
                            position->second = PositionState{};
                            cycle_state_changed = true;
                            std::cout << " [POSITION MONITOR] No " << side << " position remains for "
                                      << bot.symbol << "; resetting that trade cycle." << std::endl;
                        }
                    }
                }
                if (cycle_state_changed &&
                    !persistCycleState(config_fetcher, session.email, bot.symbol, positions)) {
                    std::cerr << " [WORKER STATE] Could not sync recovered/reset cycle progress for "
                              << bot.symbol << "." << std::endl;
                }

                if (!std::isfinite(bot.take_profit_pct) ||
                    bot.take_profit_pct <= 0.0 || bot.take_profit_pct >= 1.0 ||
                    (bot.take_profit_scope != "CYCLE" && bot.take_profit_scope != "TRADE")) {
                    std::cerr << " [ERROR] Invalid take-profit setting for " << bot.symbol
                              << "; expected 0 < percentage < 100 and scope CYCLE or TRADE."
                              << std::endl;
                    continue;
                }
                if (!exchange.reconcileCycleTakeProfits(
                            bot.symbol, open_positions, bot.take_profit_pct,
                            bot.take_profit_scope == "CYCLE")) {
                    std::cerr << " [TP SYNC] Could not reconcile limit take-profits for "
                              << bot.symbol << "; deferring new signals until the next poll."
                              << std::endl;
                    continue;
                }
                if (bot.trading_side != "BOTH" && bot.trading_side != "LONG" &&
                    bot.trading_side != "SHORT") {
                    std::cerr << " [ERROR] Invalid trading_side for " << bot.symbol
                              << "; signals disabled until the strategy side is corrected."
                              << std::endl;
                    continue;
                }

                bool is_oversold = !bot.rsi_indicators.empty();
                bool is_overbought = !bot.rsi_indicators.empty();
                bool all_rsi_valid = !bot.rsi_indicators.empty();
                double signal_rsi = -1.0;
                double last_close = 0.0;
                for (const auto& indicator : bot.rsi_indicators) {
                    std::vector<double> closes;
                    int kline_limit = std::max(50, indicator.period * 3);
                    if (!market_analyzer.fetchClosingPrices(bot.symbol, indicator.timeframe, kline_limit, closes)) {
                        std::cerr << "       -> [ERROR] Failed to fetch RSI klines for "
                                  << indicator.timeframe << std::endl;
                        all_rsi_valid = false;
                        continue;
                    }
                    const double rsi = market_analyzer.calculateWildersRSI(closes, indicator.period);
                    if (rsi < 0.0) {
                        all_rsi_valid = false;
                        continue;
                    }
                    std::cout << "       -> RSI (" << indicator.timeframe << ", "
                              << indicator.period << "): " << rsi
                              << " | oversold <= " << indicator.oversold
                              << ", overbought >= " << indicator.overbought << std::endl;
                    if (signal_rsi < 0.0) signal_rsi = rsi;
                    is_oversold = is_oversold && rsi <= indicator.oversold;
                    is_overbought = is_overbought && rsi >= indicator.overbought;
                    if (last_close <= 0.0 && !closes.empty()) last_close = closes.back();
                }

                if (signal_rsi >= 0.0 && all_rsi_valid) {
                        bool all_volume_surges = true;
                        for (const auto& indicator : bot.volume_indicators) {
                            const bool volume_surge = market_analyzer.fetchVolumeSurge(
                                bot.symbol, indicator.timeframe, indicator.multiplier);
                            all_volume_surges = all_volume_surges && volume_surge;
                        }
                        bool all_bullish_hammers = bot.hammer_indicators.empty();
                        bool all_bearish_hammers = bot.hammer_indicators.empty();
                        for (const auto& hammer : bot.hammer_indicators) {
                        bool bullish_hammer = false;
                        bool bearish_hammer = false;
                        std::string kline_url = "https://api.binance.com/api/v3/klines?symbol=" + bot.symbol + "&interval=" + hammer.timeframe + "&limit=1";
                        std::string kline_resp = fetchUrl(kline_url);

                        if (!kline_resp.empty()) {
                            try {
                                auto j = nlohmann::json::parse(kline_resp);
                                if (!j.empty()) {
                                    auto latest_candle = j[0];
                                    long long open_time_ms = latest_candle[0].get<long long>();
                                    double open  = std::stod(latest_candle[1].get<std::string>());
                                    double high  = std::stod(latest_candle[2].get<std::string>());
                                    double low   = std::stod(latest_candle[3].get<std::string>());
                                    double close = std::stod(latest_candle[4].get<std::string>());

                                    int threshold_seconds = 90;
                                    if (hammer.timeframe == "1m") threshold_seconds = 30;
                                    else if (hammer.timeframe == "3m") threshold_seconds = 90;
                                    else if (hammer.timeframe == "5m") threshold_seconds = 150;
                                    else if (hammer.timeframe == "15m") threshold_seconds = 450;

                                    if (market_analyzer.sufficientTimePass(open_time_ms, threshold_seconds)) {

                                        bullish_hammer = market_analyzer.hammerPattern(
                                            open, high, low, close, true, hammer.multiplier);
                                        bearish_hammer = market_analyzer.hammerPattern(
                                            open, high, low, close, false, hammer.multiplier);
                                    }
                                    }
                            } catch (const std::exception& e) {
                                std::cerr << "       -> [ERROR] Failed to parse live candle: " << e.what() << std::endl;
                            }
                        }
                        all_bullish_hammers = all_bullish_hammers && bullish_hammer;
                        all_bearish_hammers = all_bearish_hammers && bearish_hammer;
                        }

                        // ==========================================
                        // STANDARD RSI FALLBACK TRADE EXECUTION

                        const bool long_signal = (bot.trading_side == "LONG" || bot.trading_side == "BOTH") &&
                                is_oversold && all_volume_surges && all_bullish_hammers;
                        const bool short_signal = (bot.trading_side == "SHORT" || bot.trading_side == "BOTH") &&
                                is_overbought && all_volume_surges && all_bearish_hammers;
                        const char* volume_status = bot.volume_indicators.empty()
                                ? "disabled" : all_volume_surges ? "yes" : "no";
                        const char* bullish_hammer_status = bot.hammer_indicators.empty()
                                ? "disabled" : all_bullish_hammers ? "yes" : "no";
                        const char* bearish_hammer_status = bot.hammer_indicators.empty()
                                ? "disabled" : all_bearish_hammers ? "yes" : "no";
                        std::cout << "       -> Conditions: long_rsi=" << (is_oversold ? "yes" : "no")
                                  << ", short_rsi=" << (is_overbought ? "yes" : "no")
                                  << ", volume=" << volume_status
                                  << ", bullish_hammer=" << bullish_hammer_status
                                  << ", bearish_hammer=" << bearish_hammer_status
                                  << std::endl;
                        if (long_signal) {
                            std::cout << " [SIGNAL - BUY] " << bot.symbol << " All configured indicators are bullish." << std::endl;
                            executeOrderAndNotify(exchange, session.email, bot, "BUY",
                                                  "All configured indicators bullish", signal_rsi,
                                                  last_close, positions, config_fetcher);
                        } else if (short_signal) {
                            std::cout << " [SIGNAL - SELL] " << bot.symbol << " All configured indicators are bearish." << std::endl;
                            executeOrderAndNotify(exchange, session.email, bot, "SELL",
                                                  "All configured indicators bearish", signal_rsi,
                                                  last_close, positions, config_fetcher);
                        } else {
                            std::cout << "       -> Not all configured indicators agree. Holding position." << std::endl;
                        }

                } else {
                    std::cerr << "       -> [WARNING] No configured RSI produced a valid value." << std::endl;
                }
            }
            }
            for (auto position = positions.begin(); position != positions.end();) {
                const bool strategyStillActive = std::any_of(
                        active_strategy_keys.begin(), active_strategy_keys.end(),
                        [&position](const std::string& key) {
                            return position->first.rfind(key + ":", 0) == 0;
                        });
                if (!strategyStillActive) {
                    restored_position_keys.erase(position->first);
                    position = positions.erase(position);
                }
                else ++position;
            }
        } else {
            std::cerr << "[WARNING] Failed to sync active configs from API. Retrying next cycle..." << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    return 0;
}