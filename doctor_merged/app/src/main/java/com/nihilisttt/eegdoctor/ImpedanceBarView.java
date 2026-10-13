package com.nihilisttt.eegdoctor;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.View;

import androidx.core.content.ContextCompat;

public class ImpedanceBarView extends View {

    private static final int NUM_CH = 8;
    private static final String[] CH_NAMES = EegChannels.NAMES;
    private static final float THRESHOLD_OK = 400.0f;
    private static final float THRESHOLD_WARN = 800.0f;

    private final float[] kohm = new float[NUM_CH];
    private boolean hasData = false;

    private Paint bgPaint, barPaint, barWarnPaint, barBadPaint;
    private Paint textPaint, labelPaint, threshPaint;

    public ImpedanceBarView(Context context, AttributeSet attrs) {
        super(context, attrs);
        bgPaint = new Paint(); bgPaint.setColor(ContextCompat.getColor(context, R.color.surface_low)); bgPaint.setStyle(Paint.Style.FILL);
        barPaint = new Paint(); barPaint.setColor(ContextCompat.getColor(context, R.color.accent_success)); barPaint.setStyle(Paint.Style.FILL);
        barWarnPaint = new Paint(); barWarnPaint.setColor(ContextCompat.getColor(context, R.color.accent_warning)); barWarnPaint.setStyle(Paint.Style.FILL);
        barBadPaint = new Paint(); barBadPaint.setColor(ContextCompat.getColor(context, R.color.accent_error)); barPaint.setStyle(Paint.Style.FILL);
        textPaint = new Paint(); textPaint.setColor(ContextCompat.getColor(context, R.color.text_primary)); textPaint.setAntiAlias(true);
        labelPaint = new Paint(); labelPaint.setColor(ContextCompat.getColor(context, R.color.text_secondary)); labelPaint.setAntiAlias(true);
        threshPaint = new Paint(); threshPaint.setColor(ContextCompat.getColor(context, R.color.axis_line)); threshPaint.setStrokeWidth(0.5f);
    }

    public void setImpedance(float[] values) {
        for (int i = 0; i < NUM_CH && i < values.length; i++) kohm[i] = values[i];
        hasData = true;
        postInvalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0) return;
        canvas.drawRect(0, 0, w, h, bgPaint);

        float rowH = h / (float) NUM_CH;
        float labelW = 80f;
        float valueW = 90f;
        float barLeft = labelW;
        float barRight = w - valueW;
        float barW = barRight - barLeft;
        float maxK = 1000.0f;

        labelPaint.setTextSize(Math.min(14f, rowH * 0.35f));
        textPaint.setTextSize(Math.min(13f, rowH * 0.32f));

        float thresh5X = barLeft + (THRESHOLD_OK / maxK) * barW;
        float thresh10X = barLeft + (THRESHOLD_WARN / maxK) * barW;
        canvas.drawLine(thresh5X, 0, thresh5X, h, threshPaint);
        canvas.drawLine(thresh10X, 0, thresh10X, h, threshPaint);

        for (int i = 0; i < NUM_CH; i++) {
            float cy = (i + 0.5f) * rowH;
            int color = ContextCompat.getColor(getContext(),
                    R.color.wave_ch0 + i);
            labelPaint.setColor(color);
            canvas.drawText(CH_NAMES[i], 8f, cy + rowH * 0.12f, labelPaint);

            if (hasData) {
                float v = kohm[i];
                float ratio = Math.min(v / maxK, 1.0f);
                Paint p = v <= THRESHOLD_OK ? barPaint : (v <= THRESHOLD_WARN ? barWarnPaint : barBadPaint);
                canvas.drawRect(barLeft, cy - rowH * 0.3f, barLeft + ratio * barW, cy + rowH * 0.3f, p);

                String s;
                if (v >= 1000f) s = String.format("%.0fMΩ", v / 1000f);
                else if (v >= 1f) s = String.format("%.1fkΩ", v);
                else s = String.format("%.0fΩ", v * 1000f);
                textPaint.setColor(v <= THRESHOLD_OK ? ContextCompat.getColor(getContext(), R.color.accent_success) : (v <= THRESHOLD_WARN ? ContextCompat.getColor(getContext(), R.color.accent_warning) : ContextCompat.getColor(getContext(), R.color.accent_error)));
                canvas.drawText(s, barRight + 6f, cy + rowH * 0.12f, textPaint);
            }
        }
    }
}