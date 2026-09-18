package com.example;

import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.time.Instant;
import java.util.HexFormat;

@Service
public class RefreshTokenService {
    private final RefreshTokenRepository tokens;

    public RefreshTokenService(RefreshTokenRepository tokens) {
        this.tokens = tokens;
    }

    public void store(AppUser user, String rawToken, String tokenType, Instant expiresAt) {
        tokens.save(new RefreshToken(user, hash(rawToken), tokenType, expiresAt));
    }

    @Transactional
    public RefreshToken consume(String rawToken, String tokenType) {
        RefreshToken token = tokens.findByTokenHash(hash(rawToken)).orElseThrow();
        if (!tokenType.equals(token.getTokenType()) || token.getRevokedAt() != null || token.getExpiresAt().isBefore(Instant.now())) {
            throw new IllegalArgumentException("Refresh token is invalid or expired");
        }
        token.revoke();
        tokens.save(token);
        return token;
    }

    public void revoke(String rawToken) {
        tokens.findByTokenHash(hash(rawToken)).ifPresent(token -> {
            token.revoke();
            tokens.save(token);
        });
    }

    private String hash(String rawToken) {
        try {
            return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256")
                    .digest(rawToken.getBytes(StandardCharsets.UTF_8)));
        } catch (Exception exception) {
            throw new IllegalStateException("Could not hash refresh token", exception);
        }
    }
}