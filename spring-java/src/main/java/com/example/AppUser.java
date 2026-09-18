package com.example;

import jakarta.persistence.*;

@Entity
@Table(name = "app_users")
public class AppUser {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(nullable = false, unique = true)
    private String email;

    @Column(nullable = false)
    private String passwordHash;

    @Column(length = 4096)
    private String bitunixApiKeyEncrypted;

    @Column(length = 4096)
    private String bitunixApiSecretEncrypted;

    protected AppUser() {
    }

    public AppUser(String email, String passwordHash) {
        this.email = email;
        this.passwordHash = passwordHash;
    }

    public Long getId() { return id; }
    public String getEmail() { return email; }
    public String getPasswordHash() { return passwordHash; }
    public String getBitunixApiKeyEncrypted() { return bitunixApiKeyEncrypted; }
    public String getBitunixApiSecretEncrypted() { return bitunixApiSecretEncrypted; }

    public void setBitunixApiKeyEncrypted(String value) { this.bitunixApiKeyEncrypted = value; }
    public void setBitunixApiSecretEncrypted(String value) { this.bitunixApiSecretEncrypted = value; }
}