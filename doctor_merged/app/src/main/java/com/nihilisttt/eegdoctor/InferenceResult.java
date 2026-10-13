package com.nihilisttt.eegdoctor;

public class InferenceResult {
    private int window;
    private int dtMs;
    private int winRows;
    private String intent;
    private float scoreLeft;
    private float scoreRight;
    private float confidence;
    private boolean trained;
    private long timestamp;
    private String groundTruth;

    public InferenceResult() {
        this.timestamp = System.currentTimeMillis();
    }

    public String getGroundTruth() { return groundTruth; }
    public void setGroundTruth(String groundTruth) { this.groundTruth = groundTruth; }

    public int getWindow() { return window; }
    public void setWindow(int window) { this.window = window; }

    public int getDtMs() { return dtMs; }
    public void setDtMs(int dtMs) { this.dtMs = dtMs; }

    public int getWinRows() { return winRows; }
    public void setWinRows(int winRows) { this.winRows = winRows; }

    public String getIntent() { return intent; }
    public void setIntent(String intent) { this.intent = intent; }

    public float getScoreLeft() { return scoreLeft; }
    public void setScoreLeft(float scoreLeft) { this.scoreLeft = scoreLeft; }

    public float getScoreRight() { return scoreRight; }
    public void setScoreRight(float scoreRight) { this.scoreRight = scoreRight; }

    public float getConfidence() { return confidence; }
    public void setConfidence(float confidence) { this.confidence = confidence; }

    public boolean isTrained() { return trained; }
    public void setTrained(boolean trained) { this.trained = trained; }

    public long getTimestamp() { return timestamp; }
    public void setTimestamp(long timestamp) { this.timestamp = timestamp; }

}
