#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>
#include <cstdlib>

using json = nlohmann::json;

// cURL callback to save HTTP responses into a string
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string fetchUrl(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string readBuffer;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "[cURL Error]: " << curl_easy_strerror(res) << std::endl;
        }
        curl_easy_cleanup(curl);
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

    // Safely encode spaces, colons, and mathematical symbols
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

double calculateRSI(const std::vector<double>& prices, int period = 14) {
    if (prices.size() <= static_cast<size_t>(period)) {
        return 50.0;
    }

    double avgGain = 0.0;
    double avgLoss = 0.0;

    // Initial Average Gain and Average Loss for the first 14 periods
    for (int i = 1; i <= period; ++i) {
        double change = prices[i] - prices[i - 1];
        if (change > 0) {
            avgGain += change;
        } else {
            avgLoss += std::abs(change);
        }
    }

    avgGain /= period;
    avgLoss /= period;

    // Wilder's Smoothing for all remaining price candles
    for (size_t i = period + 1; i < prices.size(); ++i) {
        double change = prices[i] - prices[i - 1];
        double currentGain = (change > 0) ? change : 0.0;
        double currentLoss = (change < 0) ? std::abs(change) : 0.0;

        avgGain = (avgGain * (period - 1) + currentGain) / period;
        avgLoss = (avgLoss * (period - 1) + currentLoss) / period;
    }

    // division by zero (if prices only went up, loss is 0)
    if (avgLoss == 0.0) {
        return 100.0;
    }

    // Calculate final RS and RSI values
    double rs = avgGain / avgLoss;
    return 100.0 - (100.0 / (1.0 + rs));
}

int main() {
    // Keep the worker alive forever
    while (true) {
        try {
            // 1. Connect to PostgreSQL
            pqxx::connection c("dbname=artem user=artem password=123456 host=db port=5432");
            pqxx::work w(c);

            // 2. Fetch tickers from your database table
            pqxx::result rows = w.exec("SELECT name FROM asset");

            for (auto const& row : rows) {
                // Note: Binance expects symbols like "BTCUSDT" (no hyphens)
                std::string ticker = row[0].c_str();
                std::cout << "Checking " << ticker << " on Binance...\n";

                // 3. Fetch Binance Kline Data (1-hour candles)
                std::string url = "https://api.binance.com/api/v3/klines?symbol=" + ticker + "&interval=1h";
                std::string response = fetchUrl(url);

                // 4. Parse Binance JSON
                json jsonData = json::parse(response);
                std::vector<double> closingPrices;

                // Binance returns an array of arrays. Index [4] is the Closing Price.
                for (const auto& kline : jsonData) {
                    // Extract the string at index 4 and convert it to a double
                    double closePrice = std::stod(kline[4].get<std::string>());
                    closingPrices.push_back(closePrice);
                }

                // 5. Calculate RSI
                double rsi = calculateRSI(closingPrices);

                //std::string message = "Alert!%20" + ticker + "%20RSI%20is%20" + std::to_string(rsi);
                // 6. Check Condition and Alert
                if (rsi > 70.0) {
                    sendTelegramAlert("Ticker: " + ticker + " RSI value is >70, = " + std::to_string(rsi));
                }else if (rsi < 30.0) {
                    sendTelegramAlert("Ticker: " + ticker + " RSI value is <30, = " + std::to_string(rsi));
                }else {
                    sendTelegramAlert("Ticker: " + ticker + " RSI value is: " + std::to_string(rsi));
                }
            }
        } catch (const std::exception &e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }

        // Pause for 3 seconds before hitting the database and Binance again
        std::cout << "Sleeping for 5 seconds...\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    return 0;
}