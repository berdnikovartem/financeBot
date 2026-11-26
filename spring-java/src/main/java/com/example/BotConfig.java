package com.example;

import org.hibernate.annotations.JdbcTypeCode;
import org.hibernate.type.SqlTypes;
import jakarta.persistence.*;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonSetter;
import java.time.Instant;
import java.util.Map;

@Entity
@Table(name= "bot_configs")
public class BotConfig{

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @ManyToOne(fetch = FetchType.LAZY, optional = true)
    @JoinColumn(name = "user_id")
    @com.fasterxml.jackson.annotation.JsonIgnore
    private AppUser user;

    @Column(unique = true, nullable = false) //unique allows only one symbol per database
    private String symbol;

    @Column(name = "price_step_percentage")
    private Double priceStepPercentage;

    @Column(name = "last_long_price")
    private Double lastLongPrice;

    @Column(name = "last_short_price")
    private Double lastShortPrice;

    @Column(name = "last_long_entry_price")
    private Double lastLongEntryPrice;

    @Column(name = "last_short_entry_price")
    private Double lastShortEntryPrice;

    @Column(name = "long_trade_count", nullable = false)
    private Integer longTradeCount = 0;

    @Column(name = "short_trade_count", nullable = false)
    private Integer shortTradeCount = 0;

    @Column(name = "updated_at", nullable = false)
    private Instant updatedAt;

    @Transient
    private Double takeProfitPercentageOverride;

    @Transient
    private Double tradeQuantityOverride;

    @JdbcTypeCode(SqlTypes.JSON)
    @Column(columnDefinition = "jsonb")
    private Map<String, Object> parameters; //easy to scale thanks to the fact that
                                            //all params like rsi etc. are stored in json

    private boolean active;

    public Long getId(){
        return id;
    }

    public void setId(long id){
        this.id = id;
    }

    public AppUser getUser() {
        return user;
    }

    public void setUser(AppUser user) {
        this.user = user;
    }

    public String getSymbol() {
        return symbol;
    }

    public void setSymbol(String symbol) {
        this.symbol = symbol;
    }

    public Instant getUpdatedAt() {
        return updatedAt;
    }

    @PrePersist
    protected void onCreate() {
        updatedAt = Instant.now();
    }

    @PreUpdate
    protected void onUpdate() {
        updatedAt = Instant.now();
    }

    public boolean isActive() {
        return active;
    }

    public void setActive(boolean active) {
        this.active = active;
    }

    public Map<String, Object> getParameters() {
        return parameters;
    }

    public void setParameters(Map<String, Object> parameters) {
        this.parameters = parameters;
    }

    @JsonProperty("price_step_percentage")
    public double getPriceStepPercentage() {
        return priceStepPercentage == null ? 0.0 : priceStepPercentage;
    }

    @JsonSetter("price_step_percentage")
    public void setPriceStepPercentage(Double priceStepPercentage) {
        this.priceStepPercentage = priceStepPercentage;
    }

    @JsonProperty("take_profit_percentage")
    public double getTakeProfitPercentage() {
        if (takeProfitPercentageOverride != null) return takeProfitPercentageOverride;
        if (parameters != null && parameters.containsKey("take_profit_percentage")) {
            return ((Number) parameters.get("take_profit_percentage")).doubleValue();
        }
        return getTakeProfitPct() * 100.0;
    }

    @JsonSetter("take_profit_percentage")
    public void setTakeProfitPercentage(Double percentage) {
        takeProfitPercentageOverride = percentage;
        parameters = parameters == null ? new java.util.HashMap<>() : parameters;
        parameters.put("take_profit_pct", percentage == null ? 0.0 : percentage / 100.0);
    }

    @JsonProperty("trade_quantity")
    public double getTradeQuantityForApi() {
        return tradeQuantityOverride != null ? tradeQuantityOverride : getTradeQuantity();
    }

    @JsonSetter("trade_quantity")
    public void setTradeQuantityForApi(Double quantity) {
        tradeQuantityOverride = quantity;
        parameters = parameters == null ? new java.util.HashMap<>() : parameters;
        parameters.put("trade_quantity", quantity == null ? 0.001 : quantity);
    }

    public Double getLastLongPrice() {
        return lastLongPrice;
    }

    public void setLastLongPrice(Double lastLongPrice) {
        this.lastLongPrice = lastLongPrice;
    }

    public Double getLastShortPrice() {
        return lastShortPrice;
    }

    public void setLastShortPrice(Double lastShortPrice) {
        this.lastShortPrice = lastShortPrice;
    }

    public Double getLastLongEntryPrice() {
        return lastLongEntryPrice;
    }

    public void setLastLongEntryPrice(Double lastLongEntryPrice) {
        this.lastLongEntryPrice = lastLongEntryPrice;
    }

    public Double getLastShortEntryPrice() {
        return lastShortEntryPrice;
    }

