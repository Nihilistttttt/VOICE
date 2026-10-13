package com.nihilisttt.eegdoctor;

import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.Spinner;
import android.widget.ArrayAdapter;
import android.widget.AdapterView;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.core.content.ContextCompat;
import androidx.fragment.app.Fragment;

import com.google.android.material.button.MaterialButton;

import java.util.Locale;

/** Mode 1: the doctor selects one target frequency and the patient displays one full-screen stimulus. */
public class SsvepTrainingFragment extends Fragment implements DataListener, TrainingModeCoordinator.ModeController {

    private static final float[] TARGET_FREQS = {11.0f, 13.0f, 15.0f, 17.0f};
    private static final String[] FREQ_LABELS = {"11 Hz", "13 Hz", "15 Hz", "17 Hz"};
    private static final String[] DIRECTIONS = {"↑ 上", "↓ 下", "← 左", "→ 右"};

    private static final int DEFAULT_LABEL_COUNT = 7;
    private static final int DEFAULT_STEP = 500;
    private static final String DEFAULT_STEP_UNIT = "uV";
    private static final float DEFAULT_X_MAX = 2.048f;

    private SpectrumView spectrumO1;
    private SpectrumView spectrumOZ;
    private WaveformView waveOZ;
    private WaveformView waveO1;
    private TextView tvSpecYRange;
    private TextView tvSpecXRange;
    private TextView tvWaveYRange;
    private TextView tvWaveXRange;
    private TextView tvWaveLabelCount;
    private TextView tvStatus;
    private TextView tvSelectedFreq;
    private TextView tvResult;
    private TextView tvRawResult;
    private TextView tvScores;
    private TextView tvRatio;
    private TextView tvMargin;
    private TextView tvVote;
    private TextView tvProgress;
    private TextView tvWaveSource;
    private TextView tvWaveType;
    private int currentWaveType = EegChannels.WAVE_TYPE_FILT;
    private ProgressBar progressWindow;
    private MaterialButton btnFreq11;
    private MaterialButton btnFreq13;
    private MaterialButton btnFreq15;
    private MaterialButton btnFreq17;
    private MaterialButton btnStart;
    private MaterialButton btnSelfTest;
    private MaterialButton btnStop;

    private int selectedFreqIndex = -1;
    private boolean ssvepRunning;
    private float specYVal = 500f;
    private String specYUnit = "uV";
    private float specXMin = 0f;
    private float specXMax = 30f;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_ssvep_training, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        spectrumO1 = view.findViewById(R.id.spectrum_o1);
        spectrumOZ = view.findViewById(R.id.spectrum_oz);
        spectrumO1.setRange(specYVal, specYUnit);
        spectrumO1.setFreqRange(specXMin, specXMax);
        spectrumOZ.setRange(specYVal, specYUnit);
        spectrumOZ.setFreqRange(specXMin, specXMax);

        tvSpecYRange = view.findViewById(R.id.tv_spec_y_range);
        tvSpecXRange = view.findViewById(R.id.tv_spec_x_range);
        tvWaveYRange = view.findViewById(R.id.tv_wave_y_range);
        tvWaveXRange = view.findViewById(R.id.tv_wave_x_range);
        tvWaveLabelCount = view.findViewById(R.id.tv_wave_label_count);

        waveOZ = view.findViewById(R.id.wave_oz);
        waveO1 = view.findViewById(R.id.wave_o1);
        waveOZ.setWaveColor(ContextCompat.getColor(requireContext(), R.color.accent_info));
        waveO1.setWaveColor(ContextCompat.getColor(requireContext(), R.color.spectrum_bar));
        applyWaveSettings();
        tvStatus = view.findViewById(R.id.tv_ssvep_status);
        tvSelectedFreq = view.findViewById(R.id.tv_selected_freq);
        tvResult = view.findViewById(R.id.tv_ssvep_result);
        tvRawResult = view.findViewById(R.id.tv_ssvep_raw_result);
        tvScores = view.findViewById(R.id.tv_ssvep_scores);
        tvRatio = view.findViewById(R.id.tv_ssvep_ratio);
        tvMargin = view.findViewById(R.id.tv_ssvep_margin);
        tvVote = view.findViewById(R.id.tv_ssvep_vote);
        tvProgress = view.findViewById(R.id.tv_ssvep_progress);
        tvWaveSource = view.findViewById(R.id.tv_ssvep_wave_source);
        progressWindow = view.findViewById(R.id.progress_ssvep_window);
        btnFreq11 = view.findViewById(R.id.btn_freq_11);
        btnFreq13 = view.findViewById(R.id.btn_freq_13);
        btnFreq15 = view.findViewById(R.id.btn_freq_15);
        btnFreq17 = view.findViewById(R.id.btn_freq_17);
        btnStart = view.findViewById(R.id.btn_ssvep_start);
        btnSelfTest = view.findViewById(R.id.btn_ssvep_self_test);
        btnStop = view.findViewById(R.id.btn_ssvep_stop);

