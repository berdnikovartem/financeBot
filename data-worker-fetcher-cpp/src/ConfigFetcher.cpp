#include "ConfigFetcher.h"
#include <iostream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

ConfigFetcher::ConfigFetcher(std::string url) : api_base_url(std::move(url)) {}

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

    res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

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

    res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code != 200) return false;

    try {
        json json_array = json::parse(readBuffer);
        configs_out.clear();

        if (!json_array.is_array()) return false;

        for (auto& item : json_array) {
            BotParams config;
            config.symbol = item.value("symbol", "UNKNOWN");
            config.active = item.value("active", true);

            if (item.contains("parameters") && item["parameters"].is_object()) {
                auto params = item["parameters"];
                config.rsi_period = params.value("rsi_period", 14);
                config.rsi_overbought = params.value("rsi_overbought", 70.0);
                config.rsi_oversold = params.value("rsi_oversold", 30.0);
                config.timeframe = params.value("timeframe", "1h");
                //config.stop_loss_pct = params.value("stop_loss_pct", 0.02);
                //config.take_profit_pct = params.value("take_profit_pct", 0.05);
                //config.trade_amount_usdt = params.value("trade_amount_usdt", 100.0);
            }
            configs_out.push_back(config);
        }
        return true;
    } catch (...) {
        return false;
    }
}

