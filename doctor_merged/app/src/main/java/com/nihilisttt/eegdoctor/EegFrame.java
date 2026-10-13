package com.nihilisttt.eegdoctor;

public class EegFrame {
    public static final int CHANNEL_COUNT = 8;
    public static final int FEATURE_COUNT = 24;

    private String label;
    private final float[] channels = new float[CHANNEL_COUNT];
    private final float[] features = new float[FEATURE_COUNT];
    private long timestamp;

    public EegFrame() {
        this.timestamp = System.currentTimeMillis();
    }

    public String getLabel() { return label; }
    public void setLabel(String label) { this.label = label; }

    public float getChannel(int index) {
        if (index < 0 || index >= CHANNEL_COUNT) return 0f;
        return channels[index];
    }

    public void setChannel(int index, float value) {
        if (index >= 0 && index < CHANNEL_COUNT) channels[index] = value;
    }

    public float[] getChannels() { return channels; }

    public float getFeature(int index) {
        if (index < 0 || index >= FEATURE_COUNT) return 0f;
        return features[index];
    }

    public void setFeature(int index, float value) {
        if (index >= 0 && index < FEATURE_COUNT) features[index] = value;
    }

    public float[] getFeatures() { return features; }

    public long getTimestamp() { return timestamp; }
    public void setTimestamp(long timestamp) { this.timestamp = timestamp; }


}