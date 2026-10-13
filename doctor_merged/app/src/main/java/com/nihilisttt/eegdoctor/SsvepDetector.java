package com.nihilisttt.eegdoctor;

public class SsvepDetector {

    private static final int FFT_SIZE = 256;
    private static final float[] FREQS = {8.0f, 12.0f};
    private static final float FREQ_TOLERANCE_HZ = 0.5f;

    private float[][] referenceSignals;

    public SsvepDetector(float sampleRate) {
        buildReferenceSignals(sampleRate);
    }

    private void buildReferenceSignals(float sampleRate) {
        int numHarmonics = 3;
        int n = FFT_SIZE;
        referenceSignals = new float[FREQS.length][n * numHarmonics];
        for (int f = 0; f < FREQS.length; f++) {
            for (int h = 0; h < numHarmonics; h++) {
                float freq = FREQS[f] * (h + 1);
                for (int i = 0; i < n; i++) {
                    float t = i / sampleRate;
                    referenceSignals[f][h * n + i] = (float) Math.sin(2 * Math.PI * freq * t);
                    referenceSignals[f][n * numHarmonics + h * n + i] = (float) Math.cos(2 * Math.PI * freq * t);
                }
            }
        }
    }

    public int detectByFft(float[] data, float sampleRate) {
        if (data == null || data.length < FFT_SIZE) return -1;
        float[] spectrum = computeFftMagnitude(data);
        float freqRes = sampleRate / FFT_SIZE;
        float maxPower = 0;
        int maxIdx = -1;
        for (int f = 0; f < FREQS.length; f++) {
            int bin = Math.round(FREQS[f] / freqRes);
            if (bin < 0 || bin >= spectrum.length) continue;
            float power = 0;
            int halfWin = Math.max(1, (int) (FREQ_TOLERANCE_HZ / freqRes));
            for (int b = bin - halfWin; b <= bin + halfWin && b < spectrum.length; b++) {
                if (b >= 0) power += spectrum[b] * spectrum[b];
            }
            if (power > maxPower) {
                maxPower = power;
                maxIdx = f;
            }
        }
        return maxIdx;
    }

    public float computeCca(float[] x, int refIdx) {
        if (x == null || x.length < FFT_SIZE || refIdx < 0 || refIdx >= FREQS.length) return 0;
        float[] ref = referenceSignals[refIdx];
        int n = Math.min(x.length, FFT_SIZE);
        int p = ref.length / n;
        float[][] X = new float[n][1];
        float[][] Y = new float[n][p];
        for (int i = 0; i < n; i++) {
            X[i][0] = x[i];
            for (int j = 0; j < p; j++) {
                Y[i][j] = ref[j * n + i];
            }
        }
        return ccaCorrelation(X, Y, n, 1, p);
    }

    public int detectByCca(float[] data) {
        if (data == null || data.length < FFT_SIZE) return -1;
        float maxR = 0;
        int maxIdx = -1;
        for (int f = 0; f < FREQS.length; f++) {
            float r = computeCca(data, f);
            if (r > maxR) {
                maxR = r;
                maxIdx = f;
            }
        }
        return maxIdx;
    }

