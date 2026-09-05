#pragma once
#include <string>
#include <vector>

struct BotParams { //basic values
    std::string symbol;
    bool active = false;
    int rsi_period = 14;
    double rsi_overbought = 70.0;
    double rsi_oversold = 30.0;
    std::string timeframe = "1h";
};

class ConfigFetcher {
private:
    std::string api_base_url;

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
public:
    explicit ConfigFetcher(std::string url);

    bool fetchConfig(const std::string& symbol, BotParams& config_out); //will give back true if http request is succesful

    bool fetchActiveConfigs(std::vector<BotParams>& configs_out);
};