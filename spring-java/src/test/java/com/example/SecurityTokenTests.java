package com.example;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class SecurityTokenTests {
    private final JwtService jwtService = new JwtService("01234567890123456789012345678901");
    private final AppUser user = new AppUser("user@example.com", "hashed-password");

    @Test
    void accessAndWorkerTokensCarryDifferentScopes() {
        String accessToken = jwtService.createToken(user);
        String workerToken = jwtService.createWorkerToken(user);

        assertEquals("user@example.com", jwtService.getEmail(accessToken));
        assertEquals("user", jwtService.getScope(accessToken));
        assertEquals("worker", jwtService.getScope(workerToken));
    }

    @Test
    void accessParserRejectsRefreshTokens() {
        String refreshToken = jwtService.createRefreshToken(user);

        assertThrows(IllegalArgumentException.class, () -> jwtService.getEmail(refreshToken));
        assertEquals("user@example.com", jwtService.getRefreshEmail(refreshToken));
    }
}