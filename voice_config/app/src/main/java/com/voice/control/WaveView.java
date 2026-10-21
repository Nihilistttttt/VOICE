package com.voice.control;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.Path;
import android.view.View;

/**
 * 双通道压差波形图 —— EEG WaveformView 同款风格:
 *   左侧数值标签区 + 底部时间标签区 + 虚线网格(X 轴 niceNum 时间刻度)
 *   Y 固定 ±500Pa: 每 10Pa 一条刻度线(细), 数值标签按像素密度自适应步长
 *   S1(红)/S2(蓝) 双迹线叠加于同一坐标系, glow+line 双层绘制
 *   X 时间窗 1~120s 可输入调节, 阈值虚线(吹按/吹放/滚动)随参数联动
 */
public class WaveView extends View {

    private static final int MAX_POINTS = 2400;      /* 50ms/帧(20Hz) → 120s 满窗 */
    private static final long INVALIDATE_MS = 33;    /* EEG 同款刷新节流 */
    private static final float FRAME_MS = 50f;       /* 波形帧周期 */
    /* ---- Y 轴可配置(EEG 同款): 半量程 = 步长 × (标签数-1) / 2, 上下对称 ---- */
    private volatile float mYMax = 500f;             /* Y 半量程 Pa(setYAxis 算出) */
    private volatile float mStepPa = 100f;           /* Y 每格步长 Pa(标签线间隔) */
    private volatile int   mLabelCount = 11;         /* 标签条数(含 0 与 ±端点) */

    private final int[] ch1 = new int[MAX_POINTS];   /* Pa(有符号) */
    private final int[] ch2 = new int[MAX_POINTS];
    private volatile int head = 0;
    private volatile int count = 0;
    private long lastDraw = 0;

    private final Paint pGridFine = new Paint();     /* 10Pa 细刻度线 */
    private final Paint pGridCoarse = new Paint();   /* 标签级虚线网格 */
    private final Paint pZero = new Paint();         /* 0Pa 基线 */
    private final Paint pAxis = new Paint();         /* 轴线/刻度短线 */
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

    private float xSeconds = 10f;                    /* X 时间窗(秒) */
    private boolean paused = false;                  /* 冻结显示(数据仍采集) */

    private final float density;
    private final float scaledDensity;

    public WaveView(Context c) {
        super(c);
        density = getResources().getDisplayMetrics().density;
        scaledDensity = getResources().getDisplayMetrics().scaledDensity;

        pGridFine.setColor(0xFFE2E5EA);   /* 浅色细网格 */
        pGridFine.setStrokeWidth(1f);
        pGridCoarse.setColor(0xFFC9CED6);  /* 浅色粗格虚线 */
        pGridCoarse.setStrokeWidth(1f);
        pGridCoarse.setPathEffect(new DashPathEffect(
                new float[]{4f * density, 4f * density}, 0));
        pZero.setColor(0xFF8A93A0);       /* 浅色 0 基线 */
        pZero.setStrokeWidth(1.5f);
        pAxis.setColor(0xFF6B7480);        /* 浅色轴线 */
        pAxis.setStrokeWidth(1f);

        p1.setColor(0xFFFF5A5A);
        p1.setStrokeWidth(2.5f);
        p1.setStyle(Paint.Style.STROKE);
        p1.setAntiAlias(true);
        p1.setStrokeJoin(Paint.Join.ROUND);
        p2.setColor(0xFF1976D2);   /* 浅色底: 加深蓝提高对比 */
        p2.setStrokeWidth(2.5f);
        p2.setStyle(Paint.Style.STROKE);
        p2.setAntiAlias(true);
        p2.setStrokeJoin(Paint.Join.ROUND);
        pGlow1.setColor(0x33FF5A5A);
        pGlow1.setStrokeWidth(8f);
        pGlow1.setStyle(Paint.Style.STROKE);
        pGlow1.setAntiAlias(true);
        pGlow2.setColor(0x221976D2);
        pGlow2.setStrokeWidth(8f);
        pGlow2.setStyle(Paint.Style.STROKE);
        pGlow2.setAntiAlias(true);

        pLabel.setColor(0xFF37474F);   /* 浅色底: 深灰文字 */
        pLabel.setTextSize(11f * scaledDensity);
        pLabel.setAntiAlias(true);

        pThOn.setColor(0xFFE53935);
        pThOn.setStrokeWidth(2f);
        pThOn.setPathEffect(new DashPathEffect(new float[]{12, 8}, 0));
        pThOff.setColor(0xFFFB8C00);
        pThOff.setStrokeWidth(2f);
        pThOff.setPathEffect(new DashPathEffect(new float[]{12, 8}, 0));
        pThScroll.setColor(0xFF43A047);
        pThScroll.setStrokeWidth(2f);
        pThScroll.setPathEffect(new DashPathEffect(new float[]{12, 8}, 0));
        pThTag.setAntiAlias(true);
        pThTag.setTextSize(10f * scaledDensity);
    }

