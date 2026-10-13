package com.nihilisttt.eegdoctor;

import com.nihilisttt.eegdoctor.R;

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

import androidx.core.content.ContextCompat;

import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

import android.util.Log;

public class WaveformView extends View {
    private static final String TAG = "WaveformView";
    private static final int MAX_POINTS = 5000;
    private static final float SAMPLE_RATE = 250.0f;
    private static final int INVALIDATE_INTERVAL_MS = 33;
    private long lastInvalidateTime = 0;
    private float mYRange = 0.5f;
    private float mXMax = MAX_POINTS / SAMPLE_RATE;

    private float[] buffer = new float[MAX_POINTS];
    private int writeIdx = 0;
    private int pointCount = 0;
    private int logCounter = 0;
    private Paint paint, glowPaint;
    private Lock lock = new ReentrantLock();
    private final Path wavePath = new Path();
    private float cachedAvg = 0f;
    private boolean avgDirty = true;

    private Paint axisPaint, gridPaint, labelPaint, modePaint;
    private Paint canvasBgPaint;
    private String mUnit = "mV";

    private static final int MODE_FIX = 0;
    private static final int MODE_TRACK = 1;
    private static final int MODE_SLOW_TRACK = 2;
    private int currentMode = MODE_FIX;

    private float fixCenterOffset = 0f;

    private float slowTargetCenter = 0f;
    private int slowOffsetCount = 0;
    private static final float SLOW_THRESHOLD_RATIO = 0.5f;
    private static final int SLOW_SETTLE_SAMPLES = 200;

    private float lastTouchY = 0;
    private boolean isDragging = false;

    private int waveColor;

    public WaveformView(Context context, AttributeSet attrs) {
        super(context, attrs);
        TypedArray a = context.obtainStyledAttributes(attrs, R.styleable.WaveformView);
        waveColor = a.getColor(R.styleable.WaveformView_waveColor,
                ContextCompat.getColor(context, R.color.wave_ch0));
        a.recycle();
        initPaints();
        setClickable(true);
        setOnClickListener(v -> {
            int newMode = (currentMode + 1) % 3;
            switchMode(newMode);
        });
    }

    private void initPaints() {
        int bgColor = ContextCompat.getColor(getContext(), R.color.surface_base);
        int gridColor = ContextCompat.getColor(getContext(), R.color.grid_line);
        int axisColor = ContextCompat.getColor(getContext(), R.color.axis_line);
        int textColor = ContextCompat.getColor(getContext(), R.color.text_secondary);
        int modeColor = ContextCompat.getColor(getContext(), R.color.accent_info);

        canvasBgPaint = new Paint();
        canvasBgPaint.setColor(bgColor);
        canvasBgPaint.setStyle(Paint.Style.FILL);

        paint = new Paint();
        paint.setColor(waveColor);
        paint.setStrokeWidth(1.5f);
        paint.setStyle(Paint.Style.STROKE);
        paint.setAntiAlias(true);
        paint.setStrokeJoin(Paint.Join.ROUND);

        glowPaint = new Paint();
        glowPaint.setColor(waveColor);
        glowPaint.setStrokeWidth(5f);
        glowPaint.setStyle(Paint.Style.STROKE);
        glowPaint.setAntiAlias(true);
        glowPaint.setAlpha(35);
        glowPaint.setStrokeJoin(Paint.Join.ROUND);

        axisPaint = new Paint();
        axisPaint.setColor(axisColor);
        axisPaint.setStrokeWidth(1f);
        axisPaint.setStyle(Paint.Style.STROKE);

        gridPaint = new Paint();
        gridPaint.setColor(gridColor);
        gridPaint.setStrokeWidth(0.5f);
        gridPaint.setStyle(Paint.Style.STROKE);
        gridPaint.setPathEffect(new DashPathEffect(new float[]{4f, 4f}, 0));

        labelPaint = new Paint();
        labelPaint.setColor(waveColor);
        labelPaint.setTextSize(12f);
        labelPaint.setAntiAlias(true);

        modePaint = new Paint();
        modePaint.setColor(modeColor);
        modePaint.setTextSize(20f);
        modePaint.setAntiAlias(true);
    }

    public int getMode() { return currentMode; }

    public void setMode(int mode) {
        if (mode >= MODE_FIX && mode <= MODE_SLOW_TRACK) {
            switchMode(mode);
        }
    }

    private void switchMode(int newMode) {
        if (newMode == MODE_FIX) {
            float currentCenter = getCurrentWindowCenter();
            fixCenterOffset = currentCenter;
        } else if (newMode == MODE_SLOW_TRACK) {
            float avg = computeWindowAverage();
            slowTargetCenter = avg;
            slowOffsetCount = 0;
        }
        currentMode = newMode;
        invalidate();
    }

