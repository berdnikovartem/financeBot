package com.example;

import org.junit.jupiter.api.Test;
import org.springframework.security.authentication.UsernamePasswordAuthenticationToken;
import org.springframework.security.core.authority.SimpleGrantedAuthority;

import java.util.List;
import java.util.Map;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

class BotConfigControllerTests {

    @Test
    void takeProfitScopeDefaultsToCycleAndAcceptsPerTrade() {
        BotConfig config = new BotConfig();
        assertEquals("CYCLE", config.getTakeProfitScope());

        config.setParameters(Map.of("take_profit_scope", "TRADE"));
        assertEquals("TRADE", config.getTakeProfitScope());
    }

    @Test
    void tradingSideDefaultsToBothAndReturnsConfiguredSide() {
        BotConfig config = new BotConfig();
        assertEquals("BOTH", config.getTradingSide());

        config.setParameters(Map.of("trading_side", "LONG"));
        assertEquals("LONG", config.getTradingSide());

        config.setParameters(Map.of("trading_side", "SHORT"));
        assertEquals("SHORT", config.getTradingSide());
    }

    @Test
    void invalidTradingSideFailsClosed() {
        BotConfig config = new BotConfig();
        config.setParameters(Map.of("trading_side", "long"));
        assertEquals("INVALID", config.getTradingSide());
    }

    @Test
    void userConfigListIncludesPausedStrategiesOwnedByCurrentUser() {
        BotConfigRepository configs = mock(BotConfigRepository.class);
        AppUserRepository users = mock(AppUserRepository.class);
        AppUser user = new AppUser("user@example.com", "password-hash");
        BotConfig pausedConfig = new BotConfig();
        pausedConfig.setSymbol("BTCUSDT");
        pausedConfig.setActive(false);
        when(users.findByEmail(user.getEmail())).thenReturn(Optional.of(user));
        when(configs.findByUser(user)).thenReturn(List.of(pausedConfig));

        BotConfigController controller = new BotConfigController(configs, users);
        var authentication = new UsernamePasswordAuthenticationToken(
                user.getEmail(), null, List.of(new SimpleGrantedAuthority("ROLE_USER")));

        var response = controller.getUserConfigs(authentication);

        assertEquals(200, response.getStatusCode().value());
        assertSame(pausedConfig, response.getBody().get(0));
        assertFalse(response.getBody().get(0).isActive());
        verify(configs).findByUser(user);
    }

    @Test
    void activeConfigListForUserDoesNotReturnAnotherUsersOrLegacyConfigs() {
        BotConfigRepository configs = mock(BotConfigRepository.class);
        AppUserRepository users = mock(AppUserRepository.class);
        AppUser user = new AppUser("user@example.com", "password-hash");
        when(users.findByEmail(user.getEmail())).thenReturn(Optional.of(user));
        when(configs.findByUserAndActiveTrue(user)).thenReturn(List.of());

        BotConfigController controller = new BotConfigController(configs, users);
        var authentication = new UsernamePasswordAuthenticationToken(
                user.getEmail(), null, List.of(new SimpleGrantedAuthority("ROLE_USER")));

        var response = controller.getActiveConfigs(authentication);

        assertEquals(200, response.getStatusCode().value());
        assertEquals(List.of(), response.getBody());
        verify(configs).findByUserAndActiveTrue(user);
    }

    @Test
    void activeConfigResponseIncludesConfiguredTradingSide() {
        BotConfigRepository configs = mock(BotConfigRepository.class);
        AppUserRepository users = mock(AppUserRepository.class);
        AppUser user = new AppUser("user@example.com", "password-hash");
        BotConfig config = new BotConfig();
        config.setSymbol("SOLUSDT");
        config.setParameters(Map.of("trading_side", "LONG"));
        when(users.findByEmail(user.getEmail())).thenReturn(Optional.of(user));
        when(configs.findByUserAndActiveTrue(user)).thenReturn(List.of(config));

        BotConfigController controller = new BotConfigController(configs, users);
        var authentication = new UsernamePasswordAuthenticationToken(
                user.getEmail(), null, List.of(new SimpleGrantedAuthority("ROLE_USER")));

        var response = controller.getActiveConfigs(authentication);

        assertEquals(200, response.getStatusCode().value());
        assertEquals("LONG", response.getBody().get(0).getParameters().getTradingSide());
    }

