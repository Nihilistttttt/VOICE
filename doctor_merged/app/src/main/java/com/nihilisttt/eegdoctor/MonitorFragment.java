package com.nihilisttt.eegdoctor;

import com.nihilisttt.eegdoctor.R;

import android.app.AlertDialog;
import android.content.Context;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.content.ContextCompat;
import androidx.fragment.app.Fragment;

import com.google.android.material.card.MaterialCardView;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public class MonitorFragment extends Fragment implements DataListener {

    private static final int DEFAULT_STEP = 500;
    private static final String DEFAULT_STEP_UNIT = "uV";
    private static final int DEFAULT_LABEL_COUNT = 7;
    private static final float DEFAULT_X_MAX = 2.048f;

    private final List<ChannelConfig> channels;
    private final List<WaveformView> waveformViews = new ArrayList<>();
    private final List<SpectrumView> spectrumViews = new ArrayList<>();
    private final List<View> signalQualityViews = new ArrayList<>();


    private TextView tvWaveLabelCount;
    private TextView tvWaveRange;
    private TextView tvXRange;
    private TextView tvSpectrumRange;
    private TextView tvSpecXRange;
    private TextView btnPauseResume;
    private boolean isPaused = false;
    private int currentLabelCount = DEFAULT_LABEL_COUNT;
    private boolean deviceConnected = false;
    private boolean patientConnected = false;
    private float specXMin = 0f;
    private float specXMax = 125f;

    private TcpServerManager.ConnectionListener connectionListener;

    private static final String[] CHANNEL_NAMES = EegChannels.NAMES;
    private static final String[] WAVE_TYPE_NAMES = EegChannels.WAVE_TYPE_NAMES;
    private static final String[] SPEC_TYPE_NAMES = EegChannels.SPEC_TYPE_NAMES;

    private int[] waveCh = {4, 5};
    private int[] waveType = {0, 0};
    private int[] specType = {0, 0};
    private int[] waveMode = {0, 0};

    private boolean is8ChMode = false;
    private boolean isTopoMode = false;
    private EegStripView stripView;
    private TopoMapView topoMapView;
    private View btn8ch;
    private View btnTopo;
    private View btnDisplayCfg;
    private View tvSpecLabel;
    private View tvHzLabel;
    private int current8ChWaveType = EegChannels.WAVE_TYPE_FILT;
    private int currentTopoWaveType = EegChannels.WAVE_TYPE_FILT;
    private float saved2ChXMax = DEFAULT_X_MAX;


    public MonitorFragment() {
        this.channels = ChannelConfig.getDefaultDualChannel();
    }

    public MonitorFragment(List<ChannelConfig> channels) {
        this.channels = channels != null ? channels : ChannelConfig.getDefaultDualChannel();
    }

    private void loadDisplayConfigFromStore() {
        waveCh[0] = SettingsStore.getWaveChA(requireContext(), 4);
        waveCh[1] = SettingsStore.getWaveChB(requireContext(), 5);
        waveType[0] = SettingsStore.getWaveTypeA(requireContext(), 0);
        waveType[1] = SettingsStore.getWaveTypeB(requireContext(), 0);
        specType[0] = SettingsStore.getSpecTypeA(requireContext(), 0);
        specType[1] = SettingsStore.getSpecTypeB(requireContext(), 0);
        waveMode[0] = SettingsStore.getWaveModeA(requireContext(), 0);
        waveMode[1] = SettingsStore.getWaveModeB(requireContext(), 0);
    }

    private void saveDisplayConfigToStore() {
        SettingsStore.setWaveChA(requireContext(), waveCh[0]);
        SettingsStore.setWaveChB(requireContext(), waveCh[1]);
        SettingsStore.setWaveTypeA(requireContext(), waveType[0]);
        SettingsStore.setWaveTypeB(requireContext(), waveType[1]);
        SettingsStore.setSpecTypeA(requireContext(), specType[0]);
        SettingsStore.setSpecTypeB(requireContext(), specType[1]);
        for (int i = 0; i < waveformViews.size() && i < 2; i++) {
            if (waveformViews.get(i) != null) {
                waveMode[i] = waveformViews.get(i).getMode();
            }
        }
        SettingsStore.setWaveModeA(requireContext(), waveMode[0]);
        SettingsStore.setWaveModeB(requireContext(), waveMode[1]);
    }

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View root = inflater.inflate(R.layout.fragment_monitor, container, false);

        loadDisplayConfigFromStore();

        rebuildChannelsList();

        LinearLayout channelContainer = root.findViewById(R.id.channel_container);
        buildChannelViews(channelContainer);



        tvWaveRange = root.findViewById(R.id.tv_wave_range);
        tvXRange = root.findViewById(R.id.tv_x_range);
        tvSpectrumRange = root.findViewById(R.id.tv_spectrum_range);
        tvSpecXRange = root.findViewById(R.id.tv_spec_x_range);
        btnPauseResume = root.findViewById(R.id.btn_pause_resume);

        btnPauseResume.setOnClickListener(v -> {
            isPaused = !isPaused;
            btnPauseResume.setText(isPaused ? "继续" : "暂停");
            updateConnectionStatus();
        });

        btnDisplayCfg = root.findViewById(R.id.btn_display_cfg);
        btnDisplayCfg.setOnClickListener(v -> showDisplayConfigDialog());
        tvSpecLabel = root.findViewById(R.id.tv_spec_label);
        tvHzLabel = root.findViewById(R.id.tv_hz_label);

        stripView = root.findViewById(R.id.eeg_strip_view);
        topoMapView = root.findViewById(R.id.topo_map_view);
        btn8ch = root.findViewById(R.id.btn_8ch);
        btn8ch.setOnClickListener(v -> toggle8ChMode());
        btnTopo = root.findViewById(R.id.btn_topo);
        btnTopo.setOnClickListener(v -> toggleTopoMode());

        connectionListener = new TcpServerManager.ConnectionListener() {
            @Override
            public void onDeviceConnected(boolean connected) {
                deviceConnected = connected;
                if (connected && getActivity() != null) {
                    getActivity().runOnUiThread(() -> {
                        updateConnectionStatus();
                        sendDisplayConfig();
                    });
                } else if (getActivity() != null) {
                    getActivity().runOnUiThread(() -> updateConnectionStatus());
                }
            }
            @Override
            public void onPatientConnected(boolean connected) {
                patientConnected = connected;
                if (getActivity() != null) getActivity().runOnUiThread(() -> updateConnectionStatus());
            }
        };
        TcpServerManager.getInstance().addConnectionListener(connectionListener);

        setupRangeSelectors();
        return root;
    }

    private void buildChannelViews(LinearLayout container) {
        Context ctx = requireContext();
        int channelCount = channels.size();
        boolean hasSpectrum = true;

        for (int i = 0; i < channelCount; i++) {
            ChannelConfig ch = channels.get(i);

            LinearLayout row = new LinearLayout(ctx);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setWeightSum(hasSpectrum ? 4f : 1f);

            MaterialCardView waveCard = new MaterialCardView(ctx);
            LinearLayout.LayoutParams waveCardParams = new LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.MATCH_PARENT, hasSpectrum ? 3f : 1f);
            waveCardParams.setMargins(2, 2, 2, 2);
            waveCard.setLayoutParams(waveCardParams);
            waveCard.setCardElevation(1f);
            waveCard.setRadius(6f);
            waveCard.setCardBackgroundColor(ContextCompat.getColor(ctx, R.color.surface_low));
            waveCard.setStrokeColor(ContextCompat.getColor(ctx, R.color.surface_overlay));
            waveCard.setStrokeWidth(1);

            LinearLayout waveInner = new LinearLayout(ctx);
            waveInner.setOrientation(LinearLayout.VERTICAL);

            LinearLayout headerRow = new LinearLayout(ctx);
            headerRow.setOrientation(LinearLayout.HORIZONTAL);
            headerRow.setBackgroundColor(ContextCompat.getColor(ctx, R.color.surface_mid));
            headerRow.setGravity(android.view.Gravity.CENTER_VERTICAL);
            int headerPad = (int) (4 * getResources().getDisplayMetrics().density);
            headerRow.setPadding(headerPad * 2, 0, headerPad, 0);

            View colorDot = new View(ctx);
            LinearLayout.LayoutParams dotParams = new LinearLayout.LayoutParams(6, 6);
            dotParams.setMargins(0, 0, headerPad, 0);
            colorDot.setLayoutParams(dotParams);
            colorDot.setBackgroundColor(ContextCompat.getColor(ctx, ch.getColorResId()));
            headerRow.addView(colorDot);

            TextView labelView = new TextView(ctx);
            labelView.setText(ch.getElectrodeName());
            labelView.setTextColor(ContextCompat.getColor(ctx, R.color.text_secondary));
            labelView.setTextSize(10);
            LinearLayout.LayoutParams labelParams = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            labelView.setLayoutParams(labelParams);
            headerRow.addView(labelView);


            View sqView = new View(ctx);
            LinearLayout.LayoutParams sqParams = new LinearLayout.LayoutParams(20, 4);
            sqParams.setMargins(headerPad, 0, 0, 0);
            sqView.setLayoutParams(sqParams);
            sqView.setBackgroundColor(ContextCompat.getColor(ctx, R.color.accent_success));
            headerRow.addView(sqView);
            signalQualityViews.add(sqView);

            waveInner.addView(headerRow);

            WaveformView wv = new WaveformView(ctx, null);
            wv.setId(View.generateViewId());
            LinearLayout.LayoutParams wvParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
            wv.setLayoutParams(wvParams);
            wv.setWaveColor(ContextCompat.getColor(ctx, ch.getColorResId()));
            wv.setYRange(ch.getYRange());
            wv.setUnit(ch.getUnit());
            waveInner.addView(wv);
            waveformViews.add(wv);

            waveCard.addView(waveInner);
            row.addView(waveCard);

            if (hasSpectrum) {
                MaterialCardView specCard = new MaterialCardView(ctx);
                LinearLayout.LayoutParams specCardParams = new LinearLayout.LayoutParams(
                        0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
                specCardParams.setMargins(2, 2, 2, 2);
                specCard.setLayoutParams(specCardParams);
                specCard.setCardElevation(1f);
                specCard.setRadius(6f);
                specCard.setCardBackgroundColor(ContextCompat.getColor(ctx, R.color.surface_low));
                specCard.setStrokeColor(ContextCompat.getColor(ctx, R.color.surface_overlay));
                specCard.setStrokeWidth(1);

                LinearLayout specInner = new LinearLayout(ctx);
                specInner.setOrientation(LinearLayout.VERTICAL);

                TextView specLabel = new TextView(ctx);
                specLabel.setText("频谱 " + ch.getElectrodeName());
                specLabel.setTextColor(ContextCompat.getColor(ctx, R.color.text_secondary));
                specLabel.setTextSize(10);
                specLabel.setBackgroundColor(ContextCompat.getColor(ctx, R.color.surface_mid));
                int slp = (int) (8 * getResources().getDisplayMetrics().density);
                specLabel.setPadding(slp, 0, 0, 0);
                specInner.addView(specLabel);

                SpectrumView sv = new SpectrumView(ctx, null);
                sv.setId(View.generateViewId());
                LinearLayout.LayoutParams svParams = new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
                sv.setLayoutParams(svParams);
                specInner.addView(sv);
                spectrumViews.add(sv);

                specCard.addView(specInner);
                row.addView(specCard);
            }

            LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
            container.addView(row, rowParams);
        }
    }

    private void setupRangeSelectors() {

        tvWaveRange.setOnClickListener(v -> showWaveRangeDialog());
        tvXRange.setOnClickListener(v -> showXRangeDialog());
        tvSpectrumRange.setOnClickListener(v -> showSpectrumRangeDialog());
        tvSpecXRange.setOnClickListener(v -> showSpecXRangeDialog());

        int step = SettingsStore.getWaveStep(requireContext(), DEFAULT_STEP);
        String stepUnit = SettingsStore.getWaveStepUnit(requireContext(), DEFAULT_STEP_UNIT);
        int labelCount = SettingsStore.getWaveLabelCount(requireContext(), DEFAULT_LABEL_COUNT);
        float xMax = SettingsStore.getWaveXMax(requireContext(), DEFAULT_X_MAX);
        float specRange = SettingsStore.getSpecRange(requireContext(), 500f);
        String specUnit = SettingsStore.getSpecUnit(requireContext(), "uV");

        currentLabelCount = labelCount;

        tvWaveRange.setText(step + stepUnit);
        tvXRange.setText(String.format("%.0f s", xMax));
        tvSpectrumRange.setText((Math.abs(specRange - Math.round(specRange)) < 0.001f ? String.format("%.0f", specRange) : String.format("%.3g", specRange)) + " " + specUnit);

        applyWaveStep(step, stepUnit, labelCount);
        applyXRange(xMax);
        for (SpectrumView sv : spectrumViews) {
            sv.setRange(specRange, specUnit);
            sv.setFreqRange(specXMin, specXMax);
        }
        tvSpecXRange.setText((int) specXMin + "-" + (int) specXMax);
        for (int i = 0; i < waveformViews.size() && i < 2; i++) {
            waveformViews.get(i).setMode(waveMode[i]);
        }
    }

    private void showLabelCountDialog() {
        final String[] labels = {"3", "5", "7", "9"};
        int currentIndex = 0;
        for (int i = 0; i < labels.length; i++) {
            if (labels[i].equals(String.valueOf(currentLabelCount))) { currentIndex = i; break; }
        }
        new AlertDialog.Builder(requireContext())
                .setTitle("选择 Y 轴标签数量")
                .setSingleChoiceItems(labels, currentIndex, (dialog, which) -> {
                    int newCount = Integer.parseInt(labels[which]);
                    currentLabelCount = newCount;

                    String stepText = tvWaveRange.getText().toString();
                    String numericPart = stepText.replaceAll("[^0-9]", "");
                    String unitPart = stepText.replaceAll("[0-9]", "");
                    if (!numericPart.isEmpty()) {
                        applyWaveStep(Integer.parseInt(numericPart), unitPart, newCount);
                    }
                    SettingsStore.setWaveLabelCount(requireContext(), newCount);
                    dialog.dismiss();
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void applyWaveStep(int step, String unit, int labelCount) {
        float stepVolt;
        switch (unit) {
            case "uV": stepVolt = step / 1_000_000f; break;
            case "mV": stepVolt = step / 1000f; break;
            default:   stepVolt = step; break;
        }
        float halfRange = stepVolt * ((labelCount - 1) / 2.0f);
        for (WaveformView wv : waveformViews) {
            wv.setYRange(halfRange);
            wv.setUnit(unit);
        }
        if (is8ChMode) {
            stripView.setYRange(halfRange);

        }
    }

    private void showWaveRangeDialog() {
        View dialogView = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_range_input, null);
        EditText etValue = dialogView.findViewById(R.id.et_value);
        Spinner unitSpinner = dialogView.findViewById(R.id.unit_spinner);
        etValue.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
        ArrayAdapter<String> unitAdapter = new ArrayAdapter<>(requireContext(),
                android.R.layout.simple_spinner_item, new String[]{"uV", "mV", "V"});
        unitAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        unitSpinner.setAdapter(unitAdapter);
        String currentText = tvWaveRange.getText().toString();
        try {
            String numericPart = currentText.replaceAll("[^0-9]", "");
            if (!numericPart.isEmpty()) {
                etValue.setText(numericPart);
                String curUnit = currentText.replaceAll("[0-9]", "");
                int pos = unitAdapter.getPosition(curUnit);
                if (pos >= 0) unitSpinner.setSelection(pos);
            }
        } catch (Exception ignored) {}
        new AlertDialog.Builder(requireContext())
                .setTitle("设置波形步长")
                .setView(dialogView)
                .setPositiveButton("确定", (d, which) -> {
                    String str = etValue.getText().toString().trim();
                    if (str.isEmpty()) return;
                    try {
                        int stepInt = Integer.parseInt(str);
                        if (stepInt <= 0) return;
                        String unit = (String) unitSpinner.getSelectedItem();
                        applyWaveStep(stepInt, unit, currentLabelCount);
                        tvWaveRange.setText(stepInt + unit);
                        SettingsStore.setWaveStep(requireContext(), stepInt);
                        SettingsStore.setWaveStepUnit(requireContext(), unit);
                    } catch (NumberFormatException ignored) {}
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void applyXRange(float xMax) {
        for (WaveformView wv : waveformViews) { wv.setXMax(xMax); }
        if (is8ChMode) stripView.setXMax(xMax);
        tvXRange.setText(String.format("%.0f s", xMax));
    }

    private void showXRangeDialog() {
        View dialogView = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_range_input, null);
        EditText etValue = dialogView.findViewById(R.id.et_value);
        Spinner unitSpinner = dialogView.findViewById(R.id.unit_spinner);
        unitSpinner.setVisibility(View.GONE);
        etValue.setInputType(android.text.InputType.TYPE_CLASS_NUMBER | android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL);
        etValue.setHint("例如 2");
        String current = tvXRange.getText().toString().replace(" s", "");
        etValue.setText(current);
        new AlertDialog.Builder(requireContext())
                .setTitle("时间范围 (秒)")
                .setView(dialogView)
                .setPositiveButton("确定", (d, which) -> {
                    String str = etValue.getText().toString().trim();
                    if (str.isEmpty()) return;
                    try {
                        float xMax = Math.round(Float.parseFloat(str) * 10f) / 10f;
                        if (xMax <= 0) return;
                        applyXRange(xMax);
                        SettingsStore.setWaveXMax(requireContext(), xMax);
                    } catch (NumberFormatException ignored) {}
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void showSpectrumRangeDialog() {
        View dialogView = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_range_input, null);
        EditText etValue = dialogView.findViewById(R.id.et_value);
        Spinner unitSpinner = dialogView.findViewById(R.id.unit_spinner);
        String[] units = {"uV", "mV", "V", "dBuV", "dBmV", "dBV"};
        ArrayAdapter<String> unitAdapter = new ArrayAdapter<>(requireContext(),
                android.R.layout.simple_spinner_item, units);
        unitAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        unitSpinner.setAdapter(unitAdapter);
        String currentText = tvSpectrumRange.getText().toString();
        try {
            String[] parts = currentText.split(" ");
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
                        float value = Float.parseFloat(str);
                        if (value <= 0) return;
                        String unit = (String) unitSpinner.getSelectedItem();
                        for (SpectrumView sv : spectrumViews) { sv.setRange(value, unit); }
                        tvSpectrumRange.setText((Math.abs(value - Math.round(value)) < 0.001f ? String.format("%.0f", value) : String.format("%.3g", value)) + " " + unit);
                        SettingsStore.setSpecRange(requireContext(), value);
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
                        for (SpectrumView sv : spectrumViews) {
                            sv.setFreqRange(specXMin, specXMax);
                        }
                        tvSpecXRange.setText((int) specXMin + "-" + (int) specXMax);
                    } catch (NumberFormatException ignored) {}
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void showDisplayConfigDialog() {
        Context ctx = requireContext();
        int dp8 = (int) (8 * getResources().getDisplayMetrics().density);
        int dp4 = (int) (4 * getResources().getDisplayMetrics().density);
        int dp12 = (int) (12 * getResources().getDisplayMetrics().density);

        ScrollView scrollView = new ScrollView(ctx);
        LinearLayout rootLayout = new LinearLayout(ctx);
        rootLayout.setOrientation(LinearLayout.VERTICAL);
        rootLayout.setPadding(dp12, dp8, dp12, dp8);
        rootLayout.setMinimumWidth((int) (480 * getResources().getDisplayMetrics().density));

        ArrayAdapter<String> chAdapter = new ArrayAdapter<>(ctx,
                android.R.layout.simple_spinner_item, CHANNEL_NAMES);
        chAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);

        ArrayAdapter<String> waveTypeAdapter = new ArrayAdapter<>(ctx,
                android.R.layout.simple_spinner_item, WAVE_TYPE_NAMES);
        waveTypeAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);

        ArrayAdapter<String> specTypeAdapter = new ArrayAdapter<>(ctx,
                android.R.layout.simple_spinner_item, SPEC_TYPE_NAMES);
        specTypeAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);

        String[] labels = {"A", "B"};
        Spinner[] spWaveCh = new Spinner[2];
        Spinner[] spWaveType = new Spinner[2];
        Spinner[] spSpecType = new Spinner[2];

        LinearLayout colRow = new LinearLayout(ctx);
        colRow.setOrientation(LinearLayout.HORIZONTAL);
        colRow.setWeightSum(2f);

        for (int i = 0; i < 2; i++) {
            LinearLayout col = new LinearLayout(ctx);
            col.setOrientation(LinearLayout.VERTICAL);
            LinearLayout.LayoutParams colP = new LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            col.setLayoutParams(colP);

            TextView chTitle = new TextView(ctx);
            chTitle.setText("通道 " + labels[i]);
            chTitle.setTextSize(13);
            chTitle.setTypeface(null, android.graphics.Typeface.BOLD);
            chTitle.setPadding(0, dp4, 0, dp4);
            col.addView(chTitle);

            spWaveCh[i] = new Spinner(ctx);
            spWaveCh[i].setAdapter(chAdapter);
            spWaveCh[i].setSelection(waveCh[i]);
            addRow(col, "电极", spWaveCh[i]);

            spWaveType[i] = new Spinner(ctx);
            spWaveType[i].setAdapter(waveTypeAdapter);
            spWaveType[i].setSelection(waveType[i]);
            addRow(col, "波形", spWaveType[i]);

            spSpecType[i] = new Spinner(ctx);
            spSpecType[i].setAdapter(specTypeAdapter);
            spSpecType[i].setSelection(specType[i]);
            addRow(col, "频谱", spSpecType[i]);

            colRow.addView(col);

            if (i == 0) {
                View vDiv = new View(ctx);
                LinearLayout.LayoutParams vdp = new LinearLayout.LayoutParams(
                        1, LinearLayout.LayoutParams.MATCH_PARENT);
                vdp.setMargins(dp8, 0, dp8, 0);
                vDiv.setLayoutParams(vdp);
                vDiv.setBackgroundColor(ContextCompat.getColor(ctx, R.color.surface_overlay));
                colRow.addView(vDiv);
            }
        }

        rootLayout.addView(colRow);
        scrollView.addView(rootLayout);

        new AlertDialog.Builder(ctx)
                .setTitle("显示配置")
                .setView(scrollView)
                .setPositiveButton("确定", (d, which) -> {
                    for (int i = 0; i < 2; i++) {
                        waveCh[i] = spWaveCh[i].getSelectedItemPosition();
                        waveType[i] = spWaveType[i].getSelectedItemPosition();
                        specType[i] = spSpecType[i].getSelectedItemPosition();
                    }
                    sendDisplayConfig();
                    rebuildChannelViews();
                    saveDisplayConfigToStore();
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void addRow(LinearLayout parent, String label, Spinner spinner) {
        LinearLayout row = new LinearLayout(requireContext());
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(android.view.Gravity.CENTER_VERTICAL);
        TextView tv = new TextView(requireContext());
        tv.setText(label);
        tv.setTextSize(13);
        tv.setPadding(0, 0, (int) (8 * getResources().getDisplayMetrics().density), 0);
        LinearLayout.LayoutParams labelParams = new LinearLayout.LayoutParams(
                (int) (48 * getResources().getDisplayMetrics().density),
                LinearLayout.LayoutParams.WRAP_CONTENT);
        tv.setLayoutParams(labelParams);
        row.addView(tv);
        LinearLayout.LayoutParams spParams = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        spinner.setLayoutParams(spParams);
        row.addView(spinner);
        parent.addView(row);
    }

    private void addDualRow(LinearLayout parent, String label, Spinner spA, Spinner spB) {
        LinearLayout row = new LinearLayout(requireContext());
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(android.view.Gravity.CENTER_VERTICAL);
        TextView tv = new TextView(requireContext());
        tv.setText(label);
        tv.setTextSize(13);
        tv.setPadding(0, 0, (int) (8 * getResources().getDisplayMetrics().density), 0);
        LinearLayout.LayoutParams labelParams = new LinearLayout.LayoutParams(
                (int) (48 * getResources().getDisplayMetrics().density),
                LinearLayout.LayoutParams.WRAP_CONTENT);
        tv.setLayoutParams(labelParams);
        row.addView(tv);
        LinearLayout.LayoutParams spParams = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        spA.setLayoutParams(spParams);
        row.addView(spA);
        TextView slash = new TextView(requireContext());
        slash.setText(" / ");
        slash.setTextSize(13);
        row.addView(slash);
        LinearLayout.LayoutParams spBParams = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        spB.setLayoutParams(spBParams);
        row.addView(spB);
        parent.addView(row);
    }

    private void sendDisplayConfig() {
        byte[] data;
        if (is8ChMode) {
            data = EegChannels.build8ChannelDisplayConfigData(
                    current8ChWaveType, EegChannels.SPEC_TYPE_NONE);
        } else if (isTopoMode) {
            data = EegChannels.build8ChannelDisplayConfigData(
                    currentTopoWaveType, EegChannels.SPEC_TYPE_NONE);
        } else {
            int[] p = new int[24];
            p[0] = waveCh[0]; p[1] = waveType[0]; p[2] = specType[0];
            p[3] = waveCh[1]; p[4] = waveType[1]; p[5] = specType[1];
            for (int i = 2; i < 8; i++) {
                p[i * 3] = 0;
                p[i * 3 + 1] = EegChannels.WAVE_TYPE_NONE;
                p[i * 3 + 2] = EegChannels.SPEC_TYPE_NONE;
            }
            data = EegChannels.buildDisplayConfigDataFromArray(p);
        }
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_DISPLAY_CFG, data);
        Log.i("DisplayConfig", "Sent DISPLAY_CFG binary");
    }

    private void rebuildChannelsList() {
        channels.clear();
        int[] colors = {
            R.color.wave_ch0, R.color.wave_ch1, R.color.wave_filtered,
            R.color.focus_line, R.color.spectrum_bar, R.color.accent_info,
            R.color.accent_success, R.color.accent_warning
        };
        for (int i = 0; i < 2; i++) {
            int ch = waveCh[i];
            String name = EegChannels.nameOf(ch);
            channels.add(new ChannelConfig("CH " + ch, name, ChannelConfig.ChannelType.EEG,
                    colors[ch % colors.length], 0.5f, "uV", EegChannels.CMD_WAVE_RAW));
        }
    }

    private void rebuildChannelViews() {
        int[] savedModes = new int[waveformViews.size()];
        for (int i = 0; i < waveformViews.size(); i++) {
            savedModes[i] = waveformViews.get(i).getMode();
        }

        LinearLayout container = requireView().findViewById(R.id.channel_container);
        container.removeAllViews();
        waveformViews.clear();
        spectrumViews.clear();

        signalQualityViews.clear();

        rebuildChannelsList();
        buildChannelViews(container);

        for (int i = 0; i < waveformViews.size(); i++) {
            WaveformView wv = waveformViews.get(i);
            wv.clear();
            if (i < savedModes.length) {
                wv.setMode(savedModes[i]);
            }
        }

        int step = SettingsStore.getWaveStep(requireContext(), DEFAULT_STEP);
        String stepUnit = SettingsStore.getWaveStepUnit(requireContext(), DEFAULT_STEP_UNIT);
        int labelCount = SettingsStore.getWaveLabelCount(requireContext(), DEFAULT_LABEL_COUNT);
        float xMax = SettingsStore.getWaveXMax(requireContext(), DEFAULT_X_MAX);
        float specRange = SettingsStore.getSpecRange(requireContext(), 500f);
        String specUnit = SettingsStore.getSpecUnit(requireContext(), "uV");

        applyWaveStep(step, stepUnit, labelCount);
        applyXRange(xMax);
        for (SpectrumView sv : spectrumViews) {
            sv.setRange(specRange, specUnit);
            sv.setFreqRange(specXMin, specXMax);
        }

        for (int i = 0; i < 2; i++) {
            int sCh = waveCh[i];
            String specName = EegChannels.nameOf(sCh);
            String typeName = (specType[i] >= 0 && specType[i] < SPEC_TYPE_NAMES.length) ? SPEC_TYPE_NAMES[specType[i]] : "";
            updateSpectrumLabel(container, i, specName + " " + typeName);
        }
    }

    private void updateSpectrumLabel(LinearLayout container, int rowIndex, String name) {
        if (rowIndex >= container.getChildCount()) return;
        View row = container.getChildAt(rowIndex);
        if (!(row instanceof LinearLayout)) return;
        LinearLayout rowLayout = (LinearLayout) row;
        for (int j = 0; j < rowLayout.getChildCount(); j++) {
            View child = rowLayout.getChildAt(j);
            if (child instanceof MaterialCardView) {
                MaterialCardView card = (MaterialCardView) child;
                if (card.getChildCount() > 0 && card.getChildAt(0) instanceof LinearLayout) {
                    LinearLayout inner = (LinearLayout) card.getChildAt(0);
                    for (int k = 0; k < inner.getChildCount(); k++) {
                        View innerChild = inner.getChildAt(k);
                        if (innerChild instanceof TextView && !(innerChild instanceof EditText)) {
                            TextView tv = (TextView) innerChild;
                            if (tv.getText().toString().startsWith("频谱")) {
                                tv.setText("频谱 " + name);
                                return;
                            }
                        }
                    }
                }
            }
        }
    }

    private void showWifiConfigDialog() {
        LinearLayout layout = new LinearLayout(requireContext());
        layout.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (12 * getResources().getDisplayMetrics().density);
        layout.setPadding(pad, pad, pad, pad);
        EditText etSsid = new EditText(requireContext());
        etSsid.setHint("SSID");
        layout.addView(etSsid);
        EditText etPassword = new EditText(requireContext());
        etPassword.setHint("密码");
        layout.addView(etPassword);
        new AlertDialog.Builder(requireContext())
                .setTitle("WiFi 配置")
                .setView(layout)
                .setPositiveButton("添加/修改", (d, which) -> {
                    String ssid = etSsid.getText().toString().trim();
                    String pwd = etPassword.getText().toString().trim();
                    if (ssid.isEmpty()) return;
                    TcpServerManager.getInstance().sendWifiAdd(ssid, pwd);
                    Toast.makeText(getContext(), "WiFi配置已发送: " + ssid, Toast.LENGTH_SHORT).show();
                })
                .setNeutralButton("删除", (d, which) -> {
                    String ssid = etSsid.getText().toString().trim();
                    if (ssid.isEmpty()) return;
                    TcpServerManager.getInstance().sendWifiDelete(ssid);
                    Toast.makeText(getContext(), "WiFi删除已发送: " + ssid, Toast.LENGTH_SHORT).show();
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
        Log.d("MonitorFragment", "onResume: listener refreshed, waveViews=" + waveformViews.size());
    }

    @Override
    public void onPause() {
        super.onPause();
        DataDispatcher.getInstance().removeListener(this);
        Log.d("MonitorFragment", "onPause: listener removed");
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        saveDisplayConfigToStore();
        DataDispatcher.getInstance().removeListener(this);
        if (connectionListener != null) {
            TcpServerManager.getInstance().removeConnectionListener(connectionListener);
            connectionListener = null;
        }
    }

    private static int waveTypeToCmd(int waveType) {
        return EegChannels.waveTypeToCmd(waveType);
    }

    @Override
    public void onWaveData(int cmd, int ch, float val) {
        if (isPaused) return;
        if (is8ChMode) {
            if (EegChannels.isWaveCmd(cmd) && cmd == EegChannels.waveTypeToCmd(current8ChWaveType)) {
                stripView.addPoint(ch, val);
            }
            return;
        }
        if (isTopoMode) {
            if (EegChannels.isWaveCmd(cmd) && cmd == EegChannels.waveTypeToCmd(currentTopoWaveType)) {
                topoMapView.addSample(ch, val);
            }
            return;
        }
        if (EegChannels.isWaveCmd(cmd)) {
            for (int i = 0; i < 2; i++) {
                if (ch == waveCh[i] && cmd == waveTypeToCmd(waveType[i])
                        && waveformViews.size() > i && waveformViews.get(i) != null)
                    waveformViews.get(i).addPoint(val);
            }
        }
    }

    @Override
    public void onEegFrame(EegFrame frame) {
        if (isPaused) return;
        if (is8ChMode) {
            float[] ch = frame.getChannels();
            for (int i = 0; i < ch.length; i++) stripView.addPoint(i, ch[i]);
            return;
        }
        if (isTopoMode) {
            float[] ch = frame.getChannels();
            for (int i = 0; i < ch.length; i++) topoMapView.addSample(i, ch[i]);
            return;
        }
        if (waveformViews.isEmpty()) return;
        float[] ch = frame.getChannels();
        for (int i = 0; i < waveformViews.size() && i < ch.length; i++) {
            if (waveformViews.get(i) != null) waveformViews.get(i).addPoint(ch[i]);
        }
    }

    @Override
    public void onSpectrumData(int cmd, float[] mags) {
        if (isPaused || is8ChMode || isTopoMode) return;
        int ch = EegChannels.spectrumCmdToChannel(cmd);
        int specTypeIdx = EegChannels.spectrumCmdToType(cmd);
        if (ch < 0 || specTypeIdx < 0) return;
        for (int i = 0; i < 2; i++) {
            if (ch == waveCh[i] && specType[i] == specTypeIdx && spectrumViews.size() > i) {
                spectrumViews.get(i).updateSpectrum(mags);
            }
        }
    }

    @Override
    public void onFocusData(float attn0, float attn1, float ema0, float ema1,
                            int trend, int instant) {
    }

    private void toggle8ChMode() {
        if (isTopoMode) return;
        is8ChMode = !is8ChMode;
        LinearLayout channelContainer = requireView().findViewById(R.id.channel_container);
        if (is8ChMode) {
            channelContainer.setVisibility(View.GONE);
            stripView.setVisibility(View.VISIBLE);
            int step = SettingsStore.getWaveStep(requireContext(), DEFAULT_STEP);
            String stepUnit = SettingsStore.getWaveStepUnit(requireContext(), DEFAULT_STEP_UNIT);
            int labelCount = SettingsStore.getWaveLabelCount(requireContext(), DEFAULT_LABEL_COUNT);
            float stepVolt;
            switch (stepUnit) {
                case "uV": stepVolt = step / 1_000_000f; break;
                case "mV": stepVolt = step / 1000f; break;
                default:   stepVolt = step; break;
            }
            stripView.setYRange(stepVolt * ((labelCount - 1) / 2.0f));

            saved2ChXMax = SettingsStore.getWaveXMax(requireContext(), DEFAULT_X_MAX);
            float x8 = saved2ChXMax * 2f;
            stripView.setXMax(x8);
            tvXRange.setText(String.format("%.0f s", x8));

            btnDisplayCfg.setOnClickListener(v -> toggle8ChWaveType());
            if (btnDisplayCfg instanceof TextView) ((TextView) btnDisplayCfg).setText(
                    current8ChWaveType == EegChannels.WAVE_TYPE_FILT ? "滤波" : "基线");

            tvSpecLabel.setVisibility(View.GONE);
            tvSpectrumRange.setVisibility(View.GONE);
            tvHzLabel.setVisibility(View.GONE);
            tvSpecXRange.setVisibility(View.GONE);
            if (btn8ch instanceof TextView) ((TextView) btn8ch).setText("2CH");
            sendDisplayConfig();
        } else {
            channelContainer.setVisibility(View.VISIBLE);
            stripView.setVisibility(View.GONE);

            applyXRange(saved2ChXMax);

            btnDisplayCfg.setOnClickListener(v -> showDisplayConfigDialog());
            if (btnDisplayCfg instanceof TextView) ((TextView) btnDisplayCfg).setText("通道");

            tvSpecLabel.setVisibility(View.VISIBLE);
            tvSpectrumRange.setVisibility(View.VISIBLE);
            tvHzLabel.setVisibility(View.VISIBLE);
            tvSpecXRange.setVisibility(View.VISIBLE);
            if (btn8ch instanceof TextView) ((TextView) btn8ch).setText("8CH");
            sendDisplayConfig();
        }
    }

    private void toggleTopoMode() {
        if (is8ChMode) return;
        isTopoMode = !isTopoMode;
        LinearLayout channelContainer = requireView().findViewById(R.id.channel_container);
        if (isTopoMode) {
            channelContainer.setVisibility(View.GONE);
            topoMapView.setVisibility(View.VISIBLE);

            btnDisplayCfg.setOnClickListener(v -> toggleTopoWaveType());
            if (btnDisplayCfg instanceof TextView) ((TextView) btnDisplayCfg).setText(
                    currentTopoWaveType == EegChannels.WAVE_TYPE_FILT ? "滤波" : "基线");

            tvSpecLabel.setVisibility(View.GONE);
            tvSpectrumRange.setVisibility(View.GONE);
            tvHzLabel.setVisibility(View.GONE);
            tvSpecXRange.setVisibility(View.GONE);
            if (btnTopo instanceof TextView) ((TextView) btnTopo).setText("退出");
            sendDisplayConfig();
        } else {
            channelContainer.setVisibility(View.VISIBLE);
            topoMapView.setVisibility(View.GONE);

            btnDisplayCfg.setOnClickListener(v -> showDisplayConfigDialog());
            if (btnDisplayCfg instanceof TextView) ((TextView) btnDisplayCfg).setText("通道");

            tvSpecLabel.setVisibility(View.VISIBLE);
            tvSpectrumRange.setVisibility(View.VISIBLE);
            tvHzLabel.setVisibility(View.VISIBLE);
            tvSpecXRange.setVisibility(View.VISIBLE);
            if (btnTopo instanceof TextView) ((TextView) btnTopo).setText("地形图");
            sendDisplayConfig();
        }
    }

    private void toggleTopoWaveType() {
        if (currentTopoWaveType == EegChannels.WAVE_TYPE_FILT) {
            currentTopoWaveType = EegChannels.WAVE_TYPE_BASELINE;
            if (btnDisplayCfg instanceof TextView) ((TextView) btnDisplayCfg).setText("基线");
        } else {
            currentTopoWaveType = EegChannels.WAVE_TYPE_FILT;
            if (btnDisplayCfg instanceof TextView) ((TextView) btnDisplayCfg).setText("滤波");
        }
        topoMapView.clear();
        sendDisplayConfig();
    }

    private void toggle8ChWaveType() {
        if (current8ChWaveType == EegChannels.WAVE_TYPE_FILT) {
            current8ChWaveType = EegChannels.WAVE_TYPE_BASELINE;
            if (btnDisplayCfg instanceof TextView) ((TextView) btnDisplayCfg).setText("基线");
        } else {
            current8ChWaveType = EegChannels.WAVE_TYPE_FILT;
            if (btnDisplayCfg instanceof TextView) ((TextView) btnDisplayCfg).setText("滤波");
        }
        stripView.clear();
        sendDisplayConfig();
    }

    private void updateConnectionStatus() {
    }

    private String getWifiApIpAddress() {
        try {
            java.util.List<java.net.NetworkInterface> interfaces =
                    java.util.Collections.list(java.net.NetworkInterface.getNetworkInterfaces());
            for (java.net.NetworkInterface intf : interfaces) {
                java.util.List<java.net.InetAddress> addrs =
                        java.util.Collections.list(intf.getInetAddresses());
                for (java.net.InetAddress addr : addrs) {
                    if (!addr.isLoopbackAddress() && addr.getAddress().length == 4) {
                        String ip = addr.getHostAddress();
                        if (ip.startsWith("192.168.")) return ip;
                    }
                }
            }
        } catch (Exception ignored) {}
        return "192.168.43.1";
    }
}