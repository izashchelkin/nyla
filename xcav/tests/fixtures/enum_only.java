package com.example;

public enum Color {
    RED,
    GREEN,
    BLUE;

    public boolean isBright() {
        return this == RED;
    }
}
