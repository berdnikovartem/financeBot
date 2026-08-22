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
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // Force the connection to abort after 10 seconds
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

void sendTelegramAlert(const std::string& ticker, double rsi) {
    const char* tokenEnv = std::getenv("TELEGRAM_BOT_TOKEN");
    const char* chatIdEnv = std::getenv("MY_CHAT_ID");

    if (!tokenEnv || !chatIdEnv) {
        std::cout << "[Telegram] Error: Missing TELEGRAM_BOT_TOKEN or MY_CHAT_ID environment variables!\n";
        return;
    }

    std::string botToken(tokenEnv);
    std::string chatId(chatIdEnv);

    std::string message = "Alert!%20" + ticker + "%20RSI%20is%20" + std::to_string(rsi);

    std::string url = "https://api.telegram.org/bot" + botToken +
                      "/sendMessage?chat_id=" + chatId + "&text=" + message;

    std::cout << "[Telegram] Sending alert for " << ticker << "...\n";
    std::string response = fetchUrl(url);
    std::cout << "[Telegram Response]: " << response << std::endl;
}

double calculateRSI(const std::vector<double>& prices) {
    // TODO: 14-period RSI mathematical logic goes here
    return 75.0; // Placeholder returning 75.0
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

                // 6. Check Condition and Alert
                if (rsi > 70.0) {
                    sendTelegramAlert(ticker, rsi);
                }
            }
        } catch (const std::exception &e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }

        // Pause for 3 seconds before hitting the database and Binance again
        std::cout << "Sleeping for 3 seconds...\n";
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    return 0;
}