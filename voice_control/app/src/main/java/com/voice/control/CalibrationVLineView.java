package com.voice.control;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;

public class CalibrationVLineView extends View {
    private final Paint linePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint bgPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final String label;
    private final WindowManager wm;
    private final WindowManager.LayoutParams lp;
    private final int maxScreenW;
    private int x;

    public CalibrationVLineView(Context ctx, String label, int color,
            WindowManager wm, WindowManager.LayoutParams lp, int initialX, int screenW) {
        super(ctx);
        this.label = label;
        this.wm = wm;
        this.lp = lp;
        this.maxScreenW = screenW;
        linePaint.setColor(color);
        linePaint.setStyle(Paint.Style.FILL);
        bgPaint.setColor(0x44000000);
        textPaint.setColor(Color.WHITE);
        textPaint.setTextSize(26);
        textPaint.setAntiAlias(true);
        if (initialX < 0) initialX = 0;
        int maxX = screenW - lp.width;
        if (initialX > maxX) initialX = maxX;
        this.x = initialX;
        lp.x = initialX;
    }

    public int getLineX() { return x; }

    public void setLineX(int newX) {
        if (newX < 0) newX = 0;
        int maxX = maxScreenW - lp.width;
        if (newX > maxX) newX = maxX;
        x = newX;
        lp.x = newX;
        wm.updateViewLayout(this, lp);
        invalidate();
    }

    private float downRawX;
    private int downLpX;

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        switch (event.getAction()) {
            case MotionEvent.ACTION_DOWN:
                downRawX = event.getRawX();
                downLpX = lp.x;
                return true;
            case MotionEvent.ACTION_MOVE:
                float dx = event.getRawX() - downRawX;
                setLineX(downLpX + (int) dx);
                return true;
        }
        return super.onTouchEvent(event);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int w = getWidth();
        canvas.drawRect(0, 0, w, getHeight(), bgPaint);
        canvas.drawRect(w / 2 - 5, 0, w / 2 + 5, getHeight(), linePaint);
        canvas.save();
        canvas.rotate(90, w / 2, 20);
        canvas.drawText(label + " x=" + x, w / 2 - 40, 20, textPaint);
        canvas.restore();
    }
}