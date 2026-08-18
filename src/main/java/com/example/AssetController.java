package com.example;

import org.springframework.web.bind.annotation.*;

import java.util.List;

@RestController
@RequestMapping("api/assets")
public class AssetController {

    //private final AssetService as;

    public AssetController(/*AssetService as*/){
        //this.as = as;
    }

    /*@GetMapping
    public List<Asset> getAssets(){
        //return as.getAssets();
        return;
    }
    */

    /*@PostMapping
    public void addAsset(@RequestBody Asset a){
        as.insertNewAsset(a);
    }
}
