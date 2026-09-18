package com.example;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.Base64;

@Service
public class EncryptionService {
    private final byte[] key;
    private final SecureRandom random = new SecureRandom();

    public EncryptionService(@Value("${app.encryption.key}") String key) {
        try {
            this.key = Base64.getDecoder().decode(key);
        } catch (IllegalArgumentException exception) {
            throw new IllegalArgumentException("app.encryption.key must be valid Base64 for 32 bytes; generate it with: openssl rand -base64 32", exception);
        }
        if (this.key.length != 32) {
            throw new IllegalArgumentException("app.encryption.key must decode to exactly 32 bytes; generate it with: openssl rand -base64 32");
        }
    }

    public String encrypt(String value) {
        try {
            byte[] iv = new byte[12];
            random.nextBytes(iv);
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, new SecretKeySpec(key, "AES"), new GCMParameterSpec(128, iv));
            byte[] encrypted = cipher.doFinal(value.getBytes(StandardCharsets.UTF_8));
            byte[] result = new byte[iv.length + encrypted.length];
            System.arraycopy(iv, 0, result, 0, iv.length);
            System.arraycopy(encrypted, 0, result, iv.length, encrypted.length);
            return Base64.getEncoder().encodeToString(result);
        } catch (Exception exception) {
            throw new IllegalStateException("Could not encrypt exchange credential", exception);
        }
    }

    public String decrypt(String value) {
        try {
            byte[] input = Base64.getDecoder().decode(value);
            byte[] iv = java.util.Arrays.copyOfRange(input, 0, 12);
            byte[] encrypted = java.util.Arrays.copyOfRange(input, 12, input.length);
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, new SecretKeySpec(key, "AES"), new GCMParameterSpec(128, iv));
            return new String(cipher.doFinal(encrypted), StandardCharsets.UTF_8);
        } catch (Exception exception) {
            throw new IllegalStateException("Could not decrypt exchange credential", exception);
        }
    }
}