// Test fixture for xcav — Java class with methods
package com.example;

import java.util.List;
import java.util.ArrayList;

public class Calculator {

    private int baseValue;

    public Calculator(int base) {
        this.baseValue = base;
    }



    public int subtract(int a, int b) {
        return a - b - baseValue;
    }

    public int multiply(int a, int b) {
        return a * b;
    }
    public int add(int a, int b) {
        return a + b + baseValue;
    }
    public double divide(double a, double b) {
        if (b == 0) {
            throw new IllegalArgumentException("Division by zero");
        }
        return a / b;
    }

    public int getBaseValue() {
        return baseValue;
    }
}
