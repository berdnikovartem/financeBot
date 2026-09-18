#pragma once
#include <string>
#include <unordered_map>
#include <vector>

struct BitunixPosition {
    std::string positionId;
    std::string symbol;
    std::string side;
    double quantity = 0.0;
    double averageOpenPrice = 0.0;
};

class BitunixExchange {
private:
    std::string apiKey;
    std::string apiSecret;
    std::string baseUrl;
    std::unordered_map<std::string, int> quotePrecisions;

    // Helper for libcurl
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

    std::string generateSignature(const std::string& queryString, const std::string& body,
                                  const std::string& nonce, const std::string& timestamp);
    bool performSignedRequest(const std::string& path, const std::string& queryUrl,
                              const std::string& queryCanonical, const std::string& body,
                              bool post, std::string& response, long& httpCode);
    int quotePrecision(const std::string& symbol);

public:
    BitunixExchange(const std::string& key, const std::string& secret);

    BitunixExchange();

    void setCredentials(const std::string& key, const std::string& secret);

    bool fetchPositions(const std::string& symbol, std::vector<BitunixPosition>& positions);
    bool placeTakeProfit(const BitunixPosition& position, double targetPrice);

    // Core private method to handle the actual HTTP POST request
    bool placeOrder(const std::string& symbol, const std::string& side, const std::string& positionSide,
                    double quantity, bool opening);


    // Position Management Methods
    bool openLong(const std::string& symbol, double quantity);
    bool closeLong(const std::string& symbol, double quantity);
    
    bool openShort(const std::string& symbol, double quantity);
    bool closeShort(const std::string& symbol, double quantity);
};