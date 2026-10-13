package com.voice.control;

import android.graphics.Rect;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityWindowInfo;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.List;

public class WechatCallController {
    private static final String TAG = "VoiceA11y";
    private static final String[] VIDEO_CALL_BTN_DESCS = {"翻转", "模糊背景", "摄像头", "挂断", "接听", "扬声器", "麦克风", "切换画面"};

    private static VoiceAccessibilityService service;

    private boolean wechatCallPanelOpen = false;
    private boolean fromContactPage = false;
    private AccessibilityNodeInfo wechatCallNode = null;
    private final List<AccessibilityNodeInfo> wechatCallOptions = new ArrayList<>();
    private int wechatCallSelectedIndex = 0;
    private int callConfirmRetry = 0;
    private boolean wechatInVideoCall = false;
    private long lastIncomingCallDetect = 0;
    private boolean wechatCallBarVisible = false;
    private boolean wechatCallBarManipulating = false;
    private long lastCallBarToggle = 0;
    private long lastCallActionTime = 0;

    static void init(VoiceAccessibilityService s) { service = s; }

    public boolean isPanelOpen() { return wechatCallPanelOpen; }
    public boolean isFromContactPage() { return fromContactPage; }
    public void clearFromContactPage() { fromContactPage = false; }

    public static boolean isWechatCallPanelOpen() {
        return service != null && service.wechatCallController.isPanelOpen();
    }

