#pragma once
#include <string>
#include <vector>

struct BitunixPosition {
    std::string position_id;
    std::string symbol;
    std::string side;
    double quantity = 0.0;
    double average_open_price = 0.0;
};

class BitunixExchange {
private:
    std::string apiKey;
    std::string apiSecret;
    std::string baseUrl;

    // Helper for libcurl
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

public:
    BitunixExchange(const std::string& key, const std::string& secret);

    BitunixExchange();

    // Core private method to handle the actual HTTP POST request
    bool placeOrder(const std::string& symbol, const std::string& side, const std::string& positionSide,
                    const std::string& tradeSide, double quantity);
    bool placeTradeTakeProfitLimit(const std::string& symbol, const BitunixPosition& position,
                                   double quantity, double targetPrice, int entrySequence);
    bool getPendingPositions(const std::string& symbol, std::vector<BitunixPosition>& positions);
    bool reconcileCycleTakeProfits(const std::string& symbol,
                                   const std::vector<BitunixPosition>& positions,
                                   double takeProfitPercentage,
                                   bool updateOpenPositionOrders = true);


    // Position Management Methods
    bool openLong(const std::string& symbol, double quantity);
    bool closeLong(const std::string& symbol, double quantity);
    
    bool openShort(const std::string& symbol, double quantity);
    bool closeShort(const std::string& symbol, double quantity);
};