    private float ccaCorrelation(float[][] X, float[][] Y, int n, int p1, int p2) {
        float[] meanX = new float[p1];
        float[] meanY = new float[p2];
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < p1; j++) meanX[j] += X[i][j];
            for (int j = 0; j < p2; j++) meanY[j] += Y[i][j];
        }
        for (int j = 0; j < p1; j++) meanX[j] /= n;
        for (int j = 0; j < p2; j++) meanY[j] /= n;

        for (int i = 0; i < n; i++) {
            for (int j = 0; j < p1; j++) X[i][j] -= meanX[j];
            for (int j = 0; j < p2; j++) Y[i][j] -= meanY[j];
        }

        float[][] Cxx = matMulT(X, X, n, p1);
        float[][] Cyy = matMulT(Y, Y, n, p2);
        float[][] Cxy = matMulXYT(X, Y, n, p1, p2);

        float[] dx = new float[p1];
        float[] dy = new float[p2];
        for (int j = 0; j < p1; j++) dx[j] = (float) Math.sqrt(Math.abs(Cxx[j][j]) + 1e-10);
        for (int j = 0; j < p2; j++) dy[j] = (float) Math.sqrt(Math.abs(Cyy[j][j]) + 1e-10);

        float[][] Rxy = new float[p1][p2];
        for (int i = 0; i < p1; i++) {
            for (int j = 0; j < p2; j++) {
                Rxy[i][j] = Cxy[i][j] / (dx[i] * dy[j] + 1e-10f);
            }
        }

        float maxR = 0;
        for (int i = 0; i < p1; i++) {
            for (int j = 0; j < p2; j++) {
                float abs = Math.abs(Rxy[i][j]);
                if (abs > maxR) maxR = abs;
            }
        }
        return maxR;
    }

    private float[][] matMulT(float[][] A, float[][] B, int n, int p) {
        float[][] C = new float[p][p];
        for (int i = 0; i < p; i++) {
            for (int j = i; j < p; j++) {
                float sum = 0;
                for (int k = 0; k < n; k++) sum += A[k][i] * A[k][j];
                C[i][j] = sum / n;
                C[j][i] = C[i][j];
            }
        }
        return C;
    }

    private float[][] matMulXYT(float[][] X, float[][] Y, int n, int p1, int p2) {
        float[][] C = new float[p1][p2];
        for (int i = 0; i < p1; i++) {
            for (int j = 0; j < p2; j++) {
                float sum = 0;
                for (int k = 0; k < n; k++) sum += X[k][i] * Y[k][j];
                C[i][j] = sum / n;
            }
        }
        return C;
    }

    public static float[] computeFftMagnitude(float[] data) {
        int n = data.length;
        float[] re = new float[n];
        float[] im = new float[n];
        System.arraycopy(data, 0, re, 0, n);
        fft(re, im, n);
        float[] mag = new float[n / 2];
        for (int i = 0; i < n / 2; i++) {
            mag[i] = (float) Math.sqrt(re[i] * re[i] + im[i] * im[i]);
        }
        return mag;
    }

    private static void fft(float[] re, float[] im, int n) {
        int bits = 0;
        while ((1 << bits) < n) bits++;
        for (int i = 0; i < n; i++) {
            int j = bitReverse(i, bits);
            if (j > i) {
                float t = re[i]; re[i] = re[j]; re[j] = t;
                t = im[i]; im[i] = im[j]; im[j] = t;
            }
        }
        for (int len = 2; len <= n; len <<= 1) {
            float angle = -2.0f * (float) Math.PI / len;
            float wRe = (float) Math.cos(angle);
            float wIm = (float) Math.sin(angle);
            for (int i = 0; i < n; i += len) {
                float curRe = 1, curIm = 0;
                for (int j = 0; j < len / 2; j++) {
                    float tRe = curRe * re[i + j + len / 2] - curIm * im[i + j + len / 2];
                    float tIm = curRe * im[i + j + len / 2] + curIm * re[i + j + len / 2];
                    re[i + j + len / 2] = re[i + j] - tRe;
                    im[i + j + len / 2] = im[i + j] - tIm;
                    re[i + j] += tRe;
                    im[i + j] += tIm;
                    float newCurRe = curRe * wRe - curIm * wIm;
                    curIm = curRe * wIm + curIm * wRe;
                    curRe = newCurRe;
                }
            }
        }
    }

    private static int bitReverse(int x, int bits) {
        int r = 0;
        for (int i = 0; i < bits; i++) {
            r = (r << 1) | (x & 1);
            x >>= 1;
        }
        return r;
    }

    public static String freqIndexToName(int idx) {
        if (idx < 0 || idx >= FREQS.length) return "UNKNOWN";
        return idx == 0 ? "LEFT" : "RIGHT";
    }
}