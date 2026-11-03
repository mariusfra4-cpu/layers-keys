package com.jbpraise.layerskeys;

import android.graphics.Bitmap;

/** Pont vers le moteur Go (liblk.so) : son, image, doigts, MIDI. */
public final class Native {
    private Native() {}

    static { System.loadLibrary("lk"); }

    public static native void init(String filesDir, String androidId, int rate);
    public static native int audioStart();
    public static native void audioStop();
    public static native int audioRate();
    public static native int frame(Bitmap b);
    public static native int size();
    public static native void layout(float[] out);
    public static native void setView(int w, int h);
    public static native void touch(int action, int id, float x, float y);
    public static native void key(int vk, int down);
    public static native void chr(int codePoint);
    public static native void back();
    public static native void stop();
    public static native void midi(byte[] data, int off, int len);
    public static native void midiStatus(int ok, String text);
    public static native String nextReq();
    public static native void fileResult(String path);
    public static native void setClip(String text);
    public static native String status();
}
