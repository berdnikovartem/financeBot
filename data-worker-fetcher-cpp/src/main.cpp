#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <unordered_map>
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
};

using PositionBook = std::unordered_map<std::string, PositionState>;
using TakeProfitBook = std::unordered_map<std::string, std::string>;

bool executeOrderAndNotify(BitunixExchange& exchange, const BotParams& bot, const std::string& side,
                           const std::string& signal_type, double rsi, double close_price,
                           PositionBook& positions, TakeProfitBook& take_profit_orders) {
    std::cout << " [TRADE EXECUTION] Initiating " << side << " order for " << bot.symbol
              << " | Price: " << close_price << std::endl;

    // Define order properties for futures trading
    std::string order_side = side; // "BUY" or "SELL"
    std::string position_side = (side == "BUY") ? "LONG" : "SHORT";
    double quantity = bot.trade_quantity;
    if (quantity <= 0.0 || close_price <= 0.0 || bot.take_profit_pct <= 0.0) {
        std::cerr << " [ERROR] Invalid trade quantity or entry price for " << bot.symbol << std::endl;
        return false;
    }

    // Call placeOrder with all 4 expected parameters
    bool success = exchange.placeOrder(bot.symbol, order_side, position_side, quantity);

    if (success) {
        std::cout << " [SUCCESS] " << position_side << " position created for " << bot.symbol << std::endl;
        std::string alert = " POSITION OPENED [" + position_side + "]\n" +
                            "Symbol: " + bot.symbol + "\n" +
                            "Trigger: " + signal_type + "\n" +
                            "Entry Price: $" + std::to_string(close_price) + "\n" +
                            "RSI: " + std::to_string(rsi);
        auto& position = positions[bot.symbol + ":" + position_side];
        const double new_quantity = position.quantity + quantity;
        position.average_entry_price =
                ((position.average_entry_price * position.quantity) + (close_price * quantity)) / new_quantity;
        position.quantity = new_quantity;
        const double target_price = position_side == "LONG"
                ? position.average_entry_price * (1.0 + bot.take_profit_pct)
                : position.average_entry_price * (1.0 - bot.take_profit_pct);
        const std::string key = bot.symbol + ":" + position_side;
        auto existing_order = take_profit_orders.find(key);
        if (existing_order != take_profit_orders.end()) {
            if (!exchange.cancelTakeProfit(bot.symbol, existing_order->second)) {
                std::cerr << " [ERROR] Failed to cancel existing take-profit for " << key << std::endl;
                return false;
            }
        }
        std::string new_order_id;
        const std::string tp_side = position_side == "LONG" ? "SELL" : "BUY";
        if (!exchange.placeTakeProfit(bot.symbol, tp_side, position_side, position.quantity,
                                      target_price, new_order_id)) {
            std::cerr << " [ERROR] Failed to place updated take-profit for " << key << std::endl;
            return false;
        }
        take_profit_orders[key] = new_order_id;
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
    TakeProfitBook take_profit_orders;

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
            for (const auto& session : sessions) market_count += session.configs.size();
            std::cout << "[INFO] Active markets count: " << market_count
                      << " across " << sessions.size() << " user session(s)" << std::endl;

            for (const auto& session : sessions) {
                BitunixExchange exchange(session.api_key, session.api_secret);
                for (const auto& bot : session.configs) {
                std::cout << "\n[SCAN] Evaluating " << bot.symbol
                          << " | RSI indicators: " << bot.rsi_indicators.size()
                          << " | Hammer indicators: " << bot.hammer_indicators.size()
                          << " | Volume indicators: " << bot.volume_indicators.size() << std::endl;

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

                        const bool long_signal = is_oversold && all_volume_surges && all_bullish_hammers;
                        const bool short_signal = is_overbought && all_volume_surges && all_bearish_hammers;
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
                            executeOrderAndNotify(exchange, bot, "BUY", "All configured indicators bullish", signal_rsi, last_close, positions, take_profit_orders);
                        } else if (short_signal) {
                            std::cout << " [SIGNAL - SELL] " << bot.symbol << " All configured indicators are bearish." << std::endl;
                            executeOrderAndNotify(exchange, bot, "SELL", "All configured indicators bearish", signal_rsi, last_close, positions, take_profit_orders);
                        } else {
                            std::cout << "       -> Not all configured indicators agree. Holding position." << std::endl;
                        }

                } else {
                    std::cerr << "       -> [WARNING] No configured RSI produced a valid value." << std::endl;
                }
            }
            }
        } else {
            std::cerr << "[WARNING] Failed to sync active configs from API. Retrying next cycle..." << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    return 0;
}