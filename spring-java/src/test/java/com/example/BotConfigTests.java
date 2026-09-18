package com.example;

import org.junit.jupiter.api.Test;

import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;

class BotConfigTests {

    @Test
    void recordsAverageEntryPriceForLongTrades() {
        BotConfig botConfig = new BotConfig();

        botConfig.recordAverageTradePrice("LONG", 100.0);
        botConfig.recordAverageTradePrice("LONG", 110.0);
        botConfig.recordAverageTradePrice("LONG", 120.0);

        assertEquals(110.0, botConfig.getLastLongPrice(), 0.00001);
    }

    @Test
    void readsTakeProfitPercentageFromParameters() {
        BotConfig botConfig = new BotConfig();
        botConfig.setParameters(Map.of("take_profit_percentage", 4.0));

        assertEquals(4.0, botConfig.getTakeProfitPercentage(), 0.00001);
    }
}
