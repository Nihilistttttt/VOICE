package com.nihilisttt.eegdoctor;

import java.util.ArrayList;
import java.util.List;

public class DirLdaTrainer {
    public static final int FEATURE_DIM = 24;
    public static final int MODEL_FEATURE_DIM = 14;
    public static final int LEFT_CLASS = 0;
    public static final int RIGHT_CLASS = 1;

    public static class Sample {
        public final float[] features;
        public final int label;
        public Sample(float[] features, int label) {
            this.features = features;
            this.label = label;
        }
    }

    public static class TrainResult {
        public float[] mean = new float[FEATURE_DIM];
        public float[] scale = new float[FEATURE_DIM];
        public float[] weight = new float[FEATURE_DIM];
        public float bias;
        public float balancedAccuracy;
        public float shrinkage;
        public int leftCount;
        public int rightCount;
        public int correctCount;
        public int totalCount;
        public boolean valid;
        public String errorMessage;
    }

    public static int normalizeLabel(int raw) {
        if (raw == 2 || raw == LEFT_CLASS) return LEFT_CLASS;
        if (raw == 3 || raw == RIGHT_CLASS) return RIGHT_CLASS;
        return -1;
    }

    public static TrainResult train(List<Sample> samples) {
        TrainResult result = new TrainResult();
        if (samples == null || samples.size() < 4) {
            result.errorMessage = "样本不足，至少需要4个样本";
            return result;
        }

        int n = samples.size();
        int leftCount = 0, rightCount = 0;
        for (Sample s : samples) {
            if (s.label == LEFT_CLASS) leftCount++;
            else if (s.label == RIGHT_CLASS) rightCount++;
        }
        result.leftCount = leftCount;
        result.rightCount = rightCount;
        if (leftCount < 2 || rightCount < 2) {
            result.errorMessage = "每类至少需要2个样本 (LEFT=" + leftCount + ", RIGHT=" + rightCount + ")";
            return result;
        }

        double[][] X = new double[n][MODEL_FEATURE_DIM];
        int[] y = new int[n];
        for (int i = 0; i < n; i++) {
            Sample s = samples.get(i);
            for (int j = 0; j < MODEL_FEATURE_DIM; j++) {
                X[i][j] = s.features[j];
            }
            y[i] = s.label;
        }

        double[] scalerMean = new double[MODEL_FEATURE_DIM];
        double[] scalerScale = new double[MODEL_FEATURE_DIM];
        computeStandardScaler(X, scalerMean, scalerScale);

        double[][] Xstd = new double[n][MODEL_FEATURE_DIM];
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < MODEL_FEATURE_DIM; j++) {
                Xstd[i][j] = (X[i][j] - scalerMean[j]) / scalerScale[j];
            }
        }

        double[] mean0 = new double[MODEL_FEATURE_DIM];
        double[] mean1 = new double[MODEL_FEATURE_DIM];
        computeClassMeans(Xstd, y, mean0, mean1);

        double[][] cov = computeSklearnPooledCovariance(Xstd, y, result);

        double[] diff0 = new double[MODEL_FEATURE_DIM];
        for (int j = 0; j < MODEL_FEATURE_DIM; j++) {
            diff0[j] = mean1[j] - mean0[j];
        }
        double[] sw0 = solveLinearSystem(cov, diff0);

        double[] coef0 = sw0;
        double intercept0 = -0.5 * (dot(mean0, sw0) + dot(mean1, sw0));

        for (int j = 0; j < FEATURE_DIM; j++) {
            result.mean[j] = (j < MODEL_FEATURE_DIM) ? (float) scalerMean[j] : 0f;
            result.scale[j] = (j < MODEL_FEATURE_DIM) ? (float) scalerScale[j] : 1f;
            result.weight[j] = (j < MODEL_FEATURE_DIM) ? (float) coef0[j] : 0f;
        }
        result.bias = (float) intercept0;

        int correct = 0;
        for (int i = 0; i < n; i++) {
            double margin = intercept0;
            for (int j = 0; j < MODEL_FEATURE_DIM; j++) {
                margin += coef0[j] * Xstd[i][j];
            }
            int pred = (margin >= 0) ? RIGHT_CLASS : LEFT_CLASS;
            if (pred == y[i]) correct++;
        }
        result.correctCount = correct;
        result.totalCount = n;
        float leftAcc = 0f, rightAcc = 0f;
        int leftCorrect = 0, rightCorrect = 0;
        for (int i = 0; i < n; i++) {
            double margin = intercept0;
            for (int j = 0; j < MODEL_FEATURE_DIM; j++) {
                margin += coef0[j] * Xstd[i][j];
            }
            int pred = (margin >= 0) ? RIGHT_CLASS : LEFT_CLASS;
            if (y[i] == LEFT_CLASS && pred == LEFT_CLASS) leftCorrect++;
            if (y[i] == RIGHT_CLASS && pred == RIGHT_CLASS) rightCorrect++;
        }
        leftAcc = (float) leftCorrect / leftCount;
        rightAcc = (float) rightCorrect / rightCount;
        result.balancedAccuracy = 0.5f * (leftAcc + rightAcc);
        result.valid = true;
        return result;
    }

    public static byte[] packWeightPayload(TrainResult result) {
        byte[] payload = new byte[FEATURE_DIM * 4 * 3 + 4];
        int offset = 0;
        for (int i = 0; i < FEATURE_DIM; i++) {
            int bits = Float.floatToIntBits(result.mean[i]);
            payload[offset++] = (byte) (bits & 0xFF);
            payload[offset++] = (byte) ((bits >> 8) & 0xFF);
            payload[offset++] = (byte) ((bits >> 16) & 0xFF);
            payload[offset++] = (byte) ((bits >> 24) & 0xFF);
        }
        for (int i = 0; i < FEATURE_DIM; i++) {
            int bits = Float.floatToIntBits(result.scale[i]);
            payload[offset++] = (byte) (bits & 0xFF);
            payload[offset++] = (byte) ((bits >> 8) & 0xFF);
            payload[offset++] = (byte) ((bits >> 16) & 0xFF);
            payload[offset++] = (byte) ((bits >> 24) & 0xFF);
        }
        for (int i = 0; i < FEATURE_DIM; i++) {
            int bits = Float.floatToIntBits(result.weight[i]);
            payload[offset++] = (byte) (bits & 0xFF);
            payload[offset++] = (byte) ((bits >> 8) & 0xFF);
            payload[offset++] = (byte) ((bits >> 16) & 0xFF);
            payload[offset++] = (byte) ((bits >> 24) & 0xFF);
        }
        int bits = Float.floatToIntBits(result.bias);
        payload[offset++] = (byte) (bits & 0xFF);
        payload[offset++] = (byte) ((bits >> 8) & 0xFF);
        payload[offset++] = (byte) ((bits >> 16) & 0xFF);
        payload[offset++] = (byte) ((bits >> 24) & 0xFF);
        return payload;
    }

    private static void computeStandardScaler(double[][] X, double[] mean, double[] scale) {
        int n = X.length;
        int d = X[0].length;
        for (int j = 0; j < d; j++) {
            double sum = 0;
            for (int i = 0; i < n; i++) sum += X[i][j];
            mean[j] = sum / n;
        }
        for (int j = 0; j < d; j++) {
            double sumSq = 0;
            for (int i = 0; i < n; i++) {
                double diff = X[i][j] - mean[j];
                sumSq += diff * diff;
            }
            double std = Math.sqrt(sumSq / n);
            scale[j] = (std < 1e-12) ? 1.0 : std;
        }
    }

    private static void computeClassMeans(double[][] Xstd, int[] y, double[] mean0, double[] mean1) {
        int d = Xstd[0].length;
        int n0 = 0, n1 = 0;
        for (int j = 0; j < d; j++) { mean0[j] = 0; mean1[j] = 0; }
        for (int i = 0; i < Xstd.length; i++) {
            if (y[i] == LEFT_CLASS) {
                for (int j = 0; j < d; j++) mean0[j] += Xstd[i][j];
                n0++;
            } else {
                for (int j = 0; j < d; j++) mean1[j] += Xstd[i][j];
                n1++;
            }
        }
        for (int j = 0; j < d; j++) {
            mean0[j] /= n0;
            mean1[j] /= n1;
        }
    }

    private static double[][] computePooledCovariance(double[][] Xstd, int[] y,
            double[] mean0, double[] mean1, int n) {
        int d = Xstd[0].length;
        double[][] cov = new double[d][d];
        for (int i = 0; i < n; i++) {
            double[] clsMean = (y[i] == LEFT_CLASS) ? mean0 : mean1;
            for (int j = 0; j < d; j++) {
                double dj = Xstd[i][j] - clsMean[j];
                for (int k = 0; k < d; k++) {
                    double dk = Xstd[i][k] - clsMean[k];
                    cov[j][k] += dj * dk;
                }
            }
        }
        int nClasses = 2;
        for (int j = 0; j < d; j++) {
            for (int k = 0; k < d; k++) {
                cov[j][k] /= (n - nClasses);
            }
        }
        return cov;
    }

    private static double[][] computeSklearnPooledCovariance(double[][] Xstd, int[] y, TrainResult result) {
        int d = Xstd[0].length;
        int n = Xstd.length;
        double[][] cov = new double[d][d];
        double shrSum = 0;
        for (int cls = 0; cls < 2; cls++) {
            int nc = 0;
            for (int i = 0; i < n; i++) if (y[i] == cls) nc++;
            double[][] Xg = new double[nc][d];
            int idx = 0;
            for (int i = 0; i < n; i++) {
                if (y[i] == cls) {
                    for (int j = 0; j < d; j++) Xg[idx][j] = Xstd[i][j];
                    idx++;
                }
            }
            double[] cgMean = new double[d];
            double[] cgScale = new double[d];
            computeStandardScaler(Xg, cgMean, cgScale);
            double[][] XgStd = new double[nc][d];
            for (int i = 0; i < nc; i++) {
                for (int j = 0; j < d; j++) XgStd[i][j] = (Xg[i][j] - cgMean[j]) / cgScale[j];
            }
            double shr = computeLedoitWolfShrinkage(XgStd, nc);
            shrSum += shr;
            double[][] empCov = new double[d][d];
            for (int i = 0; i < nc; i++) {
                for (int j = 0; j < d; j++) {
                    for (int k = 0; k < d; k++) empCov[j][k] += XgStd[i][j] * XgStd[i][k];
                }
            }
            for (int j = 0; j < d; j++) {
                for (int k = 0; k < d; k++) empCov[j][k] /= nc;
            }
            double mu = 0;
            for (int j = 0; j < d; j++) mu += empCov[j][j];
            mu /= d;
            for (int j = 0; j < d; j++) {
                for (int k = 0; k < d; k++) {
                    double shrunk = (1 - shr) * empCov[j][k];
                    if (j == k) shrunk += shr * mu;
                    cov[j][k] += 0.5 * cgScale[j] * shrunk * cgScale[k];
                }
            }
        }
        if (result != null) result.shrinkage = (float) (shrSum / 2.0);
        return cov;
    }

    private static double computeLedoitWolfShrinkage(double[][] Xstd, int n) {
        int d = Xstd[0].length;
        double[] empDiag = new double[d];
        for (int j = 0; j < d; j++) {
            double s = 0;
            for (int i = 0; i < n; i++) s += Xstd[i][j] * Xstd[i][j];
            empDiag[j] = s / n;
        }
        double mu = 0;
        for (int j = 0; j < d; j++) mu += empDiag[j];
        mu /= d;
        double delta_ = 0;
        for (int j = 0; j < d; j++) {
            for (int k = 0; k < d; k++) {
                double s = 0;
                for (int i = 0; i < n; i++) s += Xstd[i][j] * Xstd[i][k];
                s /= n;
                delta_ += s * s;
            }
        }
        double delta = delta_ / d - mu * mu;
        double beta_ = 0;
        for (int i = 0; i < n; i++) {
            double rowSqSum = 0;
            for (int j = 0; j < d; j++) {
                double v = Xstd[i][j];
                rowSqSum += v * v;
            }
            beta_ += rowSqSum * rowSqSum;
        }

        double beta = 1.0 / (d * n) * (beta_ / n - delta_);
        if (delta < 1e-15) return 0;
        double shrinkage = beta == 0 ? 0 : Math.min(beta, delta) / delta;
        return shrinkage;
    }

    private static void applyShrinkage(double[][] cov, double shrinkage) {
        int d = cov.length;
        double trace = 0;
        for (int j = 0; j < d; j++) trace += cov[j][j];
        double mu = trace / d;
        for (int j = 0; j < d; j++) {
            for (int k = 0; k < d; k++) {
                cov[j][k] = (1 - shrinkage) * cov[j][k];
                if (j == k) cov[j][k] += shrinkage * mu;
            }
        }
    }

    private static double[] solveLinearSystem(double[][] A, double[] b) {
        int d = b.length;
        double[][] M = new double[d][d + 1];
        for (int i = 0; i < d; i++) {
            for (int j = 0; j < d; j++) M[i][j] = A[i][j];
            M[i][d] = b[i];
        }
        for (int col = 0; col < d; col++) {
            int pivot = col;
            double maxVal = Math.abs(M[col][col]);
            for (int row = col + 1; row < d; row++) {
                if (Math.abs(M[row][col]) > maxVal) {
                    maxVal = Math.abs(M[row][col]);
                    pivot = row;
                }
            }
            if (pivot != col) {
                double[] tmp = M[col]; M[col] = M[pivot]; M[pivot] = tmp;
            }
            if (Math.abs(M[col][col]) < 1e-15) {
                M[col][col] += 1e-10;
            }
            for (int row = col + 1; row < d; row++) {
                double factor = M[row][col] / M[col][col];
                for (int j = col; j <= d; j++) {
                    M[row][j] -= factor * M[col][j];
                }
            }
        }
        double[] x = new double[d];
        for (int i = d - 1; i >= 0; i--) {
            double sum = M[i][d];
            for (int j = i + 1; j < d; j++) {
                sum -= M[i][j] * x[j];
            }
            x[i] = sum / M[i][i];
        }
        return x;
    }

    private static double dot(double[] a, double[] b) {
        double sum = 0;
        for (int i = 0; i < a.length; i++) sum += a[i] * b[i];
        return sum;
    }
}