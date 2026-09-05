package com.example;

import org.hibernate.annotations.JdbcTypeCode;
import org.hibernate.type.SqlTypes;
import jakarta.persistence.*;
import java.util.Map;

@Entity
@Table(name= "bot_configs")
public class BotConfig{

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(unique = true, nullable = false) //unique allows only one symbol per database
    private String symbol;

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
}