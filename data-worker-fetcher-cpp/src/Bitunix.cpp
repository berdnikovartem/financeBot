#include "Bitunix.h"

#include "Bitunix.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {
std::string hexEncode(const unsigned char* bytes, std::size_t length) {
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < length; ++i) {
        result << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    }
    return result.str();
}

std::string sha256Hex(const std::string& value) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(value.data()), value.size(), digest);
    return hexEncode(digest, sizeof(digest));
}

std::string createNonce() {
    unsigned char bytes[16];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        throw std::runtime_error("Could not generate Bitunix request nonce");
    }
    return hexEncode(bytes, sizeof(bytes));
}

std::string decimalString(double value, int precision) {
    std::ostringstream result;
    result << std::fixed << std::setprecision(precision) << value;
    std::string formatted = result.str();
    const std::size_t decimalPoint = formatted.find('.');
    if (decimalPoint != std::string::npos) {
        formatted.erase(formatted.find_last_not_of('0') + 1);
        if (formatted.back() == '.') formatted.pop_back();
    }
    return formatted;
}

double jsonNumber(const json& value) {
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}

double quantizeTakeProfit(double price, int precision, const std::string& positionSide) {
    const double scale = std::pow(10.0, precision);
    const double scaledPrice = price * scale;
    const double roundedPrice = positionSide == "LONG"
            ? std::ceil(scaledPrice - 1e-9)
            : std::floor(scaledPrice + 1e-9);
    return roundedPrice / scale;
}

size_t marketRulesWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

bool fetchSymbolRules(const std::string& symbol, int& basePrecision, int& quotePrecision,
                      double& minimumQuantity, double& maximumMarketQuantity) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[Bitunix] Could not initialize market-rules request" << std::endl;
        return false;
    }

    char* escapedSymbol = curl_easy_escape(curl, symbol.c_str(), static_cast<int>(symbol.size()));
    if (!escapedSymbol) {
        curl_easy_cleanup(curl);
        std::cerr << "[Bitunix] Could not encode symbol for market-rules request" << std::endl;
        return false;
    }
    const std::string url = std::string("https://fapi.bitunix.com/api/v1/futures/market/trading_pairs?symbols=") +
                            escapedSymbol;
    curl_free(escapedSymbol);

    std::string responseBody;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, marketRulesWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    const CURLcode result = curl_easy_perform(curl);
    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK || httpStatus != 200) {
        std::cerr << "[Bitunix] Could not fetch market rules for " << symbol << ": "
                  << (result == CURLE_OK ? "HTTP " + std::to_string(httpStatus) : curl_easy_strerror(result))
                  << std::endl;
        return false;
    }

    try {
        const auto response = json::parse(responseBody);
        if (response.value("code", -1) != 0 || !response.contains("data") ||
            !response["data"].is_array() || response["data"].empty()) {
            std::cerr << "[Bitunix] Invalid market-rules response for " << symbol << ": "
                      << responseBody << std::endl;
            return false;
        }
        const auto& rules = response["data"].front();
        if (rules.value("symbol", std::string()) != symbol ||
            !rules.contains("basePrecision") || !rules.contains("quotePrecision") ||
            !rules.contains("minTradeVolume") || !rules.contains("maxMarketOrderVolume")) {
            std::cerr << "[Bitunix] Market rules are incomplete for " << symbol << std::endl;
            return false;
        }
        basePrecision = rules.at("basePrecision").get<int>();
        quotePrecision = rules.at("quotePrecision").get<int>();
        minimumQuantity = jsonNumber(rules.at("minTradeVolume"));
        maximumMarketQuantity = jsonNumber(rules.at("maxMarketOrderVolume"));
        if (basePrecision < 0 || basePrecision > 15 || quotePrecision < 0 || quotePrecision > 15 ||
            minimumQuantity <= 0.0 || maximumMarketQuantity < minimumQuantity) {
            std::cerr << "[Bitunix] Invalid market rules for " << symbol << std::endl;
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        std::cerr << "[Bitunix] Could not parse market rules for " << symbol << ": "
                  << exception.what() << std::endl;
        return false;
    }
}

std::string bitunixSignature(const std::string& nonce, const std::string& timestamp,
                             const std::string& apiKey, const std::string& apiSecret,
                             const std::string& body) {
    const std::string digest = sha256Hex(nonce + timestamp + apiKey + body);
    return sha256Hex(digest + apiSecret);
}
}

BitunixExchange::BitunixExchange(const std::string& key, const std::string& secret) 
    : apiKey(key), apiSecret(secret), baseUrl("https://fapi.bitunix.com") {}

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