    public void setLastShortEntryPrice(Double lastShortEntryPrice) {
        this.lastShortEntryPrice = lastShortEntryPrice;
    }

    public int getLongTradeCount() {
        return longTradeCount == null ? 0 : longTradeCount;
    }

    public void setLongTradeCount(Integer longTradeCount) {
        this.longTradeCount = longTradeCount;
    }

    public int getShortTradeCount() {
        return shortTradeCount == null ? 0 : shortTradeCount;
    }

    public void setShortTradeCount(Integer shortTradeCount) {
        this.shortTradeCount = shortTradeCount;
    }

    public void recordAverageTradePrice(String positionSide, double price) {
        if ("LONG".equalsIgnoreCase(positionSide)) {
            int count = longTradeCount == null ? 0 : longTradeCount;
            double average = lastLongPrice == null ? 0.0 : lastLongPrice;
            lastLongPrice = average <= 0.0 || count <= 0
                    ? price
                    : ((average * count) + price) / (count + 1.0);
            longTradeCount = count <= 0 || average <= 0.0 ? 1 : count + 1;
        } else if ("SHORT".equalsIgnoreCase(positionSide)) {
            int count = shortTradeCount == null ? 0 : shortTradeCount;
            double average = lastShortPrice == null ? 0.0 : lastShortPrice;
            lastShortPrice = average <= 0.0 || count <= 0
                    ? price
                    : ((average * count) + price) / (count + 1.0);
            shortTradeCount = count <= 0 || average <= 0.0 ? 1 : count + 1;
        }
    }

    public int getRsiPeriod() {
        if (parameters == null || !parameters.containsKey("rsi_period")) return 14;
        return ((Number) parameters.get("rsi_period")).intValue();
    }

    public double getRsiOverbought() {
        if (parameters == null || !parameters.containsKey("rsi_overbought")) return 70.0;
        return ((Number) parameters.get("rsi_overbought")).doubleValue();
    }

    public double getRsiOversold() {
        if (parameters == null || !parameters.containsKey("rsi_oversold")) return 30.0;
        return ((Number) parameters.get("rsi_oversold")).doubleValue();
    }

    public String getTimeframe() {
        if (parameters == null || !parameters.containsKey("timeframe")) return "1h";
        return (String) parameters.get("timeframe");
    }

    public double getTakeProfitPct() {
        if (parameters == null || !parameters.containsKey("take_profit_pct")) return 0.05;
        return ((Number) parameters.get("take_profit_pct")).doubleValue();
    }

    public double getTradeQuantity() {
        if (parameters == null || !parameters.containsKey("trade_quantity")) return 0.001;
        return ((Number) parameters.get("trade_quantity")).doubleValue();
    }

    public String getTakeProfitScope() {
        Object scope = parameters == null ? null : parameters.get("take_profit_scope");
        return "TRADE".equals(scope) ? "TRADE" : "CYCLE";
    }

    public String getTradingSide() {
        Object side = parameters == null ? null : parameters.get("trading_side");
        if (side == null) return "BOTH";
        if (side instanceof String tradingSide &&
                ("LONG".equals(tradingSide) || "SHORT".equals(tradingSide) || "BOTH".equals(tradingSide))) {
            return tradingSide;
        }
        return "INVALID";
    }

    public java.util.List<java.util.Map<String, Object>> getRsiIndicators() {
        if (parameters == null || !(parameters.get("rsi_indicators") instanceof java.util.List<?>)) {
            return java.util.List.of(java.util.Map.of(
                    "period", getRsiPeriod(),
                    "overbought", getRsiOverbought(),
                    "oversold", getRsiOversold(),
                    "timeframe", getTimeframe()));
        }
        java.util.List<java.util.Map<String, Object>> indicators =
                (java.util.List<java.util.Map<String, Object>>) (java.util.List<?>) parameters.get("rsi_indicators");
        return indicators.subList(0, Math.min(3, indicators.size()));
    }

    public String getHammerTimeframe() {
        return parameters != null && parameters.containsKey("hammer_timeframe")
                ? (String) parameters.get("hammer_timeframe")
                : getTimeframe();
    }

    public java.util.List<java.util.Map<String, Object>> getHammerIndicators() {
        return getIndicatorList("hammer_indicators", java.util.Map.of("timeframe", getHammerTimeframe(), "multiplier", 1.5));
    }

    public java.util.List<java.util.Map<String, Object>> getVolumeIndicators() {
        return getIndicatorList("volume_indicators", java.util.Map.of("timeframe", getTimeframe(), "multiplier", 1.5));
    }

    private java.util.List<java.util.Map<String, Object>> getIndicatorList(String key, java.util.Map<String, Object> fallback) {
        if (parameters == null || !(parameters.get(key) instanceof java.util.List<?> raw)) {
            return java.util.List.of(fallback);
        }
        return ((java.util.List<java.util.Map<String, Object>>) (java.util.List<?>) raw)
                .subList(0, Math.min(3, raw.size()));
    }
}