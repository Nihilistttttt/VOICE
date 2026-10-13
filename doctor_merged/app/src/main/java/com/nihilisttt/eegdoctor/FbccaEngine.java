package com.nihilisttt.eegdoctor;

import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.Deque;

/**
 * Pure-Java, dual-channel (O1+OZ) FBCCA implementation for mode 1.
 *
 * With 2 rows in X, CCA finds the optimal spatial combination of O1 and OZ
 * to maximize correlation with each reference signal. This exploits inter-channel
 * phase/amplitude differences for better SNR and discrimination.
 *
 * The 2x2 eigenvalue problem is solved analytically (quadratic formula),
 * avoiding any SVD dependency.
 */
public final class FbccaEngine {
    private static final int NUM_CH = FbccaConfig.SSVEP_CHANNELS.length;

    private final double[][] ring = new double[NUM_CH][FbccaConfig.WINDOW_SIZE];
    private int ringWrite;
    private int ringCount;
    private int sinceLastUpdate;
    private int sequence;

    private final NotchFilter[] notch = new NotchFilter[NUM_CH];

    private final Deque<Integer> voteHistory = new ArrayDeque<>();
    private final FourierTable fourierTable = new FourierTable(
            FbccaConfig.WINDOW_SIZE,
            FbccaConfig.SAMPLE_RATE,
            6.0,
            45.0);
    private final double[] timeWeights = makeTukeyWeights(FbccaConfig.WINDOW_SIZE, 0.25);

    public FbccaEngine() {
        for (int i = 0; i < NUM_CH; i++) {
            notch[i] = new NotchFilter(
                    FbccaConfig.SAMPLE_RATE,
                    FbccaConfig.NOTCH_FREQ,
                    FbccaConfig.NOTCH_RHO);
        }
    }

    public void reset() {
        for (int c = 0; c < NUM_CH; c++) {
            Arrays.fill(ring[c], 0.0);
            notch[c].reset();
        }
        ringWrite = 0;
        ringCount = 0;
        sinceLastUpdate = 0;
        sequence = 0;
        voteHistory.clear();
    }

    public int getBufferedSamples() {
        return ringCount;
    }

    public int getSamplesUntilNextUpdate() {
        if (ringCount < FbccaConfig.WINDOW_SIZE) {
            return FbccaConfig.WINDOW_SIZE - ringCount;
        }
        return Math.max(0, FbccaConfig.STEP_SIZE - sinceLastUpdate);
    }

    public FbccaOutput pushSample(double[] samplesMv) {
        if (samplesMv == null || samplesMv.length < NUM_CH) return null;
        boolean anyValid = false;
        for (int c = 0; c < NUM_CH; c++) {
            if (Double.isFinite(samplesMv[c])) {
                ring[c][ringWrite] = notch[c].process(samplesMv[c]);
                anyValid = true;
            } else {
                ring[c][ringWrite] = Double.NaN;
            }
        }
        if (!anyValid) return null;
        ringWrite = (ringWrite + 1) % ring[0].length;
        if (ringCount < ring[0].length) ringCount++;
        sinceLastUpdate++;

        if (ringCount < FbccaConfig.WINDOW_SIZE) return null;
        if (sinceLastUpdate < FbccaConfig.STEP_SIZE) return null;
        sinceLastUpdate = 0;
        return analyze(copyWindowChronological());
    }

    private double[][] copyWindowChronological() {
        double[][] out = new double[NUM_CH][ring[0].length];
        int start = ringCount < ring[0].length ? 0 : ringWrite;
        for (int c = 0; c < NUM_CH; c++) {
            for (int i = 0; i < ring[0].length; i++) {
                out[c][i] = ring[c][(start + i) % ring[0].length];
            }
        }
        return out;
    }

