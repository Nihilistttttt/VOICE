package com.nihilisttt.eegdoctor;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.core.content.ContextCompat;
import androidx.recyclerview.widget.RecyclerView;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

public class InferenceRecordAdapter extends RecyclerView.Adapter<InferenceRecordAdapter.VH> {

    private final List<InferenceRecordStore.Record> records = new ArrayList<>();
    private final SimpleDateFormat timeFmt = new SimpleDateFormat("HH:mm:ss", Locale.getDefault());

    public void setRecords(List<InferenceRecordStore.Record> list) {
        records.clear();
        records.addAll(list);
        Collections.reverse(records);
        notifyDataSetChanged();
    }

    @NonNull
    @Override
    public VH onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View v = LayoutInflater.from(parent.getContext())
                .inflate(R.layout.item_inference_record, parent, false);
        return new VH(v);
    }

    @Override
    public void onBindViewHolder(@NonNull VH h, int pos) {
        InferenceRecordStore.Record r = records.get(pos);
        h.time.setText(timeFmt.format(r.timestamp));
        if ("LEFT".equals(r.intent)) {
            h.intent.setText("←");
            h.intent.setTextColor(ContextCompat.getColor(h.itemView.getContext(), R.color.direction_left));
        } else if ("RIGHT".equals(r.intent)) {
            h.intent.setText("→");
            h.intent.setTextColor(ContextCompat.getColor(h.itemView.getContext(), R.color.direction_right));
        } else {
            h.intent.setText("?");
            h.intent.setTextColor(ContextCompat.getColor(h.itemView.getContext(), R.color.text_secondary));
        }
        h.conf.setText(String.format(Locale.getDefault(), "%.0f%%", r.confidence * 100));
        if (r.groundTruth != null && !r.groundTruth.isEmpty()) {
            h.gt.setText("LEFT".equals(r.groundTruth) ? "←" : "→");
        } else {
            h.gt.setText("--");
        }
        if (r.groundTruth != null && !r.groundTruth.isEmpty()) {
            h.result.setText(r.correct ? "✓" : "✗");
            h.result.setTextColor(ContextCompat.getColor(h.itemView.getContext(),
                    r.correct ? R.color.accent_success : R.color.accent_error));
        } else {
            h.result.setText("");
        }
    }

    @Override
    public int getItemCount() { return records.size(); }

    static class VH extends RecyclerView.ViewHolder {
        TextView time, intent, conf, gt, result;
        VH(View v) {
            super(v);
            time = v.findViewById(R.id.tv_rec_time);
            intent = v.findViewById(R.id.tv_rec_intent);
            conf = v.findViewById(R.id.tv_rec_conf);
            gt = v.findViewById(R.id.tv_rec_gt);
            result = v.findViewById(R.id.tv_rec_result);
        }
    }
}