bool BitunixExchange::placeOrder(const std::string& symbol, const std::string& side,
                                 const std::string& positionSide, const std::string& tradeSide,
                                 double quantity,
                                 double takeProfitPrice) {
    if (apiKey.empty() || apiSecret.empty() || quantity <= 0.0 || takeProfitPrice < 0.0 ||
        (tradeSide != "OPEN" && tradeSide != "CLOSE")) {
        std::cerr << "[Bitunix] Refusing order with missing credentials or invalid quantity/TP price" << std::endl;
        return false;
    }

    try {
        int basePrecision = 0;
        int quotePrecision = 0;
        double minimumQuantity = 0.0;
        double maximumMarketQuantity = 0.0;
        if (!fetchSymbolRules(symbol, basePrecision, quotePrecision, minimumQuantity, maximumMarketQuantity)) {
            return false;
        }
        const double quantityScale = std::pow(10.0, basePrecision);
        const double normalizedQuantity = std::round(quantity * quantityScale) / quantityScale;
        const double quantityTolerance = 1e-9 * std::max(1.0, std::abs(quantity));
        if (std::abs(quantity - normalizedQuantity) > quantityTolerance ||
            quantity < minimumQuantity || quantity > maximumMarketQuantity) {
            std::cerr << "[Bitunix] Refusing " << symbol << " order: quantity " << quantity
                      << " is outside market rules (min " << minimumQuantity << ", max "
                      << maximumMarketQuantity << ", precision " << basePrecision << ")" << std::endl;
            return false;
        }

        json requestBody = {
            {"symbol", symbol},
            {"qty", decimalString(normalizedQuantity, basePrecision)},
            {"side", side},
            {"tradeSide", tradeSide},
            {"orderType", "MARKET"}
        };
        if (tradeSide == "CLOSE") {
            requestBody["reduceOnly"] = true;
        }
        if (takeProfitPrice > 0.0) {
            requestBody["tpPrice"] = decimalString(
                    quantizeTakeProfit(takeProfitPrice, quotePrecision, positionSide), quotePrecision);
            requestBody["tpStopType"] = "MARK_PRICE";
            requestBody["tpOrderType"] = "MARKET";
        }
        const std::string body = requestBody.dump();
        const auto now = std::chrono::system_clock::now();
        const auto timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count());
        const std::string nonce = createNonce();
        const std::string signature = bitunixSignature(nonce, timestamp, apiKey, apiSecret, body);
        const std::string url = baseUrl + "/api/v1/futures/trade/place_order";

        CURL* curl = curl_easy_init();
        if (!curl) {
            std::cerr << "[Bitunix] Could not initialize order request" << std::endl;
            return false;
        }

        std::string responseBody;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("api-key: " + apiKey).c_str());
        headers = curl_slist_append(headers, ("nonce: " + nonce).c_str());
        headers = curl_slist_append(headers, ("timestamp: " + timestamp).c_str());
        headers = curl_slist_append(headers, ("sign: " + signature).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        const CURLcode result = curl_easy_perform(curl);
        long httpStatus = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (result != CURLE_OK) {
            std::cerr << "[Bitunix] Order request failed: " << curl_easy_strerror(result) << std::endl;
            return false;
        }

        try {
            const auto response = json::parse(responseBody);
            if (httpStatus >= 200 && httpStatus < 300 && response.value("code", -1) == 0 &&
                response.contains("data") && response["data"].is_object() &&
                response["data"].contains("orderId")) {
                std::cout << "[Bitunix] Order accepted: " << side << " " << positionSide << " "
                          << quantity << " " << symbol << std::endl;
                return true;
            }
        } catch (const json::exception& exception) {
            std::cerr << "[Bitunix] Could not parse order response: " << exception.what()
                      << " (HTTP " << httpStatus << ")" << std::endl;
            return false;
        }
        std::cerr << "[Bitunix API Error] HTTP " << httpStatus << ": " << responseBody
                  << " | Request body: " << body << std::endl;
    } catch (const std::exception& exception) {
        std::cerr << "[Bitunix] Could not create signed order request: " << exception.what() << std::endl;
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
        if (!responseJson.contains("code")
            || (responseJson["code"] != 0 && responseJson["code"] != 200) ||
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
        if (!responseJson.contains("code")
            || (responseJson["code"] != 0 && responseJson["code"] != 200)) {
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
    return placeOrder(symbol, "BUY", "LONG", "OPEN", quantity, 0.0);
}

bool BitunixExchange::closeLong(const std::string& symbol, double quantity) {
    // To close a Long, we SELL on the LONG position side
    return placeOrder(symbol, "SELL", "LONG", "CLOSE", quantity, 0.0);
}

bool BitunixExchange::openShort(const std::string& symbol, double quantity) {
    // To open a Short, we SELL on the SHORT position side
    return placeOrder(symbol, "SELL", "SHORT", "OPEN", quantity, 0.0);
}

bool BitunixExchange::closeShort(const std::string& symbol, double quantity) {
    // To close a Short, we BUY on the SHORT position side
    return placeOrder(symbol, "BUY", "SHORT", "CLOSE", quantity, 0.0);
}