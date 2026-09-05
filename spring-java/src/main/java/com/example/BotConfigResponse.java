package com.example;

public class BotConfigResponse {
    private String symbol;
    private boolean active;
    private ParametersDto parameters;

    public BotConfigResponse(String symbol, boolean active, ParametersDto parameters) {
        this.symbol = symbol;
        this.active = active;
        this.parameters = parameters;
    }

    public String getSymbol() { return symbol; }
    public boolean isActive() { return active; }
    public ParametersDto getParameters() { return parameters; }

    public static class ParametersDto {
        private int rsi_period;
        private double rsi_overbought;
        private double rsi_oversold;
        private String timeframe;
        //private double stop_loss_pct;
        //private double take_profit_pct;
        //private double trade_amount_usdt;

        public ParametersDto(int rsi_period, double rsi_overbought, double rsi_oversold,
                             String timeframe){ //double stop_loss_pct, double take_profit_pct, double trade_amount_usdt) {
            this.rsi_period = rsi_period;
            this.rsi_overbought = rsi_overbought;
            this.rsi_oversold = rsi_oversold;
            this.timeframe = timeframe;
            //this.stop_loss_pct = stop_loss_pct;
            //this.take_profit_pct = take_profit_pct;
            //this.trade_amount_usdt = trade_amount_usdt;
        }

        public int getRsi_period() { return rsi_period; }
        public double getRsi_overbought() { return rsi_overbought; }
        public double getRsi_oversold() { return rsi_oversold; }
        public String getTimeframe() { return timeframe; }
        //public double getStop_loss_pct() { return stop_loss_pct; }
        //public double getTake_profit_pct() { return take_profit_pct; }
        //public double getTrade_amount_usdt() { return trade_amount_usdt; }
    }
}