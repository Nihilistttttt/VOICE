package com.nihilisttt.eegdoctor;

import com.google.android.material.button.MaterialButton;
import com.nihilisttt.eegdoctor.R;

import android.app.AlertDialog;
import android.content.Context;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.content.ContextCompat;
import androidx.fragment.app.Fragment;

import com.google.android.material.card.MaterialCardView;

public class CompareFragment extends Fragment implements DataListener {

    private static final int NUM_VIEWS = 4;
    private static final int DEFAULT_LABEL_COUNT = 7;
    private static final int DEFAULT_STEP = 500;
    private static final String DEFAULT_STEP_UNIT = "uV";
    private static final float DEFAULT_X_MAX = 2.048f;

    private static final String[] CHANNEL_NAMES = EegChannels.NAMES;
    private static final String[] WAVE_TYPE_NAMES = EegChannels.WAVE_TYPE_NAMES;
    private static final String[] SPEC_TYPE_NAMES = EegChannels.SPEC_TYPE_NAMES;

    private static final int[] WAVE_COLORS = {
        R.color.wave_ch0, R.color.wave_filtered,
        R.color.wave_ch1, R.color.accent_info
    };
    private static final int[] SPEC_COLORS = {
        R.color.spectrum_bar, R.color.spectrum_bar_end,
        R.color.accent_info, R.color.accent_warning
    };

    private final WaveformView[] waveViews = new WaveformView[NUM_VIEWS];
    private final SpectrumView[] specViews = new SpectrumView[NUM_VIEWS];
    private final TextView[] waveLabels = new TextView[NUM_VIEWS];
    private final TextView[] specLabels = new TextView[NUM_VIEWS];

    private TextView tvXRange;
    private TextView tvWaveLabelCount;
    private TextView tvWaveRange;
    private TextView tvSpecRange;
    private TextView tvSpecXRange;
    private View tvSpecLabel;
    private View tvHzLabel;
    private MaterialButton btnToggleMode;
    private MaterialButton btnPause;

    private boolean isWaveMode = true;
    private boolean isPaused = false;

    private int currentLabelCount = DEFAULT_LABEL_COUNT;
    private int currentStep = DEFAULT_STEP;
    private String currentStepUnit = DEFAULT_STEP_UNIT;
    private float currentXMax = DEFAULT_X_MAX;
    private float specXMin = 0f;
    private float specXMax = 125f;

    private int[] waveCh = {6, 7, 4, 5};
    private int[] waveType = {0, 0, 0, 0};
    private int[] specCh = {6, 7, 4, 5};
    private int[] specType = {0, 0, 0, 0};

    private TcpServerManager.ConnectionListener connectionListener;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View root = inflater.inflate(R.layout.fragment_compare, container, false);

        waveCh[0] = SettingsStore.getWcChA(requireContext(), 6);
        waveCh[1] = SettingsStore.getWcChA(requireContext(), 7);
        waveCh[2] = SettingsStore.getWcChB(requireContext(), 4);
        waveCh[3] = SettingsStore.getWcChB(requireContext(), 5);
        specCh[0] = SettingsStore.getScChA(requireContext(), 6);
        specCh[1] = SettingsStore.getScChA(requireContext(), 7);
        specCh[2] = SettingsStore.getScChB(requireContext(), 4);
        specCh[3] = SettingsStore.getScChB(requireContext(), 5);

        LinearLayout waveGrid = root.findViewById(R.id.wave_grid_container);
        buildWaveGrid(waveGrid);
        LinearLayout specGrid = root.findViewById(R.id.spec_grid_container);
        buildSpecGrid(specGrid);

        tvXRange = root.findViewById(R.id.tv_x_range);

        tvWaveRange = root.findViewById(R.id.tv_wave_range);
        tvSpecRange = root.findViewById(R.id.tv_spec_range);
        tvSpecXRange = root.findViewById(R.id.tv_spec_x_range);
        tvSpecLabel = root.findViewById(R.id.tv_spec_label);
        tvHzLabel = root.findViewById(R.id.tv_hz_label);
        btnToggleMode = root.findViewById(R.id.btn_toggle_mode);
        btnPause = root.findViewById(R.id.btn_pause_resume);

