#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
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

void executeOrderAndNotify(BitunixExchange& exchange, const BotParams& bot, const std::string& side, const std::string& signal_type, double rsi, double close_price) {
    std::cout << " [TRADE EXECUTION] Initiating " << side << " order for " << bot.symbol
              << " | Price: " << close_price << std::endl;

    // Define order properties for futures trading
    std::string order_side = side; // "BUY" or "SELL"
    std::string position_side = (side == "BUY") ? "LONG" : "SHORT";
    double quantity = 0.001; // Adjust or use your desired position size/amount

    // Call placeOrder with all 4 expected parameters
    bool success = exchange.placeOrder(bot.symbol, order_side, position_side, quantity);

    if (success) {
        std::cout << " [SUCCESS] " << position_side << " position created for " << bot.symbol << std::endl;
        std::string alert = " POSITION OPENED [" + position_side + "]\n" +
                            "Symbol: " + bot.symbol + "\n" +
                            "Trigger: " + signal_type + "\n" +
                            "Entry Price: $" + std::to_string(close_price) + "\n" +
                            "RSI: " + std::to_string(rsi);
        sendTelegramAlert(alert);
    } else {
        std::cerr << " [ERROR] Failed to execute " << position_side << " position on exchange!" << std::endl;
        sendTelegramAlert(" EXECUTION FAILED\nFailed to open " + position_side + " position on " + bot.symbol);
    }
}

int main() {
    std::cout << "[INFO] Starting Autonomous Database-Driven C++ Crypto Worker..." << std::endl;

    ConfigFetcher config_fetcher("http://java-api:8080");
    MarketAnalyzer market_analyzer;
    BitunixExchange exchange;
    std::vector<BotParams> active_bots;

    // Startup retry loop: Wait for Java API & Database readiness
    while (!config_fetcher.fetchActiveConfigs(active_bots) || active_bots.empty()) {
        std::cout << "[INFO] Waiting for active trading pairs from database..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    std::cout << "[INFO] Initial sync complete. Tracking " << active_bots.size() << " market(s)." << std::endl;

    // Main continuous autonomous loop
    while (true) {
        std::cout << "\n----------------------------------------" << std::endl;
        std::cout << "[INFO] Polling database for latest active configurations..." << std::endl;

        if (config_fetcher.fetchActiveConfigs(active_bots)) {
            std::cout << "[INFO] Active markets count: " << active_bots.size() << std::endl;

            for (const auto& bot : active_bots) {
                std::cout << "\n[SCAN] Evaluating " << bot.symbol
                          << " | Timeframe: " << bot.timeframe
                          << " | RSI Period: " << bot.rsi_period << std::endl;

                std::vector<double> closes;
                int kline_limit = bot.rsi_period * 3;
                if (kline_limit < 50) kline_limit = 50;

                if (market_analyzer.fetchClosingPrices(bot.symbol, bot.timeframe, kline_limit, closes)) {
                    double rsi = market_analyzer.calculateWildersRSI(closes, bot.rsi_period);

                    if (rsi >= 0.0) {
                        std::cout << "       -> Current RSI (" << bot.rsi_period << "): " << rsi << std::endl;

                        bool is_oversold = (rsi <= bot.rsi_oversold);
                        bool is_overbought = (rsi >= bot.rsi_overbought);
                        bool early_signal_fired = false;

                        std::string kline_url = "https://api.binance.com/api/v3/klines?symbol=" + bot.symbol + "&interval=" + bot.timeframe + "&limit=1";
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
                                    if (bot.timeframe == "1m") threshold_seconds = 30;
                                    else if (bot.timeframe == "3m") threshold_seconds = 90;
                                    else if (bot.timeframe == "5m") threshold_seconds = 150;
                                    else if (bot.timeframe == "15m") threshold_seconds = 450;

                                    if (market_analyzer.sufficientTimePass(open_time_ms, threshold_seconds)) {

                                        // 1. EARLY BUY (LONG POSITION)
                                        if (market_analyzer.hammerPattern(open, high, low, close, true)) {
                                            std::cout << " [EARLY SIGNAL - BUY] " << bot.symbol << " Bullish Hammer Detected!" << std::endl;
                                            std::string condition = is_oversold ? "Oversold + Bullish Hammer" : "Bullish Hammer";

                                            // Execute LONG order
                                            executeOrderAndNotify(exchange, bot, "BUY", condition, rsi, close);
                                            early_signal_fired = true;
                                        }
                                        // 2. EARLY SELL (SHORT POSITION)
                                        else if (market_analyzer.hammerPattern(open, high, low, close, false)) {
                                            std::cout << " [EARLY SIGNAL - SELL] " << bot.symbol << " Bearish Shooting Star Detected!" << std::endl;
                                            std::string condition = is_overbought ? "Overbought + Bearish Shooting Star" : "Bearish Shooting Star";

                                            // Execute SHORT order
                                            executeOrderAndNotify(exchange, bot, "SELL", condition, rsi, close);
                                            early_signal_fired = true;
                                        }
                                    }
                                }
                            } catch (const std::exception& e) {
                                std::cerr << "       -> [ERROR] Failed to parse live candle: " << e.what() << std::endl;
                            }
                        }

                        // ==========================================
                        // STANDARD RSI FALLBACK TRADE EXECUTION

                        if (!early_signal_fired) {
                            double last_close = closes.back();
                            if (is_oversold) {
                                std::cout << " [SIGNAL - BUY] " << bot.symbol << " is OVERSOLD! RSI: " << rsi << std::endl;
                                executeOrderAndNotify(exchange, bot, "BUY", "Oversold RSI (No Hammer)", rsi, last_close);
                            } else if (is_overbought) {
                                std::cout << " [SIGNAL - SELL] " << bot.symbol << " is OVERBOUGHT! RSI: " << rsi << std::endl;
                                executeOrderAndNotify(exchange, bot, "SELL", "Overbought RSI (No Shooting Star)", rsi, last_close);
                            } else {
                                std::cout << "       -> Market neutral. Holding position." << std::endl;
                            }
                        }

                    } else {
                        std::cerr << "       -> [WARNING] Insufficient data to calculate RSI." << std::endl;
                    }
                } else {
                    std::cerr << "       -> [ERROR] Failed to fetch market klines." << std::endl;
                }
            }
        } else {
            std::cerr << "[WARNING] Failed to sync active configs from API. Retrying next cycle..." << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    return 0;
}