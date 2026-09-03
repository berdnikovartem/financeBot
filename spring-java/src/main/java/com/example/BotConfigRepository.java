package com.example;

import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;
import java.util.Optional;

@Repository
public interface BotConfigRepository extends JpaRepository<BotConfig, Long> {
    Optional<BotConfig> findBySymbol(String symbol); //If symbol does not exist in database when asked
                                                     //then empty object is returned back, instead of an exeption
    
}