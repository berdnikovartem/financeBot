#include "Bitunix.h"

#include "Bitunix.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <curl/curl.h>
#include <map>
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

std::string jsonStringField(const json& object, const char* key) {
    if (!object.contains(key) || object[key].is_null()) return {};
    if (object[key].is_string()) return object[key].get<std::string>();
    if (object[key].is_number_unsigned()) return std::to_string(object[key].get<unsigned long long>());
    if (object[key].is_number_integer()) return std::to_string(object[key].get<long long>());
    return {};
}

std::string normalizePositionSide(const std::string& side) {
    if (side == "LONG" || side == "BUY") return "LONG";
    if (side == "SHORT" || side == "SELL") return "SHORT";
    return {};
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
                             const std::string& queryParameters, const std::string& body) {
    const std::string digest = sha256Hex(nonce + timestamp + apiKey + queryParameters + body);
    return sha256Hex(digest + apiSecret);
}

bool signedBitunixRequest(const std::string& apiKey, const std::string& apiSecret,
                          const std::string& method, const std::string& endpoint,
                          const std::map<std::string, std::string>& queryParameters,
                          const json& requestBody, json& response) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[Bitunix] Could not initialize authenticated request" << std::endl;
        return false;
    }

    std::ostringstream queryString;
    std::string canonicalQuery;
    bool first = true;
    for (const auto& [key, value] : queryParameters) {
        char* escapedValue = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
        if (!escapedValue) {
            curl_easy_cleanup(curl);
            std::cerr << "[Bitunix] Could not encode request query parameter" << std::endl;
            return false;
        }
        if (!first) queryString << '&';
        queryString << key << '=' << escapedValue;
        curl_free(escapedValue);
        canonicalQuery += key + value;
        first = false;
    }

    const std::string body = method == "POST" ? requestBody.dump() : "";
    const std::string timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    const std::string nonce = createNonce();
    const std::string signature = bitunixSignature(nonce, timestamp, apiKey, apiSecret, canonicalQuery, body);
    std::string url = endpoint;
    if (!queryParameters.empty()) url += "?" + queryString.str();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("api-key: " + apiKey).c_str());
    headers = curl_slist_append(headers, ("nonce: " + nonce).c_str());
    headers = curl_slist_append(headers, ("timestamp: " + timestamp).c_str());
    headers = curl_slist_append(headers, ("sign: " + signature).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string responseBody;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, marketRulesWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    }

    const CURLcode result = curl_easy_perform(curl);
    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK) {
        std::cerr << "[Bitunix] Authenticated request failed: " << curl_easy_strerror(result) << std::endl;
        return false;
    }
    try {
        response = json::parse(responseBody);
    } catch (const json::exception& exception) {
        std::cerr << "[Bitunix] Could not parse authenticated response (HTTP " << httpStatus
                  << "): " << exception.what() << std::endl;
        return false;
    }
    if (httpStatus < 200 || httpStatus >= 300 || response.value("code", -1) != 0) {
        std::cerr << "[Bitunix API Error] HTTP " << httpStatus << ": " << responseBody << std::endl;
        return false;
    }
    return true;
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

bool BitunixExchange::placeOrder(const std::string& symbol, const std::string& side,
                                 const std::string& positionSide, const std::string& tradeSide,
                                 double quantity,
                                 double takeProfitPrice, bool attachTradeTakeProfit) {
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
        if (attachTradeTakeProfit && takeProfitPrice > 0.0) {
            const std::string targetPrice = decimalString(
                    quantizeTakeProfit(takeProfitPrice, quotePrecision, positionSide), quotePrecision);
            requestBody["tpPrice"] = targetPrice;
            requestBody["tpStopType"] = "MARK_PRICE";
            requestBody["tpOrderType"] = "LIMIT";
            requestBody["tpOrderPrice"] = targetPrice;
        }
        const std::string body = requestBody.dump();
        const auto now = std::chrono::system_clock::now();
        const auto timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count());
        const std::string nonce = createNonce();
        const std::string signature = bitunixSignature(nonce, timestamp, apiKey, apiSecret, "", body);
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
                          << quantity << " " << symbol;
                if (attachTradeTakeProfit && takeProfitPrice > 0.0) {
                    std::cout << " with LIMIT take-profit at " << requestBody["tpOrderPrice"];
                }
                std::cout << std::endl;
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

