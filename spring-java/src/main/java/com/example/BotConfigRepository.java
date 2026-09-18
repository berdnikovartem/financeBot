package com.example;

import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;
import java.util.Optional;
import java.util.List;

@Repository
public interface BotConfigRepository extends JpaRepository<BotConfig, Long> {
    Optional<BotConfig> findByUserAndSymbol(AppUser user, String symbol);
    Optional<BotConfig> findByIdAndUser(Long id, AppUser user);
    List<BotConfig> findByUserAndActiveTrue(AppUser user);
    List<BotConfig> findByUserIsNull();
}