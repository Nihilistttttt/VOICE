package com.nihilisttt.eegdoctor;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

public class InferenceRecordStore {

    private static final String KEY_RECORDS = "inference_records";
    private static final int MAX_RECORDS = 500;

    public static class Record {
        public long timestamp;
        public String intent;
        public float scoreLeft;
        public float scoreRight;
        public float confidence;
        public String groundTruth;
        public boolean correct;

        public JSONObject toJson() throws JSONException {
            JSONObject o = new JSONObject();
            o.put("ts", timestamp);
            o.put("intent", intent);
            o.put("sl", scoreLeft);
            o.put("sr", scoreRight);
            o.put("conf", confidence);
            o.put("gt", groundTruth != null ? groundTruth : "");
            o.put("ok", correct);
            return o;
        }

        public static Record fromJson(JSONObject o) throws JSONException {
            Record r = new Record();
            r.timestamp = o.optLong("ts", 0);
            r.intent = o.optString("intent", "");
            r.scoreLeft = (float) o.optDouble("sl", 0);
            r.scoreRight = (float) o.optDouble("sr", 0);
            r.confidence = (float) o.optDouble("conf", 0);
            r.groundTruth = o.optString("gt", "");
            r.correct = o.optBoolean("ok", false);
            return r;
        }
    }

    public static List<Record> loadAll(android.content.Context ctx) {
        String json = ctx.getSharedPreferences("eeg_doctor_settings", 0)
                .getString(KEY_RECORDS, "[]");
        List<Record> list = new ArrayList<>();
        try {
            JSONArray arr = new JSONArray(json);
            for (int i = 0; i < arr.length(); i++) {
                list.add(Record.fromJson(arr.getJSONObject(i)));
            }
        } catch (JSONException ignored) {}
        return list;
    }

    public static void saveAll(android.content.Context ctx, List<Record> records) {
        JSONArray arr = new JSONArray();
        int start = Math.max(0, records.size() - MAX_RECORDS);
        for (int i = start; i < records.size(); i++) {
            try { arr.put(records.get(i).toJson()); } catch (JSONException ignored) {}
        }
        ctx.getSharedPreferences("eeg_doctor_settings", 0)
                .edit().putString(KEY_RECORDS, arr.toString()).apply();
    }

    public static void addRecord(android.content.Context ctx, Record r) {
        List<Record> list = loadAll(ctx);
        list.add(r);
        saveAll(ctx, list);
    }

    public static void clearAll(android.content.Context ctx) {
        ctx.getSharedPreferences("eeg_doctor_settings", 0)
                .edit().remove(KEY_RECORDS).apply();
    }

    public static int countTotal(List<Record> records) { return records.size(); }

    public static int countCorrect(List<Record> records) {
        int c = 0;
        for (Record r : records) if (r.correct) c++;
        return c;
    }

    public static int countWithGroundTruth(List<Record> records) {
        int c = 0;
        for (Record r : records) if (r.groundTruth != null && !r.groundTruth.isEmpty()) c++;
        return c;
    }

    public static float calcAccuracy(List<Record> records) {
        int total = 0, correct = 0;
        for (Record r : records) {
            if (r.groundTruth != null && !r.groundTruth.isEmpty()) {
                total++;
                if (r.correct) correct++;
            }
        }
        return total > 0 ? (float) correct / total * 100f : 0f;
    }

    public static float avgConfidence(List<Record> records) {
        if (records.isEmpty()) return 0f;
        float sum = 0f;
        for (Record r : records) sum += r.confidence;
        return sum / records.size() * 100f;
    }

    public static int countLeft(List<Record> records) {
        int c = 0;
        for (Record r : records) if ("LEFT".equals(r.intent)) c++;
        return c;
    }

    public static int countRight(List<Record> records) {
        int c = 0;
        for (Record r : records) if ("RIGHT".equals(r.intent)) c++;
        return c;
    }

    public static String exportCsv(List<Record> records) {
        StringBuilder sb = new StringBuilder();
        sb.append("时间,推理结果,左得分,右得分,置信度,真实意图,是否正确\n");
        java.text.SimpleDateFormat sdf = new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", java.util.Locale.getDefault());
        for (Record r : records) {
            sb.append(sdf.format(new java.util.Date(r.timestamp))).append(",");
            sb.append(r.intent).append(",");
            sb.append(String.format(java.util.Locale.US, "%.4f", r.scoreLeft)).append(",");
            sb.append(String.format(java.util.Locale.US, "%.4f", r.scoreRight)).append(",");
            sb.append(String.format(java.util.Locale.US, "%.4f", r.confidence)).append(",");
            sb.append(r.groundTruth != null ? r.groundTruth : "").append(",");
            sb.append(r.correct ? "1" : "0").append("\n");
        }
        return sb.toString();
    }
}