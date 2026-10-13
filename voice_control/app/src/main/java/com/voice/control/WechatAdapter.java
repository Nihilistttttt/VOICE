package com.voice.control;

import android.os.Handler;

public class WechatAdapter extends AppAdapter {

    public WechatAdapter(MainActivity activity) {
        super(activity, AppConstants.WECHAT);
    }

    @Override
    public boolean handleCommand(int cmd) {
        switch (cmd) {
            case MainActivity.CMD_PLAY:
                if (VoiceAccessibilityService.isWechatVideoFeed()) {
                    GestureHelper.doTap(0.5f, 0.5f);
                    setStatus("视频号播放/暂停");
                    return true;
                }
                return false;
            case MainActivity.CMD_MODE:
                VoiceAccessibilityService.toggleWechatNavMode();
                setStatus(VoiceAccessibilityService.isWechatNavMode() ? "微信导航栏选择" : "微信页内选择");
                return true;
            case MainActivity.CMD_NEXT:
                if (VoiceAccessibilityService.isWechatVideoFeed()) {
                    VoiceAccessibilityService.wechatVideoFeedSwipe(true);
                    setStatus("视频号下一个");
                    return true;
                }
                if (!VoiceAccessibilityService.wechatCallSwitch(true)) {
                    VoiceAccessibilityService.focusNext();
                    setStatus("微信下一个");
                } else {
                    setStatus("切换通话选项");
                }
                return true;
            case MainActivity.CMD_PREV:
                if (VoiceAccessibilityService.isWechatVideoFeed()) {
                    VoiceAccessibilityService.wechatVideoFeedSwipe(false);
                    setStatus("视频号上一个");
                    return true;
                }
                if (!VoiceAccessibilityService.wechatCallSwitch(false)) {
                    VoiceAccessibilityService.focusPrev();
                    setStatus("微信上一个");
                } else {
                    setStatus("切换通话选项");
                }
                return true;
            case MainActivity.CMD_ENTER:
                if (!VoiceAccessibilityService.wechatEnterClick()) {
                    boolean wasNavMode = VoiceAccessibilityService.isWechatNavMode();
                    VoiceAccessibilityService.clickFocused();
                    if (wasNavMode) {
                        setStatus("切换页面，自动进入页内选择");
                        activity.getHandler().postDelayed(() -> {
                            if (VoiceAccessibilityService.isWechatNavMode()) {
                                VoiceAccessibilityService.toggleWechatNavMode();
                            }
                        }, 600);
                    } else {
                        setStatus("微信点击");
                    }
                } else {
                    setStatus("发起通话");
                }
                return true;
            case MainActivity.CMD_BACK:
                boolean panelWasOpen = VoiceAccessibilityService.wechatCallBack();
                VoiceAccessibilityService.performBack();
                if (panelWasOpen) {
                    setStatus("关闭通话面板");
                } else if (VoiceAccessibilityService.isWechatFromContact()) {
                    VoiceAccessibilityService.clearWechatFromContact();
                    VoiceAccessibilityService.removeHighlight();
                    setStatus("从联系人进入，连返两级");
                    Handler backHandler = activity.getBackHandler();
                    backHandler.postDelayed(() -> {
                        if (VoiceAccessibilityService.isWechatContactDetailPage()) {
                            VoiceAccessibilityService.performBack();
                            backHandler.postDelayed(() -> {
                                String pkg = VoiceAccessibilityService.getForegroundPkg();
                                android.util.Log.i("VoiceCtrl", "连返后包名: " + pkg);
                                if (!pkg.equals(AppConstants.WECHAT)) {
                                    returnToSelect();
                                } else {
                                    backHandler.postDelayed(() -> {
                                        VoiceAccessibilityService.refocusCurrent();
                                    }, 500);
                                }
                            }, 500);
                        } else {
                            android.util.Log.i("VoiceCtrl", "第二次返回前：不在联系人详情页，不再返回");
                            String pkg = VoiceAccessibilityService.getForegroundPkg();
                            if (!pkg.equals(AppConstants.WECHAT)) {
                                returnToSelect();
                            } else {
                                backHandler.postDelayed(() -> {
                                    VoiceAccessibilityService.refocusCurrent();
                                }, 500);
                            }
                        }
                    }, 500);
                } else {
                    VoiceAccessibilityService.removeHighlight();
                    setStatus("微信内部返回");
                    Handler backHandler = activity.getBackHandler();
                    backHandler.postDelayed(() -> {
                        String pkg = VoiceAccessibilityService.getForegroundPkg();
                        android.util.Log.i("VoiceCtrl", "返回后包名: " + pkg);
                        if (!pkg.equals(AppConstants.WECHAT)) {
                            returnToSelect();
                        } else {
                            backHandler.postDelayed(() -> {
                                VoiceAccessibilityService.refocusCurrent();
                            }, 500);
                        }
                    }, 500);
                }
                return true;
            default:
                return false;
        }
    }
}