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

int main() {
    std::cout << "[INFO] Starting Autonomous Database-Driven C++ Crypto Worker..." << std::endl;

    ConfigFetcher config_fetcher("http://java-api:8080");
    MarketAnalyzer market_analyzer;
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

        // Hot-reload configurations on every cycle
        if (config_fetcher.fetchActiveConfigs(active_bots)) {
            std::cout << "[INFO] Active markets count: " << active_bots.size() << std::endl;

            // Iterate through each active trading pair from the database
            for (const auto& bot : active_bots) {
                std::cout << "\n[SCAN] Evaluating " << bot.symbol
                          << " | Timeframe: " << bot.timeframe
                          << " | RSI Period: " << bot.rsi_period << std::endl;

                std::vector<double> closes;
                // Fetch enough candles to safely calculate RSI (e.g., period * 3 or at least 50 candles)
                int kline_limit = bot.rsi_period * 3;
                if (kline_limit < 50) kline_limit = 50;

                if (market_analyzer.fetchClosingPrices(bot.symbol, bot.timeframe, kline_limit, closes)) {
                    double rsi = market_analyzer.calculateWildersRSI(closes, bot.rsi_period);

                    if (rsi >= 0.0) {
                        std::cout << "       -> Current RSI (" << bot.rsi_period << "): " << rsi << std::endl;

                        // Evaluate Trading Signals based on database thresholds
                        if (rsi <= bot.rsi_oversold) {
                            std::cout << " [SIGNAL - BUY] " << bot.symbol << " is OVERSOLD! RSI: " << rsi << std::endl;
                            std::string alert = "BUY SIGNAL \nSymbol: " + bot.symbol +
                                                "\nCondition: Oversold\nRSI: " + std::to_string(rsi);
                            sendTelegramAlert(alert);
                        } else if (rsi >= bot.rsi_overbought) {
                            std::cout << " [SIGNAL - SELL] " << bot.symbol << " is OVERBOUGHT! RSI: " << rsi << std::endl;
                            std::string alert = " SELL SIGNAL \nSymbol: " + bot.symbol +
                                                "\nCondition: Overbought\nRSI: " + std::to_string(rsi);
                            sendTelegramAlert(alert);
                        } else {
                            std::cout << "       -> Market neutral. Holding position." << std::endl;
                            std::string alert = " MARKET NEUTRAL \nSymbol: " + bot.symbol +
                                                "\nCondition: NEUTRAL\nRSI: " + std::to_string(rsi);
                            sendTelegramAlert(alert);
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