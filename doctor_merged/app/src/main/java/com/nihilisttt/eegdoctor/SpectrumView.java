package com.nihilisttt.eegdoctor;

import com.nihilisttt.eegdoctor.R;

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Shader;
import android.util.AttributeSet;
import android.view.View;

import androidx.core.content.ContextCompat;


public class SpectrumView extends View {
    private static final int TOTAL_BINS = 128;
    private static final int DISPLAY_BINS = 128;
    private static final float FREQ_MAX = 125.0f;

    private float[] spectrumVoltage = new float[TOTAL_BINS];
    private float[] displayValues = new float[TOTAL_BINS];
    private String mDisplayUnit = "mV";
    private float mDisplayMax = 5f;

    private float mVoltageMax = 0.005f;
    private float mFreqMin = 0f;
    private float mFreqMax = FREQ_MAX;

    private Paint barPaint, axisPaint, gridPaint, textPaint;
    private Paint canvasBgPaint;
    private int spectrumColorStart;
    private int spectrumColorEnd;
    private LinearGradient barGradient;

    private float[] yTickCache = new float[0];
    private float yTickCacheMax = -1;
    private float[] xTickCache = new float[0];
    private boolean displayValuesDirty = true;

    public SpectrumView(Context context, AttributeSet attrs) {
        super(context, attrs);
        TypedArray a = context.obtainStyledAttributes(attrs, R.styleable.SpectrumView);
        spectrumColorStart = a.getColor(R.styleable.SpectrumView_spectrumColor,
                ContextCompat.getColor(context, R.color.spectrum_bar));
        spectrumColorEnd = a.getColor(R.styleable.SpectrumView_spectrumColorEnd,
                ContextCompat.getColor(context, R.color.spectrum_bar_end));
        a.recycle();
        initPaints();
    }

    private void initPaints() {
        int bgColor = ContextCompat.getColor(getContext(), R.color.surface_base);
        int gridColor = ContextCompat.getColor(getContext(), R.color.grid_line);
        int axisColor = ContextCompat.getColor(getContext(), R.color.axis_line);
        int textColor = ContextCompat.getColor(getContext(), R.color.text_secondary);

        canvasBgPaint = new Paint();
        canvasBgPaint.setColor(bgColor);
        canvasBgPaint.setStyle(Paint.Style.FILL);

        barPaint = new Paint();
        barPaint.setStyle(Paint.Style.FILL);
        barPaint.setAntiAlias(true);

        axisPaint = new Paint();
        axisPaint.setColor(axisColor);
        axisPaint.setStrokeWidth(1f);
        axisPaint.setStyle(Paint.Style.STROKE);

        gridPaint = new Paint();
        gridPaint.setColor(gridColor);
        gridPaint.setStrokeWidth(0.5f);
        gridPaint.setStyle(Paint.Style.STROKE);
        gridPaint.setPathEffect(new DashPathEffect(new float[]{4f, 4f}, 0));

        textPaint = new Paint();
        textPaint.setColor(textColor);
        textPaint.setTextSize(12f);
        textPaint.setAntiAlias(true);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        float left = 95, top = 10, right = w - 30, bottom = h - 30;
        barGradient = new LinearGradient(0, bottom, 0, top,
                new int[]{spectrumColorStart, spectrumColorEnd},
                null, Shader.TileMode.CLAMP);
        barPaint.setShader(barGradient);
    }

    public void updateSpectrum(float[] newSpectrum) {
        if (newSpectrum.length != TOTAL_BINS) return;
        System.arraycopy(newSpectrum, 0, spectrumVoltage, 0, TOTAL_BINS);
        displayValuesDirty = true;
        postInvalidate();
    }

    public void setRange(float value, String unit) {
        float voltageMax = convertToVoltage(value, unit);
        if (voltageMax <= 0) return;
        mVoltageMax = voltageMax;
        mDisplayUnit = unit;
        mDisplayMax = convertFromVoltage(mVoltageMax, unit);
        displayValuesDirty = true;
        yTickCacheMax = -1;
        invalidate();
    }

    public void setYMax(float maxVolt) {
        setRange(maxVolt, "V");
    }

    public void setFreqRange(float minHz, float maxHz) {
        minHz = Math.max(0f, minHz);
        maxHz = Math.min(FREQ_MAX, maxHz);
        if (minHz >= maxHz) return;
        mFreqMin = minHz;
        mFreqMax = maxHz;
        xTickCache = new float[0];
        invalidate();
    }

    public float getFreqMin() { return mFreqMin; }
    public float getFreqMax() { return mFreqMax; }

    private float convertToVoltage(float value, String unit) {
        switch (unit) {
            case "uV":   return value * 1e-6f;
            case "mV":   return value * 1e-3f;
            case "V":    return value;
            case "dBuV": return (float) Math.pow(10, value / 20.0) * 1e-6f;
            case "dBmV": return (float) Math.pow(10, value / 20.0) * 1e-3f;
            case "dBV":  return (float) Math.pow(10, value / 20.0);
            default:     return value;
        }
    }

