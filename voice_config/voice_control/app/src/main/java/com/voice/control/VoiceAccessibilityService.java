package com.voice.control;

import android.accessibilityservice.AccessibilityService;
import android.accessibilityservice.GestureDescription;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.Point;
import android.graphics.Rect;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Handler;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityWindowInfo;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.List;

public class VoiceAccessibilityService extends AccessibilityService {

    private static final String TAG = "VoiceA11y";
    private static final String PREFS = "play_btn";
    private static boolean showMarkerEnabled = false;

    public static boolean isShowMarkerEnabled() { return showMarkerEnabled; }
    public static void toggleShowMarker() { showMarkerEnabled = !showMarkerEnabled; }
    private static VoiceAccessibilityService instance;

    private static boolean wechatNavMode = false;
    private static int wechatLastNavIndex = 0;
    private boolean wechatGenericPage = false;
    private boolean wechatContactNameMatched = false;
    WechatCallController wechatCallController;
    public static boolean isWechatNavMode() { return wechatNavMode; }
    public static void toggleWechatNavMode() {
        wechatNavMode = !wechatNavMode;
        if (instance != null) {
            instance.removeHighlight();
            if (wechatNavMode) {
                instance.focusIndex = wechatLastNavIndex;
                instance.currentFocusDesc = "";
                instance.refreshFocusableNodes();
                if (!instance.focusableNodes.isEmpty()) {
                    if (instance.focusIndex >= instance.focusableNodes.size()) {
                        instance.focusIndex = 0;
                    }
                    instance.currentFocusDesc = instance.getNodeInfo(instance.focusableNodes.get(instance.focusIndex));
                    instance.focusCurrent();
                }
            } else {
                wechatLastNavIndex = instance.focusIndex;
                instance.focusIndex = 0;
                instance.currentFocusDesc = "";
                instance.refreshFocusableNodes();
                instance.focusClosestToCenter();
            }
        }
        Log.i(TAG, "微信层切换: wechatNavMode=" + wechatNavMode);
    }
    public static void resetWechatNavMode() {
        if (!wechatNavMode) return;
        wechatNavMode = false;
        if (instance != null) {
            instance.removeHighlight();
            instance.focusIndex = 0;
            instance.currentFocusDesc = "";
        }
        Log.i(TAG, "微信导航模式已重置");
    }

    final Handler handler = new Handler();
    private View highlightView = null;
    private WindowManager wm = null;
    private boolean learning = false;
    private String learnPkg = "";
    private int retryCount = 0;
    private String targetPkg = "";
    private boolean cancelled = false;

    public static class EventRecord {
        public String pkg;
        public String text;
        public String desc;
        public String viewId;
        public long timestamp;
        public int eventType;
    }

    private final List<EventRecord> learningEvents = new ArrayList<>();
    private boolean learningWindowActive = false;
    private static List<EventRecord> sCollectedEvents = null;
    private static String sLearnedPkg = "";
    private static int sLearnMode = 0;
    private int learnMode = 0;

    public static boolean isAvailable() {
        return instance != null;
    }

    @Override
    public void onServiceConnected() {
        instance = this;
        GestureHelper.init(this);
        WechatIds.init(this);
        wechatCallController = new WechatCallController();
        WechatCallController.init(this);
        Log.i(TAG, "无障碍服务已连接 " + WechatIds.getAllIds());
    }

    @Override
    public void onAccessibilityEvent(AccessibilityEvent event) {
        if (event == null) return;
        escapeWechatMiniPanel();
        wechatCallController.detectWechatIncomingCall(event);
        wechatCallController.autoToggleCallHighlight();
        if (event.getEventType() == AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED) {
            handler.postDelayed(() -> autoEnterWechatChatFromContact(), 500);
        }
        if (event.getEventType() == AccessibilityEvent.TYPE_VIEW_SCROLLED) {
            lastScrollEventTime = System.currentTimeMillis();
        }
        if (!learning) return;
        String pkg = event.getPackageName() != null ? event.getPackageName().toString() : "";
        String myPkg = getPackageName();
        if (pkg.equals(myPkg)) return;
        int type = event.getEventType();

        AccessibilityNodeInfo node = event.getSource();
        if (node == null) node = getRootInActiveWindow();
        if (node == null) return;

        String text = node.getText() != null ? node.getText().toString() : "";
        String desc = node.getContentDescription() != null ? node.getContentDescription().toString() : "";
        String viewId = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";

        EventRecord rec = new EventRecord();
        rec.pkg = pkg;
        rec.text = text;
        rec.desc = desc;
        rec.viewId = viewId;
        rec.timestamp = System.currentTimeMillis();
        rec.eventType = type;
        learningEvents.add(rec);
        Log.i(TAG, "学习收集[" + learningEvents.size() + "]: type=" + type + " pkg=" + pkg + " text=[" + text + "] desc=[" + desc + "] id=[" + viewId + "]");

        if (!learningWindowActive) {
            learningWindowActive = true;
            Toast.makeText(this, "开始收集，10秒后选择", Toast.LENGTH_LONG).show();
            handler.postDelayed(() -> {
                if (!learningWindowActive) return;
                learning = false;
                learningWindowActive = false;
                sCollectedEvents = new ArrayList<>(learningEvents);
                sLearnedPkg = learnPkg;
                sLearnMode = learnMode;
                learningEvents.clear();
                Toast.makeText(this, "收集完成", Toast.LENGTH_LONG).show();
                backToVoiceControl(0);
            }, 10000);
        }
    }

    private void backToVoiceControl(int depth) {
        if (depth >= 5) return;
        performGlobalAction(GLOBAL_ACTION_BACK);
        handler.postDelayed(() -> {
            AccessibilityNodeInfo root = getRootInActiveWindow();
            String curPkg = root != null && root.getPackageName() != null ? root.getPackageName().toString() : "";
            if (!curPkg.equals(getPackageName())) {
                backToVoiceControl(depth + 1);
            }
        }, 400);
    }

    public static List<EventRecord> consumeLearningEvents() {
        if (sCollectedEvents == null) return null;
        List<EventRecord> events = sCollectedEvents;
        sCollectedEvents = null;
        return events;
    }

    public static String getLearnedPkg() {
        return sLearnedPkg;
    }

    public static int consumeLearnMode() {
        int m = sLearnMode;
        sLearnMode = 0;
        return m;
    }

    public static void startLearning(String pkg, int mode) {
        if (instance == null) return;
        instance.learnPkg = pkg;
        instance.learnMode = mode;
        instance.learning = false;
        Log.i(TAG, "开始学习(mode=" + mode + ")，2秒后开始监听 " + pkg);
        instance.handler.postDelayed(() -> {
            instance.learning = true;
            Log.i(TAG, "学习监听已启动(mode=" + mode + ")");
        }, 2000);
    }

    public static void startLearningManual(String pkg, int mode) {
        if (instance == null) return;
        instance.learnPkg = pkg;
        instance.learnMode = mode;
        instance.learning = false;
        instance.learningWindowActive = true;
        Log.i(TAG, "手动学习(mode=" + mode + ") " + pkg);
        instance.handler.postDelayed(() -> {
            instance.learning = true;
            Log.i(TAG, "手动学习监听已启动");
        }, 2000);
    }

    public static void stopLearningManual() {
        if (instance == null) return;
        instance.learning = false;
        instance.learningWindowActive = false;
        sCollectedEvents = new ArrayList<>(instance.learningEvents);
        sLearnedPkg = instance.learnPkg;
        sLearnMode = instance.learnMode;
        Log.i(TAG, "手动学习结束，收集" + sCollectedEvents.size() + "事件");
        instance.learningEvents.clear();
        instance.backToVoiceControl(0);
    }

    @Override
    public void onInterrupt() {
    }

    @Override
    public void onDestroy() {
        instance = null;
        super.onDestroy();
    }

    public static void tryClickPlay(String pkg) {
        if (instance == null) return;
        instance.retryCount = 0;
        instance.targetPkg = pkg;
        instance.cancelled = false;
        instance.handler.postDelayed(() -> instance.doTryClick(), 3000);
    }

    public static void cancelTryClick() {
        if (instance != null) instance.cancelled = true;
    }

