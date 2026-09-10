#pragma once
#include <string>

class BitunixExchange {
private:
    std::string apiKey;
    std::string apiSecret;
    std::string baseUrl;

    // Helper for libcurl
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

    // Cryptographic signing for authenticated endpoints
    std::string generateSignature(const std::string& queryString, const std::string& timestamp);

public:
    BitunixExchange(const std::string& key, const std::string& secret);

    BitunixExchange();

    // Core private method to handle the actual HTTP POST request
    bool placeOrder(const std::string& symbol, const std::string& side, const std::string& positionSide, double quantity);


    // Position Management Methods
    bool openLong(const std::string& symbol, double quantity);
    bool closeLong(const std::string& symbol, double quantity);
    
    bool openShort(const std::string& symbol, double quantity);
    bool closeShort(const std::string& symbol, double quantity);
};