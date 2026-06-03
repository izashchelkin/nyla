package com.example;

import java.io.IOException;

public interface ServiceInterface {
    void start() throws IOException;
    String getName();
    default void shutdown() {
    }
}
