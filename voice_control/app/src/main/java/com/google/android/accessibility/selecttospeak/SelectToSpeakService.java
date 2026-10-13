package com.google.android.accessibility.selecttospeak;

import android.accessibilityservice.AccessibilityService;
import android.util.Log;
import android.view.accessibility.AccessibilityEvent;

public class SelectToSpeakService extends AccessibilityService {
    private static final String TAG = "SelectToSpeak";
    private static SelectToSpeakService instance;

    @Override
    public void onServiceConnected() {
        instance = this;
        Log.i(TAG, "SelectToSpeak服务已连接");
    }

    @Override
    public void onAccessibilityEvent(AccessibilityEvent event) {
    }

    @Override
    public void onInterrupt() {
    }

    @Override
    public void onDestroy() {
        instance = null;
        super.onDestroy();
    }

    public static boolean isAvailable() {
        return instance != null;
    }
}