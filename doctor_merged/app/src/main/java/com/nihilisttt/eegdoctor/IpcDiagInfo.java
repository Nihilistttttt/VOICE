package com.nihilisttt.eegdoctor;

public class IpcDiagInfo {
    private int ack;
    private int notify;
    private int ok;
    private int bad;
    private int v5fhb;
    private int wfiWake;
    private int active;
    private int ena;
    private int sts;
    private int isr;
    private long timestamp;

    public IpcDiagInfo() {
        this.timestamp = System.currentTimeMillis();
    }

    public int getAck() { return ack; }
    public void setAck(int ack) { this.ack = ack; }

    public int getNotify() { return notify; }
    public void setNotify(int notify) { this.notify = notify; }

    public int getOk() { return ok; }
    public void setOk(int ok) { this.ok = ok; }

    public int getBad() { return bad; }
    public void setBad(int bad) { this.bad = bad; }

    public int getV5fhb() { return v5fhb; }
    public void setV5fhb(int v5fhb) { this.v5fhb = v5fhb; }

    public int getWfiWake() { return wfiWake; }
    public void setWfiWake(int wfiWake) { this.wfiWake = wfiWake; }

    public int getActive() { return active; }
    public void setActive(int active) { this.active = active; }

    public int getEna() { return ena; }
    public void setEna(int ena) { this.ena = ena; }

    public int getSts() { return sts; }
    public void setSts(int sts) { this.sts = sts; }

    public int getIsr() { return isr; }
    public void setIsr(int isr) { this.isr = isr; }

    public long getTimestamp() { return timestamp; }

    public float getParseOkRate() {
        int total = ok + bad;
        return total > 0 ? (float) ok / total * 100f : 0f;
    }


}