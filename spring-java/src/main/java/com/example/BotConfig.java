package com.example;

import org.hibernate.annotations.JdbcTypeCode;
import org.hibernate.type.SqlTypes;
import jakarta.persistence.*;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.Map;

@Entity
@Table(name = "bot_configs", uniqueConstraints = @UniqueConstraint(columnNames = {"user_id", "symbol"}))
public class BotConfig {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @ManyToOne(fetch = FetchType.LAZY, optional = true)
    @JoinColumn(name = "user_id")
    private AppUser user;

    @Column(nullable = false)
    private String symbol;

    @JdbcTypeCode(SqlTypes.JSON)
    @Column(columnDefinition = "jsonb")
    private Map<String, Object> parameters;

    @Column(name = "price_step_percentage")
    private Double priceStepPercentage; // 3.0 = 3% diff

    @Column(name = "last_long_price")
    private Double lastLongPrice; // 0 means no active long positions

    @Column(name = "last_short_price")
    private Double lastShortPrice; // 0 means no open position

    @Column(name = "take_profit_percentage")
    private Double takeProfitPercentage;

    @Column(name = "trade_quantity")
    private Double tradeQuantity;

    @Column(name = "long_trade_count")
    private Integer longTradeCount = 0;

    @Column(name = "short_trade_count")
    private Integer shortTradeCount = 0;

    private boolean active;

    public Long getId() {
        return id;
    }

    @JsonIgnore
    public AppUser getUser() {
        return user;
    }

    @JsonIgnore
    public void setUser(AppUser user) {
        this.user = user;
    }

    public void setId(Long id) {
        this.id = id;
    }

    public String getSymbol() {
        return symbol;
    }

    public void setSymbol(String symbol) {
        this.symbol = symbol;
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
    public Double getPriceStepPercentage() {
        return priceStepPercentage;
    }

    @JsonProperty("price_step_percentage")
    public void setPriceStepPercentage(Double priceStepPercentage) {
        this.priceStepPercentage = priceStepPercentage;
    }

    @JsonProperty("take_profit_percentage")
    public Double getTakeProfitPercentage() {
        if (takeProfitPercentage != null) return takeProfitPercentage;
        if (parameters != null && parameters.containsKey("take_profit_percentage")) {
            return ((Number) parameters.get("take_profit_percentage")).doubleValue();
        }
        return 0.0;
    }

    @JsonProperty("take_profit_percentage")
    public void setTakeProfitPercentage(Double takeProfitPercentage) {
        this.takeProfitPercentage = takeProfitPercentage;
    }

    @JsonProperty("trade_quantity")
    public Double getTradeQuantity() {
        if (tradeQuantity != null) return tradeQuantity;
        if (parameters != null && parameters.containsKey("trade_quantity")) {
            return ((Number) parameters.get("trade_quantity")).doubleValue();
        }
        return 0.001;
    }

    @JsonProperty("trade_quantity")
    public void setTradeQuantity(Double tradeQuantity) {
        this.tradeQuantity = tradeQuantity;
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

    public Integer getLongTradeCount() {
        return longTradeCount == null ? 0 : longTradeCount;
    }

    public void setLongTradeCount(Integer longTradeCount) {
        this.longTradeCount = longTradeCount == null ? 0 : longTradeCount;
    }

    public Integer getShortTradeCount() {
        return shortTradeCount == null ? 0 : shortTradeCount;
    }

    public void setShortTradeCount(Integer shortTradeCount) {
        this.shortTradeCount = shortTradeCount == null ? 0 : shortTradeCount;
    }

    public void recordAverageTradePrice(String positionSide, double price) {
        if ("LONG".equalsIgnoreCase(positionSide)) {
            int count = getLongTradeCount();
            double average = lastLongPrice == null ? 0.0 : lastLongPrice;
            if (average <= 0.0 || count <= 0) {
                lastLongPrice = price;
                longTradeCount = 1;
                return;
            }
            lastLongPrice = ((average * count) + price) / (count + 1.0);
            longTradeCount = count + 1;
            return;
        }
        if ("SHORT".equalsIgnoreCase(positionSide)) {
            int count = getShortTradeCount();
            double average = lastShortPrice == null ? 0.0 : lastShortPrice;
            if (average <= 0.0 || count <= 0) {
                lastShortPrice = price;
                shortTradeCount = 1;
                return;
            }
            lastShortPrice = ((average * count) + price) / (count + 1.0);
            shortTradeCount = count + 1;
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

    public double getHammerMultiplier() {
        if (parameters == null || !parameters.containsKey("hammer_multiplier")) return 1.5;
        return ((Number) parameters.get("hammer_multiplier")).doubleValue();
    }

    public double getVolumeSurgeMultiplier() {
        if (parameters == null || !parameters.containsKey("volume_surge_multiplier")) return 1.5;
        return ((Number) parameters.get("volume_surge_multiplier")).doubleValue();
    }

    public String getTradingSide() {
        if (parameters == null || !parameters.containsKey("trading_side")) return "BOTH";
        return ((String) parameters.get("trading_side")).toUpperCase();
    }
}