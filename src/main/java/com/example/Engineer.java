package com.example;

import jakarta.persistence.Entity;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;

import java.util.Objects;
import java.util.List;

@Entity
public class Engineer {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Integer id;
    private String name;
    private String stack;

    public Engineer(){}

    public Engineer(Integer id, String name, String stack) {
        this.id = id;
        this.name = name;
        this.stack = stack;
    }

    public Integer getId(){
        return id;
    }

    public String getName(){
        return name;
    }

    public String getStack(){
        return stack;
    }

    public void setId(int id){
        this.id = id;
    }

    public void setName(String name){
        this.name = name;
    }

    public void setStack(String stack){
        this.stack = stack;
    }

    @Override
    public boolean equals(Object o) {
        if (o == null || getClass() != o.getClass()) return false;
        Engineer engineer = (Engineer) o;
        return getId() == engineer.getId() && Objects.equals(getName(), engineer.getName()) && Objects.equals(getStack(), engineer.getStack());
    }

    @Override
    public int hashCode() {
        return Objects.hash(getId(), getName(), getStack());
    }
}
