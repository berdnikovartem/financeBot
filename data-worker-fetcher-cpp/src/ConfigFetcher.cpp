#include "ConfigFetcher.h"
#include <iostream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <cstdlib>

using json = nlohmann::json;

ConfigFetcher::ConfigFetcher(std::string url) : api_base_url(std::move(url)) {
    const char* token = std::getenv("WORKER_JWT");
    if (token) worker_jwt = token;
    const char* serviceToken = std::getenv("WORKER_SERVICE_TOKEN");
    if (serviceToken) worker_service_token = serviceToken;
    const char* userEmail = std::getenv("WORKER_USER_EMAIL");
    if (userEmail) worker_user_email = userEmail;
}

bool ConfigFetcher::refreshAccessToken() {
    const char* refresh = std::getenv("WORKER_REFRESH_TOKEN");
    if (!refresh) return false;

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string body = std::string("{\"refreshToken\":\"") + refresh + "\"}";
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, (api_base_url + "/api/v1/auth/refresh").c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    struct curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    CURLcode result = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK || httpCode != 200) return false;
    try {
        worker_jwt = json::parse(response).at("token").get<std::string>();
        return true;
    } catch (...) {
        return false;
    }
}

bool ConfigFetcher::fetchWorkerSession(WorkerSession& session_out) {
    CURL* curl = curl_easy_init();
    if (!curl || (worker_jwt.empty() && (worker_service_token.empty() || worker_user_email.empty()))) return false;

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, (api_base_url + "/api/v1/worker/session").c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    struct curl_slist* headers = nullptr;
    if (!worker_service_token.empty()) {
        headers = curl_slist_append(headers, ("X-Worker-Token: " + worker_service_token).c_str());
        headers = curl_slist_append(headers, ("X-Worker-User: " + worker_user_email).c_str());
    } else {
        headers = curl_slist_append(headers, ("Authorization: Bearer " + worker_jwt).c_str());
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    CURLcode result = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK || httpCode != 200) {
        std::cerr << " [WORKER SESSION ERROR] curl=" << curl_easy_strerror(result)
                  << " HTTP " << httpCode << ": " << response << std::endl;
        return false;
    }
    try {
        auto jsonResponse = json::parse(response);
        session_out.apiKey = jsonResponse.at("apiKey").get<std::string>();
        session_out.apiSecret = jsonResponse.at("apiSecret").get<std::string>();
        if (session_out.apiKey.empty() || session_out.apiSecret.empty()) {
            std::cerr << " [WORKER SESSION ERROR] API credentials were empty." << std::endl;
            return false;
        }
        std::cout << " [WORKER SESSION] Bitunix credentials loaded successfully." << std::endl;
        return true;
    } catch (...) {
        return false;
    }
}

// with the use of libcurl libriary, creating one string that will have all data in it
size_t ConfigFetcher::WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size *nmemb;
}

bool ConfigFetcher::fetchConfig(const std::string& symbol, BotParams& config_out) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;
    std::string endpoint = api_base_url + "/api/v1/config/" + symbol;

    curl = curl_easy_init();
    if (!curl) {
        std::cerr << "ERROR with libcurl initialization" << std::endl;
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); //5L = 5sec
    struct curl_slist* headers = nullptr;
    if (!worker_service_token.empty()) {
        headers = curl_slist_append(headers, ("X-Worker-Token: " + worker_service_token).c_str());
        headers = curl_slist_append(headers, ("X-Worker-User: " + worker_user_email).c_str());
    } else if (!worker_jwt.empty()) headers = curl_slist_append(headers, ("Authorization: Bearer " + worker_jwt).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res!=CURLE_OK || http_code != 200) {
        std::cerr << "ERROR with fetching the config, HTTP code is-> " << http_code << std::endl;
        return false;
    }

    try {
        json data = json::parse(readBuffer);

        config_out.symbol = symbol;
        config_out.active = data.value("active", false);
        if (data.contains("parameters") && data["parameters"].is_object()) {
            auto params = data["parameters"];

            config_out.rsi_overbought = params.value("rsi_overbought", 70.0);
            config_out.rsi_oversold = params.value("rsi_oversold", 30.0);
            config_out.rsi_period = params.value("rsi_period", 14);
            config_out.timeframe = params.value("timeframe", "1h");
            config_out.hammer_multiplier = params.value("hammer_multiplier", 1.5);
            config_out.volume_surge_multiplier = params.value("volume_surge_multiplier", 1.5);
            config_out.trading_side = params.value("trading_side", "BOTH");
        }

        return true;
    } catch (const json::parse_error& e) {
        std::cerr << "ERROR with a JSON parse -> " << e.what() << std::endl;
        return false;
    }
}

