package com.voice.control;

import android.accessibilityservice.AccessibilityService;
import android.accessibilityservice.GestureDescription;
import android.content.Context;

import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.Canvas;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.graphics.Point;

public class GestureHelper {
    private static final String TAG = "VoiceA11y";
    private static VoiceAccessibilityService service;
    private static View clickMarker = null;
    private static View swipeMarker = null;

    static void init(VoiceAccessibilityService s) { service = s; }

    public static void doSwipeDuration(float startX, float startY, float endX, float endY, int durationMs, Runnable onComplete) {
        Point size = new Point();
        ((WindowManager) service.getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay().getRealSize(size);
        int sx = (int)(startX * size.x), sy = (int)(startY * size.y);
        int ex = (int)(endX * size.x), ey = (int)(endY * size.y);
        Path path = new Path();
        path.moveTo(sx, sy);
        path.lineTo(ex, ey);
        Log.i(TAG, "doSwipe(" + durationMs + "ms): (" + sx + "," + sy + ")→(" + ex + "," + ey + ")");
        if (VoiceAccessibilityService.isShowMarkerEnabled()) showSwipeMarker(sx, sy, ex, ey);
        GestureDescription.Builder builder = new GestureDescription.Builder();
        builder.addStroke(new GestureDescription.StrokeDescription(path, 0, durationMs));
        MainActivity.setFloatTouchable(false);
        service.dispatchGesture(builder.build(), new AccessibilityService.GestureResultCallback() {
            @Override
            public void onCompleted(GestureDescription gesture) {
                Log.i(TAG, "手势完成");
                MainActivity.setFloatTouchable(true);
                if (onComplete != null) onComplete.run();
            }
            @Override
            public void onCancelled(GestureDescription gesture) {
                Log.w(TAG, "手势被取消");
                MainActivity.setFloatTouchable(true);
                if (onComplete != null) onComplete.run();
            }
        }, null);
    }

    public static void doSwipe(float startX, float startY, float endX, float endY, Runnable onComplete) {
        Point size = new Point();
        ((WindowManager) service.getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay().getRealSize(size);
        int sx = (int)(startX * size.x), sy = (int)(startY * size.y);
        int ex = (int)(endX * size.x), ey = (int)(endY * size.y);
        Path path = new Path();
        path.moveTo(sx, sy);
        path.lineTo(ex, ey);
        Log.i(TAG, "doSwipe: (" + sx + "," + sy + ")→(" + ex + "," + ey + ") real=" + size.x + "x" + size.y);
        if (VoiceAccessibilityService.isShowMarkerEnabled()) showSwipeMarker(sx, sy, ex, ey);
        GestureDescription.Builder builder = new GestureDescription.Builder();
        builder.addStroke(new GestureDescription.StrokeDescription(path, 0, 500));
        MainActivity.setFloatTouchable(false);
        service.dispatchGesture(builder.build(), new AccessibilityService.GestureResultCallback() {
            @Override
            public void onCompleted(GestureDescription gesture) {
                Log.i(TAG, "手势完成");
                MainActivity.setFloatTouchable(true);
                if (onComplete != null) onComplete.run();
            }
            @Override
            public void onCancelled(GestureDescription gesture) {
                Log.w(TAG, "手势被取消");
                MainActivity.setFloatTouchable(true);
                if (onComplete != null) onComplete.run();
            }
        }, null);
    }

    public static void doTap(float xRatio, float yRatio) {
        doTap(xRatio, yRatio, 50);
    }

    public static void doTap(float xRatio, float yRatio, long durationMs) {
        DisplayMetrics dm = service.getResources().getDisplayMetrics();
        float x = xRatio * dm.widthPixels;
        float y = yRatio * dm.heightPixels;
        if (VoiceAccessibilityService.isShowMarkerEnabled()) showClickMarker((int) x, (int) y);
        Path path = new Path();
        path.moveTo(x, y);
        GestureDescription.Builder builder = new GestureDescription.Builder();
        builder.addStroke(new GestureDescription.StrokeDescription(path, 0, durationMs));
        Log.i(TAG, "doTap: (" + (int)x + "," + (int)y + ") dur=" + durationMs + "ms dm=" + dm.widthPixels + "x" + dm.heightPixels);
        MainActivity.setFloatTouchable(false);
        service.dispatchGesture(builder.build(), new AccessibilityService.GestureResultCallback() {
            @Override
            public void onCompleted(GestureDescription gesture) {
                Log.i(TAG, "doTap完成");
                MainActivity.setFloatTouchable(true);
            }
            @Override
            public void onCancelled(GestureDescription gesture) {
                Log.w(TAG, "doTap被取消");
                MainActivity.setFloatTouchable(true);
            }
        }, null);
    }

    public static void tapScreen(float xRatio, float yRatio) {
        if (service == null) return;
        doTap(xRatio, yRatio);
    }

    public static void swipeDown(boolean large) {
        if (service == null) return;
        float startY = large ? 0.2f : 0.3f;
        float endY = large ? 0.8f : 0.7f;
        doSwipe(0.5f, startY, 0.5f, endY, null);
    }

    public static void swipeUp(boolean large) {
        if (service == null) return;
        float startY = large ? 0.8f : 0.7f;
        float endY = large ? 0.2f : 0.3f;
        doSwipe(0.5f, startY, 0.5f, endY, null);
    }

    public static void tryDoubleTapPlayPause() {
        if (service == null) return;
        Log.i(TAG, "双击播放/暂停");
        doTap(0.5f, 0.5f);
        service.handler.postDelayed(() -> doTap(0.5f, 0.5f), 100);
    }

    private static void showSwipeMarker(int sx, int sy, int ex, int ey) {
        try {
            WindowManager wm = (WindowManager) service.getSystemService(Context.WINDOW_SERVICE);
            if (swipeMarker != null) {
                wm.removeView(swipeMarker);
                swipeMarker = null;
            }
            int left = Math.min(sx, ex) - 30;
            int top = Math.min(sy, ey) - 30;
            int w = Math.abs(ex - sx) + 60;
            int h = Math.abs(ey - sy) + 60;
            View marker = new View(service) {
                @Override
                protected void onDraw(Canvas canvas) {
                    super.onDraw(canvas);
                    Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
                    p.setColor(0xFF00FF00);
                    p.setStyle(Paint.Style.STROKE);
                    p.setStrokeWidth(5);
                    canvas.drawLine(sx - left, sy - top, ex - left, ey - top, p);
                    p.setColor(0xFFFF0000);
                    p.setStyle(Paint.Style.FILL);
                    canvas.drawCircle(sx - left, sy - top, 10, p);
                    p.setColor(0xFF0000FF);
                    canvas.drawCircle(ex - left, ey - top, 10, p);
                }
            };
            WindowManager.LayoutParams mlp = new WindowManager.LayoutParams(
                w, h,
                Build.VERSION.SDK_INT >= 26 ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                                            : WindowManager.LayoutParams.TYPE_PHONE,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);
            mlp.gravity = Gravity.TOP | Gravity.LEFT;
            mlp.x = left;
            mlp.y = top;
            wm.addView(marker, mlp);
            swipeMarker = marker;
            service.handler.postDelayed(() -> {
                if (swipeMarker != null) {
                    try { wm.removeView(swipeMarker); } catch (Exception e) {}
                    swipeMarker = null;
                }
            }, 3000);
        } catch (Exception e) {
            Log.w(TAG, "showSwipeMarker失败: " + e.getMessage());
        }
    }

    private static void showClickMarker(int x, int y) {
        try {
            WindowManager wm = (WindowManager) service.getSystemService(Context.WINDOW_SERVICE);
            if (clickMarker != null) {
                wm.removeView(clickMarker);
                clickMarker = null;
            }
            int size = 120;
            View marker = new View(service) {
                @Override
                protected void onDraw(Canvas canvas) {
                    super.onDraw(canvas);
                    Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
                    p.setColor(0xFFFF0000);
                    p.setStyle(Paint.Style.STROKE);
                    p.setStrokeWidth(6);
                    canvas.drawCircle(size / 2f, size / 2f, size / 2f - 4, p);
                    p.setStrokeWidth(3);
                    canvas.drawLine(size / 2f, 0, size / 2f, size, p);
                    canvas.drawLine(0, size / 2f, size, size / 2f, p);
                }
            };
            WindowManager.LayoutParams mlp = new WindowManager.LayoutParams(
                size, size,
                Build.VERSION.SDK_INT >= 26 ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                                            : WindowManager.LayoutParams.TYPE_PHONE,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);
            mlp.gravity = Gravity.TOP | Gravity.LEFT;
            mlp.x = x - size / 2;
            mlp.y = y - size / 2;
            wm.addView(marker, mlp);
            clickMarker = marker;
            service.handler.postDelayed(() -> {
                if (clickMarker != null) {
                    try { wm.removeView(clickMarker); } catch (Exception e) {}
                    clickMarker = null;
                }
            }, 3000);
        } catch (Exception e) {
            Log.w(TAG, "showClickMarker失败: " + e.getMessage());
        }
    }
}