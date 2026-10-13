package com.nihilisttt.eegdoctor;

import android.content.Context;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public class DirTrainFileManager {
    private static final String TAG = "DirTrainFile";
    private static final String DIR_NAME = "dir_train";

    public static File getDir(Context ctx) {
        File dir = new File(ctx.getExternalFilesDir(null), DIR_NAME);
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    public static class SessionInfo {
        public File file;
        public String name;
        public int total;
        public int left;
        public int right;
        public float balancedAccuracy;
        public boolean accuracyValid;

        public String getSummary() {
            return String.format(Locale.US, "%d样本 L%d/R%d", total, left, right);
        }

        public String getAccuracyStr() {
            if (!accuracyValid) return "N/A";
            return String.format(Locale.US, "%.1f%%", balancedAccuracy * 100);
        }
    }

    public static String saveSession(Context ctx, List<DirTrainStore.StoredSample> samples) {
        File dir = getDir(ctx);
        String ts = new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(new Date());
        String filename = "dir_" + ts + ".csv";
        File file = new File(dir, filename);
        try (FileWriter fw = new FileWriter(file)) {
            fw.append("label");
            for (int i = 0; i < DirLdaTrainer.FEATURE_DIM; i++) fw.append(",feat").append(String.valueOf(i));
            fw.append("\n");
            for (DirTrainStore.StoredSample s : samples) {
                int rawLabel = (s.label == DirLdaTrainer.LEFT_CLASS) ? 2 : 3;
                fw.append(String.valueOf(rawLabel));
                for (int i = 0; i < DirLdaTrainer.FEATURE_DIM; i++)
                    fw.append(",").append(String.format(Locale.US, "%d", (long) s.features[i]));
                fw.append("\n");
            }
            return filename;
        } catch (IOException e) {
            Log.e(TAG, "saveSession error", e);
            return null;
        }
    }

    public static List<DirTrainStore.StoredSample> loadFile(File file) {
        List<DirTrainStore.StoredSample> samples = new ArrayList<>();
        try (BufferedReader br = new BufferedReader(new FileReader(file))) {
            String line = br.readLine();
            if (line == null) return samples;
            while ((line = br.readLine()) != null) {
                String[] parts = line.split(",");
                if (parts.length < 1 + DirLdaTrainer.FEATURE_DIM) continue;
                int rawLabel;
                try { rawLabel = Integer.parseInt(parts[0]); } catch (NumberFormatException e) { continue; }
                int label = DirLdaTrainer.normalizeLabel(rawLabel);
                if (label < 0) continue;
                float[] features = new float[DirLdaTrainer.FEATURE_DIM];
                for (int i = 0; i < DirLdaTrainer.FEATURE_DIM; i++) {
                    try { features[i] = Float.parseFloat(parts[1 + i]); } catch (NumberFormatException e) { features[i] = 0; }
                }
                samples.add(new DirTrainStore.StoredSample(features, label, 0));
            }
        } catch (IOException e) {
            Log.e(TAG, "loadFile error", e);
        }
        return samples;
    }

    public static List<SessionInfo> listSessions(Context ctx) {
        List<SessionInfo> list = new ArrayList<>();
        File dir = getDir(ctx);
        File[] files = dir.listFiles((d, name) -> name.endsWith(".csv"));
        if (files == null) return list;
        for (File f : files) {
            SessionInfo info = new SessionInfo();
            info.file = f;
            info.name = f.getName();
            List<DirTrainStore.StoredSample> samples = loadFile(f);
            info.total = samples.size();
            info.left = 0;
            info.right = 0;
            for (DirTrainStore.StoredSample s : samples) {
                if (s.label == DirLdaTrainer.LEFT_CLASS) info.left++;
                else if (s.label == DirLdaTrainer.RIGHT_CLASS) info.right++;
            }
            if (info.total >= 4) {
                List<DirLdaTrainer.Sample> trainSamples = new ArrayList<>();
                for (DirTrainStore.StoredSample s : samples) trainSamples.add(new DirLdaTrainer.Sample(s.features, s.label));
                DirLdaTrainer.TrainResult result = DirLdaTrainer.train(trainSamples);
                if (result.valid) {
                    info.balancedAccuracy = result.balancedAccuracy;
                    info.accuracyValid = true;
                }
            }
            list.add(info);
        }
        list.sort((a, b) -> b.name.compareTo(a.name));
        return list;
    }

    public static String mergeFiles(Context ctx, List<File> files, String outName) {
        File dir = getDir(ctx);
        File outFile = new File(dir, outName);
        try (FileWriter fw = new FileWriter(outFile)) {
            fw.append("label");
            for (int i = 0; i < DirLdaTrainer.FEATURE_DIM; i++) fw.append(",feat").append(String.valueOf(i));
            fw.append("\n");
            for (File f : files) {
                List<DirTrainStore.StoredSample> samples = loadFile(f);
                for (DirTrainStore.StoredSample s : samples) {
                    int rawLabel = (s.label == DirLdaTrainer.LEFT_CLASS) ? 2 : 3;
                    fw.append(String.valueOf(rawLabel));
                    for (int i = 0; i < DirLdaTrainer.FEATURE_DIM; i++)
                        fw.append(",").append(String.format(Locale.US, "%d", (long) s.features[i]));
                    fw.append("\n");
                }
            }
            return outName;
        } catch (IOException e) {
            Log.e(TAG, "mergeFiles error", e);
            return null;
        }
    }

    public static void deleteFile(File file) {
        if (file != null && file.exists()) file.delete();
    }
}