bool ConfigFetcher::fetchActiveConfigs(std::vector<BotParams>& configs_out) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;
    std::string endpoint = api_base_url + "/api/v1/configs/active";

    curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    struct curl_slist* headers = nullptr;
    if (!worker_service_token.empty()) {
        headers = curl_slist_append(headers, ("X-Worker-Token: " + worker_service_token).c_str());
        headers = curl_slist_append(headers, ("X-Worker-User: " + worker_user_email).c_str());
    } else if (!worker_jwt.empty()) headers = curl_slist_append(headers, ("Authorization: Bearer " + worker_jwt).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK || http_code != 200) {
        if (http_code == 401 && refreshAccessToken()) return fetchActiveConfigs(configs_out);
        return false;
    }

    try {
        json json_array = json::parse(readBuffer);
        configs_out.clear();

        if (!json_array.is_array()) return false;

        for (auto& item : json_array) {
            BotParams config;
            config.id = item.value("id", 0);
            config.symbol = item.value("symbol", "UNKNOWN");
            config.active = item.value("active", true);

            // DCA grid state, kept in the bot_configs table by the Java API
            config.price_step_percentage = item.value("price_step_percentage", 0.0);
            config.take_profit_percentage = item.value("take_profit_percentage", 0.0);
            config.trade_quantity = item.value("trade_quantity", 0.001);
            config.last_long_price = item.value("last_long_price", 0.0);
            config.last_short_price = item.value("last_short_price", 0.0);

            if (item.contains("parameters") && item["parameters"].is_object()) {
                auto params = item["parameters"];
                config.rsi_period = params.value("rsi_period", 14);
                config.rsi_overbought = params.value("rsi_overbought", 70.0);
                config.rsi_oversold = params.value("rsi_oversold", 30.0);
                config.timeframe = params.value("timeframe", "1h");
                config.hammer_multiplier = params.value("hammer_multiplier", 1.5);
                config.volume_surge_multiplier = params.value("volume_surge_multiplier", 1.5);
                config.trading_side = params.value("trading_side", "BOTH");
                config.take_profit_percentage = item.value("take_profit_percentage", params.value("take_profit_percentage", 0.0));
                config.trade_quantity = item.value("trade_quantity", params.value("trade_quantity", 0.001));
                //config.stop_loss_pct = params.value("stop_loss_pct", 0.02);
                //config.trade_amount_usdt = params.value("trade_amount_usdt", 100.0);
            }
            configs_out.push_back(config);
        }
        return true;
    } catch (...) {
        return false;
    }
}

void ConfigFetcher::updateLastTradePrice(int botId, const std::string& positionSide, double executionPrice) {
    CURL* curl = curl_easy_init();
    if (curl) {
        // just an regular example: http://java-api:8080/api/bots/1/update-last-price?positionSide=LONG&price=62000.5
        std::string url = "http://java-api:8080/api/v1/" + std::to_string(botId) +
                          "/update-last-price?positionSide=" + positionSide +
                          "&price=" + std::to_string(executionPrice);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L); // No body needed, just URL params
        struct curl_slist* headers = nullptr;
        if (!worker_service_token.empty()) {
            headers = curl_slist_append(headers, ("X-Worker-Token: " + worker_service_token).c_str());
            headers = curl_slist_append(headers, ("X-Worker-User: " + worker_user_email).c_str());
        } else if (!worker_jwt.empty()) headers = curl_slist_append(headers, ("Authorization: Bearer " + worker_jwt).c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        CURLcode res = curl_easy_perform(curl);
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        if (res != CURLE_OK) {
            std::cerr << " [API ERROR] Failed to update last price for Bot " << botId << ": " << curl_easy_strerror(res) << std::endl;
        } else if (httpCode < 200 || httpCode >= 300) {
            std::cerr << " [API ERROR] Failed to update Bot " << botId << " " << positionSide
                      << " price, HTTP " << httpCode << std::endl;
        } else {
            std::cout << " [API SUCCESS] Updated Bot " << botId << " " << positionSide << " price to " << executionPrice << std::endl;
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}