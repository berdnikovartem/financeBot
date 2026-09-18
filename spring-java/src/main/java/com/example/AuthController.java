package com.example;

import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.web.bind.annotation.*;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.security.core.Authentication;
import org.springframework.http.HttpHeaders;
import org.springframework.http.ResponseCookie;
import org.springframework.beans.factory.annotation.Value;
import java.time.Duration;

@RestController
@RequestMapping("/api/v1/auth")
public class AuthController {
    private final AppUserRepository users;
    private final PasswordEncoder passwordEncoder;
    private final JwtService jwtService;
    private final BotConfigRepository botConfigs;
    private final RefreshTokenService refreshTokens;
    private final String bootstrapEmail;
    private final boolean secureCookies;

    public AuthController(AppUserRepository users, PasswordEncoder passwordEncoder, JwtService jwtService,
                          BotConfigRepository botConfigs,
                          RefreshTokenService refreshTokens,
                          @Value("${app.bootstrap.email:7artanis@gmail.com}") String bootstrapEmail,
                          @Value("${app.cookies.secure:false}") boolean secureCookies) {
        this.users = users;
        this.passwordEncoder = passwordEncoder;
        this.jwtService = jwtService;
        this.botConfigs = botConfigs;
        this.refreshTokens = refreshTokens;
        this.bootstrapEmail = bootstrapEmail.trim().toLowerCase();
        this.secureCookies = secureCookies;
    }

    @PostMapping("/register")
    public ResponseEntity<?> register(@RequestBody AuthRequest request) {
        String email = normalizedEmail(request.email());
        if (email.isBlank() || request.password() == null || request.password().length() < 12) {
            return ResponseEntity.badRequest().body("Email and a 12-character password are required");
        }
        if (users.existsByEmail(email)) {
            return ResponseEntity.status(HttpStatus.CONFLICT).body("Email is already registered");
        }
        AppUser user = users.save(new AppUser(email, passwordEncoder.encode(request.password())));
        if (email.equals(bootstrapEmail)) {
            var orphanedBots = botConfigs.findByUserIsNull();
            orphanedBots.forEach(bot -> bot.setUser(user));
            botConfigs.saveAll(orphanedBots);
        }
        return ResponseEntity.status(HttpStatus.CREATED).body(tokens(user));
    }

    @PostMapping("/login")
    public ResponseEntity<?> login(@RequestBody AuthRequest request) {
        return users.findByEmail(normalizedEmail(request.email()))
                .filter(user -> request.password() != null && passwordEncoder.matches(request.password(), user.getPasswordHash()))
                .<ResponseEntity<?>>map(user -> ResponseEntity.ok(tokens(user)))
                .orElseGet(() -> ResponseEntity.status(HttpStatus.UNAUTHORIZED).body("Invalid credentials"));
    }

    @PostMapping("/refresh")
    public ResponseEntity<?> refresh(@RequestBody RefreshRequest request) {
        try {
            String rawRefreshToken = request.refreshToken();
            AppUser user = users.findByEmail(jwtService.getRefreshEmail(rawRefreshToken)).orElseThrow();
            String scope = jwtService.getScope(rawRefreshToken);
            refreshTokens.consume(rawRefreshToken, scope);
            return ResponseEntity.ok(scope.equals("worker") ? workerTokens(user) : tokens(user));
        } catch (RuntimeException exception) {
            return ResponseEntity.status(HttpStatus.UNAUTHORIZED).body("Invalid refresh token");
        }
    }

    @PostMapping("/worker-token")
    public ResponseEntity<AuthResponse> workerToken(Authentication authentication) {
        AppUser user = users.findByEmail(authentication.getName()).orElseThrow();
        return ResponseEntity.ok(workerTokens(user));
    }

    @PostMapping("/browser/register")
    public ResponseEntity<?> browserRegister(@RequestBody AuthRequest request) {
        String email = normalizedEmail(request.email());
        ResponseEntity<?> validation = validateRegistration(email, request.password());
        if (validation != null) return validation;
        if (users.existsByEmail(email)) return ResponseEntity.status(HttpStatus.CONFLICT).body("Email is already registered");
        AppUser user = users.save(new AppUser(email, passwordEncoder.encode(request.password())));
        claimLegacyBots(email, user);
        return browserResponse(user, HttpStatus.CREATED);
    }