bool BitunixExchange::getPendingPositions(const std::string& symbol,
                                         std::vector<BitunixPosition>& positions) {
    json response;
    const std::string endpoint = baseUrl + "/api/v1/futures/position/get_pending_positions";
    if (!signedBitunixRequest(apiKey, apiSecret, "GET", endpoint, {{"symbol", symbol}},
                              json::object(), response)) {
        return false;
    }
    if (!response.contains("data") || !response["data"].is_array()) {
        std::cerr << "[Bitunix] Pending positions response is missing its data array for "
                  << symbol << std::endl;
        return false;
    }

    std::vector<BitunixPosition> fetched;
    try {
        for (const auto& item : response["data"]) {
            if (!item.is_object()) {
                std::cerr << "[Bitunix] Ignoring non-object pending-position row for "
                          << symbol << std::endl;
                continue;
            }
            const std::string positionSymbol = jsonStringField(item, "symbol");
            if (!positionSymbol.empty() && positionSymbol != symbol) continue;

            BitunixPosition position;
            position.position_id = jsonStringField(item, "positionId");
            position.symbol = symbol;
            const std::string responseSide = jsonStringField(item, "side");
            const std::string responsePositionSide = jsonStringField(item, "positionSide");
            position.side = normalizePositionSide(responsePositionSide);
            if (position.side.empty()) position.side = normalizePositionSide(responseSide);
            if (position.side.empty()) {
                position.side = responseSide.empty() ? responsePositionSide : responseSide;
            }
            if (!item.contains("qty")) {
                std::cerr << "[Bitunix] Pending position for " << symbol
                          << " is missing qty; refusing to scan with unknown exposure." << std::endl;
                return false;
            }
            position.quantity = jsonNumber(item.at("qty"));
            if (!std::isfinite(position.quantity)) {
                std::cerr << "[Bitunix] Pending position for " << symbol
                          << " has a non-finite qty; refusing to scan." << std::endl;
                return false;
            }
            if (position.quantity <= 0.0) continue;

            const char* averagePriceField = item.contains("avgOpenPrice")
                    ? "avgOpenPrice" : item.contains("avgPrice") ? "avgPrice" : nullptr;
            if (!averagePriceField || position.position_id.empty() ||
                (position.side != "LONG" && position.side != "SHORT")) {
                std::cerr << "[Bitunix] Invalid open position for " << symbol
                          << " (positionId: " << (position.position_id.empty() ? "missing" : "present")
                          << ", side: " << (position.side.empty() ? "missing" : position.side)
                          << ", average price: " << (averagePriceField ? "present" : "missing")
                          << "); refusing to scan with unknown exposure." << std::endl;
                return false;
            }
            position.average_open_price = jsonNumber(item.at(averagePriceField));
            if (!std::isfinite(position.average_open_price) || position.average_open_price <= 0.0) {
                std::cerr << "[Bitunix] Open position for " << symbol
                          << " has an invalid average price; refusing to scan." << std::endl;
                return false;
            }
            fetched.push_back(std::move(position));
        }
    } catch (const std::exception& exception) {
        std::cerr << "[Bitunix] Could not parse pending positions for " << symbol << ": "
                  << exception.what() << std::endl;
        return false;
    }
    positions = std::move(fetched);
    return true;
}