    private FbccaOutput analyze(double[][] x) {
        sequence++;
        boolean[] usable = new boolean[NUM_CH];
        int usableCount = 0;
        for (int c = 0; c < NUM_CH; c++) {
            usable[c] = isChannelUsable(x[c]);
            if (usable[c]) usableCount++;
        }
        if (usableCount == 0) {
            voteHistory.clear();
            return new FbccaOutput(sequence, -1, -1, 0.0, 0.0, 0.0,
                    new double[4], new int[4], false);
        }

        int[] usableIdx = new int[usableCount];
        for (int c = 0, j = 0; c < NUM_CH; c++) {
            if (usable[c]) usableIdx[j++] = c;
        }

        for (int c : usableIdx) removeMeanInPlace(x[c]);

        FourierTable.Spectrum[] spectra = new FourierTable.Spectrum[NUM_CH];
        for (int c : usableIdx) {
            spectra[c] = fourierTable.forward(x[c]);
        }

        double[][][] banks = new double[NUM_CH][FbccaConfig.FILTER_BANKS.length][];
        for (int c : usableIdx) {
            for (int b = 0; b < FbccaConfig.FILTER_BANKS.length; b++) {
                banks[c][b] = fourierTable.inverseBand(
                        spectra[c],
                        FbccaConfig.FILTER_BANKS[b][0],
                        FbccaConfig.FILTER_BANKS[b][1]);
                normalizeInPlace(banks[c][b]);
            }
        }

        double[] scores = new double[FbccaConfig.TARGET_FREQS.length];
        for (int targetIndex = 0; targetIndex < FbccaConfig.TARGET_FREQS.length; targetIndex++) {
            double target = FbccaConfig.TARGET_FREQS[targetIndex];
            double bestTotal = -1.0;
            for (double offset : FbccaConfig.FREQ_SEARCH_OFFSETS) {
                double refFreq = target + offset;
                double total = 0.0;
                for (int bankIndex = 0; bankIndex < FbccaConfig.FILTER_BANKS.length; bankIndex++) {
                    double low = FbccaConfig.FILTER_BANKS[bankIndex][0];
                    double high = FbccaConfig.FILTER_BANKS[bankIndex][1];
                    double[][] refs = generateReferences(
                            refFreq,
                            FbccaConfig.WINDOW_SIZE,
                            FbccaConfig.SAMPLE_RATE,
                            FbccaConfig.HARMONICS,
                            low,
                            high);
                    if (refs.length == 0) continue;

                    double[][] xBank = new double[usableCount][];
                    for (int j = 0; j < usableCount; j++) xBank[j] = banks[usableIdx[j]][bankIndex];

                    double rho = canonicalCorrelation(xBank, refs, timeWeights, 1e-6);
                    total += FbccaConfig.filterBankWeight(bankIndex) * rho * rho;
                }
                if (total > bestTotal) bestTotal = total;
            }
            scores[targetIndex] = Math.max(0.0, bestTotal);
        }

        Decision decision = choose(scores);
        int votedIndex = vote(decision.rawIndex);
        int[] counts = voteCounts();
        return new FbccaOutput(sequence,
                decision.rawIndex,
                votedIndex,
                decision.ratio,
                decision.bestScore,
                decision.margin,
                scores,
                counts,
                true);
    }

    private int vote(int rawIndex) {
        if (rawIndex < 0) {
            voteHistory.clear();
            return -1;
        }
        voteHistory.addLast(rawIndex);
        while (voteHistory.size() > FbccaConfig.VOTE_HISTORY_LEN) voteHistory.removeFirst();
        if (voteHistory.size() < FbccaConfig.VOTE_MIN_COUNT) return -1;
        int[] counts = voteCounts();
        int best = -1;
        int bestCount = 0;
        for (int i = 0; i < counts.length; i++) {
            if (counts[i] > bestCount) {
                bestCount = counts[i];
                best = i;
            }
        }
        return bestCount >= FbccaConfig.VOTE_MIN_COUNT ? best : -1;
    }

    private int[] voteCounts() {
        int[] counts = new int[FbccaConfig.TARGET_FREQS.length];
        for (Integer index : voteHistory) {
            if (index != null && index >= 0 && index < counts.length) counts[index]++;
        }
        return counts;
    }

    private static Decision choose(double[] scores) {
        int best = -1;
        int second = -1;
        for (int i = 0; i < scores.length; i++) {
            if (best < 0 || scores[i] > scores[best]) {
                second = best;
                best = i;
            } else if (second < 0 || scores[i] > scores[second]) {
                second = i;
            }
        }
        if (best < 0) return new Decision(-1, 0.0, 0.0, 0.0);
        double bestScore = scores[best];
        double secondScore = second < 0 ? 0.0 : scores[second];
        double ratio = second < 0 ? 999.0 : bestScore / (secondScore + 1e-12);
        double margin = bestScore - secondScore;
        int raw = bestScore >= FbccaConfig.MIN_SCORE
                && ratio >= FbccaConfig.RATIO_THRESHOLD
                && margin >= FbccaConfig.MARGIN_THRESHOLD ? best : -1;
        return new Decision(raw, ratio, bestScore, margin);
    }

