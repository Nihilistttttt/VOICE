package com.nihilisttt.eegdoctor;

import com.nihilisttt.eegdoctor.R;

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.view.View;

import androidx.core.content.ContextCompat;

import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

public class FocusChartView extends View {
    private static final int MAX_POINTS = 5000;
    private static final float SAMPLE_RATE = 10.0f;
    private static final float DEFAULT_X_MAX = 50.0f;

    private static final float Y_MIN = 0.0f;
    private static final float Y_MAX = 1.0f;
    private static final float Y_TICK_STEP = 0.05f;

    private float mXMax = DEFAULT_X_MAX;

    private float[] focusBuffer = new float[MAX_POINTS];
    private float[] relaxBuffer = new float[MAX_POINTS];
    private int writeIdx = 0;
    private int pointCount = 0;

    private Paint focusPaint, focusGlowPaint, focusFillPaint;
    private Paint relaxPaint, relaxGlowPaint, relaxFillPaint;
    private Lock lock = new ReentrantLock();

    private Paint axisPaint, gridPaint, textPaint;
    private Paint canvasBgPaint;

    private boolean focusVisible = true;
    private boolean relaxVisible = true;

    public FocusChartView(Context context, AttributeSet attrs) {
        super(context, attrs);
        initPaints();
    }

    private void initPaints() {
        int bgColor = ContextCompat.getColor(getContext(), R.color.surface_base);
        int gridColor = ContextCompat.getColor(getContext(), R.color.grid_line);
        int axisColor = ContextCompat.getColor(getContext(), R.color.axis_line);
        int textColor = ContextCompat.getColor(getContext(), R.color.text_secondary);
        int focusClr = ContextCompat.getColor(getContext(), R.color.focus_line);
        int relaxClr = ContextCompat.getColor(getContext(), R.color.accent_info);

        canvasBgPaint = new Paint();
        canvasBgPaint.setColor(bgColor);
        canvasBgPaint.setStyle(Paint.Style.FILL);

        focusPaint = new Paint();
        focusPaint.setColor(focusClr);
        focusPaint.setStrokeWidth(1.5f);
        focusPaint.setStyle(Paint.Style.STROKE);
        focusPaint.setAntiAlias(true);
        focusPaint.setStrokeJoin(Paint.Join.ROUND);

        focusGlowPaint = new Paint();
        focusGlowPaint.setColor(focusClr);
        focusGlowPaint.setStrokeWidth(5f);
        focusGlowPaint.setStyle(Paint.Style.STROKE);
        focusGlowPaint.setAntiAlias(true);
        focusGlowPaint.setAlpha(35);
        focusGlowPaint.setStrokeJoin(Paint.Join.ROUND);

        relaxPaint = new Paint();
        relaxPaint.setColor(relaxClr);
        relaxPaint.setStrokeWidth(1.5f);
        relaxPaint.setStyle(Paint.Style.STROKE);
        relaxPaint.setAntiAlias(true);
        relaxPaint.setStrokeJoin(Paint.Join.ROUND);

        relaxGlowPaint = new Paint();
        relaxGlowPaint.setColor(relaxClr);
        relaxGlowPaint.setStrokeWidth(5f);
        relaxGlowPaint.setStyle(Paint.Style.STROKE);
        relaxGlowPaint.setAntiAlias(true);
        relaxGlowPaint.setAlpha(35);
        relaxGlowPaint.setStrokeJoin(Paint.Join.ROUND);

        focusFillPaint = new Paint();
        focusFillPaint.setColor(focusClr);
        focusFillPaint.setStyle(Paint.Style.FILL);
        focusFillPaint.setAntiAlias(true);

        relaxFillPaint = new Paint();
        relaxFillPaint.setColor(relaxClr);
        relaxFillPaint.setStyle(Paint.Style.FILL);
        relaxFillPaint.setAntiAlias(true);

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
        textPaint.setTextSize(10f);
        textPaint.setAntiAlias(true);
    }

    public void setFocusVisible(boolean visible) { focusVisible = visible; invalidate(); }
    public void setRelaxVisible(boolean visible) { relaxVisible = visible; invalidate(); }
    public boolean isFocusVisible() { return focusVisible; }
    public boolean isRelaxVisible() { return relaxVisible; }

    public void setXMax(float xMax) {
        if (xMax > 0) { mXMax = xMax; invalidate(); }
    }

    public float getXMax() { return mXMax; }

    public void addPoint(float focusValue, float relaxValue) {
        lock.lock();
        focusBuffer[writeIdx] = focusValue;
        relaxBuffer[writeIdx] = relaxValue;
        writeIdx = (writeIdx + 1) % MAX_POINTS;
        if (pointCount < MAX_POINTS) pointCount++;
        lock.unlock();
        postInvalidate();
    }

