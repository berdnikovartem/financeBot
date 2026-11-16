package com.example;

import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
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
                                        new BotConfigResponse.ParametersDto(
                                                config.getRsiPeriod(), config.getRsiOverbought(),
                                                config.getRsiOversold(), config.getTimeframe(),
                                                config.getTakeProfitPct(), config.getTradeQuantity(),
                                                config.getRsiIndicators(), config.getHammerTimeframe(),
                                                config.getHammerIndicators(), config.getVolumeIndicators())))
                                .toList()))
                .toList();
        return ResponseEntity.ok(sessions);
    }
}
