package com.nihilisttt.eegdoctor;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

import androidx.core.content.ContextCompat;

import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

public class EegStripView extends View {

    private static final int NUM_CH = 8;
    private static final int MAX_POINTS = 5000;
    private static final float SAMPLE_RATE = 250.0f;
    private static final int INVALIDATE_INTERVAL_MS = 33;
    private static final float OVERFLOW_RATIO = 0.15f;

    private final float[][] buffers = new float[NUM_CH][MAX_POINTS];
    private final int[] writeIdxs = new int[NUM_CH];
    private final int[] pointCounts = new int[NUM_CH];
    private final Lock[] locks = new Lock[NUM_CH];
    {
        for (int i = 0; i < NUM_CH; i++) locks[i] = new ReentrantLock();
    }

    private float yRange = 0.0001f;

    private float xMax = MAX_POINTS / SAMPLE_RATE;
    private long lastInvalidateTime = 0;

    private final Paint[] wavePaints = new Paint[NUM_CH];
    private final Paint[] glowPaints = new Paint[NUM_CH];
    private final Path[] wavePaths = new Path[NUM_CH];
    {
        for (int i = 0; i < NUM_CH; i++) wavePaths[i] = new Path();
    }

    private Paint separatorPaint;
    private Paint labelPaint;
    private Paint bgPaint;
    private Paint axisPaint;
    private Paint gridPaint;
    private Paint gridLabelPaint;

    private static final int[] WAVE_COLOR_RES = {
            R.color.wave_ch0, R.color.wave_ch1, R.color.wave_ch2, R.color.wave_ch3,
            R.color.wave_ch4, R.color.wave_ch5, R.color.wave_ch6, R.color.wave_ch7
    };

    private static final String[] CH_NAMES = EegChannels.NAMES;

    private float lastSpanY = 0;

    private int visibleChCount = NUM_CH;
    private int[] channelMap = null;

    public void setVisibleChannels(int[] channels) {
        if (channels == null || channels.length == 0) {
            channelMap = null;
            visibleChCount = NUM_CH;
        } else {
            channelMap = new int[channels.length];
            for (int i = 0; i < channels.length; i++) {
                channelMap[i] = Math.max(0, Math.min(NUM_CH - 1, channels[i]));
            }
            visibleChCount = channels.length;
        }
        invalidate();
    }

    public EegStripView(Context context, AttributeSet attrs) {
        super(context, attrs);
        initPaints();
    }

    private void initPaints() {
        int bgColor = ContextCompat.getColor(getContext(), R.color.surface_low);
        int sepColor = ContextCompat.getColor(getContext(), R.color.surface_high);
        int axisColor = ContextCompat.getColor(getContext(), R.color.axis_line);
        int textColor = ContextCompat.getColor(getContext(), R.color.text_secondary);

        bgPaint = new Paint();
        bgPaint.setColor(bgColor);
        bgPaint.setStyle(Paint.Style.FILL);

        separatorPaint = new Paint();
        separatorPaint.setColor(sepColor);
        separatorPaint.setStrokeWidth(1f);

        axisPaint = new Paint();
        axisPaint.setColor(axisColor);
        axisPaint.setStrokeWidth(0.5f);

        gridPaint = new Paint();
        gridPaint.setColor(ContextCompat.getColor(getContext(), R.color.grid_line));
        gridPaint.setStrokeWidth(0.5f);
        gridPaint.setStyle(Paint.Style.STROKE);
        gridPaint.setPathEffect(new android.graphics.DashPathEffect(new float[]{6f, 4f}, 0f));

        gridLabelPaint = new Paint();
        gridLabelPaint.setColor(textColor);
        gridLabelPaint.setAntiAlias(true);

        labelPaint = new Paint();
        labelPaint.setColor(textColor);
        labelPaint.setAntiAlias(true);

        for (int i = 0; i < NUM_CH; i++) {
            int color = ContextCompat.getColor(getContext(), WAVE_COLOR_RES[i]);

            wavePaints[i] = new Paint();
            wavePaints[i].setColor(color);
            wavePaints[i].setStrokeWidth(1.2f);
            wavePaints[i].setStyle(Paint.Style.STROKE);
            wavePaints[i].setAntiAlias(true);
            wavePaints[i].setStrokeJoin(Paint.Join.ROUND);

            glowPaints[i] = new Paint();
            glowPaints[i].setColor(color);
            glowPaints[i].setStrokeWidth(4f);
            glowPaints[i].setStyle(Paint.Style.STROKE);
            glowPaints[i].setAntiAlias(true);
            glowPaints[i].setAlpha(30);
            glowPaints[i].setStrokeJoin(Paint.Join.ROUND);
        }
    }

    public void addPoint(int ch, float value) {
        if (ch < 0 || ch >= NUM_CH) return;
        Lock lock = locks[ch];
        lock.lock();
        buffers[ch][writeIdxs[ch]] = value;
        writeIdxs[ch] = (writeIdxs[ch] + 1) % MAX_POINTS;
        if (pointCounts[ch] < MAX_POINTS) pointCounts[ch]++;
        lock.unlock();

        long now = System.currentTimeMillis();
        if (now - lastInvalidateTime >= INVALIDATE_INTERVAL_MS) {
            lastInvalidateTime = now;
            postInvalidate();
        }
    }

