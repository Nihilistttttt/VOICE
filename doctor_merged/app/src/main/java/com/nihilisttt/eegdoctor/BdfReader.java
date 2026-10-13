package com.nihilisttt.eegdoctor;

import java.io.RandomAccessFile;
import java.io.IOException;

public class BdfReader {

    public int nChannels;
    public int sampleRate;
    public int nRecords;
    public int samplesPerRecord;
    public int totalSamples;
    public String[] labels;
    public float[][] channels;
    public float[][] channelsHp;

    private static final float HP_B0 = 0.9823854385f;
    private static final float HP_B1 = -1.9647708771f;
    private static final float HP_B2 = 0.9823854385f;
    private static final float HP_A1 = -1.9644605802f;
    private static final float HP_A2 = 0.9650811739f;

    private static String field(byte[] raw, int off, int len) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < len && off + i < raw.length; i++) {
            char c = (char) raw[off + i];
            if (c == 0) break;
            if (c != ' ') sb.append(c);
        }
        return sb.toString().trim();
    }

    private static float safeFloat(String s, float def) {
        if (s.isEmpty()) return def;
        try { return Float.parseFloat(s); } catch (NumberFormatException e) { return def; }
    }

    private static int safeInt(String s, int def) {
        if (s.isEmpty()) return def;
        try { return Integer.parseInt(s); } catch (NumberFormatException e) { return def; }
    }

    public boolean parse(String path) {
        RandomAccessFile f = null;
        try {
            f = new RandomAccessFile(path, "r");
            long fileLen = f.length();
            if (fileLen < 256) return false;
            byte[] header = new byte[256];
            f.readFully(header);

            nRecords = safeInt(field(header, 236, 8), -1);
            float duration = safeFloat(field(header, 244, 8), 1.0f);
            nChannels = safeInt(field(header, 252, 4), 8);
            if (nChannels <= 0 || nChannels > 64) return false;

            int chHeaderSize = nChannels * 256;
            byte[] chHeader = new byte[chHeaderSize];
            f.readFully(chHeader);

            labels = new String[nChannels];
            float[] pmin = new float[nChannels];
            float[] pmax = new float[nChannels];
            float[] dmin = new float[nChannels];
            float[] dmax = new float[nChannels];
            int[] sratePerCh = new int[nChannels];

            for (int i = 0; i < nChannels; i++) {
                String lbl = field(chHeader, i * 16, 16);
                labels[i] = lbl.isEmpty() ? ("Ch" + i) : lbl;
                pmin[i] = safeFloat(field(chHeader, 832 + i * 8, 8), -187500f);
                pmax[i] = safeFloat(field(chHeader, 896 + i * 8, 8), 187500f);
                dmin[i] = safeFloat(field(chHeader, 960 + i * 8, 8), -8388608f);
                dmax[i] = safeFloat(field(chHeader, 1024 + i * 8, 8), 8388607f);
                sratePerCh[i] = safeInt(field(chHeader, 1728 + i * 8, 8), 250);
            }

            samplesPerRecord = sratePerCh[0];
            sampleRate = samplesPerRecord;
            if (duration >= 1) sampleRate = (int) (samplesPerRecord / duration);

            int headerSize = 256 + chHeaderSize;
            int recordBytes = nChannels * samplesPerRecord * 3;
            long dataLen = fileLen - headerSize;
            int availRecords = (int) (dataLen / recordBytes);
            if (nRecords <= 0 || nRecords > availRecords) nRecords = availRecords;
            if (nRecords <= 0) return false;

            totalSamples = nRecords * samplesPerRecord;
            channels = new float[nChannels][totalSamples];

            byte[] recordBuf = new byte[recordBytes];
            for (int rec = 0; rec < nRecords; rec++) {
                f.seek(headerSize + (long) rec * recordBytes);
                f.readFully(recordBuf);
                for (int ch = 0; ch < nChannels; ch++) {
                    int chBase = ch * samplesPerRecord * 3;
                    float dm = dmin[ch], dx = dmax[ch], pm = pmin[ch], px = pmax[ch];
                    float scale = (dx == dm) ? 1f : (px - pm) / (dx - dm);
                    for (int s = 0; s < samplesPerRecord; s++) {
                        int p = chBase + s * 3;
                        int b0 = recordBuf[p] & 0xFF;
                        int b1 = recordBuf[p + 1] & 0xFF;
                        int b2 = recordBuf[p + 2] & 0xFF;
                        int val = b0 | (b1 << 8) | (b2 << 16);
                        if (val >= 0x800000) val -= 0x1000000;
                        channels[ch][rec * samplesPerRecord + s] = (val - dm) * scale + pm;
                    }
                }
            }
            computeHighpass();
            return true;
        } catch (IOException e) {
            return false;
        } finally {
            if (f != null) try { f.close(); } catch (IOException ignored) {}
        }
    }

    public float[] getSegment(int ch, int start, int len) {
        if (ch < 0 || ch >= nChannels) return new float[0];
        int end = Math.min(start + len, totalSamples);
        if (start < 0) start = 0;
        if (end <= start) return new float[0];
        float[] out = new float[end - start];
        System.arraycopy(channels[ch], start, out, 0, end - start);
        return out;
    }

    public float[] getSegmentHp(int ch, int start, int len) {
        if (ch < 0 || ch >= nChannels || channelsHp == null) return new float[0];
        int end = Math.min(start + len, totalSamples);
        if (start < 0) start = 0;
        if (end <= start) return new float[0];
        float[] out = new float[end - start];
        System.arraycopy(channelsHp[ch], start, out, 0, end - start);
        return out;
    }

    private void computeHighpass() {
        channelsHp = new float[nChannels][];
        for (int ch = 0; ch < nChannels; ch++) {
            channelsHp[ch] = highpassFiltfilt(channels[ch]);
        }
    }

    private static float[] highpassFiltfilt(float[] x) {
        int n = x.length;
        if (n < 4) return x.clone();
        int padlen = 6;
        int totalLen = n + 2 * padlen;
        float[] padded = new float[totalLen];
        for (int i = 0; i < padlen; i++) {
            padded[padlen - 1 - i] = 2 * x[0] - x[i + 1];
        }
        System.arraycopy(x, 0, padded, padlen, n);
        for (int i = 0; i < padlen; i++) {
            padded[padlen + n + i] = 2 * x[n - 1] - x[n - 2 - i];
        }
        float[] y1 = iirForward(padded);
        float[] y1rev = new float[totalLen];
        for (int i = 0; i < totalLen; i++) y1rev[i] = y1[totalLen - 1 - i];
        float[] y2 = iirForward(y1rev);
        float[] result = new float[n];
        for (int i = 0; i < n; i++) result[i] = y2[totalLen - 1 - (padlen + i)];
        return result;
    }

    private static float[] iirForward(float[] x) {
        float[] y = new float[x.length];
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (int i = 0; i < x.length; i++) {
            float xi = x[i];
            float yi = HP_B0 * xi + HP_B1 * x1 + HP_B2 * x2 - HP_A1 * y1 - HP_A2 * y2;
            y[i] = yi;
            x2 = x1; x1 = xi;
            y2 = y1; y1 = yi;
        }
        return y;
    }
}