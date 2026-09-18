#include "ConfigFetcher.h"
#include <iostream>
#include <cstdlib>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {
void parseIndicatorConfig(const json& params, BotParams& config) {
    config.rsi_indicators.clear();
    if (params.contains("rsi_indicators") && params["rsi_indicators"].is_array()) {
        for (const auto& item : params["rsi_indicators"]) {
            if (config.rsi_indicators.size() == 3) break;
            if (!item.is_object()) continue;
            RsiConfig indicator;
            indicator.period = item.value("period", 14);
            indicator.overbought = item.value("overbought", 70.0);
            indicator.oversold = item.value("oversold", 30.0);
            indicator.timeframe = item.value("timeframe", "1h");
            if (indicator.period > 0 && !indicator.timeframe.empty()) {
                config.rsi_indicators.push_back(indicator);
            }
        }
    }
    if (config.rsi_indicators.empty()) {
        config.rsi_indicators.push_back({
            params.value("rsi_period", 14),
            params.value("rsi_overbought", 70.0),
            params.value("rsi_oversold", 30.0),
            params.value("timeframe", "1h")
        });
    }
    config.rsi_period = config.rsi_indicators.front().period;
    config.rsi_overbought = config.rsi_indicators.front().overbought;
    config.rsi_oversold = config.rsi_indicators.front().oversold;
    config.timeframe = config.rsi_indicators.front().timeframe;
    config.hammer_timeframe = params.value("hammer_timeframe", config.timeframe);
    auto parsePatterns = [&params, &config](const char* key, std::vector<PatternConfig>& output) {
        output.clear();
        if (params.contains(key) && params[key].is_array()) {
            for (const auto& item : params[key]) {
                if (output.size() == 3 || !item.is_object()) break;
                PatternConfig pattern;
                pattern.timeframe = item.value("timeframe", "1h");
                pattern.multiplier = item.value("multiplier", 1.5);
                if (!pattern.timeframe.empty() && pattern.multiplier > 0.0) output.push_back(pattern);
            }
        }
        if (output.empty()) output.push_back(PatternConfig{config.timeframe, 1.5});
    };
    parsePatterns("hammer_indicators", config.hammer_indicators);
    parsePatterns("volume_indicators", config.volume_indicators);
    config.hammer_timeframe = config.hammer_indicators.front().timeframe;
}
}

ConfigFetcher::ConfigFetcher(std::string url) : api_base_url(std::move(url)) {}

namespace {
curl_slist* addWorkerHeaders(curl_slist* headers) {
    const char* token = std::getenv("APP_WORKER_SERVICE_TOKEN");
    const char* email = std::getenv("WORKER_USER_EMAIL");
    if (!token || std::string(token).empty() || !email || std::string(email).empty()) {
        return headers;
    }

    headers = curl_slist_append(headers, (std::string("X-Worker-Token: ") + token).c_str());
    headers = curl_slist_append(headers, (std::string("X-Worker-User: ") + email).c_str());
    return headers;
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
    curl_slist* headers = addWorkerHeaders(nullptr);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res!=CURLE_OK || http_code != 200) {
        std::cerr << "ERROR with fetching the config, HTTP code is-> " << http_code;
        if (res != CURLE_OK) {
            std::cerr << ", curl error: " << curl_easy_strerror(res);
        }
        std::cerr << " (worker authentication headers may be missing)" << std::endl;
        return false;
    }

    try {
        json data = json::parse(readBuffer);

        config_out.symbol = symbol;
        config_out.active = data.value("active", false);
        if (data.contains("parameters") && data["parameters"].is_object()) {
            auto params = data["parameters"];

            parseIndicatorConfig(params, config_out);
            config_out.take_profit_pct = params.value("take_profit_pct", 0.05);
            config_out.trade_quantity = params.value("trade_quantity", 0.001);
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
    curl_slist* headers = addWorkerHeaders(nullptr);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code != 200) {
        std::cerr << "ERROR with fetching active configs, HTTP code is-> " << http_code;
        if (res != CURLE_OK) {
            std::cerr << ", curl error: " << curl_easy_strerror(res);
        }
        std::cerr << " (worker authentication headers may be missing)" << std::endl;
        return false;
    }

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
                parseIndicatorConfig(params, config);
                config.take_profit_pct = params.value("take_profit_pct", 0.05);
                config.trade_quantity = params.value("trade_quantity", 0.001);
            }
            configs_out.push_back(config);
        }
        return true;
    } catch (...) {
        return false;
    }
}
