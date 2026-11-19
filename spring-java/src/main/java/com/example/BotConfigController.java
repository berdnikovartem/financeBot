package com.example;

import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.springframework.security.core.Authentication;

import java.util.List;
import java.util.stream.Collectors;

@RestController
@RequestMapping("/api/v1")
public class BotConfigController{

    private final BotConfigRepository repo;
    private final AppUserRepository users;

    public BotConfigController(BotConfigRepository repo, AppUserRepository users){
        this.repo = repo;
        this.users = users;
    }

    @GetMapping("/config/{symbol}") //getting of current settings of a particular asset
    public ResponseEntity<BotConfig> getConfig(@PathVariable String symbol){
        return repo.findBySymbol(symbol).map(ResponseEntity::ok)
                .orElse(ResponseEntity.notFound().build()); //if the symbol is not found in database
                                                            //then it returns the 404 error
    }

    @PutMapping("/config/{symbol}")
    public ResponseEntity<BotConfig> updateConfig(@PathVariable String symbol, @RequestBody BotConfig newConfig,
                                                  Authentication authentication){

        AppUser user = authentication.getAuthorities().stream().anyMatch(a -> a.getAuthority().equals("ROLE_USER"))
                ? users.findByEmail(authentication.getName()).orElseThrow()
                : null;
        BotConfig config = user == null ? repo.findBySymbol(symbol)
                .orElseGet(() -> {
            BotConfig fresh = new BotConfig();
            fresh.setSymbol(symbol);
            return fresh;
        }) : repo.findByUserAndSymbol(user, symbol).orElseGet(() -> {
            BotConfig fresh = new BotConfig();
            fresh.setSymbol(symbol);
            fresh.setUser(user);
            return fresh;
        });

        config.setParameters(newConfig.getParameters());
        config.setActive(newConfig.isActive());
        config.setPriceStepPercentage(newConfig.getPriceStepPercentage());
        config.setTakeProfitPercentage(newConfig.getTakeProfitPercentage());
        config.setTradeQuantityForApi(newConfig.getTradeQuantityForApi());

        return ResponseEntity.ok(repo.save(config));
    }

    @GetMapping("/configs")
    public ResponseEntity<List<BotConfig>> getUserConfigs(Authentication authentication) {
        AppUser user = users.findByEmail(authentication.getName()).orElseThrow();
        return ResponseEntity.ok(repo.findByUser(user));
    }

    @GetMapping("/configs/active")
    public ResponseEntity<List<BotConfigResponse>> getActiveConfigs(Authentication authentication) {
        boolean isUser = authentication.getAuthorities().stream()
                .anyMatch(authority -> authority.getAuthority().equals("ROLE_USER"));
        List<BotConfig> activeConfigs = isUser
                ? repo.findByUserAndActiveTrue(users.findByEmail(authentication.getName()).orElseThrow())
                : repo.findByActiveTrueAndUserIsNull();

        List<BotConfigResponse> responseList = activeConfigs.stream()
                .map(config -> new BotConfigResponse(
                        config.getSymbol(),
                        config.isActive(),
                        config.getPriceStepPercentage(),
                        config.getTakeProfitPercentage(),
                        config.getTradeQuantityForApi(),
                        new BotConfigResponse.ParametersDto(
                                config.getRsiPeriod(),
                                config.getRsiOverbought(),
                                config.getRsiOversold(),
                                config.getTimeframe(),
                                config.getTakeProfitPct(),
                                config.getTradeQuantity(),
                                config.getRsiIndicators(),
                                config.getHammerTimeframe(),
                                config.getHammerIndicators(),
                                config.getVolumeIndicators()
                        )
                ))
                .collect(Collectors.toList());

        return ResponseEntity.ok(responseList);
    }
}