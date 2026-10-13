package com.nihilisttt.eegdoctor;

import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.SharedPreferences;
import android.text.InputFilter;
import android.widget.EditText;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public class PatientManager {

    private static final String PREFS_NAME = "dir_train_prefs";
    private static final String PREF_PATIENTS = "dir_patients_data";
    private static final String PREF_SELECTED = "patient_selected_idx";

    private static PatientManager instance;

    private final List<String[]> patientList = new ArrayList<>();
    private int selectedIdx = 0;
    private Context appContext;

    public interface PatientChangeListener {
        void onPatientChanged(String id, String name);
    }

    private final List<PatientChangeListener> listeners = new ArrayList<>();

    private PatientManager() {}

    public static synchronized PatientManager getInstance() {
        if (instance == null) instance = new PatientManager();
        return instance;
    }

    public void init(Context context) {
        this.appContext = context.getApplicationContext();
        loadPatientList();
    }

    public List<String[]> getPatientList() {
        return patientList;
    }

    public int getSelectedIdx() {
        return selectedIdx;
    }

    public void setSelectedIdx(int idx) {
        if (idx >= 0 && idx < patientList.size()) {
            selectedIdx = idx;
            savePatientList();
            notifyChanged();
        }
    }

    public String getCurrentPatientId() {
        if (selectedIdx >= 0 && selectedIdx < patientList.size())
            return patientList.get(selectedIdx)[0];
        return "P000";
    }

    public String getCurrentPatientName() {
        if (selectedIdx >= 0 && selectedIdx < patientList.size())
            return patientList.get(selectedIdx)[1];
        return "unnamed";
    }

    public void addListener(PatientChangeListener l) {
        if (!listeners.contains(l)) listeners.add(l);
    }

    public void removeListener(PatientChangeListener l) {
        listeners.remove(l);
    }

    private void notifyChanged() {
        String id = getCurrentPatientId();
        String name = getCurrentPatientName();
        for (PatientChangeListener l : listeners) l.onPatientChanged(id, name);
    }

    private void loadPatientList() {
        patientList.clear();
        if (appContext == null) return;
        SharedPreferences prefs = appContext.getSharedPreferences(PREFS_NAME, 0);
        String data = prefs.getString(PREF_PATIENTS, "");
        if (!data.isEmpty()) {
            for (String entry : data.split("\\|")) {
                String[] kv = entry.split(":", 2);
                if (kv.length == 2) patientList.add(new String[]{kv[0], kv[1]});
            }
        }
        selectedIdx = prefs.getInt(PREF_SELECTED, 0);
        if (selectedIdx >= patientList.size()) selectedIdx = 0;
    }

    private void savePatientList() {
        if (appContext == null) return;
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < patientList.size(); i++) {
            if (i > 0) sb.append("|");
            sb.append(patientList.get(i)[0]).append(":").append(patientList.get(i)[1]);
        }
        SharedPreferences prefs = appContext.getSharedPreferences(PREFS_NAME, 0);
        prefs.edit().putString(PREF_PATIENTS, sb.toString()).putInt(PREF_SELECTED, selectedIdx).apply();
    }

    public void showPatientSelectorDialog(Context context, Runnable onChanged) {
        if (patientList.isEmpty()) {
            showNewPatientDialog(context, onChanged);
            return;
        }
        String[] labels = new String[patientList.size()];
        for (int i = 0; i < patientList.size(); i++) {
            labels[i] = patientList.get(i)[0] + " " + patientList.get(i)[1];
        }
        int checkedItem = selectedIdx;
        AlertDialog.Builder builder = new AlertDialog.Builder(context)
                .setTitle("选择患者")
                .setSingleChoiceItems(labels, checkedItem, (DialogInterface d, int which) -> {
                    selectedIdx = which;
                    savePatientList();
                    notifyChanged();
                    if (onChanged != null) onChanged.run();
                    d.dismiss();
                })
                .setPositiveButton("新增", (DialogInterface d, int w) -> {
                    showNewPatientDialog(context, onChanged);
                })
                .setNegativeButton("取消", null);
        builder.show();
    }

    public void showNewPatientDialog(Context context, Runnable onChanged) {
        EditText input = new EditText(context);
        input.setHint("患者姓名/编号");
        input.setInputType(android.text.InputType.TYPE_CLASS_TEXT);
        input.setFilters(new InputFilter[]{new InputFilter.LengthFilter(20)});
        new AlertDialog.Builder(context)
                .setTitle("新增患者")
                .setView(input)
                .setPositiveButton("确定", (d, w) -> {
                    String name = input.getText().toString().trim();
                    if (name.isEmpty()) name = "unnamed";
                    name = name.replaceAll("[^a-zA-Z0-9_\\-\\u4e00-\\u9fa5]", "_");
                    int nextId = patientList.size() + 1;
                    String idStr = String.format(Locale.US, "P%03d", nextId);
                    patientList.add(new String[]{idStr, name});
                    selectedIdx = patientList.size() - 1;
                    savePatientList();
                    notifyChanged();
                    if (onChanged != null) onChanged.run();
                })
                .setNegativeButton("取消", null)
                .setCancelable(false)
                .show();
    }
}