        btnToggleMode.setOnClickListener(v -> toggleMode());
        btnPause.setOnClickListener(v -> {
            isPaused = !isPaused;
            btnPause.setText(isPaused ? "继续" : "暂停");
        });
        root.findViewById(R.id.btn_channel_cfg).setOnClickListener(v -> showChannelConfigDialog());

        tvXRange.setOnClickListener(v -> showXRangeDialog());

        tvWaveRange.setOnClickListener(v -> showWaveRangeDialog());
        tvSpecRange.setOnClickListener(v -> showSpecRangeDialog());
        tvSpecXRange.setOnClickListener(v -> showSpecXRangeDialog());

        int step = SettingsStore.getWaveStep(requireContext(), DEFAULT_STEP);
        String stepUnit = SettingsStore.getWaveStepUnit(requireContext(), DEFAULT_STEP_UNIT);
        int labelCount = SettingsStore.getWaveLabelCount(requireContext(), DEFAULT_LABEL_COUNT);
        float xMax = SettingsStore.getWaveXMax(requireContext(), DEFAULT_X_MAX);
        float specRange = SettingsStore.getSpecRange(requireContext(), 500f);
        String specUnit = SettingsStore.getSpecUnit(requireContext(), "uV");

        currentLabelCount = labelCount;
        currentStep = step;
        currentStepUnit = stepUnit;
        currentXMax = xMax;

        applyXRange(currentXMax);
        applyWaveStep(currentStep, currentStepUnit, currentLabelCount);
        for (SpectrumView sv : specViews) if (sv != null) {
            sv.setRange(specRange, specUnit);
            sv.setFreqRange(specXMin, specXMax);
        }
        tvSpecRange.setText(formatSpecRange(specRange, specUnit));
        tvSpecXRange.setText((int) specXMin + "-" + (int) specXMax);

        updateWaveLabels();
        updateSpecLabels();