bool BitunixExchange::reconcileCycleTakeProfits(const std::string& symbol,
                                                const std::vector<BitunixPosition>& positions,
                                                double takeProfitPercentage,
                                                bool updateOpenPositionOrders) {
    if (apiKey.empty() || apiSecret.empty() || !std::isfinite(takeProfitPercentage) ||
        takeProfitPercentage <= 0.0 || takeProfitPercentage >= 1.0) {
        std::cerr << "[Bitunix] Refusing invalid cycle take-profit configuration for "
                  << symbol << std::endl;
        return false;
    }

    int basePrecision = 0;
    int quotePrecision = 0;
    double minimumQuantity = 0.0;
    double maximumMarketQuantity = 0.0;
    if (!fetchSymbolRules(symbol, basePrecision, quotePrecision, minimumQuantity, maximumMarketQuantity)) {
        return false;
    }

    json pendingResponse;
    const std::string pendingEndpoint = baseUrl + "/api/v1/futures/trade/get_pending_orders";
    if (!signedBitunixRequest(apiKey, apiSecret, "GET", pendingEndpoint, {{"symbol", symbol}},
                              json::object(), pendingResponse)) {
        return false;
    }
    if (!pendingResponse.contains("data") || !pendingResponse["data"].is_object() ||
        !pendingResponse["data"].contains("orderList") ||
        !pendingResponse["data"]["orderList"].is_array()) {
        std::cerr << "[Bitunix] Pending orders response is missing its orderList for "
                  << symbol << std::endl;
        return false;
    }
    const auto& pendingOrders = pendingResponse["data"]["orderList"];
    std::vector<bool> managedOrderHandled(pendingOrders.size(), false);
    auto isManagedCycleOrder = [](const json& order) {
        const std::string clientId = order.value("clientId", std::string());
        return clientId.rfind("fbcy", 0) == 0;
    };
    auto cancelOrder = [&](const json& order) {
        const std::string orderId = order.value("orderId", std::string());
        if (orderId.empty()) {
            std::cerr << "[Bitunix] Cannot replace managed cycle TP without its orderId." << std::endl;
            return false;
        }
        const json cancelBody = {
            {"symbol", symbol},
            {"orderList", json::array({json{{"orderId", orderId}}})}
        };
        json cancelResponse;
        if (!signedBitunixRequest(apiKey, apiSecret, "POST",
                                  baseUrl + "/api/v1/futures/trade/cancel_orders",
                                  {}, cancelBody, cancelResponse)) {
            return false;
        }
        const auto& data = cancelResponse["data"];
        if (!data.is_object() || !data.contains("successList") || !data["successList"].is_array() ||
            std::none_of(data["successList"].begin(), data["successList"].end(),
                         [&orderId](const json& result) {
                             return result.value("orderId", std::string()) == orderId ||
                                    result.value("id", std::string()) == orderId;
                         })) {
            std::cerr << "[Bitunix] Bitunix did not confirm cancellation of cycle TP order "
                      << orderId << std::endl;
            return false;
        }
        return true;
    };
    auto prefixForPosition = [&symbol](const std::string& positionId) {
        return "fbcy" + sha256Hex(symbol + ":" + positionId).substr(0, 12) + "-";
    };

    bool success = true;
    for (const auto& position : positions) {
        if (position.position_id.empty() ||
            (position.side != "LONG" && position.side != "SHORT") ||
            !std::isfinite(position.quantity) || position.quantity < minimumQuantity ||
            !std::isfinite(position.average_open_price) || position.average_open_price <= 0.0) {
            std::cerr << "[Bitunix] Invalid position data while reconciling cycle TP for "
                      << symbol << std::endl;
            return false;
        }
        const double rawTarget = position.side == "LONG"
                ? position.average_open_price * (1.0 + takeProfitPercentage)
                : position.average_open_price * (1.0 - takeProfitPercentage);
        if (!std::isfinite(rawTarget) || rawTarget <= 0.0) {
            std::cerr << "[Bitunix] Invalid cycle TP price for " << symbol << std::endl;
            return false;
        }
        const std::string targetPrice = decimalString(
                quantizeTakeProfit(rawTarget, quotePrecision, position.side), quotePrecision);
        const double normalizedQuantity =
                std::round(position.quantity * std::pow(10.0, basePrecision)) /
                std::pow(10.0, basePrecision);
        const std::string quantity = decimalString(normalizedQuantity, basePrecision);
        const std::string closeSide = position.side == "LONG" ? "SELL" : "BUY";
        const std::string clientPrefix = prefixForPosition(position.position_id);
        if (!updateOpenPositionOrders) continue;
        int keptOrder = -1;
        std::vector<std::size_t> positionOrderIndexes;

        for (std::size_t index = 0; index < pendingOrders.size(); ++index) {
            const auto& order = pendingOrders[index];
            const std::string clientId = order.value("clientId", std::string());
            if (clientId.rfind(clientPrefix, 0) != 0) continue;
            managedOrderHandled[index] = true;
            positionOrderIndexes.push_back(index);
            const std::string orderType = order.value("orderType", order.value("type", std::string()));
            bool matches = false;
            try {
                matches = order.value("symbol", std::string()) == symbol &&
                          order.value("side", std::string()) == closeSide &&
                          orderType == "LIMIT" &&
                          std::abs(jsonNumber(order.at("qty")) - normalizedQuantity) < 1e-10 &&
                          std::abs(jsonNumber(order.at("price")) - std::stod(targetPrice)) < 1e-10;
            } catch (const std::exception&) {
                matches = false;
            }
            if (matches && keptOrder < 0) {
                keptOrder = static_cast<int>(index);
            }
        }
        if (keptOrder < 0) {
            const std::string clientId = clientPrefix + createNonce().substr(0, 8);
            const json orderBody = {
                {"symbol", symbol},
                {"qty", quantity},
                {"price", targetPrice},
                {"side", closeSide},
                {"tradeSide", "CLOSE"},
                {"positionId", position.position_id},
                {"orderType", "LIMIT"},
                {"effect", "GTC"},
                {"reduceOnly", true},
                {"clientId", clientId}
            };
            json orderResponse;
            if (!signedBitunixRequest(apiKey, apiSecret, "POST",
                                      baseUrl + "/api/v1/futures/trade/place_order",
                                      {}, orderBody, orderResponse)) {
                return false;
            }
            std::cout << "[Bitunix] Placed cycle take-profit LIMIT for " << symbol << " "
                      << position.side << " qty " << quantity << " at " << targetPrice << std::endl;
        }
        for (const std::size_t index : positionOrderIndexes) {
            if (static_cast<int>(index) != keptOrder && !cancelOrder(pendingOrders[index])) {
                success = false;
            }
        }
    }

    for (std::size_t index = 0; index < pendingOrders.size(); ++index) {
        if (!managedOrderHandled[index] && isManagedCycleOrder(pendingOrders[index])) {
            const std::string clientId = pendingOrders[index].value("clientId", std::string());
            const bool positionStillOpen = std::any_of(
                    positions.begin(), positions.end(),
                    [&prefixForPosition, &clientId](const BitunixPosition& position) {
                        return clientId.rfind(prefixForPosition(position.position_id), 0) == 0;
                    });
            if (!positionStillOpen && !cancelOrder(pendingOrders[index])) success = false;
        }
    }
    return success;
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