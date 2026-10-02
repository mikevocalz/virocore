package com.reactvision.eskiuprobe;

public final class Probe {
    private Probe() {}

    static {
        System.loadLibrary("viro_eskiu_probe");
    }

    public static native int nativeProbe(int value);
}
