package com.example;

import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

@RestController
@RequestMapping("api/v1/config")
public class BotConfigController{

    private final BotConfigRepository repo;

    public BotConfigController(BotConfigRepository repo){
        this.repo = repo;
    }

    @GetMapping("/{symbol}") //getting of current settings of a particular asset
    public ResponseEntity<BotConfig> getConfig(@PathVariable String symbol){
        return repo.findBySymbol(symbol).map(ResponseEntity::ok)
                .orElse(ResponseEntity.notFound().build()); //if the symbol is not found in database
                                                            //then it returns the 404 error
    }

    @PutMapping("/{symbol}")
    public ResponseEntity<BotConfig> updateConfig(@PathVariable String symbol, @RequestBody BotConfig newConfig){

        BotConfig config = repo.findBySymbol(symbol).orElseGet(() -> {
            BotConfig fresh = new BotConfig();
            fresh.setSymbol(symbol);
            return fresh;
        });

        config.setParameters(newConfig.getParameters());
        config.setActive(newConfig.isActive());

        return ResponseEntity.ok(repo.save(config));
    }
}