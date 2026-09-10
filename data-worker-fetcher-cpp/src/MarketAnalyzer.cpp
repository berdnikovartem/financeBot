#include "MarketAnalyzer.h"
#include <iostream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

size_t MarketAnalyzer::WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

bool MarketAnalyzer::fetchClosingPrices(const std::string& symbol, const std::string& timeframe, int limit, std::vector<double>& closes_out) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    std::string endpoint = "https://api.binance.com/api/v3/klines?symbol=" + symbol + "&interval=" + timeframe + "&limit=" + std::to_string(limit);

    curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code != 200) {
        std::cerr << "ERROR Failed to fetch Binance klines for " << symbol << " (HTTP: " << http_code << ")" << std::endl;
        return false;
    }

    try {
        json klines = json::parse(readBuffer);
        closes_out.clear();

        if (!klines.is_array()) return false;

        for (const auto& kline : klines) {
            if (kline.is_array() && kline.size() > 4) {
                std::string close_str = kline[4].get<std::string>();
                closes_out.push_back(std::stod(close_str));
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "ERROR JSON parse error on klines for " << symbol << ": " << e.what() << std::endl;
        return false;
    }
}

double MarketAnalyzer::calculateWildersRSI(const std::vector<double>& closes, int period) {
    if (closes.size() <= static_cast<size_t>(period)) {
        return -1.0; //not enough history data
    }

    double gain_sum = 0.0;
    double loss_sum = 0.0;

    for (int i = 1; i <= period; ++i) {
        double change = closes[i] - closes[i - 1];
        if (change > 0) {
            gain_sum += change;
        } else {
            loss_sum -= change; // Make positive
        }
    }

    double avg_gain = gain_sum / period;
    double avg_loss = loss_sum / period;

    for (size_t i = period + 1; i < closes.size(); ++i) {
        double change = closes[i] - closes[i - 1];
        double current_gain = (change > 0) ? change : 0.0;
        double current_loss = (change < 0) ? -change : 0.0;

        avg_gain = (avg_gain * (period - 1) + current_gain) / period;
        avg_loss = (avg_loss * (period - 1) + current_loss) / period;
    }

    if (avg_loss == 0.0) {
        return 100.0;
    }

    double rs = avg_gain / avg_loss;
    return 100.0 - (100.0 / (1.0 + rs));
}

bool MarketAnalyzer::hammerPattern(double open, double high, double low, double close, bool side) {
    double body = std::abs(open - close);
    double upperShadow = high - std::max(open, close);
    double lowerShadow = std::min(open, close) - low;
    double totalCandleSize = high - low;

    if (totalCandleSize == 0) return false;

    if (side) {
        // Bullish Hammer
        bool longLowerShadow = lowerShadow >= (1.5 * body);
        //bool tinyUpperShadow = upperShadow <= (0.10 * totalCandleSize);
        return longLowerShadow; //&& tinyUpperShadow;
    } else {
        // Bearish Shooting Star
        bool longUpperShadow = upperShadow >= (1.5 * body);
        //bool tinyLowerShadow = lowerShadow <= (0.10 * totalCandleSize);
        return longUpperShadow; //&& tinyLowerShadow;
    }
}

bool MarketAnalyzer::volumeSurge(double currentVolume, double averageVolume, double surgeMultiplier) {
    if (averageVolume <= 0) return false;
    return currentVolume >= (averageVolume * surgeMultiplier);
}

bool MarketAnalyzer::sufficientTimePass(long long candleOpenTimeMs, int thresholdSeconds) {
    long long currentTimestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    long long elapsedMs = currentTimestampMs - candleOpenTimeMs;
    long long thresholdMs = thresholdSeconds * 1000LL;

    if (elapsedMs >= thresholdMs && candleOpenTimeMs != lastSignaledCandleOpen) {
        lastSignaledCandleOpen = candleOpenTimeMs; // Save it so we don't re-trigger
        return true;
    }
    return false;
}