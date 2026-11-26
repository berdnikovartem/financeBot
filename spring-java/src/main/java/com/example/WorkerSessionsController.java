package com.example;

import org.springframework.http.ResponseEntity;
import org.springframework.security.core.Authentication;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

@RestController
@RequestMapping("/api/v1/worker")
public class WorkerSessionsController {
    private final AppUserRepository users;
    private final BotConfigRepository configs;
    private final EncryptionService encryption;

    public WorkerSessionsController(AppUserRepository users, BotConfigRepository configs, EncryptionService encryption) {
        this.users = users;
        this.configs = configs;
        this.encryption = encryption;
    }

    @GetMapping("/sessions")
    public ResponseEntity<List<WorkerSessionResponse>> sessions() {
        List<BotConfig> activeConfigs = configs
                .findByActiveTrueAndUser_BitunixApiKeyEncryptedIsNotNullAndUser_BitunixApiSecretEncryptedIsNotNull();
        Map<Long, List<BotConfig>> activeByUser = activeConfigs.stream()
                .collect(Collectors.groupingBy(config -> config.getUser().getId()));
        List<WorkerSessionResponse> sessions = users
                .findByBitunixApiKeyEncryptedIsNotNullAndBitunixApiSecretEncryptedIsNotNull().stream()
                .map(user -> new WorkerSessionResponse(
                        user.getEmail(),
                        encryption.decrypt(user.getBitunixApiKeyEncrypted()),
                        encryption.decrypt(user.getBitunixApiSecretEncrypted()),
                        activeByUser.getOrDefault(user.getId(), List.of()).stream()
                                .map(config -> new BotConfigResponse(
                                        config.getSymbol(), config.isActive(),
                                        config.getPriceStepPercentage(), config.getTakeProfitPercentage(),
                                        config.getTradeQuantityForApi(),
                                        config.getLastLongEntryPrice(),
                                        config.getLastShortEntryPrice(),
                                        config.getLongTradeCount(),
                                        config.getShortTradeCount(),
                                        new BotConfigResponse.ParametersDto(
                                                config.getRsiPeriod(), config.getRsiOverbought(),
                                                config.getRsiOversold(), config.getTimeframe(),
                                                config.getTakeProfitPct(), config.getTradeQuantity(),
                                                config.getTakeProfitScope(),
                                                config.getRsiIndicators(), config.getHammerTimeframe(),
                                                config.getHammerIndicators(), config.getVolumeIndicators())))
                                .toList()))
                .toList();
        return ResponseEntity.ok(sessions);
    }

    @PostMapping("/cycle-state")
    public ResponseEntity<Void> updateCycleState(@RequestBody CycleStateUpdate request,
                                                 Authentication authentication) {
        if (request.email() == null || request.email().isBlank() ||
                request.symbol() == null || request.symbol().isBlank() ||
                !validSideState(request.longLastEntryPrice(), request.longTradeCount()) ||
                !validSideState(request.shortLastEntryPrice(), request.shortTradeCount())) {
            return ResponseEntity.badRequest().build();
        }

        AppUser user = users.findByEmail(request.email()).orElse(null);
        if (user == null) return ResponseEntity.notFound().build();
        if (!authentication.getAuthorities().stream()
                .anyMatch(authority -> authority.getAuthority().equals("ROLE_WORKER"))) {
            return ResponseEntity.status(403).build();
        }
        BotConfig config = configs.findByUserAndSymbol(user, request.symbol()).orElse(null);
        if (config == null) return ResponseEntity.notFound().build();

        config.setLastLongEntryPrice(request.longLastEntryPrice());
        config.setLastShortEntryPrice(request.shortLastEntryPrice());
        config.setLongTradeCount(request.longTradeCount());
        config.setShortTradeCount(request.shortTradeCount());
        configs.save(config);
        return ResponseEntity.noContent().build();
    }

    private static boolean validSideState(Double lastEntryPrice, Integer tradeCount) {
        if (lastEntryPrice == null || tradeCount == null || !Double.isFinite(lastEntryPrice) ||
                lastEntryPrice < 0.0 || tradeCount < 0) {
            return false;
        }
        return tradeCount == 0 ? lastEntryPrice == 0.0 : lastEntryPrice > 0.0;
    }

    public record CycleStateUpdate(String email, String symbol,
                                   Double longLastEntryPrice, Integer longTradeCount,
                                   Double shortLastEntryPrice, Integer shortTradeCount) {
    }
}