    private float convertFromVoltage(float voltage, String unit) {
        if (voltage <= 0) return 0;
        switch (unit) {
            case "uV":   return voltage * 1e6f;
            case "mV":   return voltage * 1e3f;
            case "V":    return voltage;
            case "dBuV": return (float) (20 * Math.log10(voltage / 1e-6));
            case "dBmV": return (float) (20 * Math.log10(voltage / 1e-3));
            case "dBV":  return (float) (20 * Math.log10(voltage));
            default:     return voltage;
        }
    }

    private float[] computeTicks(float max, int targetCount) {
        if (max <= 0) return new float[]{0f};
        float step = niceNum(max / (targetCount - 1), true);
        int count = 0;
        for (float v = 0; v <= max + step * 0.5f; v += step) count++;
        float[] ticks = new float[count];
        int idx = 0;
        for (float v = 0; v <= max + step * 0.5f; v += step) ticks[idx++] = v;
        return ticks;
    }

    private float[] getYTicks() {
        if (yTickCacheMax != mDisplayMax) {
            yTickCache = computeTicks(mDisplayMax, 6);
            yTickCacheMax = mDisplayMax;
        }
        return yTickCache;
    }

    private float[] getXTicks() {
        if (xTickCache.length == 0) {
            xTickCache = computeTicks(mFreqMax - mFreqMin, 6);
        }
        return xTickCache;
    }

    private float niceNum(float range, boolean round) {
        float exponent = (float) Math.floor(Math.log10(range));
        float fraction = range / (float) Math.pow(10, exponent);
        float niceFraction;
        if (round) {
            if (fraction < 1.5) niceFraction = 1;
            else if (fraction < 3) niceFraction = 2;
            else if (fraction < 7) niceFraction = 5;
            else niceFraction = 10;
        } else {
            if (fraction <= 1) niceFraction = 1;
            else if (fraction <= 2) niceFraction = 2;
            else if (fraction <= 5) niceFraction = 5;
            else niceFraction = 10;
        }
        return niceFraction * (float) Math.pow(10, exponent);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int width = getWidth();
        int height = getHeight();
        if (width <= 0 || height <= 0) return;

        float left = 95, top = 10, right = width - 30, bottom = height - 30;
        canvas.drawRect(0, 0, width, height, canvasBgPaint);

        float yMin = 0;
        float yMax = mDisplayMax;
        float yScale = (bottom - top) / (yMax - yMin);

        float xMin = mFreqMin;
        float xMax = mFreqMax;
        float xScale = (right - left) / (xMax - xMin);

        float[] yTicks = getYTicks();
        for (float yTick : yTicks) {
            float y = bottom - (yTick - yMin) * yScale;
            canvas.drawLine(left, y, right, y, gridPaint);
            canvas.drawLine(left - 5, y, left, y, axisPaint);

            String label;
            if (mDisplayUnit.startsWith("dB")) {
                label = formatFloat1(yTick) + " " + mDisplayUnit;
            } else {
                if (Math.abs(yTick - Math.round(yTick)) < 0.001f) {
                    label = String.valueOf((int) yTick) + " " + mDisplayUnit;
                } else {
                    label = formatFloat2(yTick) + " " + mDisplayUnit;
                }
            }
            float tw = textPaint.measureText(label);
            canvas.drawText(label, left - 10 - tw, y + 5, textPaint);
        }

        float[] xTicks = getXTicks();
        for (float xTick : xTicks) {
            float freq = xMin + xTick;
            if (freq > xMax + 0.01f) continue;
            float x = left + (freq - xMin) * xScale;
            canvas.drawLine(x, top, x, bottom, gridPaint);
            canvas.drawLine(x, bottom, x, bottom + 5, axisPaint);
            String label = String.valueOf((int) freq) + " Hz";
            canvas.drawText(label, x - 15, bottom + 20, textPaint);
        }

        canvas.drawLine(left, top, left, bottom, axisPaint);
        canvas.drawLine(left, bottom, right, bottom, axisPaint);

        if (displayValuesDirty) {
            for (int i = 0; i < TOTAL_BINS; i++) {
                float dv = convertFromVoltage(spectrumVoltage[i], mDisplayUnit);
                if (dv > mDisplayMax) dv = mDisplayMax;
                if (dv < 0) dv = 0;
                displayValues[i] = dv;
            }
            displayValuesDirty = false;
        }

        int firstBin = Math.max(0, (int) Math.floor(xMin / FREQ_MAX * TOTAL_BINS));
        int lastBinExclusive = Math.min(TOTAL_BINS, (int) Math.ceil(xMax / FREQ_MAX * TOTAL_BINS));
        int visibleBins = Math.max(1, lastBinExclusive - firstBin);
        float barWidth = (right - left) / visibleBins;
        float barGap = barWidth * 0.1f;
        for (int i = firstBin; i < lastBinExclusive; i++) {
            float barHeight = displayValues[i] * yScale;
            int visibleIndex = i - firstBin;
            float barLeft = left + visibleIndex * barWidth + barGap;
            float barTop = bottom - barHeight;
            float barRight = left + (visibleIndex + 1) * barWidth - barGap;
            if (barHeight > 1f) {
                canvas.drawRoundRect(barLeft, barTop, barRight, bottom, 1f, 1f, barPaint);
            }
        }
    }

    private static String formatFloat1(float v) {
        return String.valueOf(Math.round(v * 10f) / 10f);
    }

    private static String formatFloat2(float v) {
        return String.valueOf(Math.round(v * 100f) / 100f);
    }
}
