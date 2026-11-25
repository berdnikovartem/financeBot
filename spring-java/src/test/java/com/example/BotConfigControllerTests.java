package com.example;

import org.junit.jupiter.api.Test;
import org.springframework.security.authentication.UsernamePasswordAuthenticationToken;
import org.springframework.security.core.authority.SimpleGrantedAuthority;

import java.util.List;
import java.util.Optional;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

class BotConfigControllerTests {

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
}
