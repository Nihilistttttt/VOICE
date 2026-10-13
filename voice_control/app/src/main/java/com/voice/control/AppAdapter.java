package com.voice.control;

public abstract class AppAdapter {
    protected MainActivity activity;
    protected String pkgName;

    public AppAdapter(MainActivity activity, String pkgName) {
        this.activity = activity;
        this.pkgName = pkgName;
    }

    public String getPackageName() { return pkgName; }
    public abstract boolean handleCommand(int cmd);
    protected void setStatus(String s) { activity.setStatusText(s); }
    protected void sendMediaKey(int keyCode, String name) { activity.sendMediaKey(keyCode, name); }
    protected void returnToSelect() { activity.returnToSelect(); }
}