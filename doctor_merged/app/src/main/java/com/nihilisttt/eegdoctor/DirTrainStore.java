package com.nihilisttt.eegdoctor;

import android.content.Context;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

public class DirTrainStore {
    private static final String TAG = "DirTrainStore";
    private static final String PREFS_NAME = "eeg_doctor_settings";
    private static final String KEY_SAMPLES = "dir_train_samples";
    private static final int MAX_SAMPLES = 2000;

    public static class StoredSample {
        public float[] features;
        public int label;
        public long timestamp;

        public StoredSample(float[] features, int label, long timestamp) {
            this.features = features;
            this.label = label;
            this.timestamp = timestamp;
        }

        public JSONObject toJson() throws JSONException {
            JSONObject obj = new JSONObject();
            JSONArray featArr = new JSONArray();
            for (float f : features) featArr.put((double) f);
            obj.put("feat", featArr);
            obj.put("label", label);
            obj.put("ts", timestamp);
            return obj;
        }

        public static StoredSample fromJson(JSONObject obj) throws JSONException {
            JSONArray featArr = obj.getJSONArray("feat");
            float[] features = new float[featArr.length()];
            for (int i = 0; i < featArr.length(); i++) {
                features[i] = (float) featArr.getDouble(i);
            }
            int label = obj.getInt("label");
            long ts = obj.getLong("ts");
            return new StoredSample(features, label, ts);
        }
    }

    public static List<StoredSample> loadAll(Context ctx) {
        List<StoredSample> samples = new ArrayList<>();
        String json = ctx.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
                .getString(KEY_SAMPLES, null);
        if (json == null || json.isEmpty()) return samples;
        try {
            JSONArray arr = new JSONArray(json);
            for (int i = 0; i < arr.length(); i++) {
                samples.add(StoredSample.fromJson(arr.getJSONObject(i)));
            }
        } catch (JSONException e) {
            Log.e(TAG, "loadAll error", e);
        }
        return samples;
    }

    public static void saveAll(Context ctx, List<StoredSample> samples) {
        if (samples.size() > MAX_SAMPLES) {
            samples = new ArrayList<>(samples.subList(samples.size() - MAX_SAMPLES, samples.size()));
        }
        JSONArray arr = new JSONArray();
        for (StoredSample s : samples) {
            try {
                arr.put(s.toJson());
            } catch (JSONException e) {
                Log.e(TAG, "saveAll toJson error", e);
            }
        }
        ctx.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
                .edit().putString(KEY_SAMPLES, arr.toString()).apply();
    }

    public static void addSample(Context ctx, float[] features, int label) {
        List<StoredSample> samples = loadAll(ctx);
        samples.add(new StoredSample(features, label, System.currentTimeMillis()));
        saveAll(ctx, samples);
    }

    public static void clearAll(Context ctx) {
        ctx.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
                .edit().remove(KEY_SAMPLES).apply();
    }

    public static int count(Context ctx) {
        return loadAll(ctx).size();
    }

    public static int countLeft(Context ctx) {
        int count = 0;
        for (StoredSample s : loadAll(ctx)) {
            if (s.label == DirLdaTrainer.LEFT_CLASS) count++;
        }
        return count;
    }

    public static int countRight(Context ctx) {
        int count = 0;
        for (StoredSample s : loadAll(ctx)) {
            if (s.label == DirLdaTrainer.RIGHT_CLASS) count++;
        }
        return count;
    }
}