package com.voice.control;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;

public class CalibrationLineView extends View {
    private final Paint linePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint bgPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final String label;
    private final WindowManager wm;
    private final WindowManager.LayoutParams lp;
    private final int maxScreenH;
    private int y;

    public CalibrationLineView(Context ctx, String label, int color,
            WindowManager wm, WindowManager.LayoutParams lp, int initialY, int screenH) {
        super(ctx);
        this.label = label;
        this.wm = wm;
        this.lp = lp;
        this.maxScreenH = screenH;
        linePaint.setColor(color);
        linePaint.setStyle(Paint.Style.FILL);
        bgPaint.setColor(0x44000000);
        textPaint.setColor(Color.WHITE);
        textPaint.setTextSize(30);
        textPaint.setAntiAlias(true);
        if (initialY < 0) initialY = 0;
        int maxY = screenH - lp.height;
        if (initialY > maxY) initialY = maxY;
        this.y = initialY;
        lp.y = initialY;
    }

    public int getLineY() { return y; }

    public void setLineY(int newY) {
        if (newY < 0) newY = 0;
        int maxY = maxScreenH - lp.height;
        if (newY > maxY) newY = maxY;
        y = newY;
        lp.y = newY;
        wm.updateViewLayout(this, lp);
        invalidate();
    }

    private float downRawY;
    private int downLpY;

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        switch (event.getAction()) {
            case MotionEvent.ACTION_DOWN:
                downRawY = event.getRawY();
                downLpY = lp.y;
                return true;
            case MotionEvent.ACTION_MOVE:
                float dy = event.getRawY() - downRawY;
                setLineY(downLpY + (int) dy);
                return true;
        }
        return super.onTouchEvent(event);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int h = getHeight();
        canvas.drawRect(0, 0, getWidth(), h, bgPaint);
        canvas.drawRect(0, h / 2 - 5, getWidth(), h / 2 + 5, linePaint);
        canvas.drawText(label + "  y=" + y, 24, h / 2 - 12, textPaint);
    }
}