    private static boolean isChannelUsable(double[] x) {
        if (x == null || x.length < 10) return false;
        double sum = 0.0;
        double min = Double.POSITIVE_INFINITY;
        double max = Double.NEGATIVE_INFINITY;
        for (double v : x) {
            if (!Double.isFinite(v)) return false;
            sum += v;
            min = Math.min(min, v);
            max = Math.max(max, v);
        }
        double mean = sum / x.length;
        double var = 0.0;
        for (double v : x) {
            double d = v - mean;
            var += d * d;
        }
        double std = Math.sqrt(var / x.length);
        double ptp = max - min;
        return std >= FbccaConfig.CHANNEL_MIN_STD_MV
                && std <= FbccaConfig.CHANNEL_MAX_STD_MV
                && ptp <= FbccaConfig.CHANNEL_MAX_PTP_MV;
    }

    private static void removeMeanInPlace(double[] x) {
        double sum = 0.0;
        for (double v : x) sum += v;
        double mean = sum / x.length;
        for (int i = 0; i < x.length; i++) x[i] -= mean;
    }

    private static void normalizeInPlace(double[] x) {
        removeMeanInPlace(x);
        double sum = 0.0;
        for (double v : x) sum += v * v;
        double std = Math.sqrt(sum / Math.max(1, x.length));
        double scale = std + 1e-8;
        for (int i = 0; i < x.length; i++) x[i] /= scale;
    }

    private static double[][] generateReferences(double freq,
                                                 int n,
                                                 double sampleRate,
                                                 int harmonics,
                                                 double low,
                                                 double high) {
        int rows = 0;
        double nyquist = sampleRate / 2.0;
        for (int h = 1; h <= harmonics; h++) {
            double hf = h * freq;
            if (hf >= low && hf <= high && hf < nyquist) rows += 2;
        }
        double[][] refs = new double[rows][n];
        int row = 0;
        for (int h = 1; h <= harmonics; h++) {
            double hf = h * freq;
            if (hf < low || hf > high || hf >= nyquist) continue;
            for (int i = 0; i < n; i++) {
                double angle = 2.0 * Math.PI * hf * i / sampleRate;
                refs[row][i] = Math.sin(angle);
                refs[row + 1][i] = Math.cos(angle);
            }
            row += 2;
        }
        return refs;
    }

    /**
     * Multi-channel CCA: finds optimal linear combination of X rows to maximize
     * correlation with Y rows. For 2-channel X, solves a 2x2 generalized
     * eigenvalue problem analytically via the quadratic formula.
     *
     * ρ² = max eigenvalue of  Cxx^{-1} Cxy Cyy^{-1} Cxy^T
     *
     * For p=2, the 2x2 matrix M = Cxx^{-1} Cxy Cyy^{-1} Cxy^T has eigenvalues
     * that can be found from trace and determinant without SVD.
     */
    private static double canonicalCorrelation(double[][] xRows,
                                                double[][] yRows,
                                                double[] weights,
                                                double reg) {
        int p = xRows.length;
        int q = yRows.length;
        int n = xRows[0].length;
        if (q == 0 || n < 5 || p < 1) return 0.0;

        if (p == 1) {
            return canonicalCorrSingleX(xRows[0], yRows, weights, reg);
        }

        double weightSum = 0.0;
        for (double w : weights) weightSum += w;

        double[] meanX = new double[p];
        double[] meanY = new double[q];
        for (int i = 0; i < n; i++) {
            double w = weights[i] / weightSum;
            for (int r = 0; r < p; r++) meanX[r] += w * xRows[r][i];
            for (int r = 0; r < q; r++) meanY[r] += w * yRows[r][i];
        }

        double[][] cxx = new double[p][p];
        double[][] cxy = new double[p][q];
        double[][] cyy = new double[q][q];
        for (int i = 0; i < n; i++) {
            double w = weights[i] / weightSum;
            double[] dx = new double[p];
            for (int r = 0; r < p; r++) dx[r] = xRows[r][i] - meanX[r];
            double[] dy = new double[q];
            for (int r = 0; r < q; r++) dy[r] = yRows[r][i] - meanY[r];

            for (int r = 0; r < p; r++) {
                for (int c = r; c < p; c++) cxx[r][c] += w * dx[r] * dx[c];
                for (int c = 0; c < q; c++) cxy[r][c] += w * dx[r] * dy[c];
            }
            for (int r = 0; r < q; r++) {
                for (int c = r; c < q; c++) cyy[r][c] += w * dy[r] * dy[c];
            }
        }
        for (int r = 0; r < p; r++) {
            for (int c = 0; c < r; c++) cxx[r][c] = cxx[c][r];
            cxx[r][r] += reg;
        }
        for (int r = 0; r < q; r++) {
            for (int c = 0; c < r; c++) cyy[r][c] = cyy[c][r];
            cyy[r][r] += reg;
        }

        double[][] invCxx = invert(cxx);
        double[][] invCyy = invert(cyy);
        if (invCxx == null || invCyy == null) return 0.0;

        double[][] tmp = new double[p][q];
        for (int r = 0; r < p; r++) {
            for (int c = 0; c < q; c++) {
                double s = 0.0;
                for (int k = 0; k < p; k++) s += invCxx[r][k] * cxy[k][c];
                tmp[r][c] = s;
            }
        }

        double[][] cyyInvCxyT = new double[q][p];
        for (int r = 0; r < q; r++) {
            for (int c = 0; c < p; c++) {
                double s = 0.0;
                for (int k = 0; k < q; k++) s += invCyy[r][k] * cxy[c][k];
                cyyInvCxyT[r][c] = s;
            }
        }

        double[][] M = new double[p][p];
        for (int r = 0; r < p; r++) {
            for (int c = 0; c < p; c++) {
                double s = 0.0;
                for (int k = 0; k < q; k++) s += tmp[r][k] * cyyInvCxyT[k][c];
                M[r][c] = s;
            }
        }

        double maxEig = maxEigenvalue2x2(M);
        return Math.sqrt(Math.max(0.0, Math.min(1.0, maxEig)));
    }

