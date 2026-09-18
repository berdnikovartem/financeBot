#include "Bitunix.h"

#include <iostream>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <curl/curl.h>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

BitunixExchange::BitunixExchange(const std::string& key, const std::string& secret) 
    : apiKey(key), apiSecret(secret), baseUrl("https://fapi.bitunix.com") {}

BitunixExchange::BitunixExchange() {
    const char* api_key_env = std::getenv("MY_BITUNIX_API");
    const char* secret_key_env = std::getenv("MY_BITUNIX_SECURITY_KEY");

    baseUrl = "https://fapi.bitunix.com";

    // Optional legacy fallback. The worker normally loads per-user credentials
    // from the Java API and applies them through setCredentials().
    if (api_key_env && secret_key_env) {
        this->apiKey = std::string(api_key_env);
        this->apiSecret = std::string(secret_key_env);
    }
}

void BitunixExchange::setCredentials(const std::string& key, const std::string& secret) {
    apiKey = key;
    apiSecret = secret;
}

int BitunixExchange::quotePrecision(const std::string& symbol) {
    auto cached = quotePrecisions.find(symbol);
    if (cached != quotePrecisions.end()) return cached->second;

    CURL* curl = curl_easy_init();
    if (!curl) return 2;
    std::string response;
    std::string url = baseUrl + "/api/v1/futures/market/trading_pairs?symbols=" + symbol;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    CURLcode result = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (result == CURLE_OK) {
        try {
            auto parsed = json::parse(response);
            if (parsed.value("code", -1) == 0 && parsed["data"].is_array() && !parsed["data"].empty()) {
                int precision = parsed["data"][0].value("quotePrecision", 2);
                quotePrecisions[symbol] = precision;
                std::cout << "[Bitunix] " << symbol << " quote precision: " << precision << std::endl;
                return precision;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Bitunix] Failed to parse trading-pair metadata: " << e.what() << std::endl;
        }
    }
    std::cerr << "[Bitunix] Could not fetch quote precision for " << symbol << "; using 2 decimals." << std::endl;
    quotePrecisions[symbol] = 2;
    return 2;
}

size_t BitunixExchange::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static std::string sha256Hex(const std::string& value) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(value.data()), value.size(), hash);

    std::stringstream result;
    for (unsigned char byte : hash) {
        result << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return result.str();
}

std::string BitunixExchange::generateSignature(const std::string& queryString, const std::string& body,
                                               const std::string& nonce, const std::string& timestamp) {
    std::string digest = sha256Hex(nonce + timestamp + apiKey + queryString + body);
    return sha256Hex(digest + apiSecret);
}

bool BitunixExchange::performSignedRequest(const std::string& path, const std::string& queryUrl,
                                           const std::string& queryCanonical, const std::string& body,
                                           bool post, std::string& response, long& httpCode) {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    auto nonce = timestamp + std::to_string(std::rand());
    std::string signature = generateSignature(queryCanonical, body, nonce, timestamp);

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string url = baseUrl + path + (queryUrl.empty() ? "" : "?" + queryUrl);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("api-key: " + apiKey).c_str());
    headers = curl_slist_append(headers, ("nonce: " + nonce).c_str());
    headers = curl_slist_append(headers, ("timestamp: " + timestamp).c_str());
    headers = curl_slist_append(headers, ("sign: " + signature).c_str());
    headers = curl_slist_append(headers, "language: en-US");
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, post ? "POST" : "GET");
    if (post) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result == CURLE_OK;
}

bool BitunixExchange::placeOrder(const std::string& symbol, const std::string& side, const std::string& positionSide,
                                 double quantity, bool opening) {
    if (apiKey.empty() || apiSecret.empty() || symbol.empty() || quantity <= 0.0 || !std::isfinite(quantity)) {
        std::cerr << "[Bitunix] Refusing order with invalid credentials, symbol, or quantity." << std::endl;
        return false;
    }
    if ((side != "BUY" && side != "SELL") || (positionSide != "LONG" && positionSide != "SHORT")) {
        std::cerr << "[Bitunix] Refusing order with invalid side or position side." << std::endl;
        return false;
    }

    std::string body = "{\"symbol\":\"" + symbol + "\",\"side\":\"" + side +
                       "\",\"qty\":\"" + std::to_string(quantity) +
                       "\",\"tradeSide\":\"" + (opening ? "OPEN" : "CLOSE") +
                       "\",\"orderType\":\"MARKET\"}";
    std::string response;
    long httpCode = 0;
    if (performSignedRequest("/api/v1/futures/trade/place_order", "", "", body, true, response, httpCode)) {
        try {
            auto responseJson = json::parse(response);
            if (responseJson.value("code", -1) == 0) {
                std::cout << "[Bitunix] Order executed successfully: " << side << " " << positionSide << " " << quantity << " " << symbol << std::endl;
                return true;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Bitunix] Failed to parse response (HTTP " << httpCode << "): " << e.what() << std::endl;
        }
    }
    std::cerr << "[Bitunix API Error] HTTP " << httpCode << ": " << response << std::endl;
    return false;
}