    public void loadFromStore() {
        FocusHistoryStore store = FocusHistoryStore.getInstance();
        float[] tmpFocus = new float[MAX_POINTS];
        float[] tmpRelax = new float[MAX_POINTS];
        int count = store.snapshot(tmpFocus, tmpRelax);
        if (count == 0) return;
        lock.lock();
        for (int i = 0; i < count; i++) {
            focusBuffer[i] = tmpFocus[i];
            relaxBuffer[i] = tmpRelax[i];
        }
        writeIdx = count % MAX_POINTS;
        pointCount = count;
        lock.unlock();
        invalidate();
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
        int width = getWidth(), height = getHeight();
        if (width <= 0 || height <= 0) return;

        float left = 100, top = 10, right = width - 10, bottom = height - 50;
        canvas.drawRect(0, 0, width, height, canvasBgPaint);

        final float yMin = Y_MIN;
        final float yMax = Y_MAX;
        float yScale = (bottom - top) / (yMax - yMin);

        float xMin = 0;
        float xMax = mXMax;
        float xScale = (right - left) / (xMax - xMin);

        for (float yTick = 0; yTick <= 1.001f; yTick += Y_TICK_STEP) {
            float y = bottom - (yTick - yMin) * yScale;
            if (y < top - 1 || y > bottom + 1) continue;
            canvas.drawLine(left, y, right, y, gridPaint);
            canvas.drawLine(left - 5, y, left, y, axisPaint);
            String label = String.format("%.0f", yTick * 100);
            float tw = textPaint.measureText(label);
            canvas.drawText(label, left - 15 - tw, y + 5, textPaint);
        }

        float xTickSpacing = niceNum((xMax - xMin) / 5.0f, true);
        float xTick = (float) Math.floor(xMin / xTickSpacing) * xTickSpacing;
        while (xTick <= xMax + xTickSpacing * 0.5f) {
            float x = left + (xTick - xMin) * xScale;
            canvas.drawLine(x, top, x, bottom, gridPaint);
            canvas.drawLine(x, bottom, x, bottom + 5, axisPaint);
            String label = String.format("%.1f", xTick) + "s";
            canvas.drawText(label, x - 15, bottom + 25, textPaint);
            xTick += xTickSpacing;
        }

        canvas.drawLine(left, top, left, bottom, axisPaint);
        float centerY = bottom - (0.5f - Y_MIN) * yScale;
        canvas.drawLine(left, centerY, right, centerY, axisPaint);

        lock.lock();
        if (pointCount > 1) {
            float latestTime = (pointCount - 1) / SAMPLE_RATE;
            float windowStart = latestTime - mXMax;
            float windowEnd = latestTime;
            float windowLen = mXMax;

            Path focusPath = null;
            Path relaxPath = null;
            float firstX = 0, lastFocusX = 0, lastRelaxX = 0;

            if (focusVisible) {
                focusPath = new Path();
                boolean first = true;
                for (int i = 0; i < pointCount; i++) {
                    int idx = (writeIdx - pointCount + i + MAX_POINTS) % MAX_POINTS;
                    float val = focusBuffer[idx];
                    float t = i / SAMPLE_RATE;
                    if (t >= windowStart && t <= windowEnd) {
                        float x = left + ((t - windowStart) / windowLen) * (right - left);
                        float y = bottom - (val - yMin) * yScale;
                        if (first) { focusPath.moveTo(x, y); firstX = x; first = false; }
                        else { focusPath.lineTo(x, y); }
                        lastFocusX = x;
                    } else { first = true; }
                }
            }

            if (relaxVisible) {
                relaxPath = new Path();
                boolean first = true;
                for (int i = 0; i < pointCount; i++) {
                    int idx = (writeIdx - pointCount + i + MAX_POINTS) % MAX_POINTS;
                    float val = relaxBuffer[idx];
                    float t = i / SAMPLE_RATE;
                    if (t >= windowStart && t <= windowEnd) {
                        float x = left + ((t - windowStart) / windowLen) * (right - left);
                        float y = bottom - (val - yMin) * yScale;
                        if (first) { relaxPath.moveTo(x, y); firstX = x; first = false; }
                        else { relaxPath.lineTo(x, y); }
                        lastRelaxX = x;
                    } else { first = true; }
                }
            }

            float focusAvg = 0, relaxAvg = 0;
            int winCount = 0;
            for (int i = 0; i < pointCount; i++) {
                int idx = (writeIdx - pointCount + i + MAX_POINTS) % MAX_POINTS;
                float t = i / SAMPLE_RATE;
                if (t >= windowStart && t <= windowEnd) {
                    focusAvg += focusBuffer[idx];
                    relaxAvg += relaxBuffer[idx];
                    winCount++;
                }
            }
            if (winCount > 0) { focusAvg /= winCount; relaxAvg /= winCount; }
            boolean focusOnTop = focusAvg < relaxAvg;

            if (focusOnTop) {
                if (relaxPath != null) {
                    Path fillPath = new Path(relaxPath);
                    fillPath.lineTo(lastRelaxX, bottom);
                    fillPath.lineTo(firstX, bottom);
                    fillPath.close();
                    canvas.drawPath(fillPath, relaxFillPaint);
                    canvas.drawPath(relaxPath, relaxGlowPaint);
                    canvas.drawPath(relaxPath, relaxPaint);
                }
                if (focusPath != null) {
                    Path fillPath = new Path(focusPath);
                    fillPath.lineTo(lastFocusX, bottom);
                    fillPath.lineTo(firstX, bottom);
                    fillPath.close();
                    canvas.drawPath(fillPath, focusFillPaint);
                    canvas.drawPath(focusPath, focusGlowPaint);
                    canvas.drawPath(focusPath, focusPaint);
                }
            } else {
                if (focusPath != null) {
                    Path fillPath = new Path(focusPath);
                    fillPath.lineTo(lastFocusX, bottom);
                    fillPath.lineTo(firstX, bottom);
                    fillPath.close();
                    canvas.drawPath(fillPath, focusFillPaint);
                    canvas.drawPath(focusPath, focusGlowPaint);
                    canvas.drawPath(focusPath, focusPaint);
                }
                if (relaxPath != null) {
                    Path fillPath = new Path(relaxPath);
                    fillPath.lineTo(lastRelaxX, bottom);
                    fillPath.lineTo(firstX, bottom);
                    fillPath.close();
                    canvas.drawPath(fillPath, relaxFillPaint);
                    canvas.drawPath(relaxPath, relaxGlowPaint);
                    canvas.drawPath(relaxPath, relaxPaint);
                }
            }
        }
        lock.unlock();
    }
}
