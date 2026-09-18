#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
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

// --- HELPER: Struct to hold full candlestick data ---
struct Candle {
    long long openTimeMs;
    double open;
    double high;
    double low;
    double close;
    double volume;
};

// --- HELPER: cURL write callback ---
static size_t WriteCallbackHelper(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// --- HELPER: Fetch OHLC data for patterns and timestamps ---
std::vector<Candle> fetchFullCandles(const std::string& symbol, const std::string& timeframe, int limit) {
    std::vector<Candle> candles;
    CURL* curl = curl_easy_init();
    if (!curl) return candles;

    std::string readBuffer;
    std::string endpoint = "https://api.binance.com/api/v3/klines?symbol=" + symbol + "&interval=" + timeframe + "&limit=" + std::to_string(limit);

    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackHelper);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK) {
        try {
            // EXPLICITLY using nlohmann::json to prevent scope/template compilation errors
            nlohmann::json klinesJson = nlohmann::json::parse(readBuffer);
            if (klinesJson.is_array()) {
                for (const auto& kline : klinesJson) {
                    if (kline.is_array() && kline.size() >= 6) {
                        Candle c;
                        c.openTimeMs = kline[0].get<long long>();
                        c.open       = std::stod(kline[1].get<std::string>());
                        c.high       = std::stod(kline[2].get<std::string>());
                        c.low        = std::stod(kline[3].get<std::string>());
                        c.close      = std::stod(kline[4].get<std::string>());
                        c.volume     = std::stod(kline[5].get<std::string>());
                        candles.push_back(c);
                    }
                }
            }
        } catch (...) {} // Ignore JSON parse errors and return whatever was successfully parsed
    }
    return candles;
}

// --- HELPER: Determine sufficient time threshold ---
int getThresholdSeconds(const std::string& timeframe) {
    if (timeframe == "1m") return 30;
    if (timeframe == "3m") return 90;
    if (timeframe == "5m") return 150;
    if (timeframe == "15m") return 450;
    if (timeframe == "1h") return 1800;
    if (timeframe == "4h") return 7200;
    return 60; // Default fallback
}

// --- TRADE EXECUTION ROUTINE ---
bool executeOrderAndNotify(BitunixExchange& exchange, const BotParams& bot, const std::string& side,
                           const std::string& signal_type, double rsi, double close_price) {
    std::cout << " [TRADE EXECUTION] Initiating " << side << " order for " << bot.symbol
              << " | Price: " << close_price << std::endl;

    std::string order_side = side;
    std::string position_side;
    if (signal_type == "Take profit on long position") {
        position_side = "LONG";
    } else if (signal_type == "Take profit on short position") {
        position_side = "SHORT";
    } else {
        position_side = (side == "BUY") ? "LONG" : "SHORT";
    }
    double quantity = bot.trade_quantity;
    double maxQuantity = 1.0;
    try {
        if (const char* configuredMax = std::getenv("MAX_TRADE_QUANTITY")) maxQuantity = std::stod(configuredMax);
    } catch (const std::exception&) {
        std::cerr << " [ERROR] Invalid TRADE_QUANTITY or MAX_TRADE_QUANTITY." << std::endl;
        return false;
    }
    if (!std::isfinite(quantity) || !std::isfinite(maxQuantity) || quantity <= 0.0 || quantity > maxQuantity || maxQuantity <= 0.0) {
        std::cerr << " [ERROR] Refusing order outside the configured quantity limit: quantity="
                  << quantity << ", max=" << maxQuantity << std::endl;
        return false;
    }

    bool opening = signal_type.rfind("Take profit", 0) != 0;
    bool success = exchange.placeOrder(bot.symbol, order_side, position_side, quantity, opening);

    if (success) {
        std::cout << " [SUCCESS] " << position_side << " position created for " << bot.symbol << std::endl;
        std::string alert = " POSITION OPENED [" + position_side + "]\n" +
                            "Symbol: " + bot.symbol + "\n" +
                            "Trigger: " + signal_type + "\n" +
                            "Entry Price: $" + std::to_string(close_price) + "\n" +
                            "RSI: " + std::to_string(rsi);
        sendTelegramAlert(alert);
        return true;
    } else {
        std::cerr << " [ERROR] Failed to execute " << position_side << " position on exchange!" << std::endl;
        return false;
    }
}

