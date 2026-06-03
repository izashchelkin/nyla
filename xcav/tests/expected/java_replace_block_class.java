// Test fixture for xcav — Java class with methods
package com.example;

import java.util.List;
import java.util.ArrayList;
public class Calculator {

    private int baseValue;

    public Calculator(int base) {
        this.baseValue = base;
    }

    public int compute(int x) {
        return x + baseValue;
    }
}
