package com.example;

import jakarta.persistence.Entity;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;

import java.util.Objects;

@Entity
public class Asset {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Integer id;
    private String name;

    public Asset(){}

    public Asset(Integer id, String name){
        this.id = id;
        this.name = name;
    }

    public Integer getId() {
        return id;
    }

    public void setId(Integer id) {
        this.id = id;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    @Override
    public boolean equals(Object o){
        if (o == null || getClass() != o.getClass()) {return false;}
        Asset asset = (Asset) o;
        return getId() == asset.getId() && Objects.equals(getName(), asset.getName());
    }

    @Override
    public int hashCode(){
        return Objects.hash(getId(), getName());
    }

}