int main() {
    std::cout << "Starting Crypto Trading Worker..." << std::endl;

    BitunixExchange exchange;
    MarketAnalyzer analyzer;

    // NOTE: Make sure this matches your internal Docker network URL for the Java API
    ConfigFetcher fetcher("http://java-api:8080");

    if (std::getenv("WORKER_REFRESH_TOKEN") && !fetcher.refreshAccessToken()) {
        std::cerr << " [ERROR] Unable to refresh worker access token." << std::endl;
    }
    WorkerSession workerSession;
    bool credentialsLoaded = fetcher.fetchWorkerSession(workerSession);
    if (credentialsLoaded) {
        exchange.setCredentials(workerSession.apiKey, workerSession.apiSecret);
    } else {
        std::cerr << " [ERROR] Unable to fetch the authenticated user's Bitunix credentials. Trading is disabled until credentials load." << std::endl;
    }
    auto lastCredentialRefresh = std::chrono::steady_clock::now() - std::chrono::minutes(1);
    std::unordered_set<std::string> configuredTakeProfits;
    std::unordered_map<std::string, int> missingPositionPolls;

    while (true) {
        try {
            if (!credentialsLoaded) {
                WorkerSession retrySession;
                credentialsLoaded = fetcher.fetchWorkerSession(retrySession);
                if (credentialsLoaded) {
                    exchange.setCredentials(retrySession.apiKey, retrySession.apiSecret);
                    std::cout << " [WORKER SESSION] Bitunix credentials loaded successfully." << std::endl;
                }
                lastCredentialRefresh = std::chrono::steady_clock::now();
            } else if (std::chrono::steady_clock::now() - lastCredentialRefresh >= std::chrono::minutes(1)) {
                WorkerSession refreshedSession;
                credentialsLoaded = fetcher.fetchWorkerSession(refreshedSession);
                if (credentialsLoaded) {
                    exchange.setCredentials(refreshedSession.apiKey, refreshedSession.apiSecret);
                    std::cout << " [WORKER SESSION] Bitunix credentials refreshed from the API." << std::endl;
                } else {
                    std::cerr << " [ERROR] Bitunix credentials could not be refreshed. Trading is paused." << std::endl;
                }
                lastCredentialRefresh = std::chrono::steady_clock::now();
            }
            if (!credentialsLoaded) {
                std::cerr << " [WARNING] Trading loop paused until Java API is reachable and Bitunix credentials load." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(10));
                continue;
            }
            std::vector<BotParams> bots;
            bool fetchSuccess = fetcher.fetchActiveConfigs(bots);

            if (!fetchSuccess) {
                std::cerr << " [WARNING] Failed to fetch bot configs from Java API." << std::endl;
            } else {
                for (const auto& bot : bots) {

                    std::cout << " [MONITORING] " << bot.symbol << " | RSI period: " << bot.rsi_period
                              << " | oversold: " << bot.rsi_oversold << " | overbought: " << bot.rsi_overbought
                              << " | side: " << bot.trading_side << std::endl;

                    int limit = std::max(50, bot.rsi_period * 3);

                    // Fetch FULL candlestick data (OHLC + time)
                    std::vector<Candle> candles = fetchFullCandles(bot.symbol, bot.timeframe, limit);

                    if (candles.empty()) {
                        std::cerr << " [WARNING] No kline data returned for " << bot.symbol << std::endl;
                        continue;
                    }

                    // Extract just the closing prices to feed into your Wilders RSI calculator
                    std::vector<double> closes;
                    for (const auto& c : candles) {
                        closes.push_back(c.close);
                    }

                    // 1. Calculate RSI
                    double rsi = analyzer.calculateWildersRSI(closes, bot.rsi_period);

                    Candle currentCandle = candles.back();
                    int thresholdSeconds = getThresholdSeconds(bot.timeframe);

                    std::vector<BitunixPosition> positions;
                    if (!exchange.fetchPositions(bot.symbol, positions)) {
                        std::cerr << " [WARNING] Could not reconcile " << bot.symbol << " with Bitunix." << std::endl;
                        continue;
                    }

                    const BitunixPosition* longPosition = nullptr;
                    const BitunixPosition* shortPosition = nullptr;
                    for (const auto& position : positions) {
                        if (position.side == "LONG") longPosition = &position;
                        if (position.side == "SHORT") shortPosition = &position;
                    }

                    // The exchange is authoritative after restarts, manual trades, or partial fills.
                    std::string longPositionKey = bot.symbol + ":LONG";
                    std::string shortPositionKey = bot.symbol + ":SHORT";
                    if (longPosition) {
                        missingPositionPolls.erase(longPositionKey);
                    } else if (bot.last_long_price > 0.0) {
                        int missingPolls = ++missingPositionPolls[longPositionKey];
                        if (missingPolls >= 3) {
                            fetcher.updateLastTradePrice(bot.id, "LONG", 0.0);
                            std::cout << " [RECONCILIATION] Cleared stale LONG state for " << bot.symbol
                                      << " after " << missingPolls << " missing position polls" << std::endl;
                            missingPositionPolls.erase(longPositionKey);
                        } else {
                            std::cout << " [RECONCILIATION] Bitunix has not reported LONG " << bot.symbol
                                      << " yet (poll " << missingPolls << "/3); keeping state" << std::endl;
                        }
                    }
                    if (shortPosition) {
                        missingPositionPolls.erase(shortPositionKey);
                    } else if (bot.last_short_price > 0.0) {
                        int missingPolls = ++missingPositionPolls[shortPositionKey];
                        if (missingPolls >= 3) {
                            fetcher.updateLastTradePrice(bot.id, "SHORT", 0.0);
                            std::cout << " [RECONCILIATION] Cleared stale SHORT state for " << bot.symbol
                                      << " after " << missingPolls << " missing position polls" << std::endl;
                            missingPositionPolls.erase(shortPositionKey);
                        } else {
                            std::cout << " [RECONCILIATION] Bitunix has not reported SHORT " << bot.symbol
                                      << " yet (poll " << missingPolls << "/3); keeping state" << std::endl;
                        }
                    }
                    double longAveragePrice = longPosition ? longPosition->averageOpenPrice : bot.last_long_price;
                    double shortAveragePrice = shortPosition ? shortPosition->averageOpenPrice : bot.last_short_price;
                    if (bot.take_profit_percentage > 0.0 && longPosition) {
                        double target = longAveragePrice * (1.0 + bot.take_profit_percentage / 100.0);
                        if (configuredTakeProfits.find(longPosition->positionId) == configuredTakeProfits.end()
                            && exchange.placeTakeProfit(*longPosition, target)) {
                            configuredTakeProfits.insert(longPosition->positionId);
                        }
                    }
                    if (bot.take_profit_percentage > 0.0 && shortPosition) {
                        double target = shortAveragePrice * (1.0 - bot.take_profit_percentage / 100.0);
                        if (configuredTakeProfits.find(shortPosition->positionId) == configuredTakeProfits.end()
                            && exchange.placeTakeProfit(*shortPosition, target)) {
                            configuredTakeProfits.insert(shortPosition->positionId);
                        }
                    }

                    // 2. Check sufficient time pass
                    if (credentialsLoaded && analyzer.sufficientTimePass(bot.symbol, currentCandle.openTimeMs, thresholdSeconds)) {

                        bool isOversold = (rsi <= bot.rsi_oversold);
                        bool isOverbought = (rsi >= bot.rsi_overbought);

                        // 3. Calculate Hammer / Shooting Star (true = Bullish, false = Bearish)
                        bool isHammer = analyzer.hammerPattern(currentCandle.open, currentCandle.high, currentCandle.low, currentCandle.close, true, bot.hammer_multiplier);
                        bool isShootingStar = analyzer.hammerPattern(currentCandle.open, currentCandle.high, currentCandle.low, currentCandle.close, false, bot.hammer_multiplier);

                        double averageVolume = 0.0;
                        int volumeSamples = 0;
                        for (size_t index = candles.size() > 21 ? candles.size() - 21 : 0; index + 1 < candles.size(); ++index) {
                            averageVolume += candles[index].volume;
                            ++volumeSamples;
                        }
                        averageVolume = volumeSamples > 0 ? averageVolume / volumeSamples : 0.0;
                        bool hasVolumeSurge = analyzer.volumeSurge(currentCandle.volume, averageVolume, bot.volume_surge_multiplier);

                        double currentPrice = currentCandle.close;
                        double stepPct = bot.price_step_percentage / 100.0;
                        double takeProfitPct = bot.take_profit_percentage / 100.0;

                        bool longTakeProfitHit = (longAveragePrice > 0.0) && (currentPrice >= longAveragePrice * (1.0 + takeProfitPct));
                        bool shortTakeProfitHit = (shortAveragePrice > 0.0) && (currentPrice <= shortAveragePrice * (1.0 - takeProfitPct));

                        if (longAveragePrice > 0.0 && bot.take_profit_percentage > 0.0) {
                            std::cout << " [" << bot.symbol << "] LONG take-profit target: "
                                      << longAveragePrice * (1.0 + takeProfitPct)
                                      << " | current: " << currentPrice << std::endl;
                        }
                        if (shortAveragePrice > 0.0 && bot.take_profit_percentage > 0.0) {
                            std::cout << " [" << bot.symbol << "] SHORT take-profit target: "
                                      << shortAveragePrice * (1.0 - takeProfitPct)
                                      << " | current: " << currentPrice << std::endl;
                        }

                        // 4. Calculate DCA grid distance
                        bool longStepCleared = (longAveragePrice == 0.0) ||
                                               (currentPrice <= longAveragePrice * (1.0 - stepPct));

                        bool shortStepCleared = (shortAveragePrice == 0.0) ||
                                                (currentPrice >= shortAveragePrice * (1.0 + stepPct));

                        // --- EXECUTION LOGIC ---
                        bool longAllowed = bot.trading_side == "LONG" || bot.trading_side == "BOTH";
                        bool shortAllowed = bot.trading_side == "SHORT" || bot.trading_side == "BOTH";

                        if (longAllowed && longTakeProfitHit && longPosition
                            && configuredTakeProfits.find(longPosition->positionId) == configuredTakeProfits.end()) {
                            BotParams closeBot = bot;
                            closeBot.trade_quantity = longPosition->quantity;
                            if (executeOrderAndNotify(exchange, closeBot, "SELL", "Take profit on long position", 0.0, currentPrice)) {
                                fetcher.updateLastTradePrice(bot.id, "LONG", 0.0);
                                analyzer.markCandleSignaled(bot.symbol, currentCandle.openTimeMs);
                            }
                        }
                        else if (shortAllowed && shortTakeProfitHit && shortPosition
                            && configuredTakeProfits.find(shortPosition->positionId) == configuredTakeProfits.end()) {
                            BotParams closeBot = bot;
                            closeBot.trade_quantity = shortPosition->quantity;
                            if (executeOrderAndNotify(exchange, closeBot, "BUY", "Take profit on short position", 0.0, currentPrice)) {
                                fetcher.updateLastTradePrice(bot.id, "SHORT", 0.0);
                                analyzer.markCandleSignaled(bot.symbol, currentCandle.openTimeMs);
                            }
                        }
                        else if (longAllowed && isOversold && isHammer && hasVolumeSurge) {
                            if (longStepCleared) {
                                if (executeOrderAndNotify(exchange, bot, "BUY", "RSI Oversold + Bullish Hammer", rsi, currentPrice)) {
                                    fetcher.updateLastTradePrice(bot.id, "LONG", currentPrice);
                                    analyzer.markCandleSignaled(bot.symbol, currentCandle.openTimeMs);
                                }
                            } else {
                                std::cout << " [" << bot.symbol << "] LONG condition met, but price (" << currentPrice
                                          << ") hasn't dropped " << bot.price_step_percentage
                                          << "% from last entry (" << bot.last_long_price << ")." << std::endl;
                            }
                        }
                        else if (shortAllowed && isOverbought && isShootingStar && hasVolumeSurge) {
                            if (shortStepCleared) {
                                if (executeOrderAndNotify(exchange, bot, "SELL", "RSI Overbought + Bearish Shooting Star", rsi, currentPrice)) {
                                    fetcher.updateLastTradePrice(bot.id, "SHORT", currentPrice);
                                    analyzer.markCandleSignaled(bot.symbol, currentCandle.openTimeMs);
                                }
                            } else {
                                std::cout << " [" << bot.symbol << "] SHORT condition met, but price (" << currentPrice
                                          << ") hasn't risen " << bot.price_step_percentage
                                          << "% from last entry (" << bot.last_short_price << ")." << std::endl;
                            }
                        }
                        else {
                            std::cout << " [" << bot.symbol << "] Holding. RSI: " << rsi
                                      << " | Hammer: " << (isHammer ? "Yes" : "No")
                                      << " | Shooting Star: " << (isShootingStar ? "Yes" : "No")
                                      << " | Volume surge: " << (hasVolumeSurge ? "Yes" : "No")
                                      << std::endl;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << " [FATAL ERROR] Exception in main loop: " << e.what() << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    return 0;
}