package com.example;

import com.fasterxml.jackson.annotation.JsonProperty;

public class BotConfigResponse {
    private Long id;
    private String symbol;
    private boolean active;
    private Double priceStepPercentage;
    private Double takeProfitPercentage;
    private Double tradeQuantity;
    private Double lastLongPrice;
    private Double lastShortPrice;
    private ParametersDto parameters;

    public BotConfigResponse(Long id, String symbol, boolean active, Double priceStepPercentage,
                             Double takeProfitPercentage, Double tradeQuantity, Double lastLongPrice, Double lastShortPrice,
                             ParametersDto parameters) {
        this.id = id;
        this.symbol = symbol;
        this.active = active;
        this.priceStepPercentage = priceStepPercentage;
        this.takeProfitPercentage = takeProfitPercentage;
        this.tradeQuantity = tradeQuantity;
        this.lastLongPrice = lastLongPrice;
        this.lastShortPrice = lastShortPrice;
        this.parameters = parameters;
    }

    // The C++ worker, the jsonb column and requests.http all speak snake_case,
    // so this DTO is pinned to snake_case instead of the default bean naming.
    @JsonProperty("id")
    public Long getId() { return id; }

    @JsonProperty("symbol")
    public String getSymbol() { return symbol; }

    @JsonProperty("active")
    public boolean isActive() { return active; }

    @JsonProperty("price_step_percentage")
    public Double getPriceStepPercentage() { return priceStepPercentage; }

    @JsonProperty("take_profit_percentage")
    public Double getTakeProfitPercentage() { return takeProfitPercentage; }

    @JsonProperty("trade_quantity")
    public Double getTradeQuantity() { return tradeQuantity; }

    @JsonProperty("last_long_price")
    public Double getLastLongPrice() { return lastLongPrice; }

    @JsonProperty("last_short_price")
    public Double getLastShortPrice() { return lastShortPrice; }

    @JsonProperty("parameters")
    public ParametersDto getParameters() { return parameters; }

    public static class ParametersDto {
        private int rsiPeriod;
        private double rsiOverbought;
        private double rsiOversold;
        private String timeframe;
        private double hammerMultiplier;
        private double volumeSurgeMultiplier;
        private String tradingSide;

        public ParametersDto(int rsiPeriod, double rsiOverbought, double rsiOversold, String timeframe,
                     double hammerMultiplier, double volumeSurgeMultiplier, String tradingSide) {
            this.rsiPeriod = rsiPeriod;
            this.rsiOverbought = rsiOverbought;
            this.rsiOversold = rsiOversold;
            this.timeframe = timeframe;
            this.hammerMultiplier = hammerMultiplier;
            this.volumeSurgeMultiplier = volumeSurgeMultiplier;
            this.tradingSide = tradingSide;
        }

        @JsonProperty("rsi_period")
        public int getRsiPeriod() { return rsiPeriod; }

        @JsonProperty("rsi_overbought")
        public double getRsiOverbought() { return rsiOverbought; }

        @JsonProperty("rsi_oversold")
        public double getRsiOversold() { return rsiOversold; }

        @JsonProperty("timeframe")
        public String getTimeframe() { return timeframe; }

        @JsonProperty("hammer_multiplier")
        public double getHammerMultiplier() { return hammerMultiplier; }

        @JsonProperty("volume_surge_multiplier")
        public double getVolumeSurgeMultiplier() { return volumeSurgeMultiplier; }

        @JsonProperty("trading_side")
        public String getTradingSide() { return tradingSide; }
    }
}