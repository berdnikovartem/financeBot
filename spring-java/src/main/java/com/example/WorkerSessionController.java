package com.example;

import org.springframework.http.ResponseEntity;
import org.springframework.security.core.Authentication;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

@RestController
@RequestMapping("/api/v1/worker")
public class WorkerSessionController {
    private final AppUserRepository users;
    private final EncryptionService encryption;

    public WorkerSessionController(AppUserRepository users, EncryptionService encryption) {
        this.users = users;
        this.encryption = encryption;
    }

    @GetMapping("/session")
    public ResponseEntity<WorkerSessionResponse> session(Authentication authentication) {
        AppUser user = users.findByEmail(authentication.getName()).orElseThrow();
        if (user.getBitunixApiKeyEncrypted() == null || user.getBitunixApiSecretEncrypted() == null) {
            return ResponseEntity.notFound().build();
        }
        return ResponseEntity.ok(new WorkerSessionResponse(
                encryption.decrypt(user.getBitunixApiKeyEncrypted()),
                encryption.decrypt(user.getBitunixApiSecretEncrypted())));
    }
}