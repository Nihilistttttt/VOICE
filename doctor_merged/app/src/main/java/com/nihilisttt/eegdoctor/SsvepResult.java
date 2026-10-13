package com.nihilisttt.eegdoctor;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Locale;

/** FBCCA result shared by the doctor UI and patient result message. */
public class SsvepResult {
    private final int seq;
    private final String freq;
    private final String rawFreq;
    private final float ratio;
    private final float bestScore;
    private final float margin;
    private final float score11;
    private final float score13;
    private final float score15;
    private final float score17;
    private final int vote11;
    private final int vote13;
    private final int vote15;
    private final int vote17;
    private final boolean synthetic;
    private final boolean channelUsable;

    public SsvepResult(int seq,
                       String freq,
                       String rawFreq,
                       float ratio,
                       float bestScore,
                       float margin,
                       float score11,
                       float score13,
                       float score15,
                       float score17,
                       int vote11,
                       int vote13,
                       int vote15,
                       int vote17,
                       boolean synthetic,
                       boolean channelUsable) {
        this.seq = seq;
        this.freq = freq;
        this.rawFreq = rawFreq;
        this.ratio = ratio;
        this.bestScore = bestScore;
        this.margin = margin;
        this.score11 = score11;
        this.score13 = score13;
        this.score15 = score15;
        this.score17 = score17;
        this.vote11 = vote11;
        this.vote13 = vote13;
        this.vote15 = vote15;
        this.vote17 = vote17;
        this.synthetic = synthetic;
        this.channelUsable = channelUsable;
    }

    public static SsvepResult fromFbcca(FbccaOutput output, boolean synthetic) {
        String voted = indexToFreq(output.votedIndex);
        String raw = indexToFreq(output.rawIndex);
        return new SsvepResult(
                output.sequence,
                voted,
                raw,
                (float) output.ratio,
                (float) output.bestScore,
                (float) output.margin,
                scoreAt(output.scores, 0),
                scoreAt(output.scores, 1),
                scoreAt(output.scores, 2),
                scoreAt(output.scores, 3),
                countAt(output.voteCounts, 0),
                countAt(output.voteCounts, 1),
                countAt(output.voteCounts, 2),
                countAt(output.voteCounts, 3),
                synthetic,
                output.channelUsable);
    }

    private static float scoreAt(double[] values, int index) {
        return values != null && index < values.length ? (float) values[index] : 0f;
    }

    private static int countAt(int[] values, int index) {
        return values != null && index < values.length ? values[index] : 0;
    }

    private static String indexToFreq(int index) {
        if (index < 0 || index >= FbccaConfig.TARGET_FREQS.length) return "UNCERTAIN";
        return String.format(Locale.US, "%.2f", FbccaConfig.TARGET_FREQS[index]);
    }

    public static SsvepResult fromMcu(int seq, int rawIndex, int votedIndex,
                                       float ratio, float bestScore, float margin,
                                       float[] scores, int[] voteCounts) {
        String voted = indexToFreq(votedIndex);
        String raw = indexToFreq(rawIndex);
        return new SsvepResult(
                seq, voted, raw, ratio, bestScore, margin,
                scores != null && scores.length > 0 ? scores[0] : 0f,
                scores != null && scores.length > 1 ? scores[1] : 0f,
                scores != null && scores.length > 2 ? scores[2] : 0f,
                scores != null && scores.length > 3 ? scores[3] : 0f,
                voteCounts != null && voteCounts.length > 0 ? voteCounts[0] : 0,
                voteCounts != null && voteCounts.length > 1 ? voteCounts[1] : 0,
                voteCounts != null && voteCounts.length > 2 ? voteCounts[2] : 0,
                voteCounts != null && voteCounts.length > 3 ? voteCounts[3] : 0,
                false, true);
    }

