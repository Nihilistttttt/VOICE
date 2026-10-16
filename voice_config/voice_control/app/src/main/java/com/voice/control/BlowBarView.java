package com.voice.control;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.View;

public class BlowBarView extends View {
    private static final int MAX_SAMPLES = 200;
    private static final int MAX_ENERGY = 10000;
    private final int[] samplesA = new int[MAX_SAMPLES];
    private final int[] samplesB = new int[MAX_SAMPLES];
    private int writeIdx = 0;
    private int sampleCount = 0;
    private int threshold = 2500;

    private final Paint paint = new Paint();
    private final Paint textPaint = new Paint();
    private final Paint linePaint = new Paint();
    private final Paint threshPaint = new Paint();
    private final Paint threshTextPaint = new Paint();

    public BlowBarView(Context context, AttributeSet attrs) {
        super(context, attrs);
        textPaint.setColor(Color.WHITE);
        textPaint.setTextSize(28);
        textPaint.setAntiAlias(true);
        linePaint.setColor(Color.parseColor("#666666"));
        linePaint.setStrokeWidth(2);
        threshPaint.setColor(Color.parseColor("#FF5252"));
        threshPaint.setStrokeWidth(2);
        threshPaint.setAntiAlias(true);
        threshTextPaint.setColor(Color.parseColor("#FF5252"));
        threshTextPaint.setTextSize(22);
        threshTextPaint.setAntiAlias(true);
    }

    public void setThreshold(int t) {
        threshold = t;
        invalidate();
    }

    public void addSample(int meanA, int meanB) {
        samplesA[writeIdx] = meanA;
        samplesB[writeIdx] = meanB;
        writeIdx = (writeIdx + 1) % MAX_SAMPLES;
        if (sampleCount < MAX_SAMPLES) sampleCount++;
        invalidate();
    }

    private int energyToColor(int energy) {
        if (energy == 0) return Color.parseColor("#1a1a2e");
        float ratio = Math.min(1.0f, (float) energy / threshold);
        float hue = (1.0f - ratio) * 200;
        return Color.HSVToColor(255, new float[]{hue, 0.8f, 0.5f + ratio * 0.5f});
    }

    @Override
    protected void onDraw(Canvas canvas) {
        int w = getWidth();
        int h = getHeight();
        int halfH = h / 2;

        canvas.drawColor(Color.parseColor("#0d0d1a"));

        if (sampleCount == 0) {
            textPaint.setColor(Color.parseColor("#666666"));
            canvas.drawText("等待吹气数据...", w / 2f - 80, h / 2f, textPaint);
            return;
        }

        float barWidth = (float) w / MAX_SAMPLES;
        int startIdx = (writeIdx - sampleCount + MAX_SAMPLES) % MAX_SAMPLES;

        for (int i = 0; i < sampleCount; i++) {
            int idx = (startIdx + i) % MAX_SAMPLES;
            float x = i * barWidth;

            paint.setColor(energyToColor(samplesA[idx]));
            float aH = halfH * Math.min(1.0f, (float) samplesA[idx] / MAX_ENERGY);
            canvas.drawRect(x, halfH - aH, x + barWidth + 1, halfH, paint);

            paint.setColor(energyToColor(samplesB[idx]));
            float bH = halfH * Math.min(1.0f, (float) samplesB[idx] / MAX_ENERGY);
            canvas.drawRect(x, halfH, x + barWidth + 1, halfH + bH, paint);
        }

        canvas.drawLine(0, halfH, w, halfH, linePaint);

        float threshRatio = Math.min(1.0f, (float) threshold / MAX_ENERGY);
        float threshYA = halfH - halfH * threshRatio;
        float threshYB = halfH + halfH * threshRatio;
        canvas.drawLine(0, threshYA, w, threshYA, threshPaint);
        canvas.drawLine(0, threshYB, w, threshYB, threshPaint);
        threshTextPaint.setTextAlign(Paint.Align.RIGHT);
        canvas.drawText("阈值" + threshold, w - 6, threshYA - 4, threshTextPaint);
        canvas.drawText("阈值" + threshold, w - 6, threshYB + 18, threshTextPaint);

        textPaint.setColor(Color.parseColor("#AAAAAA"));
        textPaint.setTextSize(24);
        textPaint.setTextAlign(Paint.Align.LEFT);
        canvas.drawText("MIC_A", 8, 28, textPaint);
        canvas.drawText("MIC_B", 8, h - 8, textPaint);
    }
}
