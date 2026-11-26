package com.example;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.List;
import java.util.Map;

public class BotConfigResponse {
    private String symbol;
    private boolean active;
    private double price_step_percentage;
    private double take_profit_percentage;
    private double trade_quantity;
    private double last_long_entry_price;
    private double last_short_entry_price;
    private int long_trade_count;
    private int short_trade_count;
    private ParametersDto parameters;

    public BotConfigResponse(String symbol, boolean active, double price_step_percentage,
                             double take_profit_percentage, double trade_quantity,
                             Double last_long_entry_price, Double last_short_entry_price,
                             int long_trade_count, int short_trade_count,
                             ParametersDto parameters) {
        this.symbol = symbol;
        this.active = active;
        this.price_step_percentage = price_step_percentage;
        this.take_profit_percentage = take_profit_percentage;
        this.trade_quantity = trade_quantity;
        this.last_long_entry_price = last_long_entry_price == null ? 0.0 : last_long_entry_price;
        this.last_short_entry_price = last_short_entry_price == null ? 0.0 : last_short_entry_price;
        this.long_trade_count = long_trade_count;
        this.short_trade_count = short_trade_count;
        this.parameters = parameters;
    }

    public String getSymbol() { return symbol; }
    public boolean isActive() { return active; }
    public double getPrice_step_percentage() { return price_step_percentage; }
    public double getTake_profit_percentage() { return take_profit_percentage; }
    public double getTrade_quantity() { return trade_quantity; }
    @JsonProperty("last_long_entry_price")
    public double getLastLongEntryPrice() { return last_long_entry_price; }
    @JsonProperty("last_short_entry_price")
    public double getLastShortEntryPrice() { return last_short_entry_price; }
    @JsonProperty("long_trade_count")
    public int getLongTradeCount() { return long_trade_count; }
    @JsonProperty("short_trade_count")
    public int getShortTradeCount() { return short_trade_count; }
    public ParametersDto getParameters() { return parameters; }

    public static class ParametersDto {
        private int rsi_period;
        private double rsi_overbought;
        private double rsi_oversold;
        private String timeframe;
        private double take_profit_pct;
        private double trade_quantity;
        private String take_profit_scope;
        private List<Map<String, Object>> rsi_indicators;
        private String hammer_timeframe;
        private List<Map<String, Object>> hammer_indicators;
        private List<Map<String, Object>> volume_indicators;

        public ParametersDto(int rsi_period, double rsi_overbought, double rsi_oversold,
                             String timeframe, double take_profit_pct, double trade_quantity,
                             String take_profit_scope,
                             List<Map<String, Object>> rsi_indicators, String hammer_timeframe,
                             List<Map<String, Object>> hammer_indicators,
                             List<Map<String, Object>> volume_indicators) {
            this.rsi_period = rsi_period;
            this.rsi_overbought = rsi_overbought;
            this.rsi_oversold = rsi_oversold;
            this.timeframe = timeframe;
            this.take_profit_pct = take_profit_pct;
            this.trade_quantity = trade_quantity;
            this.take_profit_scope = take_profit_scope;
            this.rsi_indicators = rsi_indicators;
            this.hammer_timeframe = hammer_timeframe;
            this.hammer_indicators = hammer_indicators;
            this.volume_indicators = volume_indicators;
        }

        public int getRsi_period() { return rsi_period; }
        public double getRsi_overbought() { return rsi_overbought; }
        public double getRsi_oversold() { return rsi_oversold; }
        public String getTimeframe() { return timeframe; }
        public double getTake_profit_pct() { return take_profit_pct; }
        public double getTrade_quantity() { return trade_quantity; }
        public String getTake_profit_scope() { return take_profit_scope; }
        public List<Map<String, Object>> getRsi_indicators() { return rsi_indicators; }
        public String getHammer_timeframe() { return hammer_timeframe; }
        public List<Map<String, Object>> getHammer_indicators() { return hammer_indicators; }
        public List<Map<String, Object>> getVolume_indicators() { return volume_indicators; }
    }
}