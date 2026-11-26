#include "ConfigFetcher.h"
#include <cmath>
#include <iostream>
#include <cstdlib>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {
void parseIndicatorConfig(const json& params, BotParams& config) {
    config.rsi_indicators.clear();
    const bool rsiConfigured = params.contains("rsi_indicators") && params["rsi_indicators"].is_array();
    if (rsiConfigured) {
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
    if (config.rsi_indicators.empty() && !rsiConfigured) {
        config.rsi_indicators.push_back({
            params.value("rsi_period", 14),
            params.value("rsi_overbought", 70.0),
            params.value("rsi_oversold", 30.0),
            params.value("timeframe", "1h")
        });
    }
    config.rsi_period = params.value("rsi_period", 14);
    config.rsi_overbought = params.value("rsi_overbought", 70.0);
    config.rsi_oversold = params.value("rsi_oversold", 30.0);
    config.timeframe = params.value("timeframe", "1h");
    if (!config.rsi_indicators.empty()) {
        config.rsi_period = config.rsi_indicators.front().period;
        config.rsi_overbought = config.rsi_indicators.front().overbought;
        config.rsi_oversold = config.rsi_indicators.front().oversold;
        config.timeframe = config.rsi_indicators.front().timeframe;
    }
    auto parsePatterns = [&params, &config](const char* key, std::vector<PatternConfig>& output) {
        output.clear();
        const bool configured = params.contains(key) && params[key].is_array();
        if (configured) {
            for (const auto& item : params[key]) {
                if (output.size() == 3 || !item.is_object()) break;
                PatternConfig pattern;
                pattern.timeframe = item.value("timeframe", "1h");
                pattern.multiplier = item.value("multiplier", 1.5);
                if (!pattern.timeframe.empty() && pattern.multiplier > 0.0) output.push_back(pattern);
            }
        }
        if (!configured) output.push_back(PatternConfig{config.timeframe, 1.5});
    };
    parsePatterns("hammer_indicators", config.hammer_indicators);
    parsePatterns("volume_indicators", config.volume_indicators);
    config.hammer_timeframe = config.hammer_indicators.empty()
            ? config.timeframe
            : config.hammer_indicators.front().timeframe;
}
}

ConfigFetcher::ConfigFetcher(std::string url) : api_base_url(std::move(url)) {}

