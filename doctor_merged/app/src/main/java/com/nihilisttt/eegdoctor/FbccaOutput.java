package com.nihilisttt.eegdoctor;

import java.util.Arrays;

/** One FBCCA update, including raw decision and voted decision. */
public final class FbccaOutput {
    public final int sequence;
    public final int rawIndex;
    public final int votedIndex;
    public final double ratio;
    public final double bestScore;
    public final double margin;
    public final double[] scores;
    public final int[] voteCounts;
    public final boolean channelUsable;

    public FbccaOutput(int sequence,
                       int rawIndex,
                       int votedIndex,
                       double ratio,
                       double bestScore,
                       double margin,
                       double[] scores,
                       int[] voteCounts,
                       boolean channelUsable) {
        this.sequence = sequence;
        this.rawIndex = rawIndex;
        this.votedIndex = votedIndex;
        this.ratio = ratio;
        this.bestScore = bestScore;
        this.margin = margin;
        this.scores = scores == null ? new double[4] : Arrays.copyOf(scores, scores.length);
        this.voteCounts = voteCounts == null ? new int[4] : Arrays.copyOf(voteCounts, voteCounts.length);
        this.channelUsable = channelUsable;
    }
}
