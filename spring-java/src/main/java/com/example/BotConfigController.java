package com.example;

import org.springframework.http.ResponseEntity;
import org.springframework.security.core.Authentication;
import org.springframework.web.bind.annotation.*;

import java.util.List;
import java.util.stream.Collectors;

@RestController
@RequestMapping("/api/v1")
public class BotConfigController{

    private final BotConfigRepository repo;
    private final AppUserRepository userRepo;

    public BotConfigController(BotConfigRepository repo, AppUserRepository userRepo){
        this.repo = repo;
        this.userRepo = userRepo;
    }

    @GetMapping("/config/{symbol}") //getting of current settings of a particular asset
    public ResponseEntity<BotConfig> getConfig(@PathVariable String symbol, Authentication authentication){
        AppUser user = user(authentication);
        return repo.findByUserAndSymbol(user, symbol).map(ResponseEntity::ok)
                .orElse(ResponseEntity.notFound().build()); //if the symbol is not found in database
                                                            //then it returns the 404 error
    }

    @PostMapping("/{id}/update-last-price")
    public ResponseEntity<?> updateLastPrice(
            @PathVariable Long id,
            @RequestParam String positionSide,
            @RequestParam Double price,
            Authentication authentication) {

        BotConfig bot = repo.findByIdAndUser(id, user(authentication)).orElse(null);
        if (bot == null) return ResponseEntity.notFound().build();
        if (!(positionSide.equals("LONG") || positionSide.equals("SHORT")) || price == null || !Double.isFinite(price) || price < 0.0) {
            return ResponseEntity.badRequest().body("positionSide must be LONG or SHORT and price must be zero or positive");
        }

        if (price <= 0.0) {
            if (positionSide.equals("LONG")) {
                bot.setLastLongPrice(0.0);
                bot.setLongTradeCount(0);
            } else {
                bot.setLastShortPrice(0.0);
                bot.setShortTradeCount(0);
            }
        } else {
            bot.recordAverageTradePrice(positionSide, price);
        }

        repo.save(bot);
        return ResponseEntity.ok().build();
    }

    @PutMapping("/config/{symbol}")
    public ResponseEntity<BotConfigResponse> updateConfig(@PathVariable String symbol, @RequestBody BotConfig newConfig,
                                                          Authentication authentication){

        AppUser currentUser = user(authentication);
        BotConfig config = repo.findByUserAndSymbol(currentUser, symbol).orElseGet(() -> {
            BotConfig fresh = new BotConfig();
            fresh.setSymbol(symbol);
            fresh.setUser(currentUser);
            return fresh;
        });

        config.setParameters(newConfig.getParameters());
        config.setActive(newConfig.isActive());
        if (newConfig.getHammerMultiplier() < 0.1 || newConfig.getHammerMultiplier() > 10.0
                || newConfig.getVolumeSurgeMultiplier() < 0.1 || newConfig.getVolumeSurgeMultiplier() > 10.0
                || !(newConfig.getTradingSide().equals("LONG") || newConfig.getTradingSide().equals("SHORT") || newConfig.getTradingSide().equals("BOTH"))) {
            return ResponseEntity.badRequest().build();
        }
        if (newConfig.getPriceStepPercentage() != null) {
            if (!Double.isFinite(newConfig.getPriceStepPercentage()) || newConfig.getPriceStepPercentage() < 0.0 || newConfig.getPriceStepPercentage() > 100.0) {
                return ResponseEntity.badRequest().build();
            }
            config.setPriceStepPercentage(newConfig.getPriceStepPercentage());
        }
        if (newConfig.getTakeProfitPercentage() != null) {
            if (!Double.isFinite(newConfig.getTakeProfitPercentage()) || newConfig.getTakeProfitPercentage() < 0.0 || newConfig.getTakeProfitPercentage() > 100.0) {
                return ResponseEntity.badRequest().build();
            }
            config.setTakeProfitPercentage(newConfig.getTakeProfitPercentage());
        }
        if (newConfig.getTradeQuantity() != null) {
            if (!Double.isFinite(newConfig.getTradeQuantity()) || newConfig.getTradeQuantity() <= 0.0 || newConfig.getTradeQuantity() > 100.0) {
                return ResponseEntity.badRequest().build();
            }
            config.setTradeQuantity(newConfig.getTradeQuantity());
        }

        return ResponseEntity.ok(toResponse(repo.save(config)));
    }

    @GetMapping("/configs/active")
    public ResponseEntity<List<BotConfigResponse>> getActiveConfigs(Authentication authentication) {
        List<BotConfig> activeConfigs = repo.findByUserAndActiveTrue(user(authentication));

        List<BotConfigResponse> responseList = activeConfigs.stream()
                .map(this::toResponse)
                .collect(Collectors.toList());

        return ResponseEntity.ok(responseList);
    }

    private AppUser user(Authentication authentication) {
        return userRepo.findByEmail(authentication.getName()).orElseThrow();
    }

    private BotConfigResponse toResponse(BotConfig config) {
        return new BotConfigResponse(
                config.getId(),
                config.getSymbol(),
                config.isActive(),
                config.getPriceStepPercentage() != null ? config.getPriceStepPercentage() : 0.0,
                config.getTakeProfitPercentage(),
                config.getTradeQuantity(),
                config.getLastLongPrice() != null ? config.getLastLongPrice() : 0.0,
                config.getLastShortPrice() != null ? config.getLastShortPrice() : 0.0,
                new BotConfigResponse.ParametersDto(
                        config.getRsiPeriod(),
                        config.getRsiOverbought(),
                        config.getRsiOversold(),
                        config.getTimeframe(),
                        config.getHammerMultiplier(),
                        config.getVolumeSurgeMultiplier(),
                        config.getTradingSide()
                )
        );
    }
}