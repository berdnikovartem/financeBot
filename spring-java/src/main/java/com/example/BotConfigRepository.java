package com.example;

import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;
import java.util.List;
import java.util.Optional;

@Repository
public interface BotConfigRepository extends JpaRepository<BotConfig, Long> {
    Optional<BotConfig> findBySymbol(String symbol); //If symbol does not exist in database when asked
                                                     //then empty object is returned back, instead of an exeption
    List<BotConfig> findByActiveTrue();
    List<BotConfig> findByUser(AppUser user);
    List<BotConfig> findByUserAndActiveTrue(AppUser user);
    List<BotConfig> findByActiveTrueAndUserIsNull();
    List<BotConfig> findByUserIsNull();
    Optional<BotConfig> findByUserAndSymbol(AppUser user, String symbol);
    List<BotConfig> findByActiveTrueAndUser_BitunixApiKeyEncryptedIsNotNullAndUser_BitunixApiSecretEncryptedIsNotNull();
}