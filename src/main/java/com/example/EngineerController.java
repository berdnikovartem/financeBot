package com.example;

import org.springframework.web.bind.annotation.*;

import java.util.List;

@RestController
@RequestMapping("api/v1/engineers")
public class EngineerController {

    private final EngineerService es;

    public EngineerController(EngineerService es) {
        this.es = es;
    }

    @GetMapping
    public List<Engineer> getEngineers(){
        return es.getSwEngineers();
    }

    @PostMapping
    public void addEngineer(@RequestBody Engineer e){
        es.insertNewEngineer(e);
    }
}
