#pragma once
#include <string>
#include <vector>
#include <unordered_map>

class MarketAnalyzer {
private:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

    // Track the last signaled candle per symbol instead of a single global variable
    std::unordered_map<std::string, long long> lastSignaledCandles;

public:
    MarketAnalyzer() = default;

    //method to get closing prices out of BINANCE exchange
    bool fetchClosingPrices(const std::string& symbol, const std::string& timeframe, int limit, std::vector<double>& closes_out);

    //method to calculate the RSI
    double calculateWildersRSI(const std::vector<double>& closes, int period);

    // Hammer pattern recognition (supports bullish bottom hammer or bearish shooting star)
    bool hammerPattern(double open, double high, double low, double close, bool side, double multiplier = 1.5);

    // Volume surge recognition
    bool volumeSurge(double currentVolume, double averageVolume, double surgeMultiplier = 1.5);

    // Intra-candle time check: requires the symbol now
    bool sufficientTimePass(const std::string& symbol, long long candleOpenTimeMs, int thresholdSeconds);

    void markCandleSignaled(const std::string& symbol, long long candleOpenTimeMs);
};