    public static SsvepResult fromLine(String line) {
        try {
            int seq = 0;
            String freq = "UNCERTAIN";
            String raw = "UNCERTAIN";
            float ratio = 0, best = 0, margin = 0;
            float s11 = 0, s13 = 0, s15 = 0, s17 = 0;
            int v11 = 0, v13 = 0, v15 = 0, v17 = 0;
            boolean synthetic = false;
            boolean usable = true;
            String[] parts = line.split(",");
            for (String part : parts) {
                String[] kv = part.split("=", 2);
                if (kv.length != 2) continue;
                String key = kv[0].trim();
                String value = kv[1].trim();
                switch (key) {
                    case "seq": seq = Integer.parseInt(value); break;
                    case "freq": freq = value; break;
                    case "raw": raw = value; break;
                    case "ratio": ratio = Float.parseFloat(value); break;
                    case "best": best = Float.parseFloat(value); break;
                    case "margin": margin = Float.parseFloat(value); break;
                    case "s11": s11 = Float.parseFloat(value); break;
                    case "s13": s13 = Float.parseFloat(value); break;
                    case "s15": s15 = Float.parseFloat(value); break;
                    case "s17": s17 = Float.parseFloat(value); break;
                    case "v11": v11 = Integer.parseInt(value); break;
                    case "v13": v13 = Integer.parseInt(value); break;
                    case "v15": v15 = Integer.parseInt(value); break;
                    case "v17": v17 = Integer.parseInt(value); break;
                    case "synthetic": synthetic = "1".equals(value) || Boolean.parseBoolean(value); break;
                    case "usable": usable = "1".equals(value) || Boolean.parseBoolean(value); break;
                }
            }
            return new SsvepResult(seq, freq, raw, ratio, best, margin,
                    s11, s13, s15, s17, v11, v13, v15, v17, synthetic, usable);
        } catch (Exception e) {
            return null;
        }
    }

    public String toPatientCommand() {
        return String.format(Locale.US,
                "SSVEP,RESULT,seq=%d,freq=%s,raw=%s,ratio=%.6f,best=%.6f,margin=%.6f,"
                        + "s11=%.6f,s13=%.6f,s15=%.6f,s17=%.6f,"
                        + "v11=%d,v13=%d,v15=%d,v17=%d,synthetic=%d,usable=%d",
                seq, freq, rawFreq, ratio, bestScore, margin,
                score11, score13, score15, score17,
                vote11, vote13, vote15, vote17,
                synthetic ? 1 : 0, channelUsable ? 1 : 0);
    }

    /** 患者端 0x42 载荷（36 字节）：seq u32 + freqIdx u8 + rawIdx u8 + [ratio,margin,4×score] float + 4×vote u8 + synthetic + usable。 */
    public byte[] toPatientData() {
        ByteBuffer bb = ByteBuffer.allocate(36).order(ByteOrder.LITTLE_ENDIAN);
        bb.putInt(seq);
        bb.put((byte) (getFreqIndex() >= 0 ? getFreqIndex() : 0xFF));
        bb.put((byte) (getRawFreqIndex() >= 0 ? getRawFreqIndex() : 0xFF));
        bb.putFloat(ratio);
        bb.putFloat(margin);
        bb.putFloat(score11);
        bb.putFloat(score13);
        bb.putFloat(score15);
        bb.putFloat(score17);
        bb.put((byte) vote11);
        bb.put((byte) vote13);
        bb.put((byte) vote15);
        bb.put((byte) vote17);
        bb.put((byte) (synthetic ? 1 : 0));
        bb.put((byte) (channelUsable ? 1 : 0));
        return bb.array();
    }

    public int getSeq() { return seq; }
    public String getFreq() { return freq; }
    public String getRawFreq() { return rawFreq; }
    public float getRatio() { return ratio; }
    public float getBestScore() { return bestScore; }
    public float getMargin() { return margin; }
    public float getScore11() { return score11; }
    public float getScore13() { return score13; }
    public float getScore15() { return score15; }
    public float getScore17() { return score17; }
    public int getVote11() { return vote11; }
    public int getVote13() { return vote13; }
    public int getVote15() { return vote15; }
    public int getVote17() { return vote17; }
    public boolean isSynthetic() { return synthetic; }
    public boolean isChannelUsable() { return channelUsable; }

    public int getFreqIndex() {
        return freqToIndex(freq);
    }

    public int getRawFreqIndex() {
        return freqToIndex(rawFreq);
    }

    private static int freqToIndex(String value) {
        if (value == null) return -1;
        try {
            double parsed = Double.parseDouble(value);
            for (int i = 0; i < FbccaConfig.TARGET_FREQS.length; i++) {
                if (Math.abs(parsed - FbccaConfig.TARGET_FREQS[i]) < 0.2) return i;
            }
        } catch (NumberFormatException ignored) {}
        return -1;
    }

    public boolean isUncertain() {
        return "UNCERTAIN".equals(freq);
    }
}
