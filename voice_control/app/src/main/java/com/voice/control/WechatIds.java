package com.voice.control;

import android.content.Context;
import android.content.SharedPreferences;

public class WechatIds {
    private static final String SP_NAME = "wechat_ids";
    private static SharedPreferences sp;

    public static final String KEY_CHAT_ITEM_NEW = "chat_item_new";
    public static final String KEY_CHAT_ITEM_OLD = "chat_item_old";
    public static final String KEY_NAV_TAB = "nav_tab";
    public static final String KEY_MOMENTS_CARD = "moments_card";
    public static final String KEY_CONTACT_NAME = "contact_name";
    public static final String KEY_LETTER_INDEX = "letter_index";
    public static final String KEY_ROOT_FRAME = "root_frame";
    public static final String KEY_SCROLL_LIST = "scroll_list";
    public static final String KEY_DRAWER_ITEM = "drawer_item";
    public static final String KEY_MORE_FUNC_BTN = "more_func_btn";

    public static final String DEFAULT_CHAT_ITEM_NEW = "bth";
    public static final String DEFAULT_CHAT_ITEM_OLD = "cj1";
    public static final String DEFAULT_NAV_TAB = "kd_";
    public static final String DEFAULT_MOMENTS_CARD = "nq_";
    public static final String DEFAULT_CONTACT_NAME = "hg4";
    public static final String DEFAULT_LETTER_INDEX = "bqq";
    public static final String DEFAULT_ROOT_FRAME = "kai";
    public static final String DEFAULT_SCROLL_LIST = "gkp";
    public static final String DEFAULT_DRAWER_ITEM = "iwg";
    public static final String DEFAULT_MORE_FUNC_BTN = "b3q";

    public static void init(Context context) {
        sp = context.getSharedPreferences(SP_NAME, Context.MODE_PRIVATE);
    }

    private static String get(String key, String defaultVal) {
        if (sp == null) return defaultVal;
        return sp.getString(key, defaultVal);
    }

    private static void put(String key, String val) {
        if (sp == null) return;
        sp.edit().putString(key, val).apply();
    }

    public static String chatItemNew() { return get(KEY_CHAT_ITEM_NEW, DEFAULT_CHAT_ITEM_NEW); }
    public static String chatItemOld() { return get(KEY_CHAT_ITEM_OLD, DEFAULT_CHAT_ITEM_OLD); }
    public static String navTab() { return get(KEY_NAV_TAB, DEFAULT_NAV_TAB); }
    public static String momentsCard() { return get(KEY_MOMENTS_CARD, DEFAULT_MOMENTS_CARD); }
    public static String contactName() { return get(KEY_CONTACT_NAME, DEFAULT_CONTACT_NAME); }
    public static String letterIndex() { return get(KEY_LETTER_INDEX, DEFAULT_LETTER_INDEX); }
    public static String rootFrame() { return get(KEY_ROOT_FRAME, DEFAULT_ROOT_FRAME); }
    public static String scrollList() { return get(KEY_SCROLL_LIST, DEFAULT_SCROLL_LIST); }
    public static String drawerItem() { return get(KEY_DRAWER_ITEM, DEFAULT_DRAWER_ITEM); }
    public static String moreFuncBtn() { return get(KEY_MORE_FUNC_BTN, DEFAULT_MORE_FUNC_BTN); }

    public static void setChatItemNew(String id) { put(KEY_CHAT_ITEM_NEW, id); }
    public static void setNavTab(String id) { put(KEY_NAV_TAB, id); }
    public static void setMomentsCard(String id) { put(KEY_MOMENTS_CARD, id); }
    public static void setContactName(String id) { put(KEY_CONTACT_NAME, id); }
    public static void setLetterIndex(String id) { put(KEY_LETTER_INDEX, id); }
    public static void setRootFrame(String id) { put(KEY_ROOT_FRAME, id); }
    public static void setScrollList(String id) { put(KEY_SCROLL_LIST, id); }
    public static void setDrawerItem(String id) { put(KEY_DRAWER_ITEM, id); }
    public static void setMoreFuncBtn(String id) { put(KEY_MORE_FUNC_BTN, id); }

    public static boolean isChatItem(String viewId) {
        return viewId.contains(":id/" + chatItemNew()) || viewId.contains(":id/" + chatItemOld());
    }

    public static boolean isNavTab(String viewId) {
        return viewId.contains(":id/" + navTab());
    }

    public static boolean isMomentsCard(String viewId) {
        return viewId.contains(":id/" + momentsCard());
    }

    public static boolean isDrawerItem(String viewId) {
        return viewId.contains(":id/" + drawerItem());
    }

    public static String getAllIds() {
        return "chatItem=" + chatItemNew() + "/" + chatItemOld()
            + " navTab=" + navTab()
            + " momentsCard=" + momentsCard()
            + " contactName=" + contactName()
            + " letterIndex=" + letterIndex()
            + " rootFrame=" + rootFrame()
            + " scrollList=" + scrollList()
            + " drawerItem=" + drawerItem()
            + " moreFuncBtn=" + moreFuncBtn();
    }
}