    /* ==== 控制接口 ==== */

    /** 设置 X 时间窗(秒), 钳位 1~120 */
    /** Y 轴配置(EEG 同款): 步长=每格 Pa, 标签数=总条数(含 0 与 ±端点)。
     *  半量程 = 步长×(标签数-1)/2, 上下对称; 修改后自动重绘。 */
    public void setYAxis(float stepPa, int labelCount) {
        if (stepPa <= 0 || labelCount < 3 || labelCount > 41) return;
        mStepPa = stepPa;
        mLabelCount = labelCount;
        mYMax = stepPa * (labelCount - 1) / 2f;
        postInvalidate();
    }

    public void setXSeconds(float s) {
        if (s < 1f) s = 1f;
        if (s > 120f) s = 120f;
        xSeconds = s;
        invalidate();
    }

    public float getXSeconds() { return xSeconds; }

    public void togglePause() { paused = !paused; invalidate(); }

    public boolean isPaused() { return paused; }

    public void clear() { count = 0; head = 0; invalidate(); }

    public void setThresholds(int pressOn, int pressOff, int scrollOn, int scrollOff) {
        thPressOn = pressOn; thPressOff = pressOff;
        thScrollOn = scrollOn; thScrollOff = scrollOff;
        invalidate();
    }

    /** 波形数据入口(每 50ms 一帧; 暂停时继续采集, 画面冻结) */
    public void addSample(int s1, int s2) {
        ch1[head] = s1;
        ch2[head] = s2;
        head = (head + 1) % MAX_POINTS;
        if (count < MAX_POINTS) count++;
        if (paused) return;
        long now = System.currentTimeMillis();
        if (now - lastDraw >= INVALIDATE_MS) {
            lastDraw = now;
            postInvalidateOnAnimation();
        }
    }

    /** EEG niceNum: 取 1/2/5×10^n 的"好看"刻度步长 */
    private static float niceNum(float range) {
        float exp = (float) Math.floor(Math.log10(range));
        float f = range / (float) Math.pow(10, exp);
        float nice = (f < 1.5f) ? 1f : (f < 3f) ? 2f : (f < 7f) ? 5f : 10f;
        return nice * (float) Math.pow(10, exp);
    }

    @Override
    protected void onDraw(Canvas cv) {
        super.onDraw(cv);
        int w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0) return;

        cv.drawColor(0xFFF5F6F8);        /* 浅色底(用户要求, 非深色) */

        /* EEG 布局: 左侧数值标签区 + 底部时间标签区 */
        final float left = 26f * density;
        final float top = 5f * density;
        final float right = w - 5f * density;
        final float bottom = h - 24f * density;

        final float pxPerPa = (bottom - top) / (2f * mYMax);
        final float cy = bottom - mYMax * pxPerPa;      /* 0Pa 基线位置 */

