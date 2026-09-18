package com.example;

import org.springframework.http.ResponseEntity;
import org.springframework.security.core.Authentication;
import org.springframework.web.bind.annotation.*;

@RestController
@RequestMapping("/api/v1/me/exchange-credentials")
public class ExchangeCredentialsController {
    private final AppUserRepository users;
    private final EncryptionService encryption;

    public ExchangeCredentialsController(AppUserRepository users, EncryptionService encryption) {
        this.users = users;
        this.encryption = encryption;
    }

    @PutMapping
    public ResponseEntity<Void> update(@RequestBody ExchangeCredentialsRequest request, Authentication authentication) {
        if (request.apiKey() == null || request.apiSecret() == null || request.apiKey().isBlank() || request.apiSecret().isBlank()) {
            return ResponseEntity.badRequest().build();
        }
        AppUser user = users.findByEmail(authentication.getName()).orElseThrow();
        user.setBitunixApiKeyEncrypted(encryption.encrypt(request.apiKey()));
        user.setBitunixApiSecretEncrypted(encryption.encrypt(request.apiSecret()));
        users.save(user);
        return ResponseEntity.noContent().build();
    }
}