    @Test
    void userCanDeleteOnlyTheirOwnStrategy() {
        BotConfigRepository configs = mock(BotConfigRepository.class);
        AppUserRepository users = mock(AppUserRepository.class);
        AppUser user = new AppUser("user@example.com", "password-hash");
        BotConfig ownedConfig = new BotConfig();
        ownedConfig.setSymbol("SOLUSDT");
        when(users.findByEmail(user.getEmail())).thenReturn(Optional.of(user));
        when(configs.findByUserAndSymbol(user, "SOLUSDT")).thenReturn(Optional.of(ownedConfig));

        BotConfigController controller = new BotConfigController(configs, users);
        var authentication = new UsernamePasswordAuthenticationToken(
                user.getEmail(), null, List.of(new SimpleGrantedAuthority("ROLE_USER")));

        var response = controller.deleteConfig("SOLUSDT", authentication);

        assertEquals(204, response.getStatusCode().value());
        verify(configs).delete(ownedConfig);
    }

    @Test
    void deletingAnotherUsersStrategyDoesNotDeleteIt() {
        BotConfigRepository configs = mock(BotConfigRepository.class);
        AppUserRepository users = mock(AppUserRepository.class);
        AppUser user = new AppUser("user@example.com", "password-hash");
        when(users.findByEmail(user.getEmail())).thenReturn(Optional.of(user));
        when(configs.findByUserAndSymbol(user, "SOLUSDT")).thenReturn(Optional.empty());

        BotConfigController controller = new BotConfigController(configs, users);
        var authentication = new UsernamePasswordAuthenticationToken(
                user.getEmail(), null, List.of(new SimpleGrantedAuthority("ROLE_USER")));

        var response = controller.deleteConfig("SOLUSDT", authentication);

        assertEquals(404, response.getStatusCode().value());
        verify(configs, org.mockito.Mockito.never()).delete(org.mockito.ArgumentMatchers.any(BotConfig.class));
    }

    @Test
    void workerCanPersistCycleProgressForOwnedStrategy() {
        BotConfigRepository configs = mock(BotConfigRepository.class);
        AppUserRepository users = mock(AppUserRepository.class);
        EncryptionService encryption = mock(EncryptionService.class);
        AppUser user = new AppUser("user@example.com", "password-hash");
        BotConfig config = new BotConfig();
        config.setSymbol("SOLUSDT");
        when(users.findByEmail(user.getEmail())).thenReturn(Optional.of(user));
        when(configs.findByUserAndSymbol(user, "SOLUSDT")).thenReturn(Optional.of(config));
        when(configs.save(config)).thenReturn(config);

        WorkerSessionsController controller = new WorkerSessionsController(users, configs, encryption);
        var authentication = new UsernamePasswordAuthenticationToken(
                "worker-service", null, List.of(new SimpleGrantedAuthority("ROLE_WORKER")));
        var request = new WorkerSessionsController.CycleStateUpdate(
                user.getEmail(), "SOLUSDT", 67.5, 3, 0.0, 0);

        var response = controller.updateCycleState(request, authentication);

        assertEquals(204, response.getStatusCode().value());
        assertEquals(67.5, config.getLastLongEntryPrice(), 0.00001);
        assertEquals(3, config.getLongTradeCount());
        assertEquals(0.0, config.getLastShortEntryPrice(), 0.00001);
        assertEquals(0, config.getShortTradeCount());
        verify(configs).save(config);
    }

    @Test
    void workerCannotPersistCycleProgressWithInvalidSideState() {
        BotConfigRepository configs = mock(BotConfigRepository.class);
        AppUserRepository users = mock(AppUserRepository.class);
        EncryptionService encryption = mock(EncryptionService.class);
        WorkerSessionsController controller = new WorkerSessionsController(users, configs, encryption);
        var authentication = new UsernamePasswordAuthenticationToken(
                "worker-service", null, List.of(new SimpleGrantedAuthority("ROLE_WORKER")));
        var request = new WorkerSessionsController.CycleStateUpdate(
                "user@example.com", "SOLUSDT", 0.0, 1, 0.0, 0);

        var response = controller.updateCycleState(request, authentication);

        assertEquals(400, response.getStatusCode().value());
        verify(configs, org.mockito.Mockito.never()).save(org.mockito.ArgumentMatchers.any(BotConfig.class));
    }
}
