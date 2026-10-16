package com.voice.control;

import android.os.Handler;
import android.view.KeyEvent;

public class BilibiliAdapter extends AppAdapter {

    public BilibiliAdapter(MainActivity activity) {
        super(activity, AppConstants.BILIBILI);
    }

    @Override
    public boolean handleCommand(int cmd) {
        switch (cmd) {
            case MainActivity.CMD_PLAY:
                if (VoiceAccessibilityService.isBiliFullscreen()) {
                    VoiceAccessibilityService.tryDoubleTapPlayPause();
                    setStatus("双击播放/暂停");
                    return true;
                }
                return false;
            case MainActivity.CMD_NEXT:
                if (VoiceAccessibilityService.isBiliLiveRoom()) {
                    VoiceAccessibilityService.tryLiveToggleDanmaku();
                    setStatus("直播开关弹幕");
                    return true;
                } else if (VoiceAccessibilityService.isBiliFullscreen()) {
                    VoiceAccessibilityService.tapToShowControlBar();
                    Handler handler = activity.getHandler();
                    handler.postDelayed(() -> sendMediaKey(KeyEvent.KEYCODE_MEDIA_NEXT, "下一个视频"), 500);
                    setStatus("全屏下一个视频");
                    return true;
                } else if (VoiceAccessibilityService.isBiliVerticalVideo()) {
                    VoiceAccessibilityService.swipeUp(true);
                    setStatus("竖屏视频下一个");
                    return true;
                } else {
                    VoiceAccessibilityService.focusNext();
                    setStatus("B站焦点下一个");
                    return true;
                }
            case MainActivity.CMD_PREV:
                if (VoiceAccessibilityService.isBiliLiveRoom()) {
                    VoiceAccessibilityService.tryLiveFullscreen();
                    setStatus("直播全屏切换");
                    return true;
                } else if (VoiceAccessibilityService.isBiliFullscreen()) {
                    VoiceAccessibilityService.performBack();
                    setStatus("退出全屏");
                    return true;
                } else if (VoiceAccessibilityService.isBiliVerticalVideo()) {
                    VoiceAccessibilityService.swipeDown(true);
                    setStatus("竖屏视频上一个");
                    return true;
                } else if (VoiceAccessibilityService.tryToggleFullscreen()) {
                    setStatus("进入全屏");
                    return true;
                } else {
                    VoiceAccessibilityService.focusPrev();
                    setStatus("B站焦点上一个");
                    return true;
                }
            case MainActivity.CMD_ENTER:
                VoiceAccessibilityService.clickFocused();
                setStatus("B站点击焦点");
                return true;
            case MainActivity.CMD_BACK:
                VoiceAccessibilityService.removeHighlight();
                VoiceAccessibilityService.performBack();
                setStatus("B站内部返回");
                Handler backHandler = activity.getBackHandler();
                backHandler.postDelayed(() -> {
                    String pkg = VoiceAccessibilityService.getForegroundPkg();
                    android.util.Log.i("VoiceCtrl", "返回后包名: " + pkg);
                    if (!pkg.equals(AppConstants.BILIBILI)) {
                        returnToSelect();
                    } else {
                        backHandler.postDelayed(() -> {
                            VoiceAccessibilityService.refocusCurrent();
                        }, 500);
                    }
                }, 500);
                return true;
            default:
                return false;
        }
    }
}