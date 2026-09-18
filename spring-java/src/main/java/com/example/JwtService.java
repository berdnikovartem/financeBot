package com.example;

import io.jsonwebtoken.Claims;
import io.jsonwebtoken.Jwts;
import io.jsonwebtoken.security.Keys;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import javax.crypto.SecretKey;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.util.Date;

@Service
public class JwtService {
    private final SecretKey signingKey;

    public JwtService(@Value("${app.jwt.secret}") String secret) {
        if (secret.getBytes(StandardCharsets.UTF_8).length < 32) {
            throw new IllegalArgumentException("app.jwt.secret must be at least 32 bytes");
        }
        this.signingKey = Keys.hmacShaKeyFor(secret.getBytes(StandardCharsets.UTF_8));
    }

    public String createToken(AppUser user) {
        return createToken(user, "access", 900, "user");
    }

    public String createRefreshToken(AppUser user) {
        return createRefreshToken(user, "user");
    }

    public String createWorkerToken(AppUser user) {
        return createToken(user, "access", 900, "worker");
    }

    public String createWorkerRefreshToken(AppUser user) {
        return createToken(user, "refresh", 2_592_000, "worker");
    }

    private String createRefreshToken(AppUser user, String scope) {
        return createToken(user, "refresh", 2_592_000, scope);
    }

    private String createToken(AppUser user, String tokenType, long lifetimeSeconds, String scope) {
        Instant now = Instant.now();
        return Jwts.builder()
                .subject(user.getEmail())
                .claim("token_type", tokenType)
                .claim("scope", scope)
                .issuedAt(Date.from(now))
                .expiration(Date.from(now.plusSeconds(lifetimeSeconds)))
                .signWith(signingKey)
                .compact();
    }

    public String getEmail(String token) {
        Claims claims = parse(token);
        if (!"access".equals(claims.get("token_type", String.class))) {
            throw new IllegalArgumentException("Not an access token");
        }
        return claims.getSubject();
    }

    public String getRefreshEmail(String token) {
        Claims claims = parse(token);
        if (!"refresh".equals(claims.get("token_type", String.class))) {
            throw new IllegalArgumentException("Not a refresh token");
        }
        return claims.getSubject();
    }

    public Instant getExpiration(String token) {
        return parse(token).getExpiration().toInstant();
    }

    public String getScope(String token) {
        return parse(token).get("scope", String.class);
    }

    private Claims parse(String token) {
        return Jwts.parser().verifyWith(signingKey).build().parseSignedClaims(token).getPayload();
    }
}