bool BitunixExchange::fetchPositions(const std::string& symbol, std::vector<BitunixPosition>& positions) {
    std::string response;
    long httpCode = 0;
    std::string query = "symbol=" + symbol;
    if (!performSignedRequest("/api/v1/futures/position/get_pending_positions", query, "symbol" + symbol,
                              "", false, response, httpCode)) {
        std::cerr << "[Bitunix] Position request failed (HTTP " << httpCode << "): " << response << std::endl;
        return false;
    }
    try {
        auto parsed = json::parse(response);
        if (parsed.value("code", -1) != 0 || !parsed["data"].is_array()) {
            std::cerr << "[Bitunix] Position response did not contain a usable data array: " << response << std::endl;
            return false;
        }
        positions.clear();
        for (const auto& item : parsed["data"]) {
            BitunixPosition position;
            position.positionId = item.value("positionId", "");
            position.symbol = item.value("symbol", symbol);
            position.side = item.value("side", "");
            if (position.side == "BUY") position.side = "LONG";
            if (position.side == "SELL") position.side = "SHORT";
            position.quantity = std::stod(item.value("qty", "0"));
            position.averageOpenPrice = std::stod(item.value("avgOpenPrice", "0"));
            std::cout << "[Bitunix] Position: symbol=" << position.symbol
                      << " side=" << position.side << " qty=" << position.quantity
                      << " avg=" << position.averageOpenPrice << std::endl;
            if (!position.positionId.empty() && position.quantity > 0.0 && position.averageOpenPrice > 0.0) {
                positions.push_back(position);
            }
        }
        if (positions.empty()) {
            std::cout << "[Bitunix] Position response is valid but contains no usable open positions for "
                      << symbol << std::endl;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Bitunix] Failed to parse positions (HTTP " << httpCode << "): " << e.what() << std::endl;
        return false;
    }
}

bool BitunixExchange::placeTakeProfit(const BitunixPosition& position, double targetPrice) {
    int precision = quotePrecision(position.symbol);
    double scale = std::pow(10.0, precision);
    double roundedTarget = std::round(targetPrice * scale) / scale;
    json body = {
        {"symbol", position.symbol},
        {"positionId", position.positionId},
        {"tpPrice", std::to_string(roundedTarget)},
        {"tpStopType", "LAST_PRICE"},
        {"tpOrderType", "LIMIT"},
        {"tpOrderPrice", std::to_string(roundedTarget)},
        {"tpQty", std::to_string(position.quantity)}
    };
    std::cout << "[Bitunix] Placing TP payload: symbol=" << position.symbol
              << " positionId=" << position.positionId << " trigger=" << roundedTarget
              << " qty=" << position.quantity << std::endl;
    std::string response;
    long httpCode = 0;
    if (performSignedRequest("/api/v1/futures/tpsl/place_order", "", "", body.dump(), true, response, httpCode)) {
        try {
            auto parsed = json::parse(response);
            if (parsed.value("code", -1) == 0) {
                std::cout << "[Bitunix] Native take-profit placed for " << position.symbol
                          << " position " << position.positionId << " at " << roundedTarget << std::endl;
                return true;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Bitunix] Failed to parse take-profit response: " << e.what() << std::endl;
        }
    }
    std::cerr << "[Bitunix API Error] Take-profit HTTP " << httpCode << ": " << response << std::endl;
    return false;
}

bool BitunixExchange::openLong(const std::string& symbol, double quantity) {
    // To open a Long, we BUY on the LONG position side
    return placeOrder(symbol, "BUY", "LONG", quantity, true);
}

bool BitunixExchange::closeLong(const std::string& symbol, double quantity) {
    // To close a Long, we SELL on the LONG position side
    return placeOrder(symbol, "SELL", "LONG", quantity, false);
}

bool BitunixExchange::openShort(const std::string& symbol, double quantity) {
    // To open a Short, we SELL on the SHORT position side
    return placeOrder(symbol, "SELL", "SHORT", quantity, true);
}

bool BitunixExchange::closeShort(const std::string& symbol, double quantity) {
    // To close a Short, we BUY on the SHORT position side
    return placeOrder(symbol, "BUY", "SHORT", quantity, false);
}