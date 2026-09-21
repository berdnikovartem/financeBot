#pragma once
#include <string>
#include <vector>

struct RsiConfig {
    int period = 14;
    double overbought = 70.0;
    double oversold = 30.0;
    std::string timeframe = "1h";
};
struct PatternConfig { std::string timeframe = "1h"; double multiplier = 1.5; };

struct BotParams { //basic values
    std::string symbol;
    bool active = false;
    std::vector<RsiConfig> rsi_indicators;
    std::vector<PatternConfig> hammer_indicators;
    std::vector<PatternConfig> volume_indicators;
    std::string hammer_timeframe = "1h";

    // Legacy single-RSI fields retained for compatibility with existing callers.
    int rsi_period = 14;
    double rsi_overbought = 70.0;
    double rsi_oversold = 30.0;
    std::string timeframe = "1h";
    double take_profit_pct = 0.05;
    double trade_quantity = 0.001;
};

struct WorkerSession {
    std::string email;
    std::string api_key;
    std::string api_secret;
    std::vector<BotParams> configs;
};

class ConfigFetcher {
private:
    std::string api_base_url;

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
public:
    explicit ConfigFetcher(std::string url);

    bool fetchConfig(const std::string& symbol, BotParams& config_out); //will give back true if http request is succesful

    bool fetchActiveConfigs(std::vector<BotParams>& configs_out);
    bool fetchWorkerSessions(std::vector<WorkerSession>& sessions_out);
};