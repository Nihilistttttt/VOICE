package com.nihilisttt.eegdoctor;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.CheckBox;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import java.io.File;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class DirModelAdapter extends RecyclerView.Adapter<DirModelAdapter.ViewHolder> {

    public static class ModelInfo {
        public File file;
        public String name;
        public String patientId;
        public String patientName;
        public float balancedAccuracy;
        public boolean accuracyValid;
        public String dateStr;

        public String getSummary() {
            String p = patientName.isEmpty() ? "未命名" : patientName;
            return patientId + " " + p + " " + dateStr;
        }

        public String getAccuracyStr() {
            if (!accuracyValid) return "N/A";
            return String.format(java.util.Locale.US, "%.1f%%", balancedAccuracy * 100);
        }
    }

    private final List<ModelInfo> items = new ArrayList<>();
    private final Set<Integer> selected = new HashSet<>();

    public void setData(List<ModelInfo> data) {
        items.clear();
        items.addAll(data);
        selected.clear();
        notifyDataSetChanged();
    }

    public List<ModelInfo> getSelected() {
        List<ModelInfo> result = new ArrayList<>();
        for (int i : selected) {
            if (i >= 0 && i < items.size()) result.add(items.get(i));
        }
        return result;
    }

    public static class ViewHolder extends RecyclerView.ViewHolder {
        CheckBox cb;
        TextView tvName;
        TextView tvInfo;
        TextView tvAcc;

        public ViewHolder(@NonNull View v) {
            super(v);
            cb = v.findViewById(R.id.cb_dir_file);
            tvName = v.findViewById(R.id.tv_dir_name);
            tvInfo = v.findViewById(R.id.tv_dir_info);
            tvAcc = v.findViewById(R.id.tv_dir_acc);
        }
    }

    @NonNull
    @Override
    public ViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View v = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_dir_file, parent, false);
        return new ViewHolder(v);
    }

    @Override
    public void onBindViewHolder(@NonNull ViewHolder holder, int position) {
        ModelInfo info = items.get(position);
        holder.tvName.setText(info.name);
        holder.tvInfo.setText(info.getSummary());
        holder.tvAcc.setText(info.getAccuracyStr());
        boolean isSel = selected.contains(position);
        holder.cb.setChecked(isSel);
        holder.cb.setOnCheckedChangeListener((button, isChecked) -> {
            if (isChecked) selected.add(position);
            else selected.remove(position);
        });
    }

    @Override
    public int getItemCount() {
        return items.size();
    }
}