    private static double maxEigenvalue2x2(double[][] M) {
        int p = M.length;
        if (p == 1) return M[0][0];
        if (p == 2) {
            double trace = M[0][0] + M[1][1];
            double det = M[0][0] * M[1][1] - M[0][1] * M[1][0];
            double disc = trace * trace - 4.0 * det;
            if (disc < 0) disc = 0;
            return (trace + Math.sqrt(disc)) / 2.0;
        }
        double trace = 0.0;
        for (int i = 0; i < p; i++) trace += M[i][i];
        return Math.max(0.0, trace / p);
    }

    /** Single-channel CCA fallback (degenerates to weighted multiple correlation). */
    private static double canonicalCorrSingleX(double[] x,
                                                double[][] y,
                                                double[] weights,
                                                double reg) {
        int n = x.length;
        int q = y.length;
        if (q == 0 || n < 5) return 0.0;
        double weightSum = 0.0;
        for (double w : weights) weightSum += w;

        double meanX = 0.0;
        double[] meanY = new double[q];
        for (int i = 0; i < n; i++) {
            double w = weights[i] / weightSum;
            meanX += w * x[i];
            for (int r = 0; r < q; r++) meanY[r] += w * y[r][i];
        }

        double cxx = 0.0;
        double[] cxy = new double[q];
        double[][] cyy = new double[q][q];
        for (int i = 0; i < n; i++) {
            double w = weights[i] / weightSum;
            double dx = x[i] - meanX;
            cxx += w * dx * dx;
            for (int r = 0; r < q; r++) {
                double dyr = y[r][i] - meanY[r];
                cxy[r] += w * dx * dyr;
                for (int c = r; c < q; c++) {
                    double dyc = y[c][i] - meanY[c];
                    cyy[r][c] += w * dyr * dyc;
                }
            }
        }
        for (int r = 0; r < q; r++) {
            for (int c = 0; c < r; c++) cyy[r][c] = cyy[c][r];
            cyy[r][r] += reg;
        }
        cxx += reg;
        double[][] inv = invert(cyy);
        if (inv == null || cxx <= 0.0) return 0.0;
        double value = 0.0;
        for (int r = 0; r < q; r++) {
            double tmp = 0.0;
            for (int c = 0; c < q; c++) tmp += inv[r][c] * cxy[c];
            value += cxy[r] * tmp;
        }
        value /= cxx;
        return Math.sqrt(Math.max(0.0, Math.min(1.0, value)));
    }

