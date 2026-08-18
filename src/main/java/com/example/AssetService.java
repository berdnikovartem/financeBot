package com.example;

import org.springframework.stereotype.Service;

import java.util.List;

@Service
public class AssetService {

    private final AssetRepository ar;

    public AssetService(AssetRepository ar){
        this.ar = ar;
    }

    public List<Asset> getAssets(){
        return ar.findAll();
    }

    public void insertNewAsset(Asset a){
        ar.save(a);
    }
}