    private float getCurrentWindowCenter() {
        if (currentMode == MODE_FIX) {
            return fixCenterOffset;
        } else if (currentMode == MODE_TRACK) {
            return computeWindowAverage();
        } else {
            return slowTargetCenter;
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (currentMode != MODE_FIX) {
            return super.onTouchEvent(event);
        }
        int width = getWidth();
        int height = getHeight();
        if (width <= 0 || height <= 0) return super.onTouchEvent(event);
        float left = 100f;
        float touchX = event.getX();
        if (touchX < left + 40) {
            switch (event.getAction()) {
                case MotionEvent.ACTION_DOWN:
                    lastTouchY = event.getY();
                    isDragging = true;
                    return true;
                case MotionEvent.ACTION_MOVE:
                    if (isDragging) {
                        float deltaY = event.getY() - lastTouchY;
                        float top = 10f, bottom = height - 50f;
                        float yMin = fixCenterOffset - mYRange;
                        float yMax = fixCenterOffset + mYRange;
                        float yScale = (bottom - top) / (yMax - yMin);
                        float deltaVolt = deltaY / yScale;
                        fixCenterOffset += deltaVolt;
                        lastTouchY = event.getY();
                        invalidate();
                    }
                    return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    isDragging = false;
                    return true;
            }
        }
        return super.onTouchEvent(event);
    }

    public void setWaveColor(int color) {
        waveColor = color;
        paint.setColor(color);
        glowPaint.setColor(color);
        glowPaint.setAlpha(35);
        invalidate();
    }

    public void setYRange(float range) {
        if (range > 0) {

            mYRange = range;
            slowTargetCenter = computeWindowAverage();
            slowOffsetCount = 0;
            invalidate();
        }
    }

    public float getYRange() { return mYRange; }

    public void setXMax(float xMax) {
        if (xMax > 0) {
            mXMax = xMax;
            invalidate();
        }
    }

    public float getXMax() { return mXMax; }

    public void setUnit(String unit) {
        if (unit != null && !unit.equals(mUnit)) {
            mUnit = unit;
            invalidate();
        }
    }

    public void clear() {
        lock.lock();
        writeIdx = 0;
        pointCount = 0;
        lock.unlock();
        fixCenterOffset = 0f;
        slowTargetCenter = 0f;
        slowOffsetCount = 0;
        invalidate();
    }

    public void addPoint(float value) {
        lock.lock();
        buffer[writeIdx] = value;
        writeIdx = (writeIdx + 1) % MAX_POINTS;
        if (pointCount < MAX_POINTS) pointCount++;
        lock.unlock();
        avgDirty = true;
        logCounter++;
        if (logCounter % 500 == 0) {
            Log.d(TAG, "addPoint: count=" + pointCount + " val=" + value + " range=" + mYRange);
        }
        long now = System.currentTimeMillis();
        if (now - lastInvalidateTime >= INVALIDATE_INTERVAL_MS) {
            lastInvalidateTime = now;
            postInvalidate();
        }
    }

    private float computeWindowAverage() {
        if (!avgDirty) return cachedAvg;
        lock.lock();
        if (pointCount == 0) {
            lock.unlock();
            cachedAvg = 0f;
            avgDirty = false;
            return 0f;
        }
        float latestTime = (pointCount - 1) / SAMPLE_RATE;
        float windowStart = latestTime - mXMax;
        if (windowStart < 0) windowStart = 0;
        float windowEnd = latestTime;

        float sum = 0f;
        int count = 0;
        int startIdx = (int)(windowStart * SAMPLE_RATE);
        int endIdx = (int)(windowEnd * SAMPLE_RATE);
        if (startIdx < 0) startIdx = 0;
        if (endIdx >= pointCount) endIdx = pointCount - 1;
        for (int i = startIdx; i <= endIdx; i++) {
            int idx = (writeIdx - pointCount + i + MAX_POINTS) % MAX_POINTS;
            sum += buffer[idx];
            count++;
        }
        lock.unlock();
        cachedAvg = (count > 0) ? sum / count : 0f;
        avgDirty = false;
        return cachedAvg;
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

    private static String formatFloat(float value, float scale) {
        long rounded = Math.round(value * scale);
        if (scale == 1f) return String.valueOf(rounded);
        long integerPart = rounded / (long) scale;
        long fracPart = Math.abs(rounded % (long) scale);
        if (fracPart == 0) return String.valueOf(integerPart);
        StringBuilder sb = new StringBuilder();
        sb.append(integerPart).append('.');
        String fracStr = String.valueOf(fracPart);
        int digits = (int) Math.log10(scale);
        while (fracStr.length() < digits) {
            sb.append('0');
            fracStr = "0" + fracStr;
        }
        while (fracStr.endsWith("0")) {
            fracStr = fracStr.substring(0, fracStr.length() - 1);
        }
        sb.append(fracStr);
        return sb.toString();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int width = getWidth();
        int height = getHeight();
        if (width <= 0 || height <= 0) return;

        float left = 100, top = 10, right = width - 10, bottom = height - 50;
        canvas.drawRect(0, 0, width, height, canvasBgPaint);

        float avg = computeWindowAverage();

        if (currentMode == MODE_SLOW_TRACK) {
            float threshold = mYRange * SLOW_THRESHOLD_RATIO;
            if (Math.abs(avg - slowTargetCenter) > threshold) {
                slowOffsetCount++;
                if (slowOffsetCount >= SLOW_SETTLE_SAMPLES) {
                    slowTargetCenter = avg;
                    slowOffsetCount = 0;
                }
            } else {
                slowOffsetCount = 0;
            }
        }

        final float centerVal;
        if (currentMode == MODE_FIX) {
            centerVal = fixCenterOffset;
        } else if (currentMode == MODE_TRACK) {
            centerVal = avg;
        } else {
            centerVal = slowTargetCenter;
        }

        final float yMin = centerVal - mYRange;
        final float yMax = centerVal + mYRange;
        float yScale = (bottom - top) / (yMax - yMin);
        float centerY = bottom - (centerVal - yMin) * yScale;

        float xMin = 0;
        float xMax = mXMax;
        float xScale = (right - left) / (xMax - xMin);

        float unitScale;
        switch (mUnit) {
            case "uV": unitScale = 1_000_000f; break;
            case "V":  unitScale = 1f; break;
            default:   unitScale = 1000f; break;
        }

        float yTickSpacing = niceNum(mYRange / 3.0f, true);
        float yTick = (float) Math.floor(yMin / yTickSpacing) * yTickSpacing;
        float scaledTickSpacing = yTickSpacing * unitScale;
        int labelDecimals;
        if (scaledTickSpacing >= 10f) labelDecimals = 0;
        else if (scaledTickSpacing >= 1f) labelDecimals = 1;
        else if (scaledTickSpacing >= 0.1f) labelDecimals = 2;
        else labelDecimals = 3;
        float tickScale = (float) Math.pow(10, labelDecimals);
        while (yTick <= yMax + yTickSpacing * 0.5f) {
            float y = bottom - (yTick - yMin) * yScale;
            canvas.drawLine(left, y, right, y, gridPaint);
            canvas.drawLine(left - 5, y, left, y, axisPaint);
            float scaled = yTick * unitScale;
            String label = formatFloat(scaled, tickScale) + mUnit;
            float tw = labelPaint.measureText(label);
            canvas.drawText(label, left - 15 - tw, y + 5, labelPaint);
            yTick += yTickSpacing;
        }

        float xTickSpacing = niceNum((xMax - xMin) / 5.0f, true);
        float xTick = (float) Math.floor(xMin / xTickSpacing) * xTickSpacing;
        while (xTick <= xMax + xTickSpacing * 0.5f) {
            float x = left + (xTick - xMin) * xScale;
            canvas.drawLine(x, top, x, bottom, gridPaint);
            canvas.drawLine(x, bottom, x, bottom + 5, axisPaint);
            String label = formatFloat(xTick, 10f) + "s";
            canvas.drawText(label, x - 15, bottom + 25, labelPaint);
            xTick += xTickSpacing;
        }

        canvas.drawLine(left, top, left, bottom, axisPaint);
        canvas.drawLine(left, centerY, right, centerY, axisPaint);

        lock.lock();
        if (pointCount > 1) {
            wavePath.reset();
            float latestTime = (pointCount - 1) / SAMPLE_RATE;
            float windowStart = latestTime - mXMax;
            float windowEnd = latestTime;
            float windowLen = mXMax;

            int startIdx = Math.max(0, (int)(windowStart * SAMPLE_RATE));
            int endIdx = Math.min(pointCount - 1, (int)(windowEnd * SAMPLE_RATE));
            boolean first = true;
            for (int i = startIdx; i <= endIdx; i++) {
                int idx = (writeIdx - pointCount + i + MAX_POINTS) % MAX_POINTS;
                float val = buffer[idx];
                float t = i / SAMPLE_RATE;
                float x = left + ((t - windowStart) / windowLen) * (right - left);
                float y = bottom - (val - yMin) * yScale;
                if (first) {
                    wavePath.moveTo(x, y);
                    first = false;
                } else {
                    wavePath.lineTo(x, y);
                }
            }
            canvas.drawPath(wavePath, glowPaint);
            canvas.drawPath(wavePath, paint);
        }
        lock.unlock();

        float scaledRange = mYRange * unitScale;
        int rangeDecimals;
        if (scaledRange >= 100f) rangeDecimals = 0;
        else if (scaledRange >= 1f) rangeDecimals = 1;
        else if (scaledRange >= 0.01f) rangeDecimals = 2;
        else rangeDecimals = 3;
        float rangeScale = (float) Math.pow(10, rangeDecimals);
        String maxLabel = formatFloat(yMax * unitScale, rangeScale) + " " + mUnit;
        String minLabel = formatFloat(yMin * unitScale, rangeScale) + " " + mUnit;
        canvas.drawText(maxLabel, left + 5, top + 15, labelPaint);
        canvas.drawText(minLabel, left + 5, bottom - 10, labelPaint);

        String modeText;
        if (currentMode == MODE_FIX) modeText = "Fix";
        else if (currentMode == MODE_TRACK) modeText = "Track";
        else modeText = "Slow";
        float modeWidth = modePaint.measureText(modeText);
        canvas.drawText(modeText, right - modeWidth - 10, top + 25, modePaint);
    }
}
