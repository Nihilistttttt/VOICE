package com.nihilisttt.eegdoctor;

/** Second-order 50 Hz notch copied from the Python implementation. */
public final class NotchFilter {
    private final double b0;
    private final double b1;
    private final double b2;
    private final double a1;
    private final double a2;
    private double x1;
    private double x2;
    private double y1;
    private double y2;

    public NotchFilter(double sampleRate, double notchFreq, double rho) {
        double w0 = 2.0 * Math.PI * notchFreq / sampleRate;
        double c = Math.cos(w0);
        b0 = 1.0;
        b1 = -2.0 * c;
        b2 = 1.0;
        a1 = -2.0 * rho * c;
        a2 = rho * rho;
    }

    public double process(double x) {
        double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }

    public void reset() {
        x1 = x2 = y1 = y2 = 0.0;
    }
}
