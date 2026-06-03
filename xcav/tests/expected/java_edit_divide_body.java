// realistic.java — exercises all xcav Java features
package com.example;

import java.io.IOException;
import java.util.List;

public class DataService {

    // Constructor with throws
    public DataService(String configPath) throws IOException {
    }

    // Basic method
    public int compute(int a, int b) {
        return a + b;
    }

    // Throws declaration
    public double divide(double a, double b) throws ArithmeticException {
        return b != 0 ? a / b : 0;
    }

    // Static method
    public static void initialize() {
    }

    // Annotations + final modifier
    @Override
    public final String toString() {
        return "DataService";
    }

    // Multiple annotations, same line
    @Deprecated @SuppressWarnings("unchecked")
    public List<String> legacyMethod() {
        return null;
    }

    // Generic method
    public <T> T firstOrNull(List<T> items) {
        return items.isEmpty() ? null : items.get(0);
    }

    // Varargs
    public void log(String format, Object... args) {
    }

    // Abstract method — no body
    public abstract void validate();
}

interface DataProcessor {
    void process(byte[] data) throws IOException;

    // Default method
    default String name() {
        return "DataProcessor";
    }
}

enum Status {
    ACTIVE,
    INACTIVE;

    public boolean isActive() {
        return this == ACTIVE;
    }
}