    private static double[][] invert(double[][] source) {
        int n = source.length;
        double[][] a = new double[n][2 * n];
        for (int r = 0; r < n; r++) {
            System.arraycopy(source[r], 0, a[r], 0, n);
            a[r][n + r] = 1.0;
        }
        for (int col = 0; col < n; col++) {
            int pivot = col;
            for (int r = col + 1; r < n; r++) {
                if (Math.abs(a[r][col]) > Math.abs(a[pivot][col])) pivot = r;
            }
            if (Math.abs(a[pivot][col]) < 1e-12) return null;
            if (pivot != col) {
                double[] tmp = a[pivot];
                a[pivot] = a[col];
                a[col] = tmp;
            }
            double divisor = a[col][col];
            for (int c = 0; c < 2 * n; c++) a[col][c] /= divisor;
            for (int r = 0; r < n; r++) {
                if (r == col) continue;
                double factor = a[r][col];
                if (factor == 0.0) continue;
                for (int c = 0; c < 2 * n; c++) a[r][c] -= factor * a[col][c];
            }
        }
        double[][] inv = new double[n][n];
        for (int r = 0; r < n; r++) System.arraycopy(a[r], n, inv[r], 0, n);
        return inv;
    }

    private static double[] makeTukeyWeights(int n, double alpha) {
        double[] w = new double[n];
        if (n == 1) {
            w[0] = 1.0;
            return w;
        }
        double edge = alpha * (n - 1) / 2.0;
        double sum = 0.0;
        for (int i = 0; i < n; i++) {
            double value = 1.0;
            if (i < edge) {
                value = 0.5 * (1.0 + Math.cos(Math.PI * (2.0 * i / (alpha * (n - 1)) - 1.0)));
            } else if (i >= (n - 1) * (1.0 - alpha / 2.0)) {
                value = 0.5 * (1.0 + Math.cos(Math.PI * (2.0 * i / (alpha * (n - 1)) - 2.0 / alpha + 1.0)));
            }
            w[i] = Math.max(value, 1e-8);
            sum += w[i];
        }
        double mean = sum / n;
        for (int i = 0; i < n; i++) w[i] /= mean;
        return w;
    }

    private static final class Decision {
        final int rawIndex;
        final double ratio;
        final double bestScore;
        final double margin;
        Decision(int rawIndex, double ratio, double bestScore, double margin) {
            this.rawIndex = rawIndex;
            this.ratio = ratio;
            this.bestScore = bestScore;
            this.margin = margin;
        }
    }

    /** Cached Fourier basis for the 6-45 Hz region. */
    private static final class FourierTable {
        private final int n;
        private final int[] bins;
        private final double[] binFreqs;
        private final double[][] cos;
        private final double[][] sin;

        FourierTable(int n, double sampleRate, double low, double high) {
            this.n = n;
            int first = (int) Math.ceil(low * n / sampleRate);
            int last = (int) Math.floor(high * n / sampleRate);
            bins = new int[last - first + 1];
            binFreqs = new double[bins.length];
            cos = new double[bins.length][n];
            sin = new double[bins.length][n];
            for (int j = 0; j < bins.length; j++) {
                int k = first + j;
                bins[j] = k;
                binFreqs[j] = k * sampleRate / n;
                for (int i = 0; i < n; i++) {
                    double angle = 2.0 * Math.PI * k * i / n;
                    cos[j][i] = Math.cos(angle);
                    sin[j][i] = Math.sin(angle);
                }
            }
        }

        Spectrum forward(double[] x) {
            double[] re = new double[bins.length];
            double[] im = new double[bins.length];
            for (int j = 0; j < bins.length; j++) {
                double sumRe = 0.0;
                double sumIm = 0.0;
                double[] cj = cos[j];
                double[] sj = sin[j];
                for (int i = 0; i < n; i++) {
                    sumRe += x[i] * cj[i];
                    sumIm -= x[i] * sj[i];
                }
                re[j] = sumRe;
                im[j] = sumIm;
            }
            return new Spectrum(re, im);
        }

        double[] inverseBand(Spectrum spectrum, double low, double high) {
            double[] out = new double[n];
            double factor = 2.0 / n;
            for (int j = 0; j < bins.length; j++) {
                if (binFreqs[j] < low || binFreqs[j] > high) continue;
                double re = spectrum.re[j];
                double im = spectrum.im[j];
                double[] cj = cos[j];
                double[] sj = sin[j];
                for (int i = 0; i < n; i++) {
                    out[i] += factor * (re * cj[i] - im * sj[i]);
                }
            }
            return out;
        }

        static final class Spectrum {
            final double[] re;
            final double[] im;
            Spectrum(double[] re, double[] im) {
                this.re = re;
                this.im = im;
            }
        }
    }
}
