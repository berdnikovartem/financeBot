#include "Bitunix.h"

#include "Bitunix.h"
#include <iostream>
#include <chrono>
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

BitunixExchange::BitunixExchange(const std::string& key, const std::string& secret) 
    : apiKey(key), apiSecret(secret) {
    // Base URL for Bitunix Futures API

}

BitunixExchange::BitunixExchange() {
    const char* api_key_env = std::getenv("MY_BITUNIX_API");
    const char* secret_key_env = std::getenv("MY_BITUNIX_SECURITY_KEY");

    baseUrl = "https://fapi.bitunix.com";

    // 2. Fallback check & assign to member variables
    if (api_key_env && secret_key_env) {
        this->apiKey = std::string(api_key_env);
        this->apiSecret = std::string(secret_key_env);
    } else {
        std::cerr << "[BitunixExchange] Warning: BITUNIX_API_KEY or BITUNIX_SECRET_KEY environment variable missing!" << std::endl;
    }
}

size_t BitunixExchange::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Generates the HMAC-SHA256 signature required by Bitunix
std::string BitunixExchange::generateSignature(const std::string& queryString, const std::string& timestamp) {
    std::string dataToSign = queryString + timestamp; // Exchanges usually concatenate these
    
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen;
    
    HMAC(EVP_sha256(), 
         apiSecret.c_str(), apiSecret.length(),
         (unsigned char*)dataToSign.c_str(), dataToSign.length(),
         hash, &hashLen);

    std::stringstream ss;
    for(unsigned int i = 0; i < hashLen; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

bool BitunixExchange::placeOrder(const std::string& symbol, const std::string& side, const std::string& positionSide, double quantity) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    // Get current Unix timestamp in milliseconds
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::string timestamp = std::to_string(ms);

    // Build the query parameters (Standard for Futures Hedge Mode)
    // Note: Always verify exact parameter names in Bitunix official docs
    std::stringstream queryStream;
    queryStream << "symbol=" << symbol 
                << "&side=" << side 
                << "&positionSide=" << positionSide 
                << "&type=MARKET" 
                << "&quantity=" << quantity;
    
    std::string queryString = queryStream.str();
    std::string signature = generateSignature(queryString, timestamp);
    
    // Construct the final URL with the signature
    std::string url = baseUrl + "/api/v1/futures/order?" + queryString + "&signature=" + signature;

    curl = curl_easy_init();
    if (curl) {
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, ("X-API-KEY: " + apiKey).c_str());
        headers = curl_slist_append(headers, ("X-TIMESTAMP: " + timestamp).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        // We pass empty body because data is in the query string (common for crypto APIs)
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, ""); 
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK) {
            try {
                auto responseJson = json::parse(readBuffer);
                // Assuming Bitunix returns {"code": 200} or similar on success
                if (responseJson.contains("code") && responseJson["code"] == 200) {
                    std::cout << "[Bitunix] Order executed successfully: " << side << " " << positionSide << " " << quantity << " " << symbol << std::endl;
                    return true;
                } else {
                    std::cerr << "[Bitunix API Error] " << readBuffer << std::endl;
                    return false;
                }

            } catch (const std::exception& e) {
                std::cerr << "[Bitunix] Failed to parse response: " << e.what() << "\nRaw: " << readBuffer << std::endl;
                return false;
            }
        }
    }
    return false;
}

bool BitunixExchange::placeTakeProfit(const std::string& symbol, const std::string& side,
                                      const std::string& positionSide, double quantity,
                                      double triggerPrice, std::string& orderId) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::string timestamp = std::to_string(ms);
    std::ostringstream params;
    params << "symbol=" << symbol << "&side=" << side << "&positionSide=" << positionSide
           << "&tradeSide=CLOSE&type=MARKET&qty=" << std::setprecision(15) << quantity
           << "&triggerPrice=" << triggerPrice
           << "&triggerDirection=" << (side == "SELL" ? "1" : "2") << "&executePrice=0";
    const std::string queryString = params.str();
    const std::string url = baseUrl + "/api/v1/futures/plan/place_order?" + queryString +
                            "&signature=" + generateSignature(queryString, timestamp);

    CURL* curl = curl_easy_init();
    if (!curl) return false;
    std::string readBuffer;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("X-API-KEY: " + apiKey).c_str());
    headers = curl_slist_append(headers, ("X-TIMESTAMP: " + timestamp).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    const CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return false;

    try {
        const auto responseJson = json::parse(readBuffer);
        if (!responseJson.contains("code") || responseJson["code"] != 200 ||
            !responseJson.contains("data") || !responseJson["data"].contains("orderId")) {
            std::cerr << "[Bitunix API Error] Failed to place take-profit: " << readBuffer << std::endl;
            return false;
        }
        const auto& responseOrderId = responseJson["data"]["orderId"];
        orderId = responseOrderId.is_string() ? responseOrderId.get<std::string>() : responseOrderId.dump();
        return !orderId.empty();
    } catch (const std::exception& e) {
        std::cerr << "[Bitunix] Failed to parse take-profit response: " << e.what()
                  << "\nRaw: " << readBuffer << std::endl;
        return false;
    }
}

bool BitunixExchange::cancelTakeProfit(const std::string& symbol, const std::string& orderId) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::string timestamp = std::to_string(ms);
    const std::string queryString = "symbol=" + symbol + "&orderId=" + orderId;
    const std::string url = baseUrl + "/api/v1/futures/plan/cancel_orders?" + queryString +
                            "&signature=" + generateSignature(queryString, timestamp);
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    std::string readBuffer;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("X-API-KEY: " + apiKey).c_str());
    headers = curl_slist_append(headers, ("X-TIMESTAMP: " + timestamp).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    const CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return false;
    try {
        const auto responseJson = json::parse(readBuffer);
        if (!responseJson.contains("code") || responseJson["code"] != 200) {
            std::cerr << "[Bitunix API Error] Failed to cancel take-profit: " << readBuffer << std::endl;
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Bitunix] Failed to parse cancel response: " << e.what()
                  << "\nRaw: " << readBuffer << std::endl;
        return false;
    }
}

bool BitunixExchange::openLong(const std::string& symbol, double quantity) {
    // To open a Long, we BUY on the LONG position side
    return placeOrder(symbol, "BUY", "LONG", quantity);
}

bool BitunixExchange::closeLong(const std::string& symbol, double quantity) {
    // To close a Long, we SELL on the LONG position side
    return placeOrder(symbol, "SELL", "LONG", quantity);
}

bool BitunixExchange::openShort(const std::string& symbol, double quantity) {
    // To open a Short, we SELL on the SHORT position side
    return placeOrder(symbol, "SELL", "SHORT", quantity);
}

bool BitunixExchange::closeShort(const std::string& symbol, double quantity) {
    // To close a Short, we BUY on the SHORT position side
    return placeOrder(symbol, "BUY", "SHORT", quantity);
}