    private void doTryClick() {
        if (cancelled) return;
        AccessibilityNodeInfo root = getRootInActiveWindow();
        if (root == null) {
            Log.w(TAG, "根节点为空，重试");
            if (++retryCount < 5) handler.postDelayed(this::doTryClick, 2000);
            return;
        }
        String rootPkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!rootPkg.equals(targetPkg)) {
            Log.w(TAG, "当前窗口" + rootPkg + "不是目标" + targetPkg + "，取消");
            return;
        }
        boolean clicked = findAndClick(root);
        if (clicked) {
            Log.i(TAG, "点击成功");
            return;
        }
        Log.w(TAG, "未找到播放按钮，第" + retryCount + "次重试");
        if (++retryCount < 5) {
            handler.postDelayed(this::doTryClick, 2000);
        } else {
            Log.i(TAG, "5次未找到播放按钮，放弃自动播放，请手动短嗯激活");
        }
    }

    private boolean findAndClick(AccessibilityNodeInfo node) {
        if (node == null) return false;
        SharedPreferences sp = getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String learnText = sp.getString("text_" + targetPkg, "");
        String learnDesc = sp.getString("desc_" + targetPkg, "");
        String learnId = sp.getString("id_" + targetPkg, "");

        if (!learnId.isEmpty() || !learnText.isEmpty() || !learnDesc.isEmpty()) {
            if (findAndClickByFeature(node, learnText, learnDesc, learnId)) return true;
        }

        List<AccessibilityNodeInfo> nodes;
        nodes = node.findAccessibilityNodeInfosByText("播放");
        if (clickFirstClickable(nodes)) return true;
        nodes = node.findAccessibilityNodeInfosByText("play");
        if (clickFirstClickable(nodes)) return true;
        if (findAndClickByDesc(node, "播放")) return true;
        if (findAndClickByDesc(node, "play")) return true;
        if (findAndClickByViewId(node, "play")) return true;
        return false;
    }

    private boolean findAndClickByFeature(AccessibilityNodeInfo node, String learnText, String learnDesc, String learnId) {
        if (node == null) return false;
        String t = node.getText() != null ? node.getText().toString() : "";
        String d = node.getContentDescription() != null ? node.getContentDescription().toString() : "";
        String vid = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";

        if (node.isClickable()) {
            boolean matchId = !learnId.isEmpty() && learnId.equals(vid);
            boolean matchText = !learnText.isEmpty() && learnText.equals(t);
            boolean matchDesc = !learnDesc.isEmpty() && learnDesc.equals(d);
            if (matchId || matchText || matchDesc) {
                Log.i(TAG, "匹配学习特征，点击: text=" + t + " desc=" + d + " id=" + vid);
                return node.performAction(AccessibilityNodeInfo.ACTION_CLICK);
            }
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            if (findAndClickByFeature(node.getChild(i), learnText, learnDesc, learnId)) return true;
        }
        return false;
    }

    private boolean clickFirstClickable(List<AccessibilityNodeInfo> nodes) {
        for (AccessibilityNodeInfo node : nodes) {
            if (node == null) continue;
            AccessibilityNodeInfo target = node.isClickable() ? node : node.getParent();
            if (target != null && target.isClickable()) {
                return target.performAction(AccessibilityNodeInfo.ACTION_CLICK);
            }
        }
        return false;
    }

    private boolean findAndClickByDesc(AccessibilityNodeInfo node, String keyword) {
        if (node == null) return false;
        CharSequence desc = node.getContentDescription();
        if (desc != null && desc.toString().toLowerCase().contains(keyword.toLowerCase()) && node.isClickable()) {
            return node.performAction(AccessibilityNodeInfo.ACTION_CLICK);
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            if (findAndClickByDesc(node.getChild(i), keyword)) return true;
        }
        return false;
    }

    private boolean findAndClickByViewId(AccessibilityNodeInfo node, String keyword) {
        if (node == null) return false;
        String id = node.getViewIdResourceName();
        if (id != null && id.toLowerCase().contains(keyword.toLowerCase()) && node.isClickable()) {
            return node.performAction(AccessibilityNodeInfo.ACTION_CLICK);
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            if (findAndClickByViewId(node.getChild(i), keyword)) return true;
        }
        return false;
    }

    private final List<AccessibilityNodeInfo> focusableNodes = new ArrayList<>();
    private int focusIndex = -1;

    private int topLine = 240;
    private int botLine = 1504;
    private int centerTopLine = 600;
    private int centerBotLine = 1000;
    private int avgCardHeight = 425;
    private String currentFocusDesc = "";

    public static void focusNext() {
        if (instance == null) return;
        instance.focusStep(true);
    }

    public static void focusPrev() {
        if (instance == null) return;
        instance.focusStep(false);
    }

    private long lastMiniPanelEscape = 0;
    private boolean escapeWechatMiniPanel() {

        return false;
    }

    public static boolean isWechatCallPanelOpen() {
        return instance != null && instance.wechatCallController.isPanelOpen();
    }

    public static boolean isWechatVideoFeed() {
        if (instance == null) return false;
        AccessibilityNodeInfo root = instance.getRootInActiveWindow();
        if (root == null) return false;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.WECHAT)) return false;
        if (instance.isWechatMomentsPage(root)) return false;
        boolean hasPlayPause = NodeHelper.hasNodeWithDesc(root, "播放", 0) || NodeHelper.hasNodeWithDesc(root, "暂停", 0);
        boolean hasVideoUI = NodeHelper.hasNodeWithDesc(root, "浮窗", 0) || NodeHelper.hasNodeWithDesc(root, "返回", 0);
        return hasPlayPause && hasVideoUI;
    }

    public static boolean isWechatLivePage() {
        if (instance == null) return false;
        AccessibilityNodeInfo root = instance.getRootInActiveWindow();
        if (root == null) return false;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.WECHAT)) return false;
        return NodeHelper.hasNodeWithText(root, "直播", 0) && NodeHelper.hasNodeWithDesc(root, "头像", 0);
    }

    private boolean isWechatMomentsPage(AccessibilityNodeInfo root) {
        if (root == null) return false;
        if (!NodeHelper.hasNodeWithDesc(root, "拍照分享", 0)) return false;
        android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
        return NodeHelper.hasNodeWithTextInTop(root, "朋友圈", dm.heightPixels / 2, 0);
    }

    private boolean isWechatContactsPage(AccessibilityNodeInfo root) {
        if (root == null) return false;
        if (!NodeHelper.hasNodeWithDesc(root, "更多功能", 0)) return false;
        android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
        return NodeHelper.hasNodeWithTextInTop(root, "通讯录", dm.heightPixels / 2, 0);
    }

    private static long lastAutoEnterTime = 0;

    private void autoEnterWechatChatFromContact() {
        AccessibilityNodeInfo root = getRootInActiveWindow();
        if (root == null) return;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.WECHAT)) return;
        if (!NodeHelper.hasNodeWithText(root, "发消息", 0)) return;
        if (!NodeHelper.hasNodeWithText(root, "音视频通话", 0)) return;
        long now = System.currentTimeMillis();
        if (now - lastAutoEnterTime < 3000) return;
        lastAutoEnterTime = now;
        AccessibilityNodeInfo sendMsgNode = NodeHelper.findNodeWithText(root, "发消息", 0);
        if (sendMsgNode != null) {
            AccessibilityNodeInfo clickable = NodeHelper.findClickableParent(sendMsgNode);
            if (clickable != null) {
                boolean clicked = clickable.performAction(AccessibilityNodeInfo.ACTION_CLICK);
                Log.i(TAG, "联系人详情页，自动点击发消息 clicked=" + clicked);
                if (!clicked) {
                    Rect r = new Rect();
                    clickable.getBoundsInScreen(r);
                    android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
                    float x = (float)(r.left + r.width()/2) / dm.widthPixels;
                    float y = (float)(r.top + r.height()/2) / dm.heightPixels;
                    GestureHelper.doTap(x, y);
                    Log.i(TAG, "performAction失败，坐标点击: (" + x + "," + y + ")");
                }
                handler.postDelayed(() -> wechatCallController.autoOpenWechatCallPanel(), 1500);
            }
        }
    }

    public static String extractWechatIds() {
        if (instance == null) return "服务未连接";
        AccessibilityNodeInfo root = instance.getRootInActiveWindow();
        if (root == null) return "无法获取根节点";
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.WECHAT)) return "当前不在微信";
        android.util.DisplayMetrics dm = instance.getResources().getDisplayMetrics();
        int screenH = dm.heightPixels;
        int screenW = dm.widthPixels;
        java.util.Map<String, int[]> clickStats = new java.util.HashMap<>();
        java.util.Map<String, Integer> contactCandidates = new java.util.HashMap<>();
        java.util.Set<String> letterIndexIds = new java.util.HashSet<>();
        java.util.Map<String, Integer> scrollCandidates = new java.util.HashMap<>();
        java.util.Set<String> moreFuncBtnIds = new java.util.HashSet<>();
        java.util.Set<String> rootFrameIds = new java.util.HashSet<>();
        scanNodesForIds(root, clickStats, contactCandidates, letterIndexIds,
                scrollCandidates, moreFuncBtnIds, rootFrameIds, screenH, screenW);
        StringBuilder sb = new StringBuilder();
        if (!letterIndexIds.isEmpty()) {
            String lid = letterIndexIds.iterator().next();
            WechatIds.setLetterIndex(lid);
            sb.append("letterIndex=").append(lid).append(" ");
        }
        String bestNavTab = null;
        int bestNavCount = 0;
        for (java.util.Map.Entry<String, int[]> e : clickStats.entrySet()) {
            int[] s = e.getValue();
            if (s[0] >= 3 && s[1] > screenH * 3 / 4) {
                if (s[0] > bestNavCount) { bestNavCount = s[0]; bestNavTab = e.getKey(); }
            }
        }
        if (bestNavTab != null) {
            WechatIds.setNavTab(bestNavTab);
            sb.append("navTab=").append(bestNavTab).append(" ");
        }
        String bestChatItem = null;
        int bestChatCount = 0;
        for (java.util.Map.Entry<String, int[]> e : clickStats.entrySet()) {
            if (e.getKey().equals(bestNavTab)) continue;
            int[] s = e.getValue();
            if (s[0] >= 3 && s[1] > screenH / 8 && s[1] < screenH * 3 / 4) {
                if (s[0] > bestChatCount) { bestChatCount = s[0]; bestChatItem = e.getKey(); }
            }
        }
        if (bestChatItem != null) {
            WechatIds.setChatItemNew(bestChatItem);
            sb.append("chatItem=").append(bestChatItem).append(" ");
        }
        String bestContact = null;
        int bestContactCount = 0;
        for (java.util.Map.Entry<String, Integer> e : contactCandidates.entrySet()) {
            if (e.getValue() > bestContactCount) { bestContactCount = e.getValue(); bestContact = e.getKey(); }
        }
        if (bestContact != null) {
            WechatIds.setContactName(bestContact);
            sb.append("contactName=").append(bestContact).append(" ");
        }
        String bestScroll = null;
        int bestScrollCount = 0;
        for (java.util.Map.Entry<String, Integer> e : scrollCandidates.entrySet()) {
            if (e.getValue() > bestScrollCount) { bestScrollCount = e.getValue(); bestScroll = e.getKey(); }
        }
        if (bestScroll != null) {
            WechatIds.setScrollList(bestScroll);
            sb.append("scrollList=").append(bestScroll).append(" ");
        }
        if (!moreFuncBtnIds.isEmpty()) {
            String mid = moreFuncBtnIds.iterator().next();
            WechatIds.setMoreFuncBtn(mid);
            sb.append("moreFuncBtn=").append(mid).append(" ");
        }
        if (!rootFrameIds.isEmpty()) {
            String rid = rootFrameIds.iterator().next();
            WechatIds.setRootFrame(rid);
            sb.append("rootFrame=").append(rid).append(" ");
        }
        String bestMomentsCard = null;
        int bestMomentsCount = 0;
        int bestMomentsHeight = 0;
        for (java.util.Map.Entry<String, int[]> e : clickStats.entrySet()) {
            if (e.getKey().equals(bestNavTab) || e.getKey().equals(bestChatItem)) continue;
            int[] s = e.getValue();
            if (s[0] >= 2 && s[2] > 200 && s[1] > screenH / 8 && s[1] < screenH * 3 / 4) {
                if (s[0] > bestMomentsCount || (s[0] == bestMomentsCount && s[2] > bestMomentsHeight)) {
                    bestMomentsCount = s[0]; bestMomentsCard = e.getKey(); bestMomentsHeight = s[2];
                }
            }
        }
        if (bestMomentsCard != null) {
            WechatIds.setMomentsCard(bestMomentsCard);
            sb.append("momentsCard=").append(bestMomentsCard).append(" ");
        }
        String bestDrawer = null;
        int bestDrawerCount = 0;
        for (java.util.Map.Entry<String, int[]> e : clickStats.entrySet()) {
            if (e.getKey().equals(bestNavTab) || e.getKey().equals(bestChatItem)
                    || e.getKey().equals(bestMomentsCard)) continue;
            int[] s = e.getValue();
            if (s[0] >= 2 && s[1] > screenH / 8 && s[1] < screenH * 3 / 4) {
                if (s[0] > bestDrawerCount) { bestDrawerCount = s[0]; bestDrawer = e.getKey(); }
            }
        }
        if (bestDrawer != null) {
            WechatIds.setDrawerItem(bestDrawer);
            sb.append("drawerItem=").append(bestDrawer).append(" ");
        }
        Log.i(TAG, "提取微信id: " + WechatIds.getAllIds());
        return "提取完成: " + WechatIds.getAllIds();
    }

    private boolean autoExtractByPage(AccessibilityNodeInfo root, int screenH, int screenW, String pageType) {
        if (root == null) return false;
        java.util.Map<String, int[]> clickStats = new java.util.HashMap<>();
        java.util.Map<String, Integer> contactCandidates = new java.util.HashMap<>();
        java.util.Set<String> letterIndexIds = new java.util.HashSet<>();
        java.util.Map<String, Integer> scrollCandidates = new java.util.HashMap<>();
        java.util.Set<String> moreFuncBtnIds = new java.util.HashSet<>();
        java.util.Set<String> rootFrameIds = new java.util.HashSet<>();
        scanNodesForIds(root, clickStats, contactCandidates, letterIndexIds,
                scrollCandidates, moreFuncBtnIds, rootFrameIds, screenH, screenW);
        boolean extracted = false;
        StringBuilder sb = new StringBuilder();
        String bestNavTab = null;
        int bestNavCount = 0;
        for (java.util.Map.Entry<String, int[]> e : clickStats.entrySet()) {
            int[] s = e.getValue();
            if (s[0] >= 3 && s[1] > screenH * 3 / 4 && s[0] > bestNavCount) {
                bestNavCount = s[0]; bestNavTab = e.getKey();
            }
        }
        if (bestNavTab != null) {
            WechatIds.setNavTab(bestNavTab);
            sb.append("navTab=").append(bestNavTab).append(" ");
            extracted = true;
        }
        if (pageType.equals("moments")) {
            String best = null; int bestCount = 0; int bestHeight = 0;
            for (java.util.Map.Entry<String, int[]> e : clickStats.entrySet()) {
                if (e.getKey().equals(bestNavTab)) continue;
                int[] s = e.getValue();
                if (s[0] >= 2 && s[2] > 200 && s[1] > screenH / 8 && s[1] < screenH * 3 / 4
                        && (s[0] > bestCount || (s[0] == bestCount && s[2] > bestHeight))) {
                    bestCount = s[0]; best = e.getKey(); bestHeight = s[2];
                }
            }
            if (best != null) {
                WechatIds.setMomentsCard(best);
                sb.append("momentsCard=").append(best).append(" ");
                extracted = true;
            }
        } else if (pageType.equals("contacts")) {
            String bestContact = null; int bestContactCount = 0;
            for (java.util.Map.Entry<String, Integer> e : contactCandidates.entrySet()) {
                if (e.getValue() > bestContactCount) { bestContactCount = e.getValue(); bestContact = e.getKey(); }
            }
            if (bestContact != null) {
                WechatIds.setContactName(bestContact);
                sb.append("contactName=").append(bestContact).append(" ");
                extracted = true;
            }
            if (!letterIndexIds.isEmpty()) {
                String lid = letterIndexIds.iterator().next();
                WechatIds.setLetterIndex(lid);
                sb.append("letterIndex=").append(lid).append(" ");
                extracted = true;
            }
        }
        if (extracted) {
            Log.i(TAG, "自动提取[" + pageType + "]: " + sb.toString());
        }
        return extracted;
    }

    private static void scanNodesForIds(AccessibilityNodeInfo node,
            java.util.Map<String, int[]> clickStats,
            java.util.Map<String, Integer> contactCandidates,
            java.util.Set<String> letterIndexIds,
            java.util.Map<String, Integer> scrollCandidates,
            java.util.Set<String> moreFuncBtnIds,
            java.util.Set<String> rootFrameIds, int screenH, int screenW) {
        if (node == null) return;
        String viewId = node.getViewIdResourceName();
        String text = node.getText() != null ? node.getText().toString() : "";
        String desc = node.getContentDescription() != null ? node.getContentDescription().toString() : "";
        String cls = node.getClassName() != null ? node.getClassName().toString() : "";
        if (viewId != null && viewId.contains(":id/")) {
            String idPart = viewId.substring(viewId.indexOf(":id/") + 4);
            if (text.length() == 1 && text.matches("[A-Z]")) {
                letterIndexIds.add(idPart);
            }
            if (node.isClickable()) {
                android.graphics.Rect r = new android.graphics.Rect();
                node.getBoundsInScreen(r);
                int[] s = clickStats.get(idPart);
                if (s == null) {
                    s = new int[]{0, Integer.MAX_VALUE, 0};
                    clickStats.put(idPart, s);
                }
                s[0]++;
                if (r.top < s[1]) s[1] = r.top;
                if (r.height() > s[2]) s[2] = r.height();
            }
            if (!text.isEmpty() && node.getChildCount() == 0 && text.length() >= 2 && text.length() <= 50) {
                boolean isTitle = text.equals("通讯录") || text.equals("微信") || text.equals("发现")
                    || text.equals("我") || text.equals("朋友圈") || text.equals("新的朋友")
                    || text.equals("群聊") || text.equals("标签") || text.equals("公众号")
                    || text.equals("搜索");
                if (!isTitle) {
                    contactCandidates.merge(idPart, 1, Integer::sum);
                }
            }
            if (cls.contains("RecyclerView") || cls.contains("ListView")) {
                scrollCandidates.merge(idPart, 1, Integer::sum);
            }
            if (desc.contains("更多功能")) {
                moreFuncBtnIds.add(idPart);
            }
            if (cls.contains("FrameLayout")) {
                android.graphics.Rect r = new android.graphics.Rect();
                node.getBoundsInScreen(r);
                if (r.width() > screenW * 9 / 10 && r.height() > screenH * 9 / 10) {
                    rootFrameIds.add(idPart);
                }
            }
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            scanNodesForIds(node.getChild(i), clickStats, contactCandidates, letterIndexIds,
                    scrollCandidates, moreFuncBtnIds, rootFrameIds, screenH, screenW);
        }
    }


    public static void wechatVideoFeedSwipe(boolean isNext) {
        if (instance == null) return;
        float midX = 0.5f;
        if (isNext) {
            GestureHelper.doSwipe(midX, 0.7f, midX, 0.3f, null);
            Log.i(TAG, "视频号上滑下一个");
        } else {
            GestureHelper.doSwipe(midX, 0.3f, midX, 0.7f, null);
            Log.i(TAG, "视频号下滑上一个");
        }
    }

    public static boolean detectAndEnterCallMode() {
        return instance != null && instance.wechatCallController.detectAndEnterCallMode();
    }

    public static boolean wechatEnterClick() {
        return instance != null && instance.wechatCallController.wechatEnterClick();
    }

    public static boolean wechatCallSwitch(boolean isNext) {
        return instance != null && instance.wechatCallController.wechatCallSwitch(isNext);
    }

    private boolean hasNodeWithDesc(AccessibilityNodeInfo node, String descPart, int depth) {
        return NodeHelper.hasNodeWithDesc(node, descPart, depth);
    }

    private AccessibilityNodeInfo findNodeWithDesc(AccessibilityNodeInfo node, String descPart, int depth) {
        return NodeHelper.findNodeWithDesc(node, descPart, depth);
    }

    public static boolean wechatCallBack() {
        return instance != null && instance.wechatCallController.wechatCallBack();
    }

    public static boolean isWechatFromContact() {
        return instance != null && instance.wechatCallController.isFromContactPage();
    }

    public static void clearWechatFromContact() {
        if (instance != null) instance.wechatCallController.clearFromContactPage();
    }

    private boolean findNodeInScreen(AccessibilityNodeInfo node, String idPart, int depth) {
        return NodeHelper.findNodeInScreen(node, idPart, depth);
    }

    private AccessibilityNodeInfo findNodeByIdRoot(String idPart) {
        AccessibilityNodeInfo root = getRootInActiveWindow();
        if (root == null) return null;
        return NodeHelper.findNodeById(root, idPart, 0);
    }

    private AccessibilityNodeInfo findNodeById(AccessibilityNodeInfo node, String idPart, int depth) {
        return NodeHelper.findNodeById(node, idPart, depth);
    }

    private void focusStep(boolean isNext) {
        if (isRefreshing) {
            Log.i(TAG, "刷新中，忽略命令");
            return;
        }
        if (escapeWechatMiniPanel()) {
            handler.postDelayed(() -> focusStep(isNext), 600);
            return;
        }
        refreshFocusableNodes();
        if (focusableNodes.isEmpty()) {
            Toast.makeText(this, "没有可选项", Toast.LENGTH_SHORT).show();
            return;
        }
        int currentIdx = -1;
        if (!currentFocusDesc.isEmpty()) {
            int bestDist = Integer.MAX_VALUE;
            for (int i = 0; i < focusableNodes.size(); i++) {
                String ni = getNodeInfo(focusableNodes.get(i));
                if (ni.equals(currentFocusDesc)) {
                    int dist = Math.abs(i - focusIndex);
                    if (dist < bestDist) {
                        bestDist = dist;
                        currentIdx = i;
                    }
                }
            }
        }
        if (wechatNavMode && currentFocusPkg.equals(AppConstants.WECHAT)) {
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < focusableNodes.size(); i++) {
                sb.append("[").append(i).append(":").append(getNodeInfo(focusableNodes.get(i))).append("] ");
            }
            Log.i(TAG, "微信导航focusStep: curDesc=" + currentFocusDesc + " curIdx=" + currentIdx + " nodes=" + sb.toString());
        }
        if (currentIdx < 0) {
            focusIndex = isNext ? 0 : focusableNodes.size() - 1;
            currentFocusDesc = getNodeInfo(focusableNodes.get(focusIndex));
            focusCurrent();
            Log.i(TAG, "焦点不在屏幕，" + (isNext ? "NEXT选第一个" : "PREV选最后一个"));
            return;
        }
        int nextIdx = isNext ? currentIdx + 1 : currentIdx - 1;
        if (nextIdx >= 0 && nextIdx < focusableNodes.size()) {
            focusIndex = nextIdx;
            currentFocusDesc = getNodeInfo(focusableNodes.get(focusIndex));
            focusCurrent();
            return;
        }
        if (wechatNavMode && currentFocusPkg.equals(AppConstants.WECHAT)) {
            focusIndex = isNext ? 0 : focusableNodes.size() - 1;
            currentFocusDesc = getNodeInfo(focusableNodes.get(focusIndex));
            focusCurrent();
            return;
        }
        if (wechatGenericPage && currentFocusPkg.equals(AppConstants.WECHAT)) {
            focusIndex = isNext ? 0 : focusableNodes.size() - 1;
            currentFocusDesc = getNodeInfo(focusableNodes.get(focusIndex));
            focusCurrent();
            Log.i(TAG, "微信固定列表循环到" + (isNext ? "第一" : "最后") + "项");
            return;
        }
        if (currentFocusPkg.equals(AppConstants.WECHAT) && !isNext) {
            Log.i(TAG, "微信PREV到顶，用ACTION_SCROLL_BACKWARD翻页");
            swipeForNewCards(isNext, 0);
            return;
        }
        Log.i(TAG, "到屏幕边缘，翻页 isNext=" + isNext);
        swipeForNewCards(isNext, 0);
    }

    private void swipeForNewCards(boolean isNext, int depth) {
        if (depth >= 6) {
            Log.w(TAG, "翻页深度上限，聚焦最近卡片");
            focusClosestToCenter();
            return;
        }
        refreshFocusableNodes();
        if (focusableNodes.isEmpty()) {
            if (depth == 0) {
                Log.w(TAG, "无卡片，按B站首页键刷新");
                clickBiliHomeAndReset();
            } else {
                swipeForNewCards(isNext, depth + 1);
            }
            return;
        }
        final List<String> beforeCards = new ArrayList<>();
        for (AccessibilityNodeInfo n : focusableNodes) {
            String desc = getNodeInfo(n);
            if (!desc.isEmpty()) beforeCards.add(desc);
        }
        Log.i(TAG, "翻页前卡片: " + beforeCards.size() + "项 isNext=" + isNext + " depth=" + depth);
        removeHighlight();
        final int[] panelRetry = {0};
        final boolean[] prevScrollReset = {false};
        final Runnable[] afterSwipeRef = new Runnable[1];
        afterSwipeRef[0] = () -> waitForScrollStable(() -> {
            if (currentFocusPkg.equals(AppConstants.WECHAT) && escapeWechatMiniPanel()) {
                if (panelRetry[0]++ < 3) {
                    Log.i(TAG, "翻页后收回小程序面板，重试选焦点 #" + panelRetry[0]);
                    handler.postDelayed(() -> afterSwipeRef[0].run(), 600);
                    return;
                }
            }
            panelRetry[0] = 0;
            refreshFocusableNodes();
            if (prevScrollReset[0]) {
                isRefreshing = false;
                int topIdx = 0;
                int minTop = Integer.MAX_VALUE;
                for (int i = 0; i < focusableNodes.size(); i++) {
                    Rect r = new Rect();
                    focusableNodes.get(i).getBoundsInScreen(r);
                    if (r.top < minTop) {
                        minTop = r.top;
                        topIdx = i;
                    }
                }
                focusIndex = topIdx;
                currentFocusDesc = getNodeInfo(focusableNodes.get(focusIndex));
                focusCurrent();
                Log.i(TAG, "PREV翻页后扫描，焦点设为最上方卡片 idx=" + topIdx + " top=" + minTop);
                return;
            }
            List<Integer> newIndices = new ArrayList<>();
            for (int i = 0; i < focusableNodes.size(); i++) {
                String desc = getNodeInfo(focusableNodes.get(i));
                if (!desc.isEmpty() && !beforeCards.contains(desc)) {
                    newIndices.add(i);
                }
            }
            Log.i(TAG, "翻页后新卡片: " + newIndices.size() + "项 (共" + focusableNodes.size() + ")");
            if (newIndices.isEmpty()) {
                swipeForNewCards(isNext, depth + 1);
                return;
            }
            int targetCenter = (centerTopLine + centerBotLine) / 2;
            int bestIdx = newIndices.get(0);
            Rect bestR = new Rect();
            focusableNodes.get(bestIdx).getBoundsInScreen(bestR);
            int bestDist = Math.abs((bestR.top + bestR.bottom) / 2 - targetCenter);
            for (int idx : newIndices) {
                Rect r = new Rect();
                focusableNodes.get(idx).getBoundsInScreen(r);
                int dist = Math.abs((r.top + r.bottom) / 2 - targetCenter);
                if (dist < bestDist) { bestDist = dist; bestR = r; bestIdx = idx; }
            }
            focusIndex = bestIdx;
            currentFocusDesc = getNodeInfo(focusableNodes.get(focusIndex));
            focusCurrent();
        });
        if (!isNext) {
            prevScrollReset[0] = true;
            AccessibilityNodeInfo scrollable = findScrollableNode(getRootInActiveWindow(), 0);
            if (scrollable != null) {
                boolean scrolled = scrollable.performAction(AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD);
                Log.i(TAG, "PREV ACTION_SCROLL_BACKWARD=" + scrolled);
                scrollable.recycle();
                if (scrolled) {
                    isRefreshing = true;
                    waitRefreshDone(afterSwipeRef[0], 3000);
                    return;
                }
            } else {
                Log.i(TAG, "PREV 未找到可滚动节点，回退手势滑动");
            }
        }
        Runnable swipeCb = (!isNext) ? () -> waitRefreshDone(afterSwipeRef[0], 1000) : afterSwipeRef[0];
        if (currentFocusPkg.equals(AppConstants.WECHAT)) {
            Point sz = new Point();
            ((WindowManager) getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay().getRealSize(sz);
            int screenH = sz.y;
            int midY = (topLine + botLine) / 2;
            int halfSwipe = 160;
            if (focusIndex < focusableNodes.size()) {
                Rect fr = new Rect();
                focusableNodes.get(focusIndex).getBoundsInScreen(fr);
                if (fr.height() > botLine - topLine) {
                    halfSwipe = isNext ? screenH / 3 : screenH / 5;
                    Log.i(TAG, "巨长卡片高度=" + fr.height() + " 增大翻页滑动 halfSwipe=" + halfSwipe + " isNext=" + isNext);
                }
            }
            float sY, eY;
            if (isNext) {
                sY = (float)(midY + halfSwipe) / screenH;
                eY = (float)(midY - halfSwipe) / screenH;
            } else {
                sY = (float)(midY - halfSwipe) / screenH;
                eY = (float)(midY + halfSwipe) / screenH;
            }
            Log.i(TAG, "微信翻页滑动 midY=" + midY + " topLine=" + topLine + " botLine=" + botLine + " sY=" + sY + " eY=" + eY);
            doSwipe(0.5f, sY, 0.5f, eY, swipeCb);
        } else if (isNext) {
            doSwipe(0.5f, 0.7f, 0.5f, 0.3f, afterSwipeRef[0]);
        } else {
            doSwipe(0.5f, 0.3f, 0.5f, 0.7f, swipeCb);
        }
    }

    private AccessibilityNodeInfo findScrollableNode(AccessibilityNodeInfo node, int depth) {
        if (node == null || depth > 10) return null;
        if (node.isScrollable()) return AccessibilityNodeInfo.obtain(node);
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo result = findScrollableNode(node.getChild(i), depth + 1);
            if (result != null) return result;
        }
        return null;
    }

    private void waitRefreshDone(Runnable callback, long initialDelay) {
        handler.postDelayed(() -> checkRefreshDone(callback), initialDelay);
    }

    private void checkRefreshDone(Runnable callback) {
        if (hasLoadingSpinner()) {
            Log.i(TAG, "刷新中(loading spinner可见)，继续等待");
            handler.postDelayed(() -> checkRefreshDone(callback), 500);
        } else {
            Log.i(TAG, "刷新完成(loading spinner消失)");
            callback.run();
        }
    }

    private boolean hasLoadingSpinner() {
        return hasLoadingSpinner(getRootInActiveWindow(), 0);
    }

    private boolean hasLoadingSpinner(AccessibilityNodeInfo node, int depth) {
        if (node == null || depth > 15) return false;
        String cls = node.getClassName() != null ? node.getClassName().toString() : "";
        if (cls.contains("ProgressBar") || cls.contains("Loading") || cls.contains("Spinner")) {
            Rect r = new Rect();
            node.getBoundsInScreen(r);
            if (r.width() > 0 && r.height() > 0) return true;
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            if (hasLoadingSpinner(node.getChild(i), depth + 1)) return true;
        }
        return false;
    }

    private void wechatScrollBackStep(List<String> beforeCards, int attempt, Runnable finalCallback) {
        if (attempt >= 8) {
            Log.w(TAG, "微信反向滚动次数上限，执行afterSwipe");
            finalCallback.run();
            return;
        }
        AccessibilityNodeInfo scrollNode = findNodeByIdRoot(WechatIds.scrollList());
        if (scrollNode == null || !scrollNode.performAction(AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD)) {
            Log.i(TAG, "微信ACTION_SCROLL_BACKWARD无法滚动(attempt=" + attempt + ")，用doSwipe");
            doSwipe(0.5f, 0.3f, 0.5f, 0.7f, finalCallback);
            return;
        }
        Log.i(TAG, "微信ACTION_SCROLL_BACKWARD #" + (attempt + 1));
        handler.postDelayed(() -> {
            refreshFocusableNodes();
            boolean hasNew = false;
            for (AccessibilityNodeInfo n : focusableNodes) {
                String desc = getNodeInfo(n);
                if (!desc.isEmpty() && !beforeCards.contains(desc)) {
                    hasNew = true;
                    break;
                }
            }
            if (hasNew) {
                Log.i(TAG, "微信反向滚动发现新卡片，停止滚动");
                finalCallback.run();
            } else {
                wechatScrollBackStep(beforeCards, attempt + 1, finalCallback);
            }
        }, 250);
    }

    private void wechatScrollStep(List<String> beforeCards, int attempt, Runnable finalCallback) {
        if (attempt >= 8) {
            Log.w(TAG, "微信滚动次数上限，执行afterSwipe");
            finalCallback.run();
            return;
        }
        AccessibilityNodeInfo scrollNode = findNodeByIdRoot(WechatIds.scrollList());
        if (scrollNode == null || !scrollNode.performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD)) {
            Log.i(TAG, "微信ACTION_SCROLL_FORWARD无法滚动(attempt=" + attempt + ")，用doSwipe");
            doSwipe(0.5f, 0.7f, 0.5f, 0.3f, finalCallback);
            return;
        }
        Log.i(TAG, "微信ACTION_SCROLL_FORWARD #" + (attempt + 1));
        handler.postDelayed(() -> {
            refreshFocusableNodes();
            boolean hasNew = false;
            for (AccessibilityNodeInfo n : focusableNodes) {
                String desc = getNodeInfo(n);
                if (!desc.isEmpty() && !beforeCards.contains(desc)) {
                    hasNew = true;
                    break;
                }
            }
            if (hasNew) {
                Log.i(TAG, "微信滚动发现新卡片，停止滚动");
                finalCallback.run();
            } else {
                wechatScrollStep(beforeCards, attempt + 1, finalCallback);
            }
        }, 250);
    }

    private void focusClosestToCenter() {
        if (focusableNodes.isEmpty()) return;
        int targetCenter = (centerTopLine + centerBotLine) / 2;
        int bestIdx = 0;
        int bestDist = Integer.MAX_VALUE;
        for (int i = 0; i < focusableNodes.size(); i++) {
            Rect r = new Rect();
            focusableNodes.get(i).getBoundsInScreen(r);
            int center = (r.top + r.bottom) / 2;
            int dist = Math.abs(center - targetCenter);
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx = i;
            }
        }
        focusIndex = bestIdx;
        currentFocusDesc = getNodeInfo(focusableNodes.get(focusIndex));
        focusCurrent();
    }

    private void waitForScrollStable(Runnable onStable) {
        waitForScrollIdle(onStable);
    }

    private void clickBiliHomeAndReset() {
        removeHighlight();
        AccessibilityNodeInfo root = getRootInActiveWindow();
        if (root == null) {
            Toast.makeText(this, "无法刷新", Toast.LENGTH_SHORT).show();
            return;
        }
        AccessibilityNodeInfo homeBtn = findHomeButton(root);
        if (homeBtn != null) {
            homeBtn.performAction(AccessibilityNodeInfo.ACTION_CLICK);
            Log.i(TAG, "点击B站首页键刷新");
        } else {
            Log.w(TAG, "找不到B站首页键");
        }
        handler.postDelayed(() -> doRefreshAfterHome(0), 3000);
    }

    private void doRefreshAfterHome(int retry) {
        focusIndex = -1;
        currentFocusDesc = "";
        refreshFocusableNodes();
        if (focusableNodes.isEmpty()) {
            if (retry < 3) {
                Log.i(TAG, "刷新后无卡片，第" + (retry + 1) + "次重试");
                handler.postDelayed(() -> doRefreshAfterHome(retry + 1), 2000);
            } else {
                Toast.makeText(this, "刷新后无卡片", Toast.LENGTH_SHORT).show();
            }
            return;
        }
        focusClosestToCenter();
    }

    private AccessibilityNodeInfo findHomeButton(AccessibilityNodeInfo node) {
        if (node == null) return null;
        String t = node.getText() != null ? node.getText().toString() : "";
        String d = node.getContentDescription() != null ? node.getContentDescription().toString() : "";
        if ((t.contains("首页") || d.contains("首页")) && node.isClickable()) {
            Rect r = new Rect();
            node.getBoundsInScreen(r);
            if (r.top > 800) return node;
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo result = findHomeButton(node.getChild(i));
            if (result != null) return result;
        }
        return null;
    }

    public static void resetCardList() {
        if (instance == null) return;
        instance.focusIndex = -1;
        instance.currentFocusDesc = "";
    }

    public static void refocusCurrent() {
        if (instance == null) return;
        instance.refreshFocusableNodes();
        if (instance.focusableNodes.isEmpty()) return;
        instance.focusClosestToCenter();
    }

    public static void clickFocused() {
        if (instance == null) return;
        if (instance.escapeWechatMiniPanel()) return;
        if (instance.focusableNodes.isEmpty() || instance.focusIndex >= instance.focusableNodes.size()) {
            Toast.makeText(instance, "请先选择", Toast.LENGTH_SHORT).show();
            return;
        }
        AccessibilityNodeInfo node = instance.focusableNodes.get(instance.focusIndex);
        String info = instance.getNodeInfo(node);
        String viewId = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
        boolean isWechatChatItem = instance.currentFocusPkg.equals(AppConstants.WECHAT)
                && WechatIds.isChatItem(viewId);
        boolean clicked = node.performAction(AccessibilityNodeInfo.ACTION_CLICK);
        if (!clicked) {
            Rect r = new Rect();
            node.getBoundsInScreen(r);
            android.util.DisplayMetrics dm = instance.getResources().getDisplayMetrics();
            float x = (float)(r.left + r.width()/2) / dm.widthPixels;
            float y = (float)(r.top + r.height()/2) / dm.heightPixels;
            GestureHelper.doTap(x, y);
            Log.i(TAG, "节点非clickable，坐标点击: (" + x + "," + y + ")");
        }
        removeHighlight();
        Toast.makeText(instance, "点击: " + info, Toast.LENGTH_SHORT).show();
        if (isWechatChatItem) {
            Log.i(TAG, "点击微信聊天项，1.5秒后自动打开通话面板");
            instance.handler.postDelayed(() -> instance.wechatCallController.autoOpenWechatCallPanel(), 1500);
        }
    }

    private AccessibilityNodeInfo findClickableParent(AccessibilityNodeInfo node) {
        return NodeHelper.findClickableParent(node);
    }

    public static boolean isBiliFullscreen() {
        if (instance == null) return false;
        AccessibilityNodeInfo root = instance.getRootInActiveWindow();
        if (root == null) return false;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.BILIBILI)) return false;
        if (instance.findNodeById(root, "bbplayer_fullscreen")) return true;
        DisplayMetrics dm = instance.getResources().getDisplayMetrics();
        return dm.widthPixels > dm.heightPixels;
    }

    public static boolean isBiliVerticalVideo() {
        if (instance == null) return false;
        AccessibilityNodeInfo root = instance.getRootInActiveWindow();
        if (root == null) return false;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.BILIBILI)) return false;
        return instance.findNodeById(root, "story_view_pager")
                || instance.findNodeById(root, "feed_videos")
                || instance.findNodeById(root, "video_controller");
    }

    public static boolean isBiliLiveRoom() {
        if (instance == null) return false;
        AccessibilityNodeInfo root = instance.getRootInActiveWindow();
        if (root == null) return false;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.BILIBILI)) return false;
        return instance.findNodeById(root, "live_room_close")
                || instance.findNodeById(root, "live_send_danmaku")
                || instance.findNodeById(root, "anchor_nickname");
    }

    public static boolean isXimaPlaying() {
        if (instance == null) return false;
        AccessibilityNodeInfo root = instance.getRootInActiveWindow();
        if (root == null) return false;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.XIMALAYA)) return false;
        return instance.findNodeById(root, "elderly_vg_play_btn");
    }

    private static int ximaDetailIndex = 0;

    private static boolean hasDescendantText(AccessibilityNodeInfo node, String text) {
        if (node == null) return false;
        CharSequence t = node.getText();
        if (t != null && t.toString().contains(text)) return true;
        CharSequence d = node.getContentDescription();
        if (d != null && d.toString().contains(text)) return true;
        for (int i = 0; i < node.getChildCount(); i++) {
            if (hasDescendantText(node.getChild(i), text)) return true;
        }
        return false;
    }

    private static AccessibilityNodeInfo findDetailButton(String text) {
        if (instance == null) return null;
        Point size = new Point();
        ((WindowManager) instance.getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay().getRealSize(size);
        int screenW = size.x;
        int screenH = size.y;
        List<AccessibilityNodeInfo> candidates = new ArrayList<>();
        List<AccessibilityWindowInfo> windows = instance.getWindows();
        for (AccessibilityWindowInfo w : windows) {
            AccessibilityNodeInfo root = w.getRoot();
            if (root == null) continue;
            String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
            if (!pkg.equals(AppConstants.XIMALAYA)) continue;
            collectDetailButtons(root, text, screenW, screenH, candidates);
        }
        if (candidates.isEmpty()) {
            AccessibilityNodeInfo root = instance.getRootInActiveWindow();
            if (root != null) {
                String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
                if (pkg.equals(AppConstants.XIMALAYA)) {
                    collectDetailButtons(root, text, screenW, screenH, candidates);
                }
            }
        }
        AccessibilityNodeInfo best = null;
        int bestArea = Integer.MAX_VALUE;
        for (AccessibilityNodeInfo n : candidates) {
            Rect r = new Rect();
            n.getBoundsInScreen(r);
            int area = r.width() * r.height();
            if (area < bestArea) {
                bestArea = area;
                best = n;
            }
        }
        return best;
    }

    private static void collectDetailButtons(AccessibilityNodeInfo node, String text, int screenW, int screenH, List<AccessibilityNodeInfo> out) {
        if (node == null) return;
        if (node.isClickable()) {
            Rect r = new Rect();
            node.getBoundsInScreen(r);
            if (r.width() > 0 && r.height() > 0 && r.left >= 0 && r.right <= screenW
                    && r.top >= 0 && r.bottom <= screenH && hasDescendantText(node, text)) {
                out.add(node);
            }
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            collectDetailButtons(node.getChild(i), text, screenW, screenH, out);
        }
    }

    public static boolean isXimaDetailPage() {
        return findDetailButton("立即播放") != null;
    }

    private static AccessibilityNodeInfo findDetailLeftButton() {
        AccessibilityNodeInfo btn = findDetailButton("下一集播放");
        if (btn != null) return btn;
        return findDetailButton("加待播");
    }

    public static void ximaDetailToggle(boolean isNext) {
        if (instance == null) return;
        if (isNext) {
            ximaDetailIndex = (ximaDetailIndex + 1) % 2;
        } else {
            ximaDetailIndex = (ximaDetailIndex - 1 + 2) % 2;
        }
        AccessibilityNodeInfo clickable;
        String label;
        if (ximaDetailIndex == 0) {
            clickable = findDetailLeftButton();
            label = "左按钮(下一集/加待播)";
        } else {
            clickable = findDetailButton("立即播放");
            label = "立即播放";
        }
        if (clickable != null) {
            Rect r = new Rect();
            clickable.getBoundsInScreen(r);
            instance.showHighlight(r, Color.BLUE);
            Log.i(TAG, "详情页选中: " + label + " @" + r.left + "," + r.top);
        } else {
            Log.i(TAG, "详情页未找到: " + label);
        }
    }

    public static void ximaDetailClick() {
        if (instance == null) return;
        AccessibilityNodeInfo clickable;
        String label;
        if (ximaDetailIndex == 0) {
            clickable = findDetailLeftButton();
            label = "左按钮(下一集/加待播)";
        } else {
            clickable = findDetailButton("立即播放");
            label = "立即播放";
        }
        if (clickable != null) {
            clickable.performAction(AccessibilityNodeInfo.ACTION_CLICK);
            Log.i(TAG, "详情页点击: " + label);
            instance.removeHighlight();
        } else {
            Log.i(TAG, "详情页未找到: " + label);
        }
    }

    public static void tryLiveFullscreen() {
        if (instance == null) return;
        SharedPreferences sp = instance.getSharedPreferences("voice_ctrl", Context.MODE_PRIVATE);
        DisplayMetrics dm = instance.getResources().getDisplayMetrics();
        int centerTopLine = sp.getInt("calib_centerTop", dm.heightPixels * 3 / 8);
        int leftLine = sp.getInt("calib_left", dm.widthPixels / 4);
        int rightLine = sp.getInt("calib_right", dm.widthPixels * 3 / 4);
        int tapX = (leftLine + rightLine) / 2;
        Log.i(TAG, "直播全屏切换: tapX=" + tapX + " centerTop=" + centerTopLine);
        instance.doTap((float) tapX / dm.widthPixels, (float) centerTopLine / dm.heightPixels);
        instance.handler.postDelayed(() -> {
            AccessibilityNodeInfo r = instance.getRootInActiveWindow();
            if (r != null && !instance.findAndClickByDesc(r, "全屏") && !instance.findAndClickByDesc(r, "退出全屏")) {
                instance.findAndClickPlayerButton(r, 0);
            }
        }, 500);
    }

    public static void tryLiveToggleDanmaku() {
        if (instance == null) return;
        SharedPreferences sp = instance.getSharedPreferences("voice_ctrl", Context.MODE_PRIVATE);
        DisplayMetrics dm = instance.getResources().getDisplayMetrics();
        int centerTopLine = sp.getInt("calib_centerTop", dm.heightPixels * 3 / 8);
        int leftLine = sp.getInt("calib_left", dm.widthPixels / 4);
        int rightLine = sp.getInt("calib_right", dm.widthPixels * 3 / 4);
        int tapX = (leftLine + rightLine) / 2;
        Log.i(TAG, "直播开关弹幕: tapX=" + tapX + " centerTop=" + centerTopLine);
        instance.doTap((float) tapX / dm.widthPixels, (float) centerTopLine / dm.heightPixels);
        instance.handler.postDelayed(() -> {
            AccessibilityNodeInfo r = instance.getRootInActiveWindow();
            if (r != null && !instance.findAndClickByDesc(r, "弹幕")) {
                instance.findAndClickPlayerButton(r, 1);
            }
        }, 500);
    }

    private boolean findAndClickPlayerButton(AccessibilityNodeInfo root, int indexFromRight) {
        if (root == null) return false;
        AccessibilityNodeInfo underlay = findNodeByIdReturn(root, "controller_underlay");
        if (underlay == null) return false;
        Rect underlayRect = new Rect();
        underlay.getBoundsInScreen(underlayRect);
        List<AccessibilityNodeInfo> buttons = new ArrayList<>();
        collectPlayerButtons(root, underlayRect, buttons);
        if (buttons.isEmpty()) return false;
        buttons.sort((a, b) -> {
            Rect ra = new Rect(), rb = new Rect();
            a.getBoundsInScreen(ra); b.getBoundsInScreen(rb);
            return rb.left - ra.left;
        });
        if (indexFromRight >= buttons.size()) return false;
        AccessibilityNodeInfo btn = buttons.get(indexFromRight);
        Rect r = new Rect();
        btn.getBoundsInScreen(r);
        Log.i(TAG, "点击播放器按钮[" + indexFromRight + "]: @(" + r.left + "," + r.top + ")");
        return btn.performAction(AccessibilityNodeInfo.ACTION_CLICK);
    }

    private void collectPlayerButtons(AccessibilityNodeInfo node, Rect area, List<AccessibilityNodeInfo> out) {
        if (node == null) return;
        String vid = node.getViewIdResourceName();
        if (node.isClickable() && (vid == null || vid.isEmpty()) && node.getClassName() != null
                && node.getClassName().toString().contains("ImageView")) {
            Rect r = new Rect();
            node.getBoundsInScreen(r);
            if (r.left >= area.left && r.right <= area.right && r.top >= area.top && r.bottom <= area.bottom) {
                out.add(node);
            }
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            collectPlayerButtons(node.getChild(i), area, out);
        }
    }

    public static void tapToShowControlBar() {
        if (instance == null) return;
        DisplayMetrics dm = instance.getResources().getDisplayMetrics();
        boolean isLandscape = dm.widthPixels > dm.heightPixels;
        if (isLandscape) {
            Log.i(TAG, "全屏点击中心唤起控制栏");
            instance.doTap(0.5f, 0.5f);
        } else {
            SharedPreferences sp = instance.getSharedPreferences("voice_ctrl", Context.MODE_PRIVATE);
            int topLine = sp.getInt("calib_top", 240);
            int leftLine = sp.getInt("calib_left", dm.widthPixels / 4);
            int rightLine = sp.getInt("calib_right", dm.widthPixels * 3 / 4);
            int tapX = (leftLine + rightLine) / 2;
            int tapY = topLine;
            Log.i(TAG, "点击唤起控制栏: tapX=" + tapX + " tapY=" + tapY);
            instance.doTap((float) tapX / dm.widthPixels, (float) tapY / dm.heightPixels);
        }
    }

    public static boolean tryToggleFullscreen() {
        if (instance == null) return false;
        AccessibilityNodeInfo root = instance.getRootInActiveWindow();
        if (root == null) return false;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        SharedPreferences sp = instance.getSharedPreferences("voice_ctrl", Context.MODE_PRIVATE);
        String learnText = sp.getString("fs_text_" + pkg, "");
        String learnDesc = sp.getString("fs_desc_" + pkg, "");
        String learnId = sp.getString("fs_id_" + pkg, "");
        if (!(learnId.isEmpty() && learnText.isEmpty() && learnDesc.isEmpty())) {
            if (instance.findAndClickByFeature(root, learnText, learnDesc, learnId)) return true;
        }
        if (pkg.equals(AppConstants.BILIBILI)) {
            DisplayMetrics dm = instance.getResources().getDisplayMetrics();
            boolean isLandscape = dm.widthPixels > dm.heightPixels;
            boolean isVideoPage = instance.findNodeById(root, "danmaku_input")
                    || instance.findNodeById(root, "frame_like")
                    || instance.findNodeById(root, "gemini_halfscreen")
                    || instance.findNodeById(root, "bbplayer_fullscreen")
                    || isLandscape;
            if (!isVideoPage) return false;
            if (isLandscape) {
                Log.i(TAG, "全屏点击中心唤起控制栏");
                instance.doTap(0.5f, 0.5f);
            } else {
                int topLine = sp.getInt("calib_top", 240);
                int leftLine = sp.getInt("calib_left", dm.widthPixels / 4);
                int rightLine = sp.getInt("calib_right", dm.widthPixels * 3 / 4);
                int tapX = (leftLine + rightLine) / 2;
                int tapY = topLine;
                Log.i(TAG, "点击唤起控制栏: left=" + leftLine + " right=" + rightLine + " tapX=" + tapX + " topLine=" + topLine);
                instance.doTap((float) tapX / dm.widthPixels, (float) tapY / dm.heightPixels);
            }
            final int[] checkCount = {0};
            final Runnable checkBtn = new Runnable() {
                @Override
                public void run() {
                    checkCount[0]++;
                    AccessibilityNodeInfo r2 = instance.getRootInActiveWindow();
                    if (r2 != null) {
                        AccessibilityNodeInfo btn = instance.findNodeByIdReturn(r2, "gemini_halfscreen_expand");
                        if (btn != null && btn.isClickable()) {
                            Log.i(TAG, "找到展开按钮(第" + checkCount[0] + "次)");
                            btn.performAction(AccessibilityNodeInfo.ACTION_CLICK);
                            instance.handler.postDelayed(() -> {
                                Log.i(TAG, "进入全屏后点击关闭控制栏");
                                instance.doTap(0.5f, 0.5f);
                            }, 500);
                            return;
                        }
                        btn = instance.findNodeByIdReturn(r2, "bbplayer_fullscreen_back");
                        if (btn != null && btn.isClickable()) {
                            Log.i(TAG, "找到返回按钮(第" + checkCount[0] + "次)");
                            btn.performAction(AccessibilityNodeInfo.ACTION_CLICK);
                            instance.handler.postDelayed(() -> {
                                Log.i(TAG, "退出全屏后点击关闭控制栏");
                                DisplayMetrics dm2 = instance.getResources().getDisplayMetrics();
                                int tL = sp.getInt("calib_top", 240);
                                int lL = sp.getInt("calib_left", dm2.widthPixels / 4);
                                int rL = sp.getInt("calib_right", dm2.widthPixels * 3 / 4);
                                instance.doTap((float) (lL + rL) / 2 / dm2.widthPixels, (float) tL / dm2.heightPixels);
                            }, 500);
                            return;
                        }
                    }
                    if (checkCount[0] < 25) {
                        instance.handler.postDelayed(this, 200);
                    } else {
                        Log.i(TAG, "25次未找到全屏按钮，按返回键退出全屏");
                        instance.performGlobalAction(GLOBAL_ACTION_BACK);
                    }
                }
            };
            instance.handler.postDelayed(checkBtn, 300);
            return true;
        }
        return false;
    }

    private void logFullscreenNodes(AccessibilityNodeInfo node) {
        if (node == null) return;
        if (node.isClickable()) {
            String d = node.getContentDescription() != null ? node.getContentDescription().toString() : "";
            String vid = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
            String t = node.getText() != null ? node.getText().toString() : "";
            Rect r = new Rect();
            node.getBoundsInScreen(r);
            Log.i(TAG, "可点击: " + r.width() + "x" + r.height() + " @(" + r.left + "," + r.top
                + ") text=[" + t + "] desc=[" + d + "] id=[" + vid + "]");
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            logFullscreenNodes(node.getChild(i));
        }
    }

    private boolean findNodeById(AccessibilityNodeInfo node, String idPart) {
        return NodeHelper.findNodeById(node, idPart);
    }

    private AccessibilityNodeInfo findNodeByIdReturn(AccessibilityNodeInfo node, String idPart) {
        return NodeHelper.findNodeByIdReturn(node, idPart);
    }

    private boolean findAndClickFullscreenBili(AccessibilityNodeInfo node) {
        if (node == null) return false;
        String vid = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
        String d = node.getContentDescription() != null ? node.getContentDescription().toString() : "";
        if (node.isClickable() && (vid.contains("full") || d.contains("全屏")) && !vid.contains("seekbar")) {
            Log.i(TAG, "点击全屏: desc=[" + d + "] id=[" + vid + "]");
            return node.performAction(AccessibilityNodeInfo.ACTION_CLICK);
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            if (findAndClickFullscreenBili(node.getChild(i))) return true;
        }
        return false;
    }

    private void refreshFocusableNodes() {
        focusableNodes.clear();
        AccessibilityNodeInfo root = getRootInActiveWindow();
        if (root == null) return;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        currentFocusPkg = pkg;
        SharedPreferences sp = getSharedPreferences("voice_ctrl", Context.MODE_PRIVATE);
        Point size = new Point();
        ((WindowManager) getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay().getRealSize(size);
        int defTop = pkg.equals(AppConstants.WECHAT) ? 226 : 240;
        int defBot = pkg.equals(AppConstants.WECHAT) ? size.y - 150 : size.y - 100;
        int defCT = defTop + (defBot - defTop) * 3 / 8;
        int defCB = defTop + (defBot - defTop) * 5 / 8;
        topLine = sp.getInt("calib_top_" + pkg, sp.getInt("calib_top", defTop));
        botLine = sp.getInt("calib_bot_" + pkg, sp.getInt("calib_bot", defBot));
        centerTopLine = sp.getInt("calib_centerTop_" + pkg, sp.getInt("calib_centerTop", defCT));
        centerBotLine = sp.getInt("calib_centerBot_" + pkg, sp.getInt("calib_centerBot", defCB));
        Log.i(TAG, "校准线[" + pkg + "]: top=" + topLine + " bot=" + botLine + " centerTop=" + centerTopLine + " centerBot=" + centerBotLine);
        if (wechatNavMode && pkg.equals(AppConstants.WECHAT)) {
            collectWechatNavTabs(root, size.y, size.x);
            if (focusableNodes.isEmpty()) {
                Log.i(TAG, "navTab失效,触发自动提取");
                if (autoExtractByPage(root, size.y, size.x, "nav")) {
                    collectWechatNavTabs(root, size.y, size.x);
                }
            }
            wechatGenericPage = false;
        } else {
            wechatGenericPage = false;
            if (pkg.equals(AppConstants.WECHAT) && isWechatMomentsPage(root)) {
                Log.i(TAG, "微信页面分流: 朋友圈");
                collectWechatMomentsCards(root, size.y, size.x);
                if (!focusableNodes.isEmpty()) {
                    int maxH = 0;
                    for (AccessibilityNodeInfo fn : focusableNodes) {
                        Rect fr = new Rect();
                        fn.getBoundsInScreen(fr);
                        if (fr.height() > maxH) maxH = fr.height();
                    }
                    if (maxH < 200) {
                        Log.i(TAG, "momentsCard节点高度不足(" + maxH + ")，清空重新提取");
                        focusableNodes.clear();
                    }
                }
                if (focusableNodes.isEmpty()) {
                    Log.i(TAG, "momentsCard失效,触发自动提取");
                    if (autoExtractByPage(root, size.y, size.x, "moments")) {
                        collectWechatMomentsCards(root, size.y, size.x);
                    }
                }
            } else {
                collectClickable(root, size.y, size.x, pkg);
                if (pkg.equals(AppConstants.WECHAT) && focusableNodes.isEmpty()) {
                    if (isWechatContactsPage(root)) {
                        Log.i(TAG, "微信页面分流: 通讯录");
                        wechatContactNameMatched = false;
                        collectWechatContactsItems(root, size.y, size.x);
                        if (focusableNodes.isEmpty() || !wechatContactNameMatched) {
                            Log.i(TAG, "contactName/letterIndex失效,触发自动提取");
                            if (autoExtractByPage(root, size.y, size.x, "contacts")) {
                                wechatContactNameMatched = false;
                                collectWechatContactsItems(root, size.y, size.x);
                            }
                        }
                    } else if (NodeHelper.hasNodeWithText(root, "直播", 0) && NodeHelper.hasNodeWithDesc(root, "头像", 0)) {
                        Log.i(TAG, "微信页面分流: 直播");
                        collectWechatLiveCards(root, size.y, size.x);
                    } else {
                        Log.i(TAG, "微信页面分流: 通用 isMoments=" + isWechatMomentsPage(root) + " isContacts=" + isWechatContactsPage(root));
                        collectWechatGenericItems(root, size.y, size.x);
                        wechatGenericPage = true;
                    }
                }
            }
        }
        focusableNodes.sort((a, b) -> {
            Rect ra = new Rect(), rb = new Rect();
            a.getBoundsInScreen(ra);
            b.getBoundsInScreen(rb);
            if (ra.top != rb.top) return ra.top - rb.top;
            return ra.left - rb.left;
        });
        int totalH = 0, count = 0;
        int heightThreshold = pkg.equals(AppConstants.WECHAT) ? 100 : 200;
        for (AccessibilityNodeInfo n : focusableNodes) {
            Rect rr = new Rect();
            n.getBoundsInScreen(rr);
            if (rr.height() > heightThreshold) { totalH += rr.height(); count++; }
        }
        if (count > 0) avgCardHeight = totalH / count;
        if (focusIndex >= focusableNodes.size()) focusIndex = 0;
    }

    private void collectWechatNavTabs(AccessibilityNodeInfo node, int screenH, int screenW) {
        if (node == null) return;
        String viewId = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
        if (WechatIds.isNavTab(viewId) && node.isClickable()) {
            Rect r = new Rect();
            node.getBoundsInScreen(r);
            String tabText = "";
            for (int i = 0; i < node.getChildCount(); i++) {
                AccessibilityNodeInfo child = node.getChild(i);
                if (child.getText() != null) { tabText = child.getText().toString(); break; }
            }
            if (r.width() > 0 && r.height() > 0) {
                Log.i(TAG, "微信导航栏项: " + tabText + " @(" + r.left + "," + r.top + ") " + r.width() + "x" + r.height());
                focusableNodes.add(AccessibilityNodeInfo.obtain(node));
            }
            return;
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            collectWechatNavTabs(node.getChild(i), screenH, screenW);
        }
    }

    private void collectWechatGenericItems(AccessibilityNodeInfo node, int screenH, int screenW) {
        if (node == null) return;
        String viewId = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
        if (node.isClickable() && !WechatIds.isNavTab(viewId)) {
            Rect r = new Rect();
            node.getBoundsInScreen(r);
            if (r.width() > 0 && r.height() > 50 && r.left >= 0 && r.left < screenW
                    && r.top >= topLine && r.bottom <= botLine) {
                boolean hasContent = false;
                if (node.getText() != null && !node.getText().toString().isEmpty()) hasContent = true;
                if (!hasContent && node.getContentDescription() != null && !node.getContentDescription().toString().isEmpty()) hasContent = true;
                if (!hasContent) hasContent = NodeHelper.hasTextOrDescDeep(node, 0);
                if (hasContent) {
                    focusableNodes.add(AccessibilityNodeInfo.obtain(node));
                }
                return;
            }
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            collectWechatGenericItems(node.getChild(i), screenH, screenW);
        }
    }

    private void collectWechatLiveCards(AccessibilityNodeInfo node, int screenH, int screenW) {
        if (node == null) return;
        String viewId = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
        if (node.isClickable() && !WechatIds.isNavTab(viewId)) {
            Rect r = new Rect();
            node.getBoundsInScreen(r);
            if (r.width() > 0 && r.height() > 100 && r.left >= 0 && r.left < screenW
                    && r.bottom > topLine && r.top < botLine) {
                boolean hasContent = false;
                for (int i = 0; i < node.getChildCount(); i++) {
                    AccessibilityNodeInfo child = node.getChild(i);
                    if (child.getText() != null && !child.getText().toString().isEmpty()) { hasContent = true; break; }
                    if (child.getContentDescription() != null && !child.getContentDescription().toString().isEmpty()) { hasContent = true; break; }
                }
                if (hasContent) {
                    focusableNodes.add(AccessibilityNodeInfo.obtain(node));
                }
                return;
            }
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            collectWechatLiveCards(node.getChild(i), screenH, screenW);
        }
    }

    private void collectWechatMomentsCards(AccessibilityNodeInfo node, int screenH, int screenW) {
        if (node == null) return;
        String viewId = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
        Rect r = new Rect();
        node.getBoundsInScreen(r);
        boolean isCard = WechatIds.isMomentsCard(viewId) && node.isClickable();
        boolean isAd = !isCard && node.isClickable() && r.height() > 200
                && (NodeHelper.hasNodeWithText(node, "广告", 0)
                || NodeHelper.hasNodeWithDesc(node, "头像", 0));
        if (isCard || isAd) {
            if (r.width() > 0 && r.height() > 50 && r.left >= 0 && r.left < screenW
                    && r.bottom > topLine && r.top < botLine) {
                boolean hasContent = false;
                if (node.getText() != null && !node.getText().toString().isEmpty()) hasContent = true;
                if (!hasContent && node.getContentDescription() != null && !node.getContentDescription().toString().isEmpty()) hasContent = true;
                if (!hasContent) hasContent = NodeHelper.hasTextOrDescDeep(node, 0);
                if (hasContent) {
                    focusableNodes.add(AccessibilityNodeInfo.obtain(node));
                }
                return;
            }
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            collectWechatMomentsCards(node.getChild(i), screenH, screenW);
        }
    }

    private void collectWechatContactsItems(AccessibilityNodeInfo node, int screenH, int screenW) {
        if (node == null) return;
        String viewId = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
        if (node.isClickable() && !WechatIds.isNavTab(viewId) && !WechatIds.isDrawerItem(viewId)) {
            Rect r = new Rect();
            node.getBoundsInScreen(r);
            if (r.width() > 0 && r.height() > 50 && r.left >= 0 && r.left < screenW
                    && r.bottom > topLine && r.top < botLine) {
                if (NodeHelper.findNodeById(node, WechatIds.letterIndex(), 0) != null) {
                    AccessibilityNodeInfo hg4 = NodeHelper.findNodeById(node, WechatIds.contactName(), 0);
                    if (hg4 != null) {
                        wechatContactNameMatched = true;
                        AccessibilityNodeInfo container = hg4;
                        while (container != null) {
                            AccessibilityNodeInfo p = container.getParent();
                            if (p == null || NodeHelper.findNodeById(p, WechatIds.letterIndex(), 0) != null) break;
                            container = p;
                        }
                        if (container != null) {
                            focusableNodes.add(AccessibilityNodeInfo.obtain(container));
                        }
                    }
                    return;
                }
                AccessibilityNodeInfo hg4 = NodeHelper.findNodeById(node, WechatIds.contactName(), 0);
                if (hg4 != null) {
                    wechatContactNameMatched = true;
                    focusableNodes.add(AccessibilityNodeInfo.obtain(node));
                    return;
                }
                boolean hasContent = false;
                for (int i = 0; i < node.getChildCount(); i++) {
                    AccessibilityNodeInfo child = node.getChild(i);
                    if (child.getText() != null && !child.getText().toString().isEmpty()) { hasContent = true; break; }
                    if (child.getContentDescription() != null && !child.getContentDescription().toString().isEmpty()) { hasContent = true; break; }
                }
                if (hasContent) {
                    focusableNodes.add(AccessibilityNodeInfo.obtain(node));
                }
                return;
            }
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            collectWechatContactsItems(node.getChild(i), screenH, screenW);
        }
    }

    private void collectClickable(AccessibilityNodeInfo node, int screenH, int screenW, String pkg) {
        if (node == null) return;
        if (node.isClickable()) {
            String desc = node.getContentDescription() != null ? node.getContentDescription().toString() : "";
            String viewId = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
            boolean isWechatItem = pkg.equals(AppConstants.WECHAT) && WechatIds.isChatItem(viewId);
            if (desc.contains("观看") || desc.contains("看过") || desc.contains("弹幕")
                    || desc.contains("点赞") || desc.contains("投币") || desc.contains("收藏")
                    || desc.contains("人气值") || desc.contains("UP主")
                    || desc.contains("主播") || desc.contains("专辑")
                    || viewId.contains("card")
                    || isWechatItem
                    || hasChildWithId(node, "main_sound_name")) {
                Rect r = new Rect();
                node.getBoundsInScreen(r);
                if (r.width() > 0 && r.height() > 0 && r.left >= 0 && r.left < screenW) {
                    boolean boundsOk;
                    if (pkg.equals(AppConstants.WECHAT)) {
                        boundsOk = r.top >= topLine && r.bottom <= botLine;
                    } else {
                        boundsOk = r.bottom > topLine && r.top < botLine;
                    }
                    if (boundsOk) {
                        focusableNodes.add(AccessibilityNodeInfo.obtain(node));
                    }
                }
                return;
            }
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            collectClickable(node.getChild(i), screenH, screenW, pkg);
        }
    }

    private boolean hasChildWithId(AccessibilityNodeInfo node, String idPart) {
        return NodeHelper.hasChildWithId(node, idPart);
    }

    public static void scanAllFocusable() {
        if (instance == null) return;
        instance.refreshFocusableNodes();
        int n = instance.focusableNodes.size();
        Log.i(TAG, "====== 焦点扫描: 共" + n + "项 ======");
        for (int i = 0; i < n; i++) {
            AccessibilityNodeInfo node = instance.focusableNodes.get(i);
            Rect r = new Rect();
            node.getBoundsInScreen(r);
            String text = node.getText() != null ? node.getText().toString() : "";
            String desc = node.getContentDescription() != null ? node.getContentDescription().toString() : "";
            String viewId = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
            Log.i(TAG, String.format("[%d] %dx%d @(%d,%d) text=%s desc=%s id=%s",
                i, r.width(), r.height(), r.left, r.top, text, desc, viewId));
        }
        Toast.makeText(instance, "扫描完成: " + n + "项", Toast.LENGTH_LONG).show();
    }

    public static void dumpAllNodes() {
        if (instance == null) return;
        AccessibilityNodeInfo root = instance.getRootInActiveWindow();
        if (root == null) { Log.i(TAG, "dumpAllNodes: root=null"); return; }
        DisplayMetrics dm = instance.getResources().getDisplayMetrics();
        Log.i(TAG, "====== dumpAllNodes dm=" + dm.widthPixels + "x" + dm.heightPixels + " ======");
        instance.dumpNode(root, 0);
        Toast.makeText(instance, "节点已输出到日志", Toast.LENGTH_SHORT).show();
    }

    private void dumpNode(AccessibilityNodeInfo node, int depth) {
        if (node == null) return;
        String indent = "";
        for (int i = 0; i < depth && i < 10; i++) indent += "  ";
        String text = node.getText() != null ? node.getText().toString() : "";
        String desc = node.getContentDescription() != null ? node.getContentDescription().toString() : "";
        String viewId = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
        String cls = node.getClassName() != null ? node.getClassName().toString() : "";
        Rect r = new Rect();
        node.getBoundsInScreen(r);
        Log.i(TAG, indent + "id=[" + viewId + "] cls=[" + cls + "] "
            + r.width() + "x" + r.height() + " @(" + r.left + "," + r.top
            + ") click=" + node.isClickable() + " text=[" + text + "] desc=[" + desc + "]");
        for (int i = 0; i < node.getChildCount(); i++) {
            dumpNode(node.getChild(i), depth + 1);
        }
    }

    public static void performBack() {
        if (instance != null) instance.performGlobalAction(GLOBAL_ACTION_BACK);
    }

    public static String getForegroundPkg() {
        if (instance == null) return "";
        AccessibilityNodeInfo root = instance.getRootInActiveWindow();
        if (root == null || root.getPackageName() == null) return "";
        return root.getPackageName().toString();
    }

    public static boolean isWechatContactDetailPage() {
        if (instance == null) return false;
        AccessibilityNodeInfo root = instance.getRootInActiveWindow();
        if (root == null) return false;
        String pkg = root.getPackageName() != null ? root.getPackageName().toString() : "";
        if (!pkg.equals(AppConstants.WECHAT)) return false;
        return NodeHelper.hasNodeWithText(root, "发消息", 0) && NodeHelper.hasNodeWithText(root, "音视频通话", 0);
    }

    private void focusCurrent() {
        AccessibilityNodeInfo node = focusableNodes.get(focusIndex);
        if (!currentFocusPkg.equals(AppConstants.WECHAT)) {
            node.performAction(AccessibilityNodeInfo.ACTION_FOCUS);
        }
        String info = getNodeInfo(node);
        Rect r = new Rect();
        node.getBoundsInScreen(r);
        String viewId = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
        DisplayMetrics dm = getResources().getDisplayMetrics();
        int screenH = dm.heightPixels;
        Log.i(TAG, "焦点[" + (focusIndex + 1) + "/" + focusableNodes.size() + "] " + r.width() + "x" + r.height() + " @(" + r.left + "," + r.top + ") bottom=" + r.bottom + " topLine=" + topLine + " botLine=" + botLine + " " + info + " id=" + viewId);
        Toast.makeText(this, "[" + (focusIndex + 1) + "/" + focusableNodes.size() + "] " + info, Toast.LENGTH_SHORT).show();
        if (wechatNavMode && currentFocusPkg.equals(AppConstants.WECHAT)) {
            showHighlight(r);
        } else {
            scrollToVisible(node, 0);
        }
    }

    private int lastScrollTop = Integer.MAX_VALUE;
    private int lastScrollBottom = Integer.MAX_VALUE;
    private float scrollUpRatio = 1.8f;
    private float scrollDownRatio = 1.5f;
    private String currentFocusPkg = "";
    private long lastScrollEventTime = 0;
    private Runnable pendingScrollIdleCallback = null;
    private int lastConfirmScrollY = Integer.MIN_VALUE;
    private boolean isRefreshing = false;

    private void checkScrollIdle() {
        if (pendingScrollIdleCallback == null) return;
        long elapsed = System.currentTimeMillis() - lastScrollEventTime;
        if (elapsed < 300) {
            handler.postDelayed(this::checkScrollIdle, 300 - elapsed);
            return;
        }
        AccessibilityNodeInfo root = getRootInActiveWindow();
        int posHash = 0;
        if (root != null) {
            int n = root.getChildCount();
            int limit = Math.min(n, 3);
            for (int i = 0; i < limit; i++) {
                AccessibilityNodeInfo child = root.getChild(i);
                if (child != null) {
                    Rect r = new Rect();
                    child.getBoundsInScreen(r);
                    posHash = posHash * 31 + r.top;
                }
            }
        }
        if (posHash == lastConfirmScrollY) {
            Log.i(TAG, "滑动停止: 事件停" + elapsed + "ms 位置稳定=" + posHash);
            Runnable cb = pendingScrollIdleCallback;
            pendingScrollIdleCallback = null;
            lastConfirmScrollY = Integer.MIN_VALUE;
            cb.run();
        } else {
            lastConfirmScrollY = posHash;
            handler.postDelayed(this::checkScrollIdle, 100);
        }
    }


    private void waitForScrollIdle(Runnable onStable) {
        pendingScrollIdleCallback = onStable;
        lastScrollEventTime = System.currentTimeMillis();
        lastConfirmScrollY = Integer.MIN_VALUE;
        handler.postDelayed(this::checkScrollIdle, 300);
    }

    private void finishHighlight(AccessibilityNodeInfo node, Rect r) {
        currentFocusDesc = getNodeInfo(node);
        showHighlightClipped(r);
    }

    private void scrollToVisible(AccessibilityNodeInfo node, int depth) {
        if (depth >= 10) {
            Log.i(TAG, "滚动深度上限，显示高亮");
            Rect r0 = new Rect();
            node.getBoundsInScreen(r0);
            finishHighlight(node, r0);
            return;
        }
        Rect r = new Rect();
        node.getBoundsInScreen(r);
        int centerCard = (r.top + r.bottom) / 2;
        int targetCenter = (centerTopLine + centerBotLine) / 2;
        int offset = centerCard - targetCenter;
        Log.i(TAG, "scrollToVisible: top=" + r.top + " bottom=" + r.bottom + " h=" + r.height()
            + " topLine=" + topLine + " botLine=" + botLine
            + " centerCard=" + centerCard + " target=" + targetCenter + " offset=" + offset
            + " depth=" + depth);
        boolean inBounds = r.top >= topLine && r.bottom <= botLine;
        boolean heightOk = r.height() >= avgCardHeight * 4 / 5;
        boolean inCenter = centerCard >= centerTopLine && centerCard <= centerBotLine;
        boolean cardTooTall = r.height() > (botLine - topLine);
        boolean isWechat = currentFocusPkg.equals(AppConstants.WECHAT);
        if (cardTooTall && inCenter) {
            if (isWechat) {
                Log.i(TAG, "scrollToVisible: 微信巨长卡片已居中不滑动 h=" + r.height() + " visibleH=" + (botLine - topLine));
                finishHighlight(node, r);
                return;
            }
            if (Math.abs(offset) < avgCardHeight / 3) {
                Log.i(TAG, "scrollToVisible: 巨长卡片中心已对齐不滑动 h=" + r.height() + " offset=" + offset);
                finishHighlight(node, r);
                return;
            }
            Log.i(TAG, "scrollToVisible: 巨长卡片中心偏离继续滑动 h=" + r.height() + " offset=" + offset);
        }
        if (r.top >= centerTopLine && r.bottom <= centerBotLine) {
            finishHighlight(node, r);
            return;
        }
        boolean wechatNoScroll = isWechat && wechatGenericPage && inBounds;
        if (wechatNoScroll || (inBounds && heightOk && inCenter)) {
            Log.i(TAG, "scrollToVisible: 不滑动 inBounds=" + inBounds + " heightOk=" + heightOk
                + " avgH=" + avgCardHeight + (wechatNoScroll ? " (微信)" : " (居中)"));
            finishHighlight(node, r);
            return;
        }
        if (r.top == lastScrollTop && r.bottom == lastScrollBottom) {
            Log.i(TAG, "滚动停滞: top=" + r.top + " bottom=" + r.bottom);
            finishHighlight(node, r);
            return;
        }
        lastScrollTop = r.top;
        lastScrollBottom = r.bottom;
        Point size = new Point();
        ((WindowManager) getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay().getRealSize(size);
        int screenH = size.y;
        boolean scrollUp = offset > 0;
        float ratio = scrollUp ? scrollUpRatio : scrollDownRatio;
        int moveDist = (int)(Math.abs(offset) / ratio);
        if (moveDist < 50) moveDist = 50;
        if (moveDist > screenH * 4 / 5) moveDist = screenH * 4 / 5;
        if (inBounds && (isWechat || !scrollUp) && moveDist > 300) moveDist = 300;
        float swipeRatio = (float) moveDist / screenH;
        Log.i(TAG, "需要滚动: moveDist=" + moveDist + " scrollUp=" + scrollUp + " ratio=" + ratio + " depth=" + depth);
        removeHighlight();
        final int prevCenter = centerCard;
        final int finalMoveDist = moveDist;
        final boolean finalScrollUp = scrollUp;
        Runnable afterSwipe = () -> waitForStable(node, () -> {
            node.refresh();
            Rect r2 = new Rect();
            node.getBoundsInScreen(r2);
            int newCenter = (r2.top + r2.bottom) / 2;
            int actualMove = Math.abs(newCenter - prevCenter);
            if (actualMove > 30 && finalMoveDist > 150) {
                float actualRatio = (float) actualMove / finalMoveDist;
                if (actualRatio > 0.8f && actualRatio < 3.0f) {
                    if (finalScrollUp) {
                            scrollUpRatio = scrollUpRatio * 0.7f + actualRatio * 0.3f;
                        Log.i(TAG, "比例校准: 上 actualRatio=" + String.format("%.2f", actualRatio) + " newRatio=" + String.format("%.2f", scrollUpRatio));
                    } else {
                        scrollDownRatio = scrollDownRatio * 0.7f + actualRatio * 0.3f;
                        Log.i(TAG, "比例校准: 下 actualRatio=" + String.format("%.2f", actualRatio) + " newRatio=" + String.format("%.2f", scrollDownRatio));
                    }
                }
            }
            scrollToVisible(node, depth + 1);
        });
        if (scrollUp) {
            float startY = 0.5f + swipeRatio / 2;
            float endY = 0.5f - swipeRatio / 2;
            if (startY > 0.95f) { startY = 0.95f; endY = startY - swipeRatio; }
            doSwipe(0.5f, startY, 0.5f, endY, afterSwipe);
        } else {
            float startY = 0.5f - swipeRatio / 2;
            float endY = 0.5f + swipeRatio / 2;
            if (endY > 0.95f) { endY = 0.95f; startY = endY - swipeRatio; }
            doSwipe(0.5f, startY, 0.5f, endY, afterSwipe);
        }
    }

    private void waitForStable(AccessibilityNodeInfo node, Runnable onStable) {
        waitForScrollIdle(onStable);
    }

    void showHighlight(Rect r) {
        showHighlight(r, Color.RED);
    }

    void showHighlightClipped(Rect r) {
        Rect c = new Rect(r);
        if (c.top < topLine) c.top = topLine;
        if (c.bottom > botLine) c.bottom = botLine;
        if (c.left < 0) c.left = 0;
        DisplayMetrics dm = getResources().getDisplayMetrics();
        if (c.right > dm.widthPixels) c.right = dm.widthPixels;
        showHighlight(c);
    }

    void showHighlight(Rect r, int color) {
        if (r.width() <= 0 || r.height() <= 0) return;
        if (wm == null) wm = (WindowManager) getSystemService(Context.WINDOW_SERVICE);
        GradientDrawable border = new GradientDrawable();
        border.setStroke(6, color);
        border.setColor(Color.TRANSPARENT);
        if (highlightView == null) {
            highlightView = new View(this);
            highlightView.setBackground(border);
            WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                r.width(), r.height(),
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);
            lp.gravity = Gravity.TOP | Gravity.LEFT;
            lp.x = r.left;
            lp.y = r.top;
            wm.addView(highlightView, lp);
        } else {
            highlightView.setBackground(border);
            WindowManager.LayoutParams lp = (WindowManager.LayoutParams) highlightView.getLayoutParams();
            lp.x = r.left;
            lp.y = r.top;
            lp.width = r.width();
            lp.height = r.height();
            wm.updateViewLayout(highlightView, lp);
        }
    }

    public static void removeHighlight() {
        if (instance == null || instance.highlightView == null || instance.wm == null) return;
        instance.wm.removeView(instance.highlightView);
        instance.highlightView = null;
    }


    private String getNodeInfo(AccessibilityNodeInfo node) {
        return NodeHelper.getNodeInfo(node);
    }

    private String getChildText(AccessibilityNodeInfo node, String idPart) {
        return NodeHelper.getChildText(node, idPart);
    }

    private String getChildTextDeep(AccessibilityNodeInfo node, String idPart) {
        return NodeHelper.getChildTextDeep(node, idPart);
    }

    public static void swipeDown(boolean large) {
        GestureHelper.swipeDown(large);
    }

    public static void swipeUp(boolean large) {
        GestureHelper.swipeUp(large);
    }

    private static int testSwipeMode = 0;
    public static int getTestSwipeMode() { return testSwipeMode; }

    public static void testSwipe() {
        if (instance == null) return;
        String[] modes = {
            "1:doSwipe(0.59→0.39) 500ms",
            "2:doSwipe(0.55→0.35) 200ms快",
            "3:doSwipe(0.55→0.35) 1000ms慢",
            "4:doSwipe(0.50→0.30) 500ms",
            "5:doSwipe(0.65→0.45) 500ms",
            "6:ACTION_SCROLL_FORWARD",
            "7:doSwipe(0.70→0.30) 500ms大",
            "8:doSwipe(0.45→0.65) 下滑500ms",
        };
        int mode = testSwipeMode % modes.length;
        testSwipeMode++;
        Log.i(TAG, "===测试滑动模式 " + modes[mode] + "===");
        switch (mode) {
            case 0: instance.doSwipe(0.5f, 0.59f, 0.5f, 0.39f, null); break;
            case 1: instance.doSwipeDuration(0.5f, 0.55f, 0.5f, 0.35f, 200, null); break;
            case 2: instance.doSwipeDuration(0.5f, 0.55f, 0.5f, 0.35f, 1000, null); break;
            case 3: instance.doSwipe(0.5f, 0.50f, 0.5f, 0.30f, null); break;
            case 4: instance.doSwipe(0.5f, 0.65f, 0.5f, 0.45f, null); break;
            case 5: {
                AccessibilityNodeInfo sn = instance.findNodeByIdRoot("gkp");
                if (sn != null) sn.performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD);
                else Log.w(TAG, "gkp节点未找到");
                break;
            }
            case 6: instance.doSwipe(0.5f, 0.70f, 0.5f, 0.30f, null); break;
            case 7: instance.doSwipe(0.5f, 0.45f, 0.5f, 0.65f, null); break;
        }
    }

    private void doSwipeDuration(float startX, float startY, float endX, float endY, int durationMs, Runnable onComplete) {
        GestureHelper.doSwipeDuration(startX, startY, endX, endY, durationMs, onComplete);
    }


    public static void tapScreen(float xRatio, float yRatio) {
        GestureHelper.tapScreen(xRatio, yRatio);
    }

    private void doSwipe(float startX, float startY, float endX, float endY, Runnable onComplete) {
        GestureHelper.doSwipe(startX, startY, endX, endY, onComplete);
    }

    public static void tryDoubleTapPlayPause() {
        GestureHelper.tryDoubleTapPlayPause();
    }

    private void doTap(float xRatio, float yRatio) {
        GestureHelper.doTap(xRatio, yRatio);
    }

    private void doTap(float xRatio, float yRatio, long durationMs) {
        GestureHelper.doTap(xRatio, yRatio, durationMs);
    }
}