    @PostMapping("/browser/login")
    public ResponseEntity<?> browserLogin(@RequestBody AuthRequest request) {
        AppUser user = users.findByEmail(normalizedEmail(request.email())).orElse(null);
        if (user == null || request.password() == null || !passwordEncoder.matches(request.password(), user.getPasswordHash())) {
            return ResponseEntity.status(HttpStatus.UNAUTHORIZED).body("Invalid credentials");
        }
        return browserResponse(user, HttpStatus.OK);
    }

    @PostMapping("/browser/refresh")
    public ResponseEntity<?> browserRefresh(@CookieValue(name = "REFRESH_TOKEN", required = false) String rawRefreshToken) {
        try {
            AppUser user = users.findByEmail(jwtService.getRefreshEmail(rawRefreshToken)).orElseThrow();
            if (!"user".equals(jwtService.getScope(rawRefreshToken))) throw new IllegalArgumentException();
            refreshTokens.consume(rawRefreshToken, "user");
            return browserResponse(user, HttpStatus.OK);
        } catch (RuntimeException exception) {
            return ResponseEntity.status(HttpStatus.UNAUTHORIZED).body("Invalid refresh token");
        }
    }

    @PostMapping("/browser/logout")
    public ResponseEntity<Void> browserLogout(@CookieValue(name = "REFRESH_TOKEN", required = false) String refreshToken) {
        if (refreshToken != null) refreshTokens.revoke(refreshToken);
        return ResponseEntity.noContent()
                .header(HttpHeaders.SET_COOKIE, expiredCookie("ACCESS_TOKEN", "/").toString())
                .header(HttpHeaders.SET_COOKIE, expiredCookie("REFRESH_TOKEN", "/api/v1/auth/browser").toString())
                .build();
    }

    private ResponseEntity<?> browserResponse(AppUser user, HttpStatus status) {
        AuthResponse issued = tokens(user);
        return ResponseEntity.status(status)
                .header(HttpHeaders.SET_COOKIE, authCookie("ACCESS_TOKEN", issued.token(), Duration.ofMinutes(15), "/").toString())
                .header(HttpHeaders.SET_COOKIE, authCookie("REFRESH_TOKEN", issued.refreshToken(), Duration.ofDays(30), "/api/v1/auth/browser").toString())
                .body(new BrowserAuthResponse(user.getEmail()));
    }

    private void claimLegacyBots(String email, AppUser user) {
        if (email.equals(bootstrapEmail)) {
            var orphanedBots = botConfigs.findByUserIsNull();
            orphanedBots.forEach(bot -> bot.setUser(user));
            botConfigs.saveAll(orphanedBots);
        }
    }

    private ResponseEntity<?> validateRegistration(String email, String password) {
        if (email.isBlank() || password == null || password.length() < 12) {
            return ResponseEntity.badRequest().body("Email and a 12-character password are required");
        }
        return null;
    }

    private ResponseCookie authCookie(String name, String value, Duration maxAge, String path) {
        return ResponseCookie.from(name, value).httpOnly(true).secure(secureCookies).sameSite("Strict").path(path).maxAge(maxAge).build();
    }

    private ResponseCookie expiredCookie(String name, String path) {
        return authCookie(name, "", Duration.ZERO, path);
    }

    @PostMapping("/logout")
    public ResponseEntity<Void> logout(@RequestBody RefreshRequest request) {
        if (request.refreshToken() != null) refreshTokens.revoke(request.refreshToken());
        return ResponseEntity.noContent().build();
    }

    private AuthResponse tokens(AppUser user) {
        String refreshToken = jwtService.createRefreshToken(user);
        refreshTokens.store(user, refreshToken, "user", jwtService.getExpiration(refreshToken));
        return new AuthResponse(jwtService.createToken(user), refreshToken);
    }

    private AuthResponse workerTokens(AppUser user) {
        String refreshToken = jwtService.createWorkerRefreshToken(user);
        refreshTokens.store(user, refreshToken, "worker", jwtService.getExpiration(refreshToken));
        return new AuthResponse(jwtService.createWorkerToken(user), refreshToken);
    }

    private String normalizedEmail(String email) {
        return email == null ? "" : email.trim().toLowerCase();
    }
}