    public boolean detectAndEnterCallMode() {
        if (wechatCallPanelOpen) return false;
        long now = System.currentTimeMillis();
        if (now - lastCallActionTime < 5000) return false;
        AccessibilityNodeInfo root = service.getRootInActiveWindow();
        if (root == null) return false;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.WECHAT)) return false;
        boolean hasHangup = NodeHelper.hasNodeWithDesc(root, "挂断", 0);
        boolean hasAccept = NodeHelper.hasNodeWithDesc(root, "接听", 0);
        if (hasHangup && hasAccept) {
            Log.i(TAG, "检测到微信来电页面，自动进入通话控制模式");
            Toast.makeText(service, "微信来电，自动切换控制", Toast.LENGTH_LONG).show();
            collectVideoCallButtons(root);
            return true;
        }
        if (hasHangup && !hasAccept) {
            Log.i(TAG, "检测到微信通话中页面，自动进入通话控制模式");
            Toast.makeText(service, "微信通话中，自动切换控制", Toast.LENGTH_LONG).show();
            collectVideoCallButtons(root);
            return true;
        }
        List<AccessibilityNodeInfo> kaiNodes = root.findAccessibilityNodeInfosByViewId("com.tencent.mm:id/kai");
        if (!kaiNodes.isEmpty()) {
            if (NodeHelper.hasNodeWithDesc(root, "播放", 0) || NodeHelper.hasNodeWithDesc(root, "暂停", 0)) {
                Log.i(TAG, "kai节点存在但有播放/暂停按钮，判定为视频号页面而非通话页面");
                return false;
            }
            if (NodeHelper.hasNodeWithDesc(root, "头像", 0) || NodeHelper.hasNodeWithText(root, "直播", 0)) {
                Log.i(TAG, "kai节点存在但有头像/直播，判定为直播页面而非通话页面");
                return false;
            }
            Log.i(TAG, "检测到微信通话页面但控制栏消失，点击屏幕唤出");
            Toast.makeText(service, "唤出控制栏", Toast.LENGTH_SHORT).show();
            GestureHelper.doTap(0.5f, 0.5f);
            lastIncomingCallDetect = System.currentTimeMillis();
            wechatCallPanelOpen = true;
            wechatInVideoCall = true;
            wechatCallBarManipulating = true;
            wechatCallBarVisible = false;
            service.handler.postDelayed(() -> retryWakeAndCollect(0), 1200);
            return true;
        }
        if (NodeHelper.hasNodeWithDesc(root, "邀请你视频通话", 0)) {
            AccessibilityNodeInfo inviteNode = NodeHelper.findNodeWithDesc(root, "邀请你视频通话", 0);
            if (inviteNode != null) {
                Rect r = new Rect();
                inviteNode.getBoundsInScreen(r);
                DisplayMetrics dm = service.getResources().getDisplayMetrics();
                float cx = (r.left + r.right) / 2.0f / dm.widthPixels;
                float cy = (r.top + r.bottom) / 2.0f / dm.heightPixels;
                Log.i(TAG, "检测到微信来电小卡片，点击展开 @(" + r.left + "," + r.top + ")");
                Toast.makeText(service, "微信来电，点击展开", Toast.LENGTH_LONG).show();
                GestureHelper.doTap(cx, cy);
                lastIncomingCallDetect = System.currentTimeMillis();
                return true;
            }
        }
        AccessibilityNodeInfo inviteFromWindows = findIncomingCallCardInWindows();
        if (inviteFromWindows != null) {
            Rect r = new Rect();
            inviteFromWindows.getBoundsInScreen(r);
            DisplayMetrics dm = service.getResources().getDisplayMetrics();
            float cx = (r.left + r.right) / 2.0f / dm.widthPixels;
            float cy = (r.top + r.bottom) / 2.0f / dm.heightPixels;
            Log.i(TAG, "从其他窗口检测到微信来电小卡片，点击展开 @(" + r.left + "," + r.top + ")");
            Toast.makeText(service, "微信来电，点击展开", Toast.LENGTH_LONG).show();
            GestureHelper.doTap(cx, cy);
            lastIncomingCallDetect = System.currentTimeMillis();
            return true;
        }
        return false;
    }

    private AccessibilityNodeInfo findIncomingCallCardInWindows() {
        List<AccessibilityWindowInfo> windows = service.getWindows();
        if (windows == null) return null;
        for (AccessibilityWindowInfo window : windows) {
            AccessibilityNodeInfo root = window.getRoot();
            if (root == null) continue;
            String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
            if (!pkg.equals(AppConstants.WECHAT)) continue;
            AccessibilityNodeInfo inviteNode = NodeHelper.findNodeWithDesc(root, "邀请你视频通话", 0);
            if (inviteNode != null) return inviteNode;
        }
        return null;
    }

    public boolean wechatEnterClick() {
        if (wechatCallPanelOpen) {
            if (wechatInVideoCall) {
                AccessibilityNodeInfo root = service.getRootInActiveWindow();
                if (root == null || !NodeHelper.hasNodeWithDesc(root, "挂断", 0)) {
                    Log.i(TAG, "视频通话控制栏消失，点击屏幕唤出");
                    VoiceAccessibilityService.removeHighlight();
                    GestureHelper.doTap(0.5f, 0.5f);
                    wechatCallBarManipulating = true;
                    wechatCallBarVisible = false;
                    service.handler.postDelayed(() -> retryWakeAndCollect(0), 1200);
                    return true;
                }
            }
            AccessibilityNodeInfo callNode = null;
            String callType = "";
            if (!wechatCallOptions.isEmpty() && wechatCallSelectedIndex < wechatCallOptions.size()) {
                callNode = wechatCallOptions.get(wechatCallSelectedIndex);
                callType = callNode.getContentDescription() != null ? callNode.getContentDescription().toString() : "";
                if (callType.isEmpty()) {
                    for (int i = 0; i < callNode.getChildCount(); i++) {
                        if (callNode.getChild(i).getText() != null) { callType = callNode.getChild(i).getText().toString(); break; }
                    }
                }
            } else {
                AccessibilityNodeInfo root = service.getRootInActiveWindow();
                if (root == null) return false;
                List<AccessibilityNodeInfo> videoNodes = root.findAccessibilityNodeInfosByText("视频通话");
                List<AccessibilityNodeInfo> voiceNodes = root.findAccessibilityNodeInfosByText("语音通话");
                if (!videoNodes.isEmpty()) {
                    callNode = NodeHelper.findClickableParent(videoNodes.get(0));
                    callType = "视频通话";
                } else if (!voiceNodes.isEmpty()) {
                    callNode = NodeHelper.findClickableParent(voiceNodes.get(0));
                    callType = "语音通话";
                }
            }
            if (callNode != null) {
                Rect r = new Rect();
                callNode.getBoundsInScreen(r);
                DisplayMetrics dm = service.getResources().getDisplayMetrics();
                float cx = (r.left + r.right) / 2.0f / dm.widthPixels;
                float cy = (r.top + r.bottom) / 2.0f / dm.heightPixels;
                Log.i(TAG, "点击通话选项: " + callType + " 中心(" + (r.left + r.right) / 2 + "," + (r.top + r.bottom) / 2 + ")");
                GestureHelper.doTap(cx, cy);
                lastIncomingCallDetect = System.currentTimeMillis();
                VoiceAccessibilityService.removeHighlight();
                wechatCallPanelOpen = false;
                wechatInVideoCall = false;
                wechatCallOptions.clear();
                wechatCallNode = null;
                wechatCallBarVisible = false;
                wechatCallBarManipulating = false;
                Toast.makeText(service, "点击" + callType, Toast.LENGTH_SHORT).show();
                service.handler.postDelayed(this::checkAndHighlightCallConfirm, 1000);
                return true;
            }
            Log.i(TAG, "未找到通话选项，重置面板状态");
            wechatCallPanelOpen = false;
            wechatInVideoCall = false;
            wechatCallOptions.clear();
            wechatCallNode = null;
            wechatCallBarVisible = false;
            wechatCallBarManipulating = false;
            return false;
        }
        return false;
    }

    public boolean wechatCallSwitch(boolean isNext) {
        if (!wechatCallPanelOpen) return false;
        if (wechatInVideoCall) {
            AccessibilityNodeInfo root = service.getRootInActiveWindow();
            if (root == null || !NodeHelper.hasNodeWithDesc(root, "挂断", 0)) {
                Log.i(TAG, "视频通话控制栏消失，点击屏幕唤出");
                VoiceAccessibilityService.removeHighlight();
                GestureHelper.doTap(0.5f, 0.5f);
                wechatCallBarManipulating = true;
                wechatCallBarVisible = false;
                service.handler.postDelayed(() -> retryWakeAndCollect(0), 1200);
                return true;
            }
        }
        if (wechatCallOptions.isEmpty()) return false;
        int next = isNext ? wechatCallSelectedIndex + 1 : wechatCallSelectedIndex - 1;
        if (next < 0) next = wechatCallOptions.size() - 1;
        if (next >= wechatCallOptions.size()) next = 0;
        wechatCallSelectedIndex = next;
        highlightCallOption();
        return true;
    }

    private void highlightCallOption() {
        if (wechatCallSelectedIndex < 0 || wechatCallSelectedIndex >= wechatCallOptions.size()) return;
        AccessibilityNodeInfo node = wechatCallOptions.get(wechatCallSelectedIndex);
        Rect r = new Rect();
        node.getBoundsInScreen(r);
        String t = node.getContentDescription() != null ? node.getContentDescription().toString() : "";
        if (t.isEmpty()) {
            for (int i = 0; i < node.getChildCount(); i++) {
                if (node.getChild(i).getText() != null) { t = node.getChild(i).getText().toString(); break; }
            }
        }
        Log.i(TAG, "高亮通话选项[" + (wechatCallSelectedIndex + 1) + "/" + wechatCallOptions.size() + "]: " + t + " @(" + r.left + "," + r.top + ")");
        service.showHighlight(r);
        Toast.makeText(service, t + "，按ENTER确认", Toast.LENGTH_SHORT).show();
    }

    private void checkAndHighlightCallConfirm() {
        AccessibilityNodeInfo root = service.getRootInActiveWindow();
        if (root == null) return;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.WECHAT)) return;
        if (NodeHelper.hasNodeWithDesc(root, "挂断", 0)) {
            if (NodeHelper.hasNodeWithDesc(root, "接听", 0)) {
                if (callConfirmRetry < 10) {
                    callConfirmRetry++;
                    Log.i(TAG, "检测到挂断+接听，仍在等待接听页面，重试" + callConfirmRetry + "/10");
                    service.handler.postDelayed(this::checkAndHighlightCallConfirm, 500);
                } else {
                    Log.i(TAG, "等待接听重试上限，按当前页面收集按钮");
                    callConfirmRetry = 0;
                    collectVideoCallButtons(root);
                }
                return;
            }
            Log.i(TAG, "检测到视频通话页面，收集按钮");
            callConfirmRetry = 0;
            collectVideoCallButtons(root);
            return;
        }
        List<AccessibilityNodeInfo> cancelNodes = root.findAccessibilityNodeInfosByText("取消");
        if (!cancelNodes.isEmpty()) {
            callConfirmRetry = 0;
            wechatInVideoCall = false;
            List<AccessibilityNodeInfo> videoNodes = root.findAccessibilityNodeInfosByText("视频通话");
            List<AccessibilityNodeInfo> voiceNodes = root.findAccessibilityNodeInfosByText("语音通话");
            wechatCallOptions.clear();
            for (AccessibilityNodeInfo vn : videoNodes) {
                AccessibilityNodeInfo cn = NodeHelper.findClickableParent(vn);
                if (cn != null) wechatCallOptions.add(cn);
            }
            for (AccessibilityNodeInfo vn : voiceNodes) {
                AccessibilityNodeInfo cn = NodeHelper.findClickableParent(vn);
                if (cn != null) wechatCallOptions.add(cn);
            }
            if (wechatCallOptions.isEmpty()) {
                Log.i(TAG, "确认面板无通话选项");
                return;
            }
            wechatCallOptions.sort((a, b) -> {
                Rect ra = new Rect(), rb = new Rect();
                a.getBoundsInScreen(ra);
                b.getBoundsInScreen(rb);
                return ra.top - rb.top;
            });
            wechatCallSelectedIndex = 0;
            wechatCallPanelOpen = true;
            highlightCallOption();
            return;
        }
        if (callConfirmRetry < 8) {
            callConfirmRetry++;
            Log.i(TAG, "未检测到面板，重试" + callConfirmRetry + "/8");
            if (callConfirmRetry == 3) {
                Log.i(TAG, "尝试点击屏幕唤出控制栏");
                GestureHelper.doTap(0.5f, 0.5f);
            }
            service.handler.postDelayed(this::checkAndHighlightCallConfirm, 500);
        } else {
            Log.i(TAG, "重试上限，放弃检测");
            clearWechatCallState();
        }
    }

    private void collectVideoCallButtons(AccessibilityNodeInfo root) {
        wechatCallOptions.clear();
        wechatInVideoCall = true;
        collectDescButtons(root, 0);
        if (wechatCallOptions.isEmpty()) {
            Log.i(TAG, "视频通话页面无按钮，清除状态");
            clearWechatCallState();
            return;
        }
        wechatCallOptions.sort((a, b) -> {
            Rect ra = new Rect(), rb = new Rect();
            a.getBoundsInScreen(ra);
            b.getBoundsInScreen(rb);
            if (Math.abs(ra.top - rb.top) > 50) return ra.top - rb.top;
            return ra.left - rb.left;
        });
        wechatCallSelectedIndex = 0;
        wechatCallPanelOpen = true;
        wechatCallBarVisible = true;
        highlightCallOption();
        Log.i(TAG, "视频通话页面收集" + wechatCallOptions.size() + "个按钮");
    }

    private void collectDescButtons(AccessibilityNodeInfo node, int depth) {
        if (node == null || depth > 12) return;
        String desc = node.getContentDescription() != null ? node.getContentDescription().toString() : "";
        if (!desc.isEmpty()) {
            for (String keyword : VIDEO_CALL_BTN_DESCS) {
                if (desc.contains(keyword)) {
                    Rect r = new Rect();
                    node.getBoundsInScreen(r);
                    if (r.width() > 50 && r.height() > 50) {
                        wechatCallOptions.add(AccessibilityNodeInfo.obtain(node));
                    }
                    break;
                }
            }
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            collectDescButtons(node.getChild(i), depth + 1);
        }
    }

    public void clearWechatCallState() {
        VoiceAccessibilityService.removeHighlight();
        wechatCallPanelOpen = false;
        wechatInVideoCall = false;
        wechatCallOptions.clear();
        wechatCallNode = null;
        wechatCallSelectedIndex = 0;
        callConfirmRetry = 0;
        wechatCallBarVisible = false;
        wechatCallBarManipulating = false;
        String screen = detectWechatScreen();
        Log.i(TAG, "清除微信通话状态，当前界面: " + screen);
        Toast.makeText(service, "已退出通话操作", Toast.LENGTH_SHORT).show();
    }

    private void collectAndRestoreIndex(AccessibilityNodeInfo root) {
        int savedIndex = wechatCallSelectedIndex;
        String savedDesc = "";
        if (savedIndex >= 0 && savedIndex < wechatCallOptions.size()) {
            AccessibilityNodeInfo savedNode = wechatCallOptions.get(savedIndex);
            if (savedNode.getContentDescription() != null) {
                savedDesc = savedNode.getContentDescription().toString();
            }
        }
        collectVideoCallButtons(root);
        if (!savedDesc.isEmpty()) {
            for (int i = 0; i < wechatCallOptions.size(); i++) {
                AccessibilityNodeInfo n = wechatCallOptions.get(i);
                String d = n.getContentDescription() != null ? n.getContentDescription().toString() : "";
                if (d.equals(savedDesc)) {
                    wechatCallSelectedIndex = i;
                    highlightCallOption();
                    Log.i(TAG, "恢复选中位置: " + savedDesc + " [" + (i + 1) + "/" + wechatCallOptions.size() + "]");
                    break;
                }
            }
        }
    }

    private void retryWakeAndCollect(int retry) {
        AccessibilityNodeInfo root = service.getRootInActiveWindow();
        if (root != null && NodeHelper.hasNodeWithDesc(root, "挂断", 0)) {
            wechatCallBarManipulating = false;
            wechatCallBarVisible = true;
            collectAndRestoreIndex(root);
            return;
        }
        if (retry < 4) {
            Log.i(TAG, "唤出重试" + (retry + 1) + "/4");
            service.handler.postDelayed(() -> retryWakeAndCollect(retry + 1), 500);
        } else {
            wechatCallBarManipulating = false;
            clearWechatCallState();
        }
    }

    public void autoToggleCallHighlight() {
        if (!wechatInVideoCall || !wechatCallPanelOpen) return;
        if (wechatCallBarManipulating) return;
        long now = System.currentTimeMillis();
        if (now - lastCallBarToggle < 800) return;
        AccessibilityNodeInfo root = service.getRootInActiveWindow();
        if (root == null) return;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.WECHAT)) return;
        boolean hasHangup = NodeHelper.hasNodeWithDesc(root, "挂断", 0);
        if (hasHangup && !wechatCallBarVisible) {
            wechatCallBarVisible = true;
            lastCallBarToggle = now;
            collectAndRestoreIndex(root);
            Log.i(TAG, "控制栏出现，自动显示红框");
        } else if (!hasHangup && wechatCallBarVisible) {
            wechatCallBarVisible = false;
            lastCallBarToggle = now;
            VoiceAccessibilityService.removeHighlight();
            Log.i(TAG, "控制栏消失，自动隐藏红框");
        }
    }

    private String detectWechatScreen() {
        AccessibilityNodeInfo root = service.getRootInActiveWindow();
        if (root == null) return "未知";
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.WECHAT)) return "非微信(" + pkg + ")";
        if (NodeHelper.hasNodeWithDesc(root, "挂断", 0)) return "视频通话页面";
        List<AccessibilityNodeInfo> cancelNodes = root.findAccessibilityNodeInfosByText("取消");
        if (!cancelNodes.isEmpty()) return "确认面板";
        List<AccessibilityNodeInfo> videoNodes = root.findAccessibilityNodeInfosByText("视频通话");
        if (!videoNodes.isEmpty()) return "功能面板";
        List<AccessibilityNodeInfo> bthNodes = root.findAccessibilityNodeInfosByViewId("com.tencent.mm:id/" + WechatIds.chatItemNew());
        if (!bthNodes.isEmpty()) return "聊天列表";
        List<AccessibilityNodeInfo> b3qNodes = root.findAccessibilityNodeInfosByViewId("com.tencent.mm:id/" + WechatIds.moreFuncBtn());
        if (!b3qNodes.isEmpty()) return "聊天页面";
        return "其他微信界面";
    }

    public void detectWechatIncomingCall(AccessibilityEvent event) {
        String pkg = event.getPackageName() != null ? event.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.WECHAT)) return;
        if (wechatCallPanelOpen && wechatInVideoCall) return;
        int type = event.getEventType();
        if (type != AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED &&
            type != AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED) return;
        long now = System.currentTimeMillis();
        if (now - lastIncomingCallDetect < 3000) return;
        if (now - lastCallActionTime < 5000) return;
        lastIncomingCallDetect = now;
        AccessibilityNodeInfo root = service.getRootInActiveWindow();
        if (root == null) return;
        if (NodeHelper.hasNodeWithDesc(root, "挂断", 0) && NodeHelper.hasNodeWithDesc(root, "接听", 0)) {
            Log.i(TAG, "检测到微信来电，自动切换到通话控制模式");
            Toast.makeText(service, "微信来电，自动切换控制", Toast.LENGTH_LONG).show();
            collectVideoCallButtons(root);
            return;
        }
        if (type == AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED) {
            AccessibilityNodeInfo inviteNode = findIncomingCallCardInWindows();
            if (inviteNode != null) {
                Rect r = new Rect();
                inviteNode.getBoundsInScreen(r);
                DisplayMetrics dm = service.getResources().getDisplayMetrics();
                float cx = (r.left + r.right) / 2.0f / dm.widthPixels;
                float cy = (r.top + r.bottom) / 2.0f / dm.heightPixels;
                Log.i(TAG, "事件检测到微信来电小卡片(其他窗口)，点击展开 @(" + r.left + "," + r.top + ")");
                Toast.makeText(service, "微信来电，点击展开", Toast.LENGTH_LONG).show();
                GestureHelper.doTap(cx, cy);
            }
        }
    }

    public boolean wechatCallBack() {
        if (wechatCallPanelOpen) {
            VoiceAccessibilityService.removeHighlight();
            wechatCallPanelOpen = false;
            wechatInVideoCall = false;
            wechatCallOptions.clear();
            wechatCallNode = null;
            wechatCallBarVisible = false;
            wechatCallBarManipulating = false;
            Log.i(TAG, "关闭通话面板");
            service.handler.postDelayed(this::rehighlightWechatCallPanel, 800);
            return true;
        }
        return false;
    }

    private void rehighlightWechatCallPanel() {
        AccessibilityNodeInfo root = service.getRootInActiveWindow();
        if (root == null) return;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.WECHAT)) return;
        if (NodeHelper.hasNodeWithDesc(root, "挂断", 0)) {
            collectVideoCallButtons(root);
            return;
        }
        List<AccessibilityNodeInfo> cancelNodes = root.findAccessibilityNodeInfosByText("取消");
        if (!cancelNodes.isEmpty()) return;
        List<AccessibilityNodeInfo> videoNodes = root.findAccessibilityNodeInfosByText("视频通话");
        if (!videoNodes.isEmpty()) {
            AccessibilityNodeInfo callNode = NodeHelper.findClickableParent(videoNodes.get(0));
            if (callNode != null) {
                Rect r = new Rect();
                callNode.getBoundsInScreen(r);
                Log.i(TAG, "重新高亮w9功能面板视频通话 @(" + r.left + "," + r.top + ")");
                service.showHighlight(r);
                wechatCallPanelOpen = true;
                Toast.makeText(service, "视频通话，按ENTER", Toast.LENGTH_SHORT).show();
            }
        }
    }

    public void autoOpenWechatCallPanel() {
        AccessibilityNodeInfo root = service.getRootInActiveWindow();
        if (root == null) return;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.WECHAT)) return;
        AccessibilityNodeInfo plusBtn = null;
        List<AccessibilityNodeInfo> plusNodes = root.findAccessibilityNodeInfosByViewId("com.tencent.mm:id/" + WechatIds.moreFuncBtn());
        if (!plusNodes.isEmpty()) {
            plusBtn = plusNodes.get(0);
        } else {
            Log.i(TAG, "id查找加号按钮" + WechatIds.moreFuncBtn() + "失败，回退用desc查找");
            AccessibilityNodeInfo descNode = NodeHelper.findNodeWithDesc(root, "更多功能", 0);
            if (descNode != null) {
                plusBtn = NodeHelper.findClickableParent(descNode);
                if (plusBtn != null) {
                    String vid = plusBtn.getViewIdResourceName();
                    if (vid != null && vid.contains(":id/")) {
                        String newId = vid.substring(vid.indexOf(":id/") + 4);
                        if (!newId.equals(WechatIds.moreFuncBtn())) {
                            WechatIds.setMoreFuncBtn(newId);
                            Log.i(TAG, "desc回退找到加号按钮，更新moreFuncBtn: " + newId);
                        }
                    }
                }
            }
        }
        if (plusBtn == null) {
            Log.i(TAG, "未找到微信加号按钮，可能不在聊天页面");
            return;
        }
        Rect r = new Rect();
        plusBtn.getBoundsInScreen(r);
        Log.i(TAG, "找到加号按钮 @(" + r.left + "," + r.top + ") " + r.width() + "x" + r.height() + "，点击");
        plusBtn.performAction(AccessibilityNodeInfo.ACTION_CLICK);
        fromContactPage = true;
        service.handler.postDelayed(this::findAndHighlightCallOption, 1000);
    }

    private void findAndHighlightCallOption() {
        AccessibilityNodeInfo root = service.getRootInActiveWindow();
        if (root == null) return;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.WECHAT)) return;
        List<AccessibilityNodeInfo> videoNodes = root.findAccessibilityNodeInfosByText("视频通话");
        List<AccessibilityNodeInfo> voiceNodes = root.findAccessibilityNodeInfosByText("语音通话");
        AccessibilityNodeInfo callNode = null;
        String callType = "";
        if (!videoNodes.isEmpty()) {
            callNode = NodeHelper.findClickableParent(videoNodes.get(0));
            callType = "视频通话";
        } else if (!voiceNodes.isEmpty()) {
            callNode = NodeHelper.findClickableParent(voiceNodes.get(0));
            callType = "语音通话";
        }
        if (callNode == null) {
            Log.i(TAG, "未找到通话选项");
            return;
        }
        Rect r = new Rect();
        callNode.getBoundsInScreen(r);
        Log.i(TAG, "找到" + callType + "选项 @(" + r.left + "," + r.top + ") " + r.width() + "x" + r.height() + "，高亮");
        service.showHighlight(r);
        wechatCallPanelOpen = true;
        wechatCallNode = AccessibilityNodeInfo.obtain(callNode);
        Toast.makeText(service, callType + "已选中，按ENTER确认", Toast.LENGTH_LONG).show();
    }
}