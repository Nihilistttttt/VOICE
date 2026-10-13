package com.nihilisttt.eegdoctor;

public class FocusHistoryStore {
    private static final int MAX_POINTS = 5000;
    private static final FocusHistoryStore INSTANCE = new FocusHistoryStore();

    private final float[] focusBuffer = new float[MAX_POINTS];
    private final float[] relaxBuffer = new float[MAX_POINTS];
    private int writeIdx = 0;
    private int pointCount = 0;

    public static FocusHistoryStore getInstance() { return INSTANCE; }

    public synchronized void addPoint(float focus, float relax) {
        focusBuffer[writeIdx] = focus;
        relaxBuffer[writeIdx] = relax;
        writeIdx = (writeIdx + 1) % MAX_POINTS;
        if (pointCount < MAX_POINTS) pointCount++;
    }

    public synchronized int snapshot(float[] outFocus, float[] outRelax) {
        int count = pointCount;
        for (int i = 0; i < count; i++) {
            int idx = (writeIdx - count + i + MAX_POINTS) % MAX_POINTS;
            outFocus[i] = focusBuffer[idx];
            outRelax[i] = relaxBuffer[idx];
        }
        return count;
    }

    public synchronized int getPointCount() { return pointCount; }

    public synchronized void clear() {
        writeIdx = 0;
        pointCount = 0;
    }
}