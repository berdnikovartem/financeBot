#pragma once
#include <string>
#include <vector>

struct BotParams { //basic values
    int id = 0;
    std::string symbol;
    bool active = false;
    int rsi_period = 14;
    double rsi_overbought = 70.0;
    double rsi_oversold = 30.0;
    std::string timeframe = "1h";
    double hammer_multiplier = 1.5;
    double volume_surge_multiplier = 1.5;
    std::string trading_side = "BOTH";

    double price_step_percentage = 0.0;
    double take_profit_percentage = 0.0;
    double trade_quantity = 0.001;
    double last_long_price = 0.0;  // 0 means no active long position
    double last_short_price = 0.0; // 0 means no active short position
};

struct WorkerSession {
    std::string apiKey;
    std::string apiSecret;
};

class ConfigFetcher {
private:
    std::string api_base_url;
    std::string worker_jwt;
    std::string worker_service_token;
    std::string worker_user_email;

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
public:
    explicit ConfigFetcher(std::string url);

    bool fetchConfig(const std::string& symbol, BotParams& config_out); //will give back true if http request is succesful

    bool fetchActiveConfigs(std::vector<BotParams>& configs_out);

    bool refreshAccessToken();
    bool fetchWorkerSession(WorkerSession& session_out);

    void updateLastTradePrice(int botId, const std::string& positionSide, double executionPrice);
};