        return root;
    }

    private void buildWaveGrid(LinearLayout grid) {
        Context ctx = requireContext();
        grid.removeAllViews();
        LinearLayout row = new LinearLayout(ctx);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setWeightSum(2f);
        row.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        for (int i = 0; i < NUM_VIEWS; i++) {
            if (i == 2) {
                grid.addView(row);
                row = new LinearLayout(ctx);
                row.setOrientation(LinearLayout.HORIZONTAL);
                row.setWeightSum(2f);
                row.setLayoutParams(new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
            }
            MaterialCardView card = new MaterialCardView(ctx);
            LinearLayout.LayoutParams cp = new LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
            cp.setMargins(2, 2, 2, 2);
            card.setLayoutParams(cp);
            card.setCardElevation(1f);
            card.setRadius(6f);
            card.setCardBackgroundColor(ContextCompat.getColor(ctx, R.color.surface_low));
            card.setStrokeColor(ContextCompat.getColor(ctx, R.color.surface_overlay));
            card.setStrokeWidth(1);

            LinearLayout inner = new LinearLayout(ctx);
            inner.setOrientation(LinearLayout.VERTICAL);

            LinearLayout header = makeHeader(ctx, WAVE_COLORS[i], waveLabels, i);
            inner.addView(header);

            WaveformView wv = new WaveformView(ctx, null);
            wv.setId(View.generateViewId());
            wv.setLayoutParams(new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
            wv.setWaveColor(ContextCompat.getColor(ctx, WAVE_COLORS[i]));
            inner.addView(wv);
            waveViews[i] = wv;

            card.addView(inner);
            row.addView(card);
        }
        grid.addView(row);
    }

    private void buildSpecGrid(LinearLayout grid) {
        Context ctx = requireContext();
        grid.removeAllViews();
        LinearLayout row = new LinearLayout(ctx);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setWeightSum(2f);
        row.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        for (int i = 0; i < NUM_VIEWS; i++) {
            if (i == 2) {
                grid.addView(row);
                row = new LinearLayout(ctx);
                row.setOrientation(LinearLayout.HORIZONTAL);
                row.setWeightSum(2f);
                row.setLayoutParams(new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
            }
            MaterialCardView card = new MaterialCardView(ctx);
            LinearLayout.LayoutParams cp = new LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
            cp.setMargins(2, 2, 2, 2);
            card.setLayoutParams(cp);
            card.setCardElevation(1f);
            card.setRadius(6f);
            card.setCardBackgroundColor(ContextCompat.getColor(ctx, R.color.surface_low));
            card.setStrokeColor(ContextCompat.getColor(ctx, R.color.surface_overlay));
            card.setStrokeWidth(1);

            LinearLayout inner = new LinearLayout(ctx);
            inner.setOrientation(LinearLayout.VERTICAL);

            LinearLayout header = makeHeader(ctx, SPEC_COLORS[i], specLabels, i);
            inner.addView(header);

            SpectrumView sv = new SpectrumView(ctx, null);
            sv.setId(View.generateViewId());
            sv.setLayoutParams(new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
            inner.addView(sv);
            specViews[i] = sv;

            card.addView(inner);
            row.addView(card);
        }
        grid.addView(row);
    }

    private LinearLayout makeHeader(Context ctx, int colorRes, TextView[] labels, int i) {
        LinearLayout header = new LinearLayout(ctx);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setBackgroundColor(ContextCompat.getColor(ctx, R.color.surface_mid));
        header.setGravity(Gravity.CENTER_VERTICAL);
        int hp = (int) (4 * getResources().getDisplayMetrics().density);
        header.setPadding(hp * 2, 0, hp, 0);

        View dot = new View(ctx);
        LinearLayout.LayoutParams dotP = new LinearLayout.LayoutParams(6, 6);
        dotP.setMargins(0, 0, hp, 0);
        dot.setLayoutParams(dotP);
        dot.setBackgroundColor(ContextCompat.getColor(ctx, colorRes));
        header.addView(dot);

        TextView label = new TextView(ctx);
        label.setTextColor(ContextCompat.getColor(ctx, R.color.text_secondary));
        label.setTextSize(10);
        label.setLayoutParams(new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        header.addView(label);
        labels[i] = label;
        return header;
    }

    private void toggleMode() {
        isWaveMode = !isWaveMode;
        View vTimeLabel = requireView().findViewById(R.id.tv_time_label);
        View vWaveAmpLabel = requireView().findViewById(R.id.tv_wave_amp_label);
        if (isWaveMode) {
            requireView().findViewById(R.id.wave_grid_container).setVisibility(View.VISIBLE);
            requireView().findViewById(R.id.spec_grid_container).setVisibility(View.GONE);
            btnToggleMode.setText("频谱");
            vTimeLabel.setVisibility(View.VISIBLE);
            tvXRange.setVisibility(View.VISIBLE);
            vWaveAmpLabel.setVisibility(View.VISIBLE);
            tvWaveRange.setVisibility(View.VISIBLE);
            tvHzLabel.setVisibility(View.GONE);
            tvSpecXRange.setVisibility(View.GONE);
            tvSpecLabel.setVisibility(View.GONE);
            tvSpecRange.setVisibility(View.GONE);
        } else {
            requireView().findViewById(R.id.wave_grid_container).setVisibility(View.GONE);
            requireView().findViewById(R.id.spec_grid_container).setVisibility(View.VISIBLE);
            btnToggleMode.setText("波形");
            vTimeLabel.setVisibility(View.GONE);
            tvXRange.setVisibility(View.GONE);
            vWaveAmpLabel.setVisibility(View.GONE);
            tvWaveRange.setVisibility(View.GONE);
            tvHzLabel.setVisibility(View.VISIBLE);
            tvSpecXRange.setVisibility(View.VISIBLE);
            tvSpecLabel.setVisibility(View.VISIBLE);
            tvSpecRange.setVisibility(View.VISIBLE);
        }
        sendDisplayConfig();
    }

    private void updateWaveLabels() {
        for (int i = 0; i < NUM_VIEWS; i++) {
            String chName = EegChannels.nameOf(waveCh[i]);
            String typeName = (waveType[i] >= 0 && waveType[i] < WAVE_TYPE_NAMES.length) ? WAVE_TYPE_NAMES[waveType[i]] : "";
            if (waveLabels[i] != null) waveLabels[i].setText(chName + " " + typeName);
        }
    }

    private void updateSpecLabels() {
        for (int i = 0; i < NUM_VIEWS; i++) {
            String chName = EegChannels.nameOf(specCh[i]);
            String typeName = (specType[i] >= 0 && specType[i] < SPEC_TYPE_NAMES.length) ? SPEC_TYPE_NAMES[specType[i]] : "";
            if (specLabels[i] != null) specLabels[i].setText(chName + " " + typeName);
        }
    }

    private void showChannelConfigDialog() {
        if (isWaveMode) showWaveChannelConfig();
        else showSpecChannelConfig();
    }

    private void showWaveChannelConfig() {
        Context ctx = requireContext();
        int dp4 = (int) (4 * getResources().getDisplayMetrics().density);
        int dp8 = (int) (8 * getResources().getDisplayMetrics().density);

        android.widget.ScrollView scrollView = new android.widget.ScrollView(ctx);
        LinearLayout rootLayout = new LinearLayout(ctx);
        rootLayout.setOrientation(LinearLayout.VERTICAL);
        rootLayout.setPadding(dp4 * 3, dp4 * 2, dp4 * 3, 0);
        rootLayout.setMinimumWidth((int) (480 * getResources().getDisplayMetrics().density));

        ArrayAdapter<String> chAdapter = new ArrayAdapter<>(ctx,
                android.R.layout.simple_spinner_item, CHANNEL_NAMES);
        chAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        ArrayAdapter<String> waveTypeAdapter = new ArrayAdapter<>(ctx,
                android.R.layout.simple_spinner_item, WAVE_TYPE_NAMES);
        waveTypeAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);

        String[] slotNames = {"左上", "左下", "右上", "右下"};
        Spinner[] spCh = new Spinner[NUM_VIEWS];
        Spinner[] spWaveType = new Spinner[NUM_VIEWS];

        for (int row = 0; row < 2; row++) {
            LinearLayout colRow = new LinearLayout(ctx);
            colRow.setOrientation(LinearLayout.HORIZONTAL);
            colRow.setWeightSum(2f);
            for (int col = 0; col < 2; col++) {
                int i = row * 2 + col;
                LinearLayout colLayout = new LinearLayout(ctx);
                colLayout.setOrientation(LinearLayout.VERTICAL);
                colLayout.setLayoutParams(new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));

                TextView title = new TextView(ctx);
                title.setText(slotNames[i]);
                title.setTextSize(13);
                title.setTypeface(null, android.graphics.Typeface.BOLD);
                title.setPadding(0, dp4, 0, dp4);
                colLayout.addView(title);

                spCh[i] = new Spinner(ctx);
                spCh[i].setAdapter(chAdapter);
                spCh[i].setSelection(waveCh[i]);
                addDialogRow(colLayout, "电极", spCh[i]);

                spWaveType[i] = new Spinner(ctx);
                spWaveType[i].setAdapter(waveTypeAdapter);
                spWaveType[i].setSelection(waveType[i]);
                addDialogRow(colLayout, "波形", spWaveType[i]);

                colRow.addView(colLayout);
                if (col == 0) addVDiv(colRow, ctx, dp8);
            }
            rootLayout.addView(colRow);
            if (row == 0) addHDiv(rootLayout, ctx, dp8);
        }
        scrollView.addView(rootLayout);

        new AlertDialog.Builder(ctx)
                .setTitle("波形通道配置")
                .setView(scrollView)
                .setPositiveButton("确定", (d, id) -> {
                    for (int i = 0; i < NUM_VIEWS; i++) {
                        waveCh[i] = spCh[i].getSelectedItemPosition();
                        waveType[i] = spWaveType[i].getSelectedItemPosition();
                    }
                    updateWaveLabels();
                    clearWaveViews();
                    SettingsStore.setWcChA(requireContext(), waveCh[0]);
                    SettingsStore.setWcChB(requireContext(), waveCh[2]);
                    sendDisplayConfig();
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void showSpecChannelConfig() {
        Context ctx = requireContext();
        int dp4 = (int) (4 * getResources().getDisplayMetrics().density);
        int dp8 = (int) (8 * getResources().getDisplayMetrics().density);

        android.widget.ScrollView scrollView = new android.widget.ScrollView(ctx);
        LinearLayout rootLayout = new LinearLayout(ctx);
        rootLayout.setOrientation(LinearLayout.VERTICAL);
        rootLayout.setPadding(dp4 * 3, dp4 * 2, dp4 * 3, 0);
        rootLayout.setMinimumWidth((int) (480 * getResources().getDisplayMetrics().density));

        ArrayAdapter<String> chAdapter = new ArrayAdapter<>(ctx,
                android.R.layout.simple_spinner_item, CHANNEL_NAMES);
        chAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        ArrayAdapter<String> specTypeAdapter = new ArrayAdapter<>(ctx,
                android.R.layout.simple_spinner_item, SPEC_TYPE_NAMES);
        specTypeAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);

        String[] slotNames = {"左上", "左下", "右上", "右下"};
        Spinner[] spCh = new Spinner[NUM_VIEWS];
        Spinner[] spSpecType = new Spinner[NUM_VIEWS];

        for (int row = 0; row < 2; row++) {
            LinearLayout colRow = new LinearLayout(ctx);
            colRow.setOrientation(LinearLayout.HORIZONTAL);
            colRow.setWeightSum(2f);
            for (int col = 0; col < 2; col++) {
                int i = row * 2 + col;
                LinearLayout colLayout = new LinearLayout(ctx);
                colLayout.setOrientation(LinearLayout.VERTICAL);
                colLayout.setLayoutParams(new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));

                TextView title = new TextView(ctx);
                title.setText(slotNames[i]);
                title.setTextSize(13);
                title.setTypeface(null, android.graphics.Typeface.BOLD);
                title.setPadding(0, dp4, 0, dp4);
                colLayout.addView(title);

                spCh[i] = new Spinner(ctx);
                spCh[i].setAdapter(chAdapter);
                spCh[i].setSelection(specCh[i]);
                addDialogRow(colLayout, "电极", spCh[i]);

                spSpecType[i] = new Spinner(ctx);
                spSpecType[i].setAdapter(specTypeAdapter);
                spSpecType[i].setSelection(specType[i]);
                addDialogRow(colLayout, "频谱", spSpecType[i]);

                colRow.addView(colLayout);
                if (col == 0) addVDiv(colRow, ctx, dp8);
            }
            rootLayout.addView(colRow);
            if (row == 0) addHDiv(rootLayout, ctx, dp8);
        }
        scrollView.addView(rootLayout);

        new AlertDialog.Builder(ctx)
                .setTitle("频谱通道配置")
                .setView(scrollView)
                .setPositiveButton("确定", (d, id) -> {
                    for (int i = 0; i < NUM_VIEWS; i++) {
                        specCh[i] = spCh[i].getSelectedItemPosition();
                        specType[i] = spSpecType[i].getSelectedItemPosition();
                    }
                    updateSpecLabels();
                    SettingsStore.setScChA(requireContext(), specCh[0]);
                    SettingsStore.setScChB(requireContext(), specCh[2]);
                    sendDisplayConfig();
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void addDialogRow(LinearLayout parent, String label, Spinner spinner) {
        LinearLayout row = new LinearLayout(requireContext());
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        TextView tv = new TextView(requireContext());
        tv.setText(label);
        tv.setTextSize(13);
        int dp8 = (int) (8 * getResources().getDisplayMetrics().density);
        tv.setPadding(0, 0, dp8, 0);
        tv.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        row.addView(tv);
        spinner.setLayoutParams(new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        row.addView(spinner);
        parent.addView(row);
    }

    private void addVDiv(LinearLayout parent, Context ctx, int dp8) {
        View v = new View(ctx);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(1, LinearLayout.LayoutParams.MATCH_PARENT);
        lp.setMargins(dp8, 0, dp8, 0);
        v.setLayoutParams(lp);
        v.setBackgroundColor(ContextCompat.getColor(ctx, R.color.surface_overlay));
        parent.addView(v);
    }

    private void addHDiv(LinearLayout parent, Context ctx, int dp8) {
        View v = new View(ctx);
        v.setBackgroundColor(ContextCompat.getColor(ctx, R.color.surface_overlay));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 1);
        lp.setMargins(0, dp8, 0, dp8);
        v.setLayoutParams(lp);
        parent.addView(v);
    }

    private void clearWaveViews() {
        for (WaveformView wv : waveViews) if (wv != null) wv.clear();
    }

    private void sendDisplayConfig() {
        int[] p = new int[24];
        if (isWaveMode) {
            p[0] = waveCh[0]; p[1] = waveType[0]; p[2] = EegChannels.SPEC_TYPE_NONE;
            p[3] = waveCh[1]; p[4] = waveType[1]; p[5] = EegChannels.SPEC_TYPE_NONE;
            p[6] = waveCh[2]; p[7] = waveType[2]; p[8] = EegChannels.SPEC_TYPE_NONE;
            p[9] = waveCh[3]; p[10] = waveType[3]; p[11] = EegChannels.SPEC_TYPE_NONE;
        } else {
            p[0] = specCh[0]; p[1] = EegChannels.WAVE_TYPE_NONE; p[2] = specType[0];
            p[3] = specCh[1]; p[4] = EegChannels.WAVE_TYPE_NONE; p[5] = specType[1];
            p[6] = specCh[2]; p[7] = EegChannels.WAVE_TYPE_NONE; p[8] = specType[2];
            p[9] = specCh[3]; p[10] = EegChannels.WAVE_TYPE_NONE; p[11] = specType[3];
        }
        for (int i = 4; i < 8; i++) {
            p[i * 3] = 0;
            p[i * 3 + 1] = EegChannels.WAVE_TYPE_NONE;
            p[i * 3 + 2] = EegChannels.SPEC_TYPE_NONE;
        }
        byte[] data = EegChannels.buildDisplayConfigDataFromArray(p);
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_DISPLAY_CFG, data);
    }

    private void applyXRange(float xMax) {
        for (WaveformView wv : waveViews) if (wv != null) wv.setXMax(xMax);
        tvXRange.setText(String.format("%.0f s", xMax));
        currentXMax = xMax;
        SettingsStore.setWaveXMax(requireContext(), xMax);
    }

    private void applyWaveStep(int step, String unit, int labelCount) {
        float stepVolt;
        switch (unit) {
            case "uV": stepVolt = step / 1_000_000f; break;
            case "mV": stepVolt = step / 1000f; break;
            default:   stepVolt = step; break;
        }
        float halfRange = stepVolt * ((labelCount - 1) / 2.0f);
        for (WaveformView wv : waveViews) if (wv != null) {
            wv.setYRange(halfRange);
            wv.setUnit(unit);
        }
        tvWaveRange.setText(step + unit);

        currentStep = step;
        currentStepUnit = unit;
        currentLabelCount = labelCount;
        SettingsStore.setWaveStep(requireContext(), step);
        SettingsStore.setWaveStepUnit(requireContext(), unit);
        SettingsStore.setWaveLabelCount(requireContext(), labelCount);
    }

    private String formatSpecRange(float value, String unit) {
        return (Math.abs(value - Math.round(value)) < 0.001f
                ? String.format("%.0f", value) : String.format("%.3g", value)) + " " + unit;
    }

    private void showXRangeDialog() {
        View dialogView = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_range_input, null);
        EditText etValue = dialogView.findViewById(R.id.et_value);
        dialogView.findViewById(R.id.unit_spinner).setVisibility(View.GONE);
        etValue.setInputType(android.text.InputType.TYPE_CLASS_NUMBER | android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL);
        etValue.setHint("例如 2");
        etValue.setText(String.format("%.1f", currentXMax));
        new AlertDialog.Builder(requireContext())
                .setTitle("时间范围 (秒)")
                .setView(dialogView)
                .setPositiveButton("确定", (d, which) -> {
                    String str = etValue.getText().toString().trim();
                    if (str.isEmpty()) return;
                    try { float xMax = Math.round(Float.parseFloat(str) * 10f) / 10f; if (xMax <= 0) return; applyXRange(xMax); }
                    catch (NumberFormatException ignored) {}
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void showLabelCountDialog() {
        final String[] labels = {"3", "5", "7", "9"};
        int ci = 0;
        for (int i = 0; i < labels.length; i++) if (labels[i].equals(String.valueOf(currentLabelCount))) { ci = i; break; }
        new AlertDialog.Builder(requireContext())
                .setTitle("选择 Y 轴标签数量")
                .setSingleChoiceItems(labels, ci, (dialog, which) -> {
                    applyWaveStep(currentStep, currentStepUnit, Integer.parseInt(labels[which]));
                    dialog.dismiss();
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void showWaveRangeDialog() {
        View dialogView = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_range_input, null);
        EditText etValue = dialogView.findViewById(R.id.et_value);
        Spinner unitSpinner = dialogView.findViewById(R.id.unit_spinner);
        etValue.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
        ArrayAdapter<String> ua = new ArrayAdapter<>(requireContext(),
                android.R.layout.simple_spinner_item, new String[]{"uV", "mV", "V"});
        ua.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        unitSpinner.setAdapter(ua);
        etValue.setText(String.valueOf(currentStep));
        int pos = ua.getPosition(currentStepUnit);
        if (pos >= 0) unitSpinner.setSelection(pos);
        new AlertDialog.Builder(requireContext())
                .setTitle("设置波形步长")
                .setView(dialogView)
                .setPositiveButton("确定", (d, which) -> {
                    String str = etValue.getText().toString().trim();
                    if (str.isEmpty()) return;
                    try { int si = Integer.parseInt(str); if (si <= 0) return; applyWaveStep(si, (String) unitSpinner.getSelectedItem(), currentLabelCount); }
                    catch (NumberFormatException ignored) {}
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void showSpecRangeDialog() {
        View dialogView = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_range_input, null);
        EditText etValue = dialogView.findViewById(R.id.et_value);
        Spinner unitSpinner = dialogView.findViewById(R.id.unit_spinner);
        String[] units = {"uV", "mV", "V", "dBuV", "dBmV", "dBV"};
        ArrayAdapter<String> ua = new ArrayAdapter<>(requireContext(),
                android.R.layout.simple_spinner_item, units);
        ua.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        unitSpinner.setAdapter(ua);
        String cur = tvSpecRange.getText().toString();
        try {
            String[] parts = cur.split(" ");
            if (parts.length >= 2) {
                etValue.setText(parts[0]);
                int pos = java.util.Arrays.asList(units).indexOf(parts[1]);
                if (pos >= 0) unitSpinner.setSelection(pos);
            }
        } catch (Exception ignored) {}
        new AlertDialog.Builder(requireContext())
                .setTitle("设置频谱 Y 轴最大值")
                .setView(dialogView)
                .setPositiveButton("确定", (d, which) -> {
                    String str = etValue.getText().toString().trim();
                    if (str.isEmpty()) return;
                    try {
                        float val = Float.parseFloat(str);
                        if (val <= 0) return;
                        String unit = (String) unitSpinner.getSelectedItem();
                        for (SpectrumView sv : specViews) if (sv != null) sv.setRange(val, unit);
                        tvSpecRange.setText(formatSpecRange(val, unit));
                        SettingsStore.setSpecRange(requireContext(), val);
                        SettingsStore.setSpecUnit(requireContext(), unit);
                    } catch (NumberFormatException ignored) {}
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void showSpecXRangeDialog() {
        LinearLayout layout = new LinearLayout(requireContext());
        layout.setOrientation(LinearLayout.HORIZONTAL);
        int dp8 = (int) (8 * getResources().getDisplayMetrics().density);
        EditText etMin = new EditText(requireContext());
        etMin.setHint("起始Hz");
        etMin.setInputType(android.text.InputType.TYPE_CLASS_NUMBER | android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL);
        etMin.setText(String.valueOf((int) specXMin));
        EditText etMax = new EditText(requireContext());
        etMax.setHint("结束Hz");
        etMax.setInputType(android.text.InputType.TYPE_CLASS_NUMBER | android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL);
        etMax.setText(String.valueOf((int) specXMax));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        etMin.setLayoutParams(lp);
        etMax.setLayoutParams(lp);
        layout.addView(etMin);
        View div = new View(requireContext());
        div.setLayoutParams(new LinearLayout.LayoutParams(dp8, 1));
        layout.addView(div);
        layout.addView(etMax);
        layout.setPadding(dp8, dp8, dp8, dp8);
        new AlertDialog.Builder(requireContext())
                .setTitle("频谱 X 轴范围 (Hz)")
                .setView(layout)
                .setPositiveButton("确定", (d, which) -> {
                    try {
                        float min = Float.parseFloat(etMin.getText().toString().trim());
                        float max = Float.parseFloat(etMax.getText().toString().trim());
                        if (min >= max || max > 125f) return;
                        specXMin = Math.max(0f, min);
                        specXMax = max;
                        for (SpectrumView sv : specViews) if (sv != null) sv.setFreqRange(specXMin, specXMax);
                        tvSpecXRange.setText((int) specXMin + "-" + (int) specXMax);
                    } catch (NumberFormatException ignored) {}
                })
                .setNegativeButton("取消", null)
                .show();
    }

    @Override
    public void onResume() {
        super.onResume();
        DataDispatcher.getInstance().removeListener(this);
        DataDispatcher.getInstance().addListener(this);
        sendDisplayConfig();
        if (connectionListener == null) {
            connectionListener = new TcpServerManager.ConnectionListener() {
                @Override
                public void onDeviceConnected(boolean connected) {
                    if (connected && getActivity() != null) {
                        getActivity().runOnUiThread(() -> sendDisplayConfig());
                    }
                }
                @Override
                public void onPatientConnected(boolean connected) {}
            };
            TcpServerManager.getInstance().addConnectionListener(connectionListener);
        }
    }

    @Override
    public void onPause() {
        super.onPause();
        DataDispatcher.getInstance().removeListener(this);
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        DataDispatcher.getInstance().removeListener(this);
        if (connectionListener != null) {
            TcpServerManager.getInstance().removeConnectionListener(connectionListener);
            connectionListener = null;
        }
    }

    private static int waveTypeToCmd(int wt) {
        return EegChannels.waveTypeToCmd(wt);
    }

    @Override
    public void onWaveData(int cmd, int chNum, float val) {
        if (isPaused || !isWaveMode) return;
        for (int i = 0; i < NUM_VIEWS; i++) {
            if (cmd == waveTypeToCmd(waveType[i]) && chNum == waveCh[i] && waveViews[i] != null)
                waveViews[i].addPoint(val);
        }
    }

    @Override
    public void onSpectrumData(int cmd, float[] mags) {
        if (isPaused || isWaveMode) return;
        int chNum = EegChannels.spectrumCmdToChannel(cmd);
        int cmdType = EegChannels.spectrumCmdToType(cmd);
        if (chNum < 0 || cmdType < 0) return;
        for (int i = 0; i < NUM_VIEWS; i++) {
            if (specType[i] == cmdType && chNum == specCh[i] && specViews[i] != null) {
                specViews[i].updateSpectrum(mags);
            }
        }
    }

    @Override
    public void onFocusData(float attn0, float attn1, float ema0, float ema1, int trend, int instant) {}
}