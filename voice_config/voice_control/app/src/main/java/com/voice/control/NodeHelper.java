package com.voice.control;

import android.graphics.Rect;
import android.view.accessibility.AccessibilityNodeInfo;

public class NodeHelper {

    public static boolean hasNodeWithDesc(AccessibilityNodeInfo node, String descPart, int depth) {
        if (node == null || depth > 30) return false;
        String desc = node.getContentDescription() != null ? node.getContentDescription().toString() : "";
        if (desc.contains(descPart)) return true;
        for (int i = 0; i < node.getChildCount(); i++) {
            if (hasNodeWithDesc(node.getChild(i), descPart, depth + 1)) return true;
        }
        return false;
    }

    public static boolean hasNodeWithText(AccessibilityNodeInfo node, String textPart, int depth) {
        if (node == null || depth > 30) return false;
        String text = node.getText() != null ? node.getText().toString() : "";
        if (text.contains(textPart)) return true;
        for (int i = 0; i < node.getChildCount(); i++) {
            if (hasNodeWithText(node.getChild(i), textPart, depth + 1)) return true;
        }
        return false;
    }

    public static boolean hasNodeWithTextInTop(AccessibilityNodeInfo node, String textPart, int maxTop, int depth) {
        if (node == null || depth > 30) return false;
        String text = node.getText() != null ? node.getText().toString() : "";
        if (text.contains(textPart)) {
            Rect r = new Rect();
            node.getBoundsInScreen(r);
            if (r.top < maxTop) return true;
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            if (hasNodeWithTextInTop(node.getChild(i), textPart, maxTop, depth + 1)) return true;
        }
        return false;
    }

    public static AccessibilityNodeInfo findNodeWithDesc(AccessibilityNodeInfo node, String descPart, int depth) {
        if (node == null || depth > 30) return null;
        String desc = node.getContentDescription() != null ? node.getContentDescription().toString() : "";
        if (desc.contains(descPart)) return node;
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo result = findNodeWithDesc(node.getChild(i), descPart, depth + 1);
            if (result != null) return result;
        }
        return null;
    }

    public static AccessibilityNodeInfo findNodeWithText(AccessibilityNodeInfo node, String textPart, int depth) {
        if (node == null || depth > 30) return null;
        String text = node.getText() != null ? node.getText().toString() : "";
        if (text.contains(textPart)) return node;
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo result = findNodeWithText(node.getChild(i), textPart, depth + 1);
            if (result != null) return result;
        }
        return null;
    }

    public static boolean findNodeInScreen(AccessibilityNodeInfo node, String idPart, int depth) {
        if (node == null || depth > 6) return false;
        String vid = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
        if (vid.contains(":id/" + idPart)) {
            Rect r = new Rect();
            node.getBoundsInScreen(r);
            if (r.top > 0 && r.height() > 0) return true;
        }
        for (int i = 0; i < node.getChildCount(); i++) {
            if (findNodeInScreen(node.getChild(i), idPart, depth + 1)) return true;
        }
        return false;
    }

    public static AccessibilityNodeInfo findNodeById(AccessibilityNodeInfo node, String idPart, int depth) {
        if (node == null || depth > 8) return null;
        String vid = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
        if (vid.contains(":id/" + idPart)) return node;
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo result = findNodeById(node.getChild(i), idPart, depth + 1);
            if (result != null) return result;
        }
        return null;
    }

    public static boolean findNodeById(AccessibilityNodeInfo node, String idPart) {
        if (node == null) return false;
        String vid = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
        if (vid.contains(idPart)) return true;
        for (int i = 0; i < node.getChildCount(); i++) {
            if (findNodeById(node.getChild(i), idPart)) return true;
        }
        return false;
    }

    public static AccessibilityNodeInfo findNodeByIdReturn(AccessibilityNodeInfo node, String idPart) {
        if (node == null) return null;
        String vid = node.getViewIdResourceName() != null ? node.getViewIdResourceName() : "";
        if (vid.contains(idPart)) return node;
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo found = findNodeByIdReturn(node.getChild(i), idPart);
            if (found != null) return found;
        }
        return null;
    }

    public static AccessibilityNodeInfo findClickableParent(AccessibilityNodeInfo node) {
        AccessibilityNodeInfo current = node;
        while (current != null) {
            if (current.isClickable()) return current;
            current = current.getParent();
        }
        return null;
    }

    public static boolean hasChildWithId(AccessibilityNodeInfo node, String idPart) {
        if (node == null) return false;
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo child = node.getChild(i);
            if (child == null) continue;
            String vid = child.getViewIdResourceName() != null ? child.getViewIdResourceName() : "";
            if (vid.contains(idPart)) return true;
        }
        return false;
    }

    public static boolean hasTextOrDescDeep(AccessibilityNodeInfo node, int depth) {
        if (node == null || depth > 15) return false;
        if (node.getText() != null && !node.getText().toString().isEmpty()) return true;
        if (node.getContentDescription() != null && !node.getContentDescription().toString().isEmpty()) return true;
        for (int i = 0; i < node.getChildCount(); i++) {
            if (hasTextOrDescDeep(node.getChild(i), depth + 1)) return true;
        }
        return false;
    }

    public static String getChildText(AccessibilityNodeInfo node, String idPart) {
        if (node == null) return "";
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo child = node.getChild(i);
            if (child == null) continue;
            String vid = child.getViewIdResourceName() != null ? child.getViewIdResourceName() : "";
            if (vid.contains(idPart)) {
                String t = child.getText() != null ? child.getText().toString() : "";
                if (!t.isEmpty()) return t;
            }
        }
        return "";
    }

    public static String getChildTextDeep(AccessibilityNodeInfo node, String idPart) {
        if (node == null) return "";
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo child = node.getChild(i);
            if (child == null) continue;
            String vid = child.getViewIdResourceName() != null ? child.getViewIdResourceName() : "";
            if (vid.contains(idPart)) {
                String t = child.getText() != null ? child.getText().toString() : "";
                if (!t.isEmpty()) return t;
            }
            String deep = getChildTextDeep(child, idPart);
            if (!deep.isEmpty()) return deep;
        }
        return "";
    }

    public static String getNodeInfo(AccessibilityNodeInfo node) {
        String text = node.getText() != null ? node.getText().toString() : "";
        String desc = node.getContentDescription() != null ? node.getContentDescription().toString() : "";
        String info = text.isEmpty() ? desc : text;
        if (info.isEmpty()) {
            info = getChildText(node, "main_sound_name");
            if (info.isEmpty()) info = getChildText(node, "main_tv_order_no");
            if (info.isEmpty()) info = getChildTextDeep(node, "otg");
            if (info.isEmpty()) {
                Rect r = new Rect();
                node.getBoundsInScreen(r);
                info = "@" + r.top + "x" + r.left;
            }
        }
        return info;
    }
}