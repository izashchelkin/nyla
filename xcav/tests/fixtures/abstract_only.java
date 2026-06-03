package com.example;

public abstract class AbstractService {
    public abstract void init();
    public abstract String process(byte[] input);
    public abstract void shutdown();
}