    public void setYRange(float range) {
        if (range > 0) {
            yRange = range;
            invalidate();
        }
    }

    public float getYRange() { return yRange; }


    public void setXMax(float xMax) {
        if (xMax > 0) {
            this.xMax = xMax;
            invalidate();
        }
    }

    public float getXMax() { return xMax; }

    public void clear() {
        for (int i = 0; i < NUM_CH; i++) {
            locks[i].lock();
            writeIdxs[i] = 0;
            pointCounts[i] = 0;
            locks[i].unlock();
        }
        invalidate();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (event.getPointerCount() == 2) {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_POINTER_DOWN:
                    lastSpanY = spanY(event);
                    return true;
                case MotionEvent.ACTION_MOVE:
                    float span = spanY(event);
                    if (lastSpanY > 0 && span > 0) {
                        float ratio = lastSpanY / span;
                        yRange = Math.max(0.000005f, Math.min(0.01f, yRange * ratio));
                        lastSpanY = span;
                        invalidate();
                    }
                    return true;
            }
        }
        return super.onTouchEvent(event);
    }

    private float spanY(MotionEvent e) {
        return Math.abs(e.getY(1) - e.getY(0));
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int width = getWidth();
        int height = getHeight();
        if (width <= 0 || height <= 0) return;

        float labelWidth = 60f;
        float left = labelWidth;
        float right = width - 10f;

        canvas.drawRect(0, 0, width, height, bgPaint);

        float edgePad = height / (float) (visibleChCount + 1) * 0.5f;
        float drawAreaHeight = height - 2 * edgePad;
        float channelHeight = drawAreaHeight / (float) visibleChCount;
        float yMin = -yRange;
        float yMax = yRange;
        float drawHalf = channelHeight * (0.5f + OVERFLOW_RATIO);

        float rulerStepUv = yRange * 2f * 1_000_000f;
        gridLabelPaint.setTextSize(Math.min(10f, channelHeight * 0.22f));

        for (int line = 0; line <= visibleChCount; line++) {
            float lineY = edgePad + line * channelHeight;
            canvas.drawLine(left, lineY, right, lineY, gridPaint);
            float valUv = (visibleChCount - line) * rulerStepUv;
            String label = formatUvPositive(valUv);
            canvas.drawText(label, left + 2f, lineY - 1f, gridLabelPaint);
        }

        labelPaint.setTextSize(Math.min(14f, channelHeight * 0.35f));

        for (int dispCh = 0; dispCh < visibleChCount; dispCh++) {
            int ch = (channelMap != null) ? channelMap[dispCh] : dispCh;
            float centerY = edgePad + (dispCh + 0.5f) * channelHeight;
            float drawTop = centerY - drawHalf;
            float drawBottom = centerY + drawHalf;
            float yScale = (drawBottom - drawTop) / (yMax - yMin);

            canvas.drawLine(0, centerY, left, centerY, separatorPaint);

            int color = wavePaints[ch].getColor();
            labelPaint.setColor(color);
            float labelX = 8f;
            float labelY = centerY - channelHeight * 0.15f;
            canvas.drawText(CH_NAMES[ch], labelX, labelY, labelPaint);

            labelPaint.setColor(color);

            locks[ch].lock();
            int pc = pointCounts[ch];
            if (pc > 1) {
                Path path = wavePaths[ch];
                path.reset();
                float latestTime = (pc - 1) / SAMPLE_RATE;
                float windowStart = latestTime - xMax;
                if (windowStart < 0) windowStart = 0;
                float windowLen = xMax;

                int startIdx = Math.max(0, (int) (windowStart * SAMPLE_RATE));
                int endIdx = Math.min(pc - 1, (int) (latestTime * SAMPLE_RATE));
                boolean first = true;
                for (int i = startIdx; i <= endIdx; i++) {
                    int idx = (writeIdxs[ch] - pc + i + MAX_POINTS) % MAX_POINTS;
                    float val = buffers[ch][idx];
                    float t = i / SAMPLE_RATE;
                    float x = left + ((t - windowStart) / windowLen) * (right - left);
                    float y = drawBottom - (val - yMin) * yScale;
                    if (first) {
                        path.moveTo(x, y);
                        first = false;
                    } else {
                        path.lineTo(x, y);
                    }
                }
                canvas.drawPath(path, glowPaints[ch]);
                canvas.drawPath(path, wavePaints[ch]);
            }
            locks[ch].unlock();
        }

        canvas.drawLine(left, 0, left, height, axisPaint);
    }

    private static String formatUvPositive(float uv) {
        if (uv >= 1000f) return String.format("%.1fmV", uv / 1000f);
        if (uv >= 1f) return String.format("%.0fμV", uv);
        return String.format("%.2fμV", uv);
    }
}