namespace {
curl_slist* addWorkerHeaders(curl_slist* headers) {
    const char* token = std::getenv("APP_WORKER_SERVICE_TOKEN");
    const char* email = std::getenv("WORKER_USER_EMAIL");
    if (!token || std::string(token).empty()) {
        return headers;
    }

    headers = curl_slist_append(headers, (std::string("X-Worker-Token: ") + token).c_str());
    if (email && !std::string(email).empty()) {
        headers = curl_slist_append(headers, (std::string("X-Worker-User: ") + email).c_str());
    }
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
        config_out.price_step_percentage = data.value("price_step_percentage", 0.0);
        if (data.contains("parameters") && data["parameters"].is_object()) {
            auto params = data["parameters"];

            parseIndicatorConfig(params, config_out);
            config_out.take_profit_pct = params.value("take_profit_pct", 0.05);
            config_out.take_profit_scope = params.value("take_profit_scope", std::string("CYCLE"));
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
            config.price_step_percentage = item.value("price_step_percentage", 0.0);

            if (item.contains("parameters") && item["parameters"].is_object()) {
                auto params = item["parameters"];
                parseIndicatorConfig(params, config);
                config.take_profit_pct = params.value("take_profit_pct", 0.05);
                config.take_profit_scope = params.value("take_profit_scope", std::string("CYCLE"));
                config.trading_side = params.value("trading_side", std::string("BOTH"));
                config.trade_quantity = params.value("trade_quantity", 0.001);
            }
            configs_out.push_back(config);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool ConfigFetcher::fetchWorkerSessions(std::vector<WorkerSession>& sessions_out) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    std::string readBuffer;
    const std::string endpoint = api_base_url + "/api/v1/worker/sessions";
    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_slist* headers = addWorkerHeaders(nullptr);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    const CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || http_code != 200) {
        std::cerr << "ERROR with fetching worker sessions, HTTP code is-> " << http_code;
        if (res != CURLE_OK) std::cerr << ", curl error: " << curl_easy_strerror(res);
        std::cerr << std::endl;
        return false;
    }
    try {
        const json payload = json::parse(readBuffer);
        if (!payload.is_array()) return false;
        sessions_out.clear();
        for (const auto& session_json : payload) {
            WorkerSession session;
            session.email = session_json.value("email", "");
            session.api_key = session_json.value("apiKey", "");
            session.api_secret = session_json.value("apiSecret", "");
            if (session.email.empty() || session.api_key.empty() || session.api_secret.empty()) continue;
            for (const auto& item : session_json.value("configs", json::array())) {
                BotParams config;
                config.symbol = item.value("symbol", "UNKNOWN");
                config.active = item.value("active", true);
                config.price_step_percentage = item.value("price_step_percentage", 0.0);
                config.last_long_entry_price = item.value("last_long_entry_price", 0.0);
                config.last_short_entry_price = item.value("last_short_entry_price", 0.0);
                config.long_trade_count = item.value("long_trade_count", 0);
                config.short_trade_count = item.value("short_trade_count", 0);
                if (item.contains("parameters") && item["parameters"].is_object()) {
                    const auto& params = item["parameters"];
                    parseIndicatorConfig(params, config);
                    config.take_profit_pct = params.value("take_profit_pct", 0.05);
                    config.take_profit_scope = params.value("take_profit_scope", std::string("CYCLE"));
                    config.trading_side = params.value("trading_side", std::string("BOTH"));
                    config.trade_quantity = params.value("trade_quantity", 0.001);
                }
                session.configs.push_back(config);
            }
            if (!session.configs.empty()) sessions_out.push_back(std::move(session));
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "ERROR parsing worker sessions: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigFetcher::persistCycleState(const std::string& email, const std::string& symbol,
                                      double longLastEntryPrice, int longTradeCount,
                                      double shortLastEntryPrice, int shortTradeCount) {
    if (email.empty() || symbol.empty() || !std::isfinite(longLastEntryPrice) ||
        !std::isfinite(shortLastEntryPrice) || longLastEntryPrice < 0.0 ||
        shortLastEntryPrice < 0.0 || longTradeCount < 0 || shortTradeCount < 0 ||
        ((longTradeCount == 0) != (longLastEntryPrice == 0.0)) ||
        ((shortTradeCount == 0) != (shortLastEntryPrice == 0.0))) {
        std::cerr << "[WORKER STATE] Refusing invalid cycle state for " << symbol << std::endl;
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[WORKER STATE] Could not initialize cycle-state request for "
                  << symbol << std::endl;
        return false;
    }

    const std::string endpoint = api_base_url + "/api/v1/worker/cycle-state";
    const json body = {
        {"email", email},
        {"symbol", symbol},
        {"longLastEntryPrice", longLastEntryPrice},
        {"longTradeCount", longTradeCount},
        {"shortLastEntryPrice", shortLastEntryPrice},
        {"shortTradeCount", shortTradeCount}
    };
    const std::string requestBody = body.dump();
    curl_slist* headers = addWorkerHeaders(nullptr);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(requestBody.size()));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    const CURLcode result = curl_easy_perform(curl);
    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK || httpStatus < 200 || httpStatus >= 300) {
        std::cerr << "[WORKER STATE] Failed to persist cycle state for " << symbol
                  << " (HTTP " << httpStatus;
        if (result != CURLE_OK) std::cerr << ", " << curl_easy_strerror(result);
        std::cerr << ")" << std::endl;
        return false;
    }
    return true;
}
