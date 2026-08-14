package com.example;

import org.springframework.stereotype.Service;

import java.util.List;

@Service
public class EngineerService {
    private final EngineerRepository er;

    public EngineerService(EngineerRepository er) {
        this.er = er;
    }

    public List<Engineer> getSwEngineers(){
        return er.findAll();
    }

    public void insertNewEngineer(Engineer e) {
        er.save(e);
    }
}
