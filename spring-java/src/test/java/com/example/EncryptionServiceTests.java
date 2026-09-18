package com.example;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class EncryptionServiceTests {
    private final EncryptionService encryption = new EncryptionService(
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");

    @Test
    void encryptedCredentialsCanBeRecoveredWithoutMatchingPlaintext() {
        String encrypted = encryption.encrypt("bitunix-secret");

        assertNotEquals("bitunix-secret", encrypted);
        assertEquals("bitunix-secret", encryption.decrypt(encrypted));
    }
}