#pragma once
#include <string>
#include <vector>

class MarketAnalyzer {
private:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

public:
    MarketAnalyzer() = default;

    //method to get closing prices out of BINANCE exchange
    bool fetchClosingPrices(const std::string& symbol, const std::string& timeframe, int limit, std::vector<double>& closes_out);

    //method to calculate the RSI
    double calculateWildersRSI(const std::vector<double>& closes, int period);
};