package com.voice.control;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.Log;
import android.view.View;

/**
 * 双通道压差波形图 —— 完全照搬 EEG WaveformView 的可控项:
 *   setXPoints(时间窗) / setYMode(量程) / setPaused(冻结) / clear(清屏)
 *   + 阈值虚线联动(吹按/吹放/滚动, 随参数移动)
 * S1 红迹上半区, S2 蓝迹下半区, 暗色示波器风。
 */
public class WaveView extends View {

    private static final int N = 360;               /* 最大缓冲 360 点 ≈ 21.6s @60ms */
    private static final long INVALIDATE_MS = 50;

    private final int[] ch1 = new int[N];           /* Pa(有符号) */
    private final int[] ch2 = new int[N];
    private volatile int head = 0;
    private volatile int count = 0;
    private long lastDraw = 0;
    private int drawSeq = 0;

    private final Paint pGrid = new Paint();
    private final Paint pZero = new Paint();
    private final Paint p1 = new Paint();
    private final Paint p2 = new Paint();
    private final Paint pGlow1 = new Paint();
    private final Paint pGlow2 = new Paint();
    private final Paint pLabel = new Paint();
    private final Paint pThOn = new Paint();
    private final Paint pThOff = new Paint();
    private final Paint pThScroll = new Paint();
    private final Paint pThTag = new Paint();
    private final Path path = new Path();

    private int thPressOn = 250, thPressOff = 150;
    private int thScrollOn = 150, thScrollOff = 100;

    /* ==== 可控项(EEG 同款) ==== */
    private int xPoints = 180;                      /* X 时间窗: 90/180/360 点 */
    private int yMode = 0;                          /* 0=自动 1=±500 2=±200 */
    private boolean paused = false;                 /* 冻结显示(数据仍采集) */
    private static final int[] X_CHOICES = {90, 180, 360};
    private static final String[] Y_NAMES = {"Y:自动", "Y:±500", "Y:±200"};

    public WaveView(Context c) {
        super(c);
        pGrid.setColor(0xFF22222E); pGrid.setStrokeWidth(1f);
        pZero.setColor(0xFF3C3C54); pZero.setStrokeWidth(1.5f);
        p1.setColor(0xFFFF5A5A); p1.setStrokeWidth(3.5f);
        p1.setStyle(Paint.Style.STROKE); p1.setAntiAlias(true);
        p1.setStrokeJoin(Paint.Join.ROUND);
        p2.setColor(0xFF53C7FF); p2.setStrokeWidth(3.5f);
        p2.setStyle(Paint.Style.STROKE); p2.setAntiAlias(true);
        p2.setStrokeJoin(Paint.Join.ROUND);
        pGlow1.setColor(0x33FF5A5A); pGlow1.setStrokeWidth(10f);
        pGlow1.setStyle(Paint.Style.STROKE); pGlow1.setAntiAlias(true);
        pGlow2.setColor(0x3353C7FF); pGlow2.setStrokeWidth(10f);
        pGlow2.setStyle(Paint.Style.STROKE); pGlow2.setAntiAlias(true);
        pLabel.setColor(0xFFC9C9D6); pLabel.setTextSize(28f);
        pLabel.setAntiAlias(true);
        pThOn.setColor(0xFFE53935); pThOn.setStrokeWidth(2f);
        pThOn.setPathEffect(new DashPathEffect(new float[]{12, 8}, 0));
        pThOff.setColor(0xFFFB8C00); pThOff.setStrokeWidth(2f);
        pThOff.setPathEffect(new DashPathEffect(new float[]{12, 8}, 0));
        pThScroll.setColor(0xFF43A047); pThScroll.setStrokeWidth(2f);
        pThScroll.setPathEffect(new DashPathEffect(new float[]{12, 8}, 0));
        pThTag.setAntiAlias(true);
        pThTag.setTextSize(22f);
    }

    /* ==== 控制接口(EEG 同款) ==== */
    public void cycleX() {                          /* X 时间窗循环 */
        for (int i = 0; i < X_CHOICES.length; i++) {
            if (X_CHOICES[i] == xPoints) {
                xPoints = X_CHOICES[(i + 1) % X_CHOICES.length];
                break;
            }
        }
        invalidate();
    }

    public void cycleY() { yMode = (yMode + 1) % 3; invalidate(); }
    public void togglePause() { paused = !paused; invalidate(); }
    public void clear() { count = 0; head = 0; invalidate(); }
    public boolean isPaused() { return paused; }
    public String getXLabel() { return xPoints == 90 ? "X:5s" : xPoints == 180 ? "X:10s" : "X:20s"; }
    public String getYLabel() { return Y_NAMES[yMode]; }

    public void setThresholds(int pressOn, int pressOff, int scrollOn, int scrollOff) {
        thPressOn = pressOn; thPressOff = pressOff;
        thScrollOn = scrollOn; thScrollOff = scrollOff;
        invalidate();
    }

    /** 波形数据入口(每 60ms 一帧; 暂停时继续采集, 画面冻结) */
    public void addSample(int s1, int s2) {
        ch1[head] = s1;
        ch2[head] = s2;
        head = (head + 1) % N;
        if (count < N) count++;
        if (paused) return;
        long now = System.currentTimeMillis();
        if (now - lastDraw >= INVALIDATE_MS) {
            lastDraw = now;
            postInvalidateOnAnimation();
        }
    }

    private float halfRange() {
        if (yMode == 1) return 500f;
        if (yMode == 2) return 200f;
        float peak = 100f;                           /* 自动: 追峰值 */
        for (int i = 0; i < count; i++) {
            float a1 = Math.abs(ch1[i]), a2 = Math.abs(ch2[i]);
            if (a1 > peak) peak = a1;
            if (a2 > peak) peak = a2;
        }
        return peak;
    }

    @Override
    protected void onDraw(Canvas cv) {
        super.onDraw(cv);
        if ((drawSeq++ % 10) == 0) {
            Log.d("ESP_UI", "onDraw count=" + count + " head=" + head
                    + " last1=" + ch1[(head - 1 + N) % N]);
        }
        int w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0) return;

        cv.drawColor(0xFF14141E);

        for (int gx = 0; gx <= w; gx += Math.max(1, w / 16)) {
            cv.drawLine(gx, 0, gx, h, pGrid);
        }
        for (int gy = 0; gy <= h; gy += Math.max(1, h / 8)) {
            cv.drawLine(0, gy, w, gy, pGrid);
        }

        float c1 = h * 0.27f, c2 = h * 0.75f;
        float span = h * 0.20f;
        cv.drawLine(0, c1, w, c1, pZero);
        cv.drawLine(0, c2, w, c2, pZero);

        float hr = halfRange();
        float scale = span / hr;

        /* 阈值虚线(双通道, 随参数联动) */
        drawThreshold(cv, w, c1, thPressOn, scale, pThOn, "吹按");
        drawThreshold(cv, w, c1, thPressOff, scale, pThOff, "吹放");
        drawThreshold(cv, w, c1, thScrollOn, scale, pThScroll, "滚动");
        drawThreshold(cv, w, c2, thPressOn, scale, pThOn, null);
        drawThreshold(cv, w, c2, thPressOff, scale, pThOff, null);
        drawThreshold(cv, w, c2, thScrollOn, scale, pThScroll, null);

        /* 迹线: 只画最近 xPoints 点(X 时间窗) */
        int shown = Math.min(count, xPoints);
        if (shown > 1) {
            drawTrace(cv, ch1, c1, -scale, shown, path, pGlow1, p1);
            drawTrace(cv, ch2, c2, -scale, shown, path, pGlow2, p2);
        }

        int cur = (head - 1 + N) % N;
        if (count > 0) {
            pLabel.setColor(0xFFFF8A8A);
            cv.drawText("S1 " + ch1[cur] + " Pa", 16, 36, pLabel);
            pLabel.setColor(0xFF8AD4FF);
            cv.drawText("S2 " + ch2[cur] + " Pa", 16, c2 + h * 0.13f, pLabel);
        }
        pLabel.setColor(0xFF9FA8C0);
        cv.drawText(getXLabel() + "  " + getYLabel() + (paused ? "  [已暂停]" : ""),
                w - 520, 34, pLabel);
    }

    private void drawThreshold(Canvas cv, int w, float center, int value,
                               float scale, Paint p, String tag) {
        float y = center - value * scale;
        if (y < 0 || y > getHeight()) return;
        cv.drawLine(0, y, w, y, p);
        if (tag != null) {
            pThTag.setColor(p.getColor());
            cv.drawText(tag, w - 130, y - 6, pThTag);
        }
    }

    private void drawTrace(Canvas cv, int[] ch, float center, float scale,
                           int shown, Path path, Paint glow, Paint line) {
        path.reset();
        for (int k = 0; k < shown; k++) {
            int idx = (head - shown + k + N) % N;
            float x = getWidth() * k / (float) (xPoints - 1);
            float y = center + ch[idx] * scale;
            if (k == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        cv.drawPath(path, glow);
        cv.drawPath(path, line);
    }
}
