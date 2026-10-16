package com.voice.control;

import android.os.Handler;
import android.view.KeyEvent;

public class XimalayaAdapter extends AppAdapter {

    public XimalayaAdapter(MainActivity activity) {
        super(activity, AppConstants.XIMALAYA);
    }

    @Override
    public boolean handleCommand(int cmd) {
        switch (cmd) {
            case MainActivity.CMD_NEXT:
                if (VoiceAccessibilityService.isXimaPlaying()) {
                    sendMediaKey(KeyEvent.KEYCODE_MEDIA_NEXT, "下一首");
                    return true;
                } else if (VoiceAccessibilityService.isXimaDetailPage()) {
                    VoiceAccessibilityService.ximaDetailToggle(true);
                    setStatus("选中立即播放");
                    return true;
                } else {
                    VoiceAccessibilityService.focusNext();
                    setStatus("喜马拉雅下一个");
                    return true;
                }
            case MainActivity.CMD_PREV:
                if (VoiceAccessibilityService.isXimaPlaying()) {
                    sendMediaKey(KeyEvent.KEYCODE_MEDIA_PREVIOUS, "上一首");
                    return true;
                } else if (VoiceAccessibilityService.isXimaDetailPage()) {
                    VoiceAccessibilityService.ximaDetailToggle(false);
                    setStatus("选中下一集播放");
                    return true;
                } else {
                    VoiceAccessibilityService.focusPrev();
                    setStatus("喜马拉雅上一个");
                    return true;
                }
            case MainActivity.CMD_ENTER:
                if (VoiceAccessibilityService.isXimaDetailPage()) {
                    VoiceAccessibilityService.ximaDetailClick();
                    setStatus("详情页确认");
                    return true;
                } else {
                    VoiceAccessibilityService.clickFocused();
                    setStatus("喜马拉雅点击焦点");
                    return true;
                }
            case MainActivity.CMD_BACK:
                VoiceAccessibilityService.removeHighlight();
                VoiceAccessibilityService.performBack();
                setStatus("喜马拉雅内部返回");
                Handler backHandler = activity.getBackHandler();
                backHandler.postDelayed(() -> {
                    String pkg = VoiceAccessibilityService.getForegroundPkg();
                    android.util.Log.i("VoiceCtrl", "返回后包名: " + pkg);
                    if (!pkg.equals(AppConstants.XIMALAYA)) {
                        returnToSelect();
                    }
                }, 500);
                return true;
            default:
                return false;
        }
    }
}