        /* --- Y 轴: 细格线(步长/5) + 粗格标签线(每步长一条, 标签=步长倍数)---
         * 标签语义与 EEG WaveformView 一致: ±端点与 0 处标数值 */
        int fineN = Math.round(2f * mYMax / (mStepPa / 5f));
        for (int i = 1; i < fineN; i++) {
            float pa = -mYMax + i * (mStepPa / 5f);
            float y = bottom - (pa + mYMax) * pxPerPa;
            cv.drawLine(left, y, right, y, pGridFine);
        }
        for (int i = 0; i < mLabelCount; i++) {
            float pa = -mYMax + i * mStepPa;
            float y = bottom - (pa + mYMax) * pxPerPa;
            cv.drawLine(left, y, right, y, pGridCoarse);
            if (i > 0 && i < mLabelCount - 1) {
                cv.drawLine(left - 5f * density, y, left, y, pAxis);
            }
            String lab = (pa == Math.floor(pa))
                    ? String.valueOf((int) pa) : String.valueOf(pa);
            float tw = pLabel.measureText(lab);
            cv.drawText(lab, left - 7f * density - tw, y + 4f * density, pLabel);
        }
        cv.drawLine(left, cy, right, cy, pZero);         /* 0Pa 基线 */
        String zero = "0";
        cv.drawText(zero, left - 7f * density - pLabel.measureText(zero),
                cy + 4f * density, pLabel);
        cv.drawLine(left, top, left, bottom, pAxis);     /* 左轴线 */

        /* --- X 轴: EEG niceNum 时间刻度 --- */
        float xTick = niceNum(xSeconds / 5f);
        int maxK = Math.round(xSeconds / xTick);
        for (int k = 0; k <= maxK; k++) {
            float t = k * xTick;
            float x = left + (t / xSeconds) * (right - left);
            if (k > 0) cv.drawLine(x, top, x, bottom, pGridCoarse);
            cv.drawLine(x, bottom, x, bottom + 5f * density, pAxis);
            String lab = (xTick % 1f == 0f)
                    ? String.valueOf((long) t) + "s"
                    : String.valueOf(t) + "s";
            cv.drawText(lab, x - pLabel.measureText(lab) / 2f,
                    bottom + 18f * density, pLabel);
        }

        /* --- 阈值虚线(统一坐标系, 右侧标签) --- */
        drawThreshold(cv, left, right, cy, pxPerPa, thPressOn, pThOn, "吹按");
        drawThreshold(cv, left, right, cy, pxPerPa, thPressOff, pThOff, "吹放");
        drawThreshold(cv, left, right, cy, pxPerPa, thScrollOn, pThScroll, "滚动");

        /* --- 迹线: S1 红 + S2 蓝 叠加于同一坐标系 --- */
        int totalPts = Math.max(2, Math.round(xSeconds * 1000f / FRAME_MS));
        int shown = Math.min(count, totalPts);
        if (shown > 1) {
            drawTrace(cv, ch1, left, right, cy, pxPerPa, shown, totalPts, pGlow1, p1);
            drawTrace(cv, ch2, left, right, cy, pxPerPa, shown, totalPts, pGlow2, p2);
        }

        /* --- 上下限标签(EEG 左上/左下角) --- */
        cv.drawText("+" + (int) mYMax + " Pa", left + 6f * density,
                top + 13f * scaledDensity, pLabel);
        cv.drawText("-" + (int) mYMax + " Pa", left + 6f * density,
                bottom - 5f * density, pLabel);

        if (paused) {
            String p = "[已暂停]";
            cv.drawText(p, right - pLabel.measureText(p) - 6f * density,
                    top + 13f * scaledDensity, pLabel);
        }
    }

    private void drawThreshold(Canvas cv, float left, float right, float cy,
                               float pxPerPa, int value, Paint p, String tag) {
        float y = cy - value * pxPerPa;
        if (y < 0 || y > getHeight()) return;
        cv.drawLine(left, y, right, y, p);
        if (tag != null) {
            pThTag.setColor(p.getColor());
            cv.drawText(tag, right - pThTag.measureText(tag) - 6f * density,
                    y - 5f * density, pThTag);
        }
    }

    private void drawTrace(Canvas cv, int[] ch, float left, float right, float cy,
                           float pxPerPa, int shown, int totalPts,
                           Paint glow, Paint line) {
        path.reset();
        for (int k = 0; k < shown; k++) {
            int idx = (head - shown + k + MAX_POINTS) % MAX_POINTS;
            float x = left + (right - left) * k / (totalPts - 1);
            float y = cy - ch[idx] * pxPerPa;
            if (k == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        cv.drawPath(path, glow);
        cv.drawPath(path, line);
    }
}