        tvWaveType = view.findViewById(R.id.tv_wave_type);
        tvWaveType.setOnClickListener(v -> toggleWaveType());

        tvSpecYRange.setOnClickListener(v -> showYRangeDialog());
        tvSpecXRange.setOnClickListener(v -> showXRangeDialog());
        tvWaveYRange.setOnClickListener(v -> showWaveYRangeDialog());
        tvWaveXRange.setOnClickListener(v -> showWaveXRangeDialog());
        tvWaveLabelCount.setOnClickListener(v -> showLabelCountDialog());

        btnFreq11.setOnClickListener(v -> selectFreq(0));
        btnFreq13.setOnClickListener(v -> selectFreq(1));
        btnFreq15.setOnClickListener(v -> selectFreq(2));
        btnFreq17.setOnClickListener(v -> selectFreq(3));
        btnStart.setOnClickListener(v -> startModeOne(false));
        btnSelfTest.setOnClickListener(v -> startModeOne(true));
        btnStop.setOnClickListener(v -> stopSsvep());

        updateFreqButtons();
        resetDisplay();
        DataDispatcher.getInstance().addListener(this);
        TrainingModeCoordinator.getInstance().registerController(
                TrainingModeCoordinator.Mode.SSVEP, this);
    }

    private void selectFreq(int index) {
        selectedFreqIndex = index;
        tvSelectedFreq.setText("当前目标: " + DIRECTIONS[index] + " / " + FREQ_LABELS[index]);
        updateFreqButtons();
        if (ssvepRunning) {
            SsvepAnalysisManager analysis = SsvepAnalysisManager.getInstance();
            analysis.switchFrequency(index);
            TcpServerManager server = TcpServerManager.getInstance();
            server.setSsvepActive(true, index);
            server.sendDisplayToOutputAsync(EegProtocol.CMD_SSVEP_START_P, new byte[]{(byte) index}, null);
            CommandSender.getInstance().ssvepStart(index);
        }
    }

    private void updateFreqButtons() {
        MaterialButton[] buttons = {btnFreq11, btnFreq13, btnFreq15, btnFreq17};
        int selectedColor = ContextCompat.getColor(requireContext(), R.color.brand_primary);
        int normalColor = ContextCompat.getColor(requireContext(), R.color.text_secondary);
        for (int i = 0; i < buttons.length; i++) {
            if (i == selectedFreqIndex) {
                buttons[i].setBackgroundColor(selectedColor);
                buttons[i].setTextColor(0xFFFFFFFF);
            } else {
                buttons[i].setBackgroundColor(0x00000000);
                buttons[i].setTextColor(normalColor);
            }
        }
    }

    private void startModeOne(boolean selfTest) {
        if (ssvepRunning) return;
        TrainingModeCoordinator.getInstance().activate(TrainingModeCoordinator.Mode.SSVEP);
        if (selectedFreqIndex < 0) {
            setStatus("请先选择一个目标频率", true);
            return;
        }
        TcpServerManager server = TcpServerManager.getInstance();
        if (!SettingsStore.isGlxssOutput(requireContext()) && !server.isPatientControlConnected()) {
            setStatus("患者控制通道未连接", true);
            return;
        }

        final int freqIndex = selectedFreqIndex;
        SsvepAnalysisManager analysis = SsvepAnalysisManager.getInstance();
        analysis.prepareSession(freqIndex);
        server.setSsvepActive(true, freqIndex);
        ssvepRunning = true;
        setRunningUi(true);
        resetDisplay();
        setStatus(selfTest
                ? "正在启动患者刺激与无设备算法自测"
                : "正在启动患者刺激，等待首帧回执", false);

        boolean queued = server.sendDisplayToOutputAsync(EegProtocol.CMD_SSVEP_START_P, new byte[]{(byte) freqIndex}, success -> {
            if (!isAdded()) return;
            if (!success) {
                analysis.stopSession();
                server.setSsvepActive(false, -1);
                ssvepRunning = false;
                setRunningUi(false);
                setStatus("患者刺激指令发送失败", true);
                return;
            }

            if (!ssvepRunning) return;
            // 41002 未连接时该命令只产生正常的未连接日志，不影响患者端和算法自测。
            CommandSender.getInstance().ssvepStart(freqIndex);
            if (selfTest) {
                CommandSender.getInstance().ssvepSelftestStart(freqIndex);
                analysis.confirmStimulusStarted(freqIndex, 240.0f);
                setStatus("MCU自测模式：V5F生成合成" + FbccaConfig.TARGET_FREQS[freqIndex] + "Hz信号", false);
            } else {
                analysis.confirmStimulusStarted(freqIndex, 240.0f);
                setStatus("患者刺激已下发；等待刺激首帧和脑电数据", false);
            }
        });

        if (!queued) {
            analysis.stopSession();
            server.setSsvepActive(false, -1);
            ssvepRunning = false;
            setRunningUi(false);
            setStatus("无法加入患者控制发送队列", true);
        }
    }

    private void stopSsvep() {
        TcpServerManager server = TcpServerManager.getInstance();
        boolean active = ssvepRunning
                || SsvepAnalysisManager.getInstance().isRunning()
                || server.isSsvepActive();
        if (!active) return;
        ssvepRunning = false;
        SsvepAnalysisManager.getInstance().stopSession();
        CommandSender.getInstance().ssvepStop();
        CommandSender.getInstance().ssvepSelftestStop();
        server.setSsvepActive(false, -1);
        server.sendDisplayToOutput(EegProtocol.CMD_SSVEP_STOP_P, null);
        setRunningUi(false);
        setStatus("已停止", false);
        progressWindow.setProgress(0);
        TrainingModeCoordinator.getInstance().release(TrainingModeCoordinator.Mode.SSVEP);
    }

    @Override
    public void stopForModeSwitch(TrainingModeCoordinator.Mode nextMode) {
        ssvepRunning = false;
        SsvepAnalysisManager.getInstance().stopSession();
        TcpServerManager.getInstance().setSsvepActive(false, -1);
        setRunningUi(false);
        progressWindow.setProgress(0);
    }

    private void setRunningUi(boolean running) {
        btnStart.setEnabled(!running);
        btnSelfTest.setEnabled(!running);
        btnStop.setEnabled(running);

    }

    private void setStatus(String message, boolean error) {
        tvStatus.setText("状态: " + message);
        tvStatus.setTextColor(error ? 0xFFFF5252 : 0xFF00A86B);
    }

    private void resetDisplay() {
        tvResult.setText("--");
        tvRawResult.setText("原始判决: --");
        tvScores.setText("11: --   13: --   15: --   17: --");
        tvRatio.setText("--");
        tvMargin.setText("--");
        tvVote.setText("投票: --");
        tvProgress.setText("步进: 0/" + FbccaConfig.STEP_SIZE + "  投票: 0/" + FbccaConfig.VOTE_HISTORY_LEN);
        tvWaveSource.setText(String.format(Locale.US,
                "波形源: 优先 0x%02X，1秒后回退 0x%02X；输入比例 ×%.0f mV",
                FbccaConfig.PREFERRED_WAVE_CMD,
                FbccaConfig.FALLBACK_WAVE_CMD,
                FbccaConfig.INPUT_SCALE_TO_MV));
        progressWindow.setProgress(0);
    }

    @Override
    public void onSsvepProgress(SsvepProgress progress) {
        if (progress == null || getView() == null) return;
        if (progress.getState() == SsvepProgress.State.ANALYZING
                && progress.getWindowSamples() == FbccaConfig.VOTE_HISTORY_LEN) {
            int votes = progress.getBufferedSamples();
            int voteWin = progress.getWindowSamples();
            int pct = Math.min(100, Math.round(votes * 100f / voteWin));
            progressWindow.setProgress(pct);
            tvProgress.setText(progress.getMessage());
        } else {
            int stepSize = FbccaConfig.STEP_SIZE;
            int untilNext = progress.getSamplesUntilNextUpdate();
            int stepCollected = stepSize - untilNext;
            if (stepCollected < 0) stepCollected = 0;
            if (stepCollected > stepSize) stepCollected = stepSize;
            int pct = Math.round(stepCollected * 100f / stepSize);
            progressWindow.setProgress(Math.min(100, pct));
            tvProgress.setText(String.format(Locale.US,
                    "步进: %d/%d  %s",
                    stepCollected, stepSize,
                    progress.getMessage()));
        }
        if (progress.getWaveCommand() > 0) {
            tvWaveSource.setText(String.format(Locale.US,
                    "当前波形源: CMD=0x%02X，通道0，输入换算×%.0f mV",
                    progress.getWaveCommand(), FbccaConfig.INPUT_SCALE_TO_MV));
        } else if (progress.isSynthetic()) {
            tvWaveSource.setText("当前波形源: 合成 SSVEP 测试信号");
        }
    }

    @Override
    public void onSsvepResult(SsvepResult result) {
        if (result == null || getView() == null) return;
        int totalVotes = result.getVote11() + result.getVote13()
                + result.getVote15() + result.getVote17();
        int votePct = Math.min(100, Math.round(totalVotes * 100f / FbccaConfig.VOTE_HISTORY_LEN));
        progressWindow.setProgress(votePct);
        tvProgress.setText(String.format(Locale.US,
                "投票: %d/%d（%d%%）",
                totalVotes, FbccaConfig.VOTE_HISTORY_LEN, votePct));

        int finalIndex = result.getFreqIndex();
        int rawIndex = result.getRawFreqIndex();
        if (!result.isChannelUsable()) {
            tvResult.setText("通道质量不合格");
            tvResult.setTextColor(0xFFD32F2F);
        } else if (result.isUncertain()) {
            tvResult.setText("UNCERTAIN");
            tvResult.setTextColor(0xFFFF9800);
        } else {
            tvResult.setText(DIRECTIONS[finalIndex] + " / " + result.getFreq() + " Hz");
            tvResult.setTextColor(0xFF00C853);
        }
        tvRawResult.setText("原始判决: "
                + (rawIndex >= 0 ? DIRECTIONS[rawIndex] + " / " + result.getRawFreq() + " Hz" : "UNCERTAIN")
                + (result.isSynthetic() ? "（自测）" : ""));
        tvScores.setText(String.format(Locale.US,
                "11: %.4f   13: %.4f\n15: %.4f   17: %.4f",
                result.getScore11(), result.getScore13(),
                result.getScore15(), result.getScore17()));
        tvRatio.setText(String.format(Locale.US, "%.3f", result.getRatio()));
        tvMargin.setText(String.format(Locale.US, "%.5f", result.getMargin()));
        tvVote.setText(String.format(Locale.US,
                "投票 11:%d  13:%d  15:%d  17:%d",
                result.getVote11(), result.getVote13(), result.getVote15(), result.getVote17()));
    }

    private void toggleWaveType() {
        currentWaveType = (currentWaveType + 1) % 3;
        String[] labels = {"原始", "滤波", "基线"};
        tvWaveType.setText(labels[currentWaveType]);
        if (waveOZ != null) waveOZ.clear();
        if (waveO1 != null) waveO1.clear();
        sendDisplayConfig();
    }

    private void sendDisplayConfig() {
        int[] p = new int[24];
        p[0] = EegChannels.CH_O1; p[1] = currentWaveType; p[2] = EegChannels.SPEC_TYPE_RAW;
        p[3] = EegChannels.CH_OZ; p[4] = currentWaveType; p[5] = EegChannels.SPEC_TYPE_RAW;
        for (int i = 2; i < 8; i++) {
            p[i * 3] = 0;
            p[i * 3 + 1] = EegChannels.WAVE_TYPE_NONE;
            p[i * 3 + 2] = EegChannels.SPEC_TYPE_NONE;
        }
        byte[] data = EegChannels.buildDisplayConfigDataFromArray(p);
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_DISPLAY_CFG, data);
        Log.i("SsvepTraining", "Sent DISPLAY_CFG binary");
    }

    @Override
    public void onResume() {
        super.onResume();
        DataDispatcher.getInstance().removeListener(this);
        DataDispatcher.getInstance().addListener(this);
        sendDisplayConfig();
    }

    @Override
    public void onPause() {
        super.onPause();
        if (ssvepRunning) stopSsvep();
        DataDispatcher.getInstance().removeListener(this);
    }

    @Override
    public void onSpectrumData(int cmd, float[] mags) {
        int ch = EegChannels.spectrumCmdToChannel(cmd);
        int specType = EegChannels.spectrumCmdToType(cmd);
        if (ch < 0 || specType < 0) return;
        if (ch == EegChannels.CH_O1 && specType == EegChannels.SPEC_TYPE_RAW && spectrumO1 != null)
            spectrumO1.updateSpectrum(mags);
        if (ch == EegChannels.CH_OZ && specType == EegChannels.SPEC_TYPE_RAW && spectrumOZ != null)
            spectrumOZ.updateSpectrum(mags);
    }

    private void showYRangeDialog() {
        View dialogView = LayoutInflater.from(requireContext())
                .inflate(R.layout.dialog_range_input, null);
        EditText etValue = dialogView.findViewById(R.id.et_value);
        Spinner unitSpinner = dialogView.findViewById(R.id.unit_spinner);
        String[] units = {"uV", "mV", "V", "dBuV", "dBmV", "dBV"};
        ArrayAdapter<String> ua = new ArrayAdapter<>(requireContext(),
                android.R.layout.simple_spinner_item, units);
        ua.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        unitSpinner.setAdapter(ua);
        new AlertDialog.Builder(requireContext())
                .setTitle("频谱 Y 轴最大值")
                .setView(dialogView)
                .setPositiveButton("确定", (d, which) -> {
                    String str = etValue.getText().toString().trim();
                    if (str.isEmpty()) return;
                    try {
                        float val = Float.parseFloat(str);
                        if (val <= 0) return;
                        String unit = (String) unitSpinner.getSelectedItem();
                        specYVal = val;
                        specYUnit = unit;
                        if (spectrumO1 != null) spectrumO1.setRange(val, unit);
                        if (spectrumOZ != null) spectrumOZ.setRange(val, unit);
                        tvSpecYRange.setText(formatSpecRange(val, unit));
                    } catch (NumberFormatException ignored) {}
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void showXRangeDialog() {
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
                        if (spectrumO1 != null) spectrumO1.setFreqRange(specXMin, specXMax);
                        if (spectrumOZ != null) spectrumOZ.setFreqRange(specXMin, specXMax);
                        tvSpecXRange.setText((int) specXMin + "-" + (int) specXMax + " Hz");
                    } catch (NumberFormatException ignored) {}
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private static String formatSpecRange(float val, String unit) {
        if (val == (int) val) return (int) val + " " + unit;
        return String.format(Locale.US, "%.1f %s", val, unit);
    }

    @Override
    public void onDestroyView() {
        TrainingModeCoordinator.getInstance().unregisterController(
                TrainingModeCoordinator.Mode.SSVEP, this);
        DataDispatcher.getInstance().removeListener(this);
        stopSsvep();
        super.onDestroyView();
    }

    private void applyWaveSettings() {
        int step = SettingsStore.getWaveStep(requireContext(), DEFAULT_STEP);
        String stepUnit = SettingsStore.getWaveStepUnit(requireContext(), DEFAULT_STEP_UNIT);
        int labelCount = SettingsStore.getWaveLabelCount(requireContext(), DEFAULT_LABEL_COUNT);
        float xMax = SettingsStore.getWaveXMax(requireContext(), DEFAULT_X_MAX);
        applyWaveXRange(xMax);
        applyWaveStep(step, stepUnit, labelCount);
    }

    private void applyWaveXRange(float xMax) {
        if (waveOZ != null) waveOZ.setXMax(xMax);
        if (waveO1 != null) waveO1.setXMax(xMax);
        tvWaveXRange.setText(String.format(Locale.US, "%.0f s", xMax));
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
        if (waveOZ != null) { waveOZ.setYRange(halfRange); waveOZ.setUnit(unit); }
        if (waveO1 != null) { waveO1.setYRange(halfRange); waveO1.setUnit(unit); }
        tvWaveYRange.setText(step + unit);
        tvWaveLabelCount.setText(String.valueOf(labelCount));
        SettingsStore.setWaveStep(requireContext(), step);
        SettingsStore.setWaveStepUnit(requireContext(), unit);
        SettingsStore.setWaveLabelCount(requireContext(), labelCount);
    }

    private void showWaveYRangeDialog() {
        View dialogView = LayoutInflater.from(requireContext())
                .inflate(R.layout.dialog_range_input, null);
        EditText etValue = dialogView.findViewById(R.id.et_value);
        Spinner unitSpinner = dialogView.findViewById(R.id.unit_spinner);
        etValue.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
        ArrayAdapter<String> ua = new ArrayAdapter<>(requireContext(),
                android.R.layout.simple_spinner_item, new String[]{"uV", "mV", "V"});
        ua.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        unitSpinner.setAdapter(ua);
        etValue.setText(String.valueOf(SettingsStore.getWaveStep(requireContext(), DEFAULT_STEP)));
        int pos = ua.getPosition(SettingsStore.getWaveStepUnit(requireContext(), DEFAULT_STEP_UNIT));
        if (pos >= 0) unitSpinner.setSelection(pos);
        new AlertDialog.Builder(requireContext())
                .setTitle("设置波形步长")
                .setView(dialogView)
                .setPositiveButton("确定", (d, which) -> {
                    String str = etValue.getText().toString().trim();
                    if (str.isEmpty()) return;
                    try {
                        int si = Integer.parseInt(str);
                        if (si <= 0) return;
                        applyWaveStep(si, (String) unitSpinner.getSelectedItem(),
                                SettingsStore.getWaveLabelCount(requireContext(), DEFAULT_LABEL_COUNT));
                    } catch (NumberFormatException ignored) {}
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void showWaveXRangeDialog() {
        View dialogView = LayoutInflater.from(requireContext())
                .inflate(R.layout.dialog_range_input, null);
        EditText etValue = dialogView.findViewById(R.id.et_value);
        Spinner unitSpinner = dialogView.findViewById(R.id.unit_spinner);
        ArrayAdapter<String> ua = new ArrayAdapter<>(requireContext(),
                android.R.layout.simple_spinner_item, new String[]{"秒"});
        ua.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        unitSpinner.setAdapter(ua);
        etValue.setInputType(android.text.InputType.TYPE_CLASS_NUMBER
                | android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL);
        etValue.setText(String.format(Locale.US, "%.1f",
                SettingsStore.getWaveXMax(requireContext(), DEFAULT_X_MAX)));
        new AlertDialog.Builder(requireContext())
                .setTitle("波形时间范围")
                .setView(dialogView)
                .setPositiveButton("确定", (d, which) -> {
                    String str = etValue.getText().toString().trim();
                    if (str.isEmpty()) return;
                    try {
                        float val = Math.round(Float.parseFloat(str) * 10f) / 10f;
                        if (val <= 0) return;
                        applyWaveXRange(val);
                    } catch (NumberFormatException ignored) {}
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void showLabelCountDialog() {
        View dialogView = LayoutInflater.from(requireContext())
                .inflate(R.layout.dialog_range_input, null);
        EditText etValue = dialogView.findViewById(R.id.et_value);
        Spinner unitSpinner = dialogView.findViewById(R.id.unit_spinner);
        ArrayAdapter<String> ua = new ArrayAdapter<>(requireContext(),
                android.R.layout.simple_spinner_item, new String[]{"个"});
        ua.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        unitSpinner.setAdapter(ua);
        etValue.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
        etValue.setText(String.valueOf(SettingsStore.getWaveLabelCount(requireContext(), DEFAULT_LABEL_COUNT)));
        new AlertDialog.Builder(requireContext())
                .setTitle("设置波形标签数量")
                .setView(dialogView)
                .setPositiveButton("确定", (d, which) -> {
                    String str = etValue.getText().toString().trim();
                    if (str.isEmpty()) return;
                    try {
                        int n = Integer.parseInt(str);
                        if (n < 2) n = 2;
                        applyWaveStep(SettingsStore.getWaveStep(requireContext(), DEFAULT_STEP),
                                SettingsStore.getWaveStepUnit(requireContext(), DEFAULT_STEP_UNIT), n);
                    } catch (NumberFormatException ignored) {}
                })
                .setNegativeButton("取消", null)
                .show();
    }

    @Override public void onWaveData(int cmd, int ch, float val) {
        if (cmd != EegChannels.waveTypeToCmd(currentWaveType)) return;
        if (ch == EegChannels.CH_OZ && waveOZ != null) waveOZ.addPoint(val);
        else if (ch == EegChannels.CH_O1 && waveO1 != null) waveO1.addPoint(val);
    }
    @Override public void onFocusData(float attn0, float attn1, float ema0, float ema1, int trend, int instant) {}
}
