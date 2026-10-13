package com.nihilisttt.eegdoctor;

import com.nihilisttt.eegdoctor.R;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.Spinner;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.content.ContextCompat;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.google.android.material.card.MaterialCardView;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MiTrainingFragment extends Fragment implements DataListener, TrainingModeCoordinator.ModeController {

    private static final int STATE_IDLE = 0;
    private static final int STATE_WAIT_READY = 1;
    private static final int STATE_REST = 2;
    private static final int STATE_TRIAL_ACTIVE = 3;
    private static final int STATE_MODEL_TRAINING = 4;

    private static final int REST_DURATION_MS = 5000;
    private static final int MI_TRIAL_DURATION_MS = 15000;
    private static final String[] TRIAL_SEQUENCE = {"LEFT", "RIGHT", "LEFT", "RIGHT"};

    private int currentState = STATE_IDLE;
    private int currentTrialIndex = 0;
    private boolean isTraining = false;
    private boolean waitingReadyTrain = false;


    private TextView tvDirection;
    private TextView tvHint;
    private TextView tvTrialCount;
    private ProgressBar progressTrial;
    private View btnStartTraining;
    private View btnStopTraining;
    private EegStripView stripView;
    private LinearLayout gridContainer;
    private final WaveformView[] waveViews = new WaveformView[4];
    private final TextView[] labelViews = new TextView[4];
    private TextView tvWaveType;
    private TextView tvYRange;
    private TextView tvXRange;
    private View btnToggleLayout;

    private static final int[] MI_CHANNELS = {
            EegChannels.CH_C3, EegChannels.CH_C4,
            EegChannels.CH_CP3, EegChannels.CH_CP4
    };
    private static final int[] MI_COLORS = {
            R.color.wave_ch6, R.color.wave_ch7,
            R.color.wave_ch4, R.color.wave_ch5
    };

    private int currentWaveType = EegChannels.WAVE_TYPE_FILT;
    private float yRangeUv = 100f;
    private float splitXMax = 2.0f;
    private float xMax = 4.0f;
    private boolean isCombinedMode = true;
    private int miMode = 0;

    private View cardTraining;
    private View cardInference;
    private View cardDirTrain;
    private View btnModeToggle;

    private TextView tvInferTargetArrow;
    private TextView tvInferResultArrow;
    private final Handler arrowHandler = new Handler(Looper.getMainLooper());
    private TextView tvInferConfidence;
    private TextView tvInferScores;
    private View btnStartInfer;
    private View btnStopInfer;
    private Spinner spinnerInferMode;
    private Spinner spinnerInferRounds;
    private TextView tvStatTotal;
    private TextView tvStatAccuracy;
    private TextView tvStatAvgConf;
    private TextView tvStatLeft;
    private TextView tvStatRight;
    private View btnExport;
    private View btnClearRecords;
    private RecyclerView rvRecords;
    private InferenceRecordAdapter inferAdapter;

    private RecyclerView rvDirFiles;
    private DirFileAdapter dirFileAdapter;
    private RecyclerView rvDirModels;
    private DirModelAdapter dirModelAdapter;
    private View btnTrainSelected, btnSendWeight, btnMergeSelected, btnDeleteSelected, btnDirBack, btnImportFile, btnExportModel, btnDeleteModel, btnScanSd;
    private Spinner spinnerPatient;
    private final List<String[]> patientList = new ArrayList<>();
    private int selectedPatientIdx = 0;
    private static final int REQ_IMPORT_DIR_FILE = 1001;
    private static final String PREF_PATIENTS = "dir_patients_data";
    private TextView tvTrainResult, tvDirLog;
    private DirLdaTrainer.TrainResult lastDirResult = null;
    private final List<DirTrainStore.StoredSample> dirCollectBuffer = new ArrayList<>();
    private final ExecutorService dirBgExecutor = Executors.newSingleThreadExecutor(r -> { Thread t = new Thread(r, "dir-train-bg"); t.setDaemon(true); return t; });
    private final StringBuilder dirLogBuilder = new StringBuilder();
    private final java.text.SimpleDateFormat dirTimeFmt = new java.text.SimpleDateFormat("HH:mm:ss", Locale.getDefault());

    private boolean isInferencing = false;
    private int resultCountSinceLastRefresh = 0;
    private String currentTarget = null;
    private final ExecutorService recordWriter = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "inference-record-writer");
        t.setDaemon(true);
        return t;
    });
    private final Handler targetHandler = new Handler(Looper.getMainLooper());

    private Handler handler = new Handler(Looper.getMainLooper());
    private long restStartTime;
    private long trialStartTime;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View root = inflater.inflate(R.layout.fragment_mi_training, container, false);

        tvDirection = root.findViewById(R.id.tv_mi_direction);
        tvHint = root.findViewById(R.id.tv_mi_hint);
        tvTrialCount = root.findViewById(R.id.tv_mi_trial_count);
        progressTrial = root.findViewById(R.id.progress_mi_trial);
        btnStartTraining = root.findViewById(R.id.btn_mi_start_training);
        btnStopTraining = root.findViewById(R.id.btn_mi_stop_training);
        stripView = root.findViewById(R.id.strip_view_mi);
        stripView.setVisibleChannels(MI_CHANNELS);
        stripView.setYRange(yRangeUv / 1_000_000f);
        stripView.setXMax(xMax);

        gridContainer = root.findViewById(R.id.mi_grid_container);
        buildGrid();

        tvWaveType = root.findViewById(R.id.tv_mi_wave_type);
        tvYRange = root.findViewById(R.id.tv_mi_y_range);
        tvXRange = root.findViewById(R.id.tv_mi_x_range);
        btnToggleLayout = root.findViewById(R.id.btn_mi_toggle_layout);

        tvWaveType.setOnClickListener(v -> toggleWaveType());
        tvYRange.setOnClickListener(v -> showYRangeDialog());
        tvXRange.setOnClickListener(v -> showXRangeDialog());
        btnToggleLayout.setOnClickListener(v -> toggleLayout());

        btnStartTraining.setOnClickListener(v -> startTraining());
        btnStopTraining.setOnClickListener(v -> stopTraining());
        TrainingModeCoordinator.getInstance().registerController(
                TrainingModeCoordinator.Mode.MI, this);

        cardTraining = root.findViewById(R.id.card_mi_training);
        cardInference = root.findViewById(R.id.card_mi_inference);
        cardDirTrain = root.findViewById(R.id.card_dir_train);
        btnModeToggle = root.findViewById(R.id.btn_mi_mode_toggle);
        btnModeToggle.setOnClickListener(v -> toggleMode());

        initDirTrainViews(root);
        applyMode();

        tvInferTargetArrow = root.findViewById(R.id.tv_infer_target_arrow);
        tvInferResultArrow = root.findViewById(R.id.tv_infer_result_arrow);
        tvInferConfidence = root.findViewById(R.id.tv_infer_confidence);
        tvInferScores = root.findViewById(R.id.tv_infer_scores);
        btnStartInfer = root.findViewById(R.id.btn_start_infer);
        btnStopInfer = root.findViewById(R.id.btn_stop_infer);
        spinnerInferMode = root.findViewById(R.id.spinner_infer_mode);
        spinnerInferRounds = root.findViewById(R.id.spinner_infer_rounds);
        tvStatTotal = root.findViewById(R.id.tv_stat_total);
        tvStatAccuracy = root.findViewById(R.id.tv_stat_accuracy);
        tvStatAvgConf = root.findViewById(R.id.tv_stat_avg_conf);
        tvStatLeft = root.findViewById(R.id.tv_stat_left);
        tvStatRight = root.findViewById(R.id.tv_stat_right);
        btnExport = root.findViewById(R.id.btn_export);
        btnClearRecords = root.findViewById(R.id.btn_clear_records);

        ArrayAdapter<String> modeAdapter = new ArrayAdapter<>(requireContext(),
                android.R.layout.simple_spinner_item, new String[]{"循环", "随机"});
        modeAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinnerInferMode.setAdapter(modeAdapter);
        ArrayAdapter<String> roundsAdapter = new ArrayAdapter<>(requireContext(),
                android.R.layout.simple_spinner_item, new String[]{"1次","2次","3次","4次","5次","6次","7次","8次","9次","10次"});
        roundsAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinnerInferRounds.setAdapter(roundsAdapter);
        spinnerInferRounds.setSelection(2);
        spinnerInferRounds.setEnabled(false);
        spinnerInferMode.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(android.widget.AdapterView<?> parent, View view, int position, long id) {
                spinnerInferRounds.setEnabled(position == 0);
            }
            @Override public void onNothingSelected(android.widget.AdapterView<?> parent) {}
        });

        btnStartInfer.setOnClickListener(v -> startInference());
        btnStopInfer.setOnClickListener(v -> stopInference());
        btnExport.setOnClickListener(v -> exportRecords());
        btnClearRecords.setOnClickListener(v -> confirmClearRecords());
        refreshStats();

        return root;
    }

    private void toggleMode() {
        miMode = (miMode + 1) % 3;
        applyMode();
        sendMiDisplayConfig();
    }

    private void applyMode() {
        com.google.android.material.button.MaterialButton btn = (com.google.android.material.button.MaterialButton) btnModeToggle;
        if (miMode == 0) {
            cardTraining.setVisibility(View.VISIBLE);
            cardInference.setVisibility(View.GONE);
            cardDirTrain.setVisibility(View.GONE);
            btn.setText("切换到推理");
        } else if (miMode == 1) {
            cardTraining.setVisibility(View.GONE);
            cardInference.setVisibility(View.VISIBLE);
            cardDirTrain.setVisibility(View.GONE);
            btn.setText("切换到训练");
        } else {
            cardTraining.setVisibility(View.GONE);
            cardInference.setVisibility(View.GONE);
            cardDirTrain.setVisibility(View.VISIBLE);
            btn.setText("切换到采集");
            refreshDirFileList();
        }
    }

    private static final long READY_TRAIN_TIMEOUT_MS = 15000L;

    private void startTraining() {
        Log.i("MiTraining", "btnStartTraining clicked, isTraining=" + isTraining);
        if (isTraining) return;
        TrainingModeCoordinator.getInstance().activate(TrainingModeCoordinator.Mode.MI);
        isTraining = true;
        currentTrialIndex = 0;
        waitingReadyTrain = true;
        synchronized (dirCollectBuffer) { dirCollectBuffer.clear(); }

        btnStartTraining.setEnabled(false);
        btnStopTraining.setEnabled(true);

        tvDirection.setText("启动中...");
        tvDirection.setTextColor(ContextCompat.getColor(requireContext(), R.color.accent_info));
        tvHint.setText("等待下位机就绪");
        tvTrialCount.setText("");
        progressTrial.setProgress(0);

        CommandSender.getInstance().setModeCollect();
        CommandSender.getInstance().startTraining();

        handler.postDelayed(() -> {
            if (waitingReadyTrain) {
                stopTrainingInternal(true);
                tvDirection.setText("超时");
                tvDirection.setTextColor(ContextCompat.getColor(requireContext(), R.color.accent_error));
                tvHint.setText("下位机未响应，请检查连接");
            }
        }, READY_TRAIN_TIMEOUT_MS);
    }

    private void stopTraining() {
        stopTrainingInternal(true);
        TrainingModeCoordinator.getInstance().release(TrainingModeCoordinator.Mode.MI);
    }

    private void stopTrainingInternal(boolean updateUi) {
        Log.i("MiTraining", "stopTraining");
        boolean wasActive = isTraining || currentState != STATE_IDLE || waitingReadyTrain;
        isTraining = false;
        waitingReadyTrain = false;
        currentState = STATE_IDLE;
        handler.removeCallbacksAndMessages(null);
        if (wasActive) {
            TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_STOP, null);
            TcpServerManager.getInstance().sendDisplayToOutput(EegProtocol.CMD_TRAIN_STOP, null);
        }
        saveDirCollectBuffer();
        if (updateUi && getView() != null) {
            btnStartTraining.setEnabled(true);
            btnStopTraining.setEnabled(false);
            tvDirection.setText("已停止");
            if (getContext() != null) {
                tvDirection.setTextColor(ContextCompat.getColor(requireContext(), R.color.training_rest));
            }
            tvHint.setText("点击下方按钮开始训练");
            tvTrialCount.setText("");
            progressTrial.setIndeterminate(false);
            progressTrial.setProgress(0);
        }
    }

    @Override
    public void stopForModeSwitch(TrainingModeCoordinator.Mode nextMode) {
        isTraining = false;
        waitingReadyTrain = false;
        currentState = STATE_IDLE;
        handler.removeCallbacksAndMessages(null);
        if (getView() != null) {
            btnStartTraining.setEnabled(true);
            btnStopTraining.setEnabled(false);
        }
    }

    private void enterRestPhase() {
        if (!isTraining) return;
        Log.i("MiTraining", "enterRestPhase: trialIndex=" + currentTrialIndex);
        currentState = STATE_REST;
        restStartTime = System.currentTimeMillis();

        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_MODE_SET, new byte[]{1});

        tvDirection.setText("休息");
        tvDirection.setTextColor(ContextCompat.getColor(requireContext(), R.color.training_rest));
        tvHint.setText("放松，准备下一个想象任务");
        tvTrialCount.setText("试次 " + (currentTrialIndex + 1) + " / " + TRIAL_SEQUENCE.length);
        progressTrial.setMax(REST_DURATION_MS / 100);
        progressTrial.setProgress(0);

        updateRestTimer();
    }

    private void updateRestTimer() {
        if (!isTraining || currentState != STATE_REST) return;
        long elapsed = System.currentTimeMillis() - restStartTime;
        int remaining = Math.max(0, (REST_DURATION_MS - (int) elapsed) / 1000);
        tvHint.setText("放松，" + remaining + " 秒后开始");
        progressTrial.setProgress((int) (elapsed / 100));
        if (elapsed >= REST_DURATION_MS) {
            sendNextTrial();
        } else {
            handler.postDelayed(this::updateRestTimer, 100);
        }
    }

    private void sendNextTrial() {
        if (!isTraining) return;
        if (currentTrialIndex < TRIAL_SEQUENCE.length) {
            String side = TRIAL_SEQUENCE[currentTrialIndex];
            if ("LEFT".equals(side)) {
                CommandSender.getInstance().trialLeft();
            } else {
                CommandSender.getInstance().trialRight();
            }
            tvDirection.setText("发送 " + side + " 试次...");
            tvHint.setText("等待下位机确认");
        }
    }

    private void handleTaskStart(String side) {
        if (!isTraining) return;
        currentState = STATE_TRIAL_ACTIVE;
        trialStartTime = System.currentTimeMillis();
        boolean isLeft = "LEFT".equals(side);

        tvDirection.setText(isLeft ? "← 想象左手" : "想象右手 →");
        tvDirection.setTextColor(ContextCompat.getColor(requireContext(),
                isLeft ? R.color.direction_left : R.color.direction_right));
        tvHint.setText("运动想象（" + side + "）");
        tvTrialCount.setText("试次 " + (currentTrialIndex + 1) + " / " + TRIAL_SEQUENCE.length);
        progressTrial.setMax(MI_TRIAL_DURATION_MS / 100);
        progressTrial.setProgress(0);

        updateTrialTimer();
    }

    private void updateTrialTimer() {
        if (!isTraining || currentState != STATE_TRIAL_ACTIVE) return;
        long elapsed = System.currentTimeMillis() - trialStartTime;
        progressTrial.setProgress((int) (elapsed / 100));
        if (elapsed >= MI_TRIAL_DURATION_MS) {
            progressTrial.setProgress(progressTrial.getMax());
        } else {
            handler.postDelayed(this::updateTrialTimer, 100);
        }
    }

    private void handleTaskDone() {
        if (!isTraining) return;
        currentTrialIndex++;

        if (currentTrialIndex < TRIAL_SEQUENCE.length) {
            enterRestPhase();
        } else {
            trainingComplete();
        }
    }

    private void trainingComplete() {
        isTraining = false;
        currentState = STATE_IDLE;
        handler.removeCallbacksAndMessages(null);

        btnStartTraining.setEnabled(true);
        btnStopTraining.setEnabled(false);

        tvDirection.setText("训练完成");
        tvDirection.setTextColor(ContextCompat.getColor(requireContext(), R.color.accent_success));
        tvHint.setText("数据已保存，可切换到训练页面查看");
        tvTrialCount.setText("");
        progressTrial.setIndeterminate(false);
        progressTrial.setProgress(progressTrial.getMax());
        saveDirCollectBuffer();
    }

    private void handleReadyTrain() {
        Log.i("MiTraining", "handleReadyTrain: isTraining=" + isTraining + " waitingReadyTrain=" + waitingReadyTrain);
        if (!isTraining) {
            isTraining = true;
            currentTrialIndex = 0;
        }
        waitingReadyTrain = false;
        enterRestPhase();
    }


    private void handleReadyTest() {

    }

    @Override
    public void onResume() {
        super.onResume();
        DataDispatcher.getInstance().removeListener(this);
        DataDispatcher.getInstance().addListener(this);
        sendMiDisplayConfig();
    }

    @Override
    public void onPause() {
        super.onPause();
        DataDispatcher.getInstance().removeListener(this);
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_DISPLAY_CFG,
                EegChannels.buildAllNoneDisplayConfigData());
    }

    private void sendMiDisplayConfig() {
        int[] p = new int[24];
        for (int i = 0; i < 8; i++) {
            p[i * 3] = i;
            p[i * 3 + 1] = EegChannels.WAVE_TYPE_NONE;
            p[i * 3 + 2] = EegChannels.SPEC_TYPE_NONE;
        }
        for (int ch : MI_CHANNELS) {
            p[ch * 3 + 1] = currentWaveType;
        }
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_DISPLAY_CFG,
                EegChannels.buildDisplayConfigDataFromArray(p));
    }

    private void buildGrid() {
        Context ctx = requireContext();
        gridContainer.removeAllViews();

        LinearLayout row = new LinearLayout(ctx);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setWeightSum(2f);
        row.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        for (int i = 0; i < 4; i++) {
            if (i == 2) {
                gridContainer.addView(row);
                row = new LinearLayout(ctx);
                row.setOrientation(LinearLayout.HORIZONTAL);
                row.setWeightSum(2f);
                row.setLayoutParams(new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
            }

            MaterialCardView card = new MaterialCardView(ctx);
            LinearLayout.LayoutParams cardParams = new LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
            cardParams.setMargins(2, 2, 2, 2);
            card.setLayoutParams(cardParams);
            card.setCardElevation(1f);
            card.setRadius(6f);
            card.setCardBackgroundColor(ContextCompat.getColor(ctx, R.color.surface_low));
            card.setStrokeColor(ContextCompat.getColor(ctx, R.color.surface_overlay));
            card.setStrokeWidth(1);

            LinearLayout inner = new LinearLayout(ctx);
            inner.setOrientation(LinearLayout.VERTICAL);

            LinearLayout headerRow = new LinearLayout(ctx);
            headerRow.setOrientation(LinearLayout.HORIZONTAL);
            headerRow.setBackgroundColor(ContextCompat.getColor(ctx, R.color.surface_mid));
            headerRow.setGravity(Gravity.CENTER_VERTICAL);
            int hp = (int) (4 * getResources().getDisplayMetrics().density);
            headerRow.setPadding(hp * 2, 0, hp, 0);

            View dot = new View(ctx);
            LinearLayout.LayoutParams dotP = new LinearLayout.LayoutParams(6, 6);
            dotP.setMargins(0, 0, hp, 0);
            dot.setLayoutParams(dotP);
            dot.setBackgroundColor(ContextCompat.getColor(ctx, MI_COLORS[i]));
            headerRow.addView(dot);

            TextView label = new TextView(ctx);
            label.setTextColor(ContextCompat.getColor(ctx, R.color.text_secondary));
            label.setTextSize(10);
            label.setLayoutParams(new LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
            label.setText(EegChannels.nameOf(MI_CHANNELS[i]));
            headerRow.addView(label);
            labelViews[i] = label;

            inner.addView(headerRow);

            WaveformView wv = new WaveformView(ctx, null);
            wv.setId(View.generateViewId());
            wv.setLayoutParams(new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
            wv.setWaveColor(ContextCompat.getColor(ctx, MI_COLORS[i]));
            wv.setYRange(yRangeUv / 1_000_000f);
            wv.setXMax(xMax);
            wv.setUnit("uV");
            inner.addView(wv);
            waveViews[i] = wv;

            card.addView(inner);
            row.addView(card);
        }
        gridContainer.addView(row);
    }

    private void toggleLayout() {
        isCombinedMode = !isCombinedMode;
        if (isCombinedMode) {
            stripView.setVisibility(View.VISIBLE);
            gridContainer.setVisibility(View.GONE);
            btnToggleLayout.setSelected(false);
            ((com.google.android.material.button.MaterialButton) btnToggleLayout).setText("分屏");
            xMax = splitXMax * 2f;
        } else {
            stripView.setVisibility(View.GONE);
            gridContainer.setVisibility(View.VISIBLE);
            btnToggleLayout.setSelected(true);
            ((com.google.android.material.button.MaterialButton) btnToggleLayout).setText("同屏");
            splitXMax = xMax / 2f;
            xMax = splitXMax;
        }
        stripView.setXMax(xMax);
        for (WaveformView wv : waveViews) if (wv != null) wv.setXMax(xMax);
        tvXRange.setText(String.format("%.0f s", xMax));
    }

    private void toggleWaveType() {
        if (currentWaveType == EegChannels.WAVE_TYPE_FILT) {
            currentWaveType = EegChannels.WAVE_TYPE_BASELINE;
            tvWaveType.setText("基线");
        } else {
            currentWaveType = EegChannels.WAVE_TYPE_FILT;
            tvWaveType.setText("滤波");
        }
        stripView.clear();
        for (WaveformView wv : waveViews) if (wv != null) wv.clear();
        sendMiDisplayConfig();
    }

    private void showYRangeDialog() {
        View dialogView = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_range_input, null);
        EditText etValue = dialogView.findViewById(R.id.et_value);
        Spinner unitSpinner = dialogView.findViewById(R.id.unit_spinner);
        etValue.setInputType(android.text.InputType.TYPE_CLASS_NUMBER | android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL);
        etValue.setHint("例如 100");
        ArrayAdapter<String> ua = new ArrayAdapter<>(requireContext(),
                android.R.layout.simple_spinner_item, new String[]{"μV", "mV"});
        ua.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        unitSpinner.setAdapter(ua);
        if (yRangeUv >= 1000f) {
            etValue.setText(String.format("%.0f", yRangeUv / 1000f));
            unitSpinner.setSelection(1);
        } else {
            etValue.setText(String.format("%.0f", yRangeUv));
            unitSpinner.setSelection(0);
        }
        new AlertDialog.Builder(requireContext())
                .setTitle("Y轴范围")
                .setView(dialogView)
                .setPositiveButton("确定", (d, which) -> {
                    String str = etValue.getText().toString().trim();
                    if (str.isEmpty()) return;
                    try {
                        float val = Float.parseFloat(str);
                        if (val <= 0) return;
                        String unit = (String) unitSpinner.getSelectedItem();
                        yRangeUv = "mV".equals(unit) ? val * 1000f : val;
                        stripView.setYRange(yRangeUv / 1_000_000f);
                        for (WaveformView wv : waveViews) if (wv != null) wv.setYRange(yRangeUv / 1_000_000f);
                        tvYRange.setText(yRangeUv >= 1000f
                                ? String.format("%.0fmV", yRangeUv / 1000f)
                                : String.format("%.0fμV", yRangeUv));
                    } catch (NumberFormatException ignored) {}
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void showXRangeDialog() {
        View dialogView = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_range_input, null);
        EditText etValue = dialogView.findViewById(R.id.et_value);
        dialogView.findViewById(R.id.unit_spinner).setVisibility(View.GONE);
        etValue.setInputType(android.text.InputType.TYPE_CLASS_NUMBER | android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL);
        etValue.setHint("例如 4");
        etValue.setText(String.format("%.1f", xMax));
        new AlertDialog.Builder(requireContext())
                .setTitle("时间范围 (秒)")
                .setView(dialogView)
                .setPositiveButton("确定", (d, which) -> {
                    String str = etValue.getText().toString().trim();
                    if (str.isEmpty()) return;
                    try {
                        float val = Math.round(Float.parseFloat(str) * 10f) / 10f;
                        if (val <= 0) return;
                        xMax = val;
                        stripView.setXMax(xMax);
                        for (WaveformView wv : waveViews) if (wv != null) wv.setXMax(xMax);
                        tvXRange.setText(String.format("%.0f s", xMax));
                    } catch (NumberFormatException ignored) {}
                })
                .setNegativeButton("取消", null)
                .show();
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_IMPORT_DIR_FILE || resultCode != Activity.RESULT_OK || data == null) return;
        Uri uri = data.getData();
        if (uri != null) importDirFile(uri);
    }

    @Override
    public void onDestroyView() {
        TrainingModeCoordinator.getInstance().unregisterController(
                TrainingModeCoordinator.Mode.MI, this);
        stopTrainingInternal(false);
        super.onDestroyView();
        DataDispatcher.getInstance().removeListener(this);
        handler.removeCallbacksAndMessages(null);
        arrowHandler.removeCallbacksAndMessages(null);
    }

    @Override
    public void onTaskStart(String side) {
        handler.post(() -> handleTaskStart(side));
    }

    @Override
    public void onTaskDone() {
        handler.post(this::handleTaskDone);
    }

    @Override
    public void onReadyTrain() {
        handler.post(this::handleReadyTrain);
    }

    @Override
    public void onReadyTest() {
        handler.post(this::handleReadyTest);
    }

    @Override
    public void onInferenceResult(InferenceResult result) {
        if (currentState == STATE_MODEL_TRAINING && result.isTrained()) {
            handler.post(this::handleReadyTest);
        }
        if (!isInferencing) return;

        InferenceRecordStore.Record rec = new InferenceRecordStore.Record();
        rec.timestamp = System.currentTimeMillis();
        rec.intent = result.getIntent();
        rec.scoreLeft = result.getScoreLeft();
        rec.scoreRight = result.getScoreRight();
        rec.confidence = result.getConfidence();
        String gt = result.getGroundTruth();
        if (gt != null) {
            currentTarget = gt;
            rec.groundTruth = gt;
            rec.correct = gt.equals(result.getIntent());
        }
        recordWriter.execute(() -> {
            InferenceRecordStore.addRecord(requireContext(), rec);
            if (getActivity() != null) getActivity().runOnUiThread(() -> refreshStats());
        });

        if (getActivity() != null) getActivity().runOnUiThread(() -> {
            tvInferConfidence.setText(String.format("置信度: %.1f%%", result.getConfidence() * 100));
            tvInferScores.setText(String.format("L:%.3f R:%.3f", result.getScoreLeft(), result.getScoreRight()));
            String intent = result.getIntent();
            String intentArrow = "LEFT".equals(intent) ? "←" : "→";
            String targetArrow = currentTarget != null ? ("LEFT".equals(currentTarget) ? "←" : "→") : "←";
            boolean correct = currentTarget != null && currentTarget.equals(intent);
            int feedbackColor = ContextCompat.getColor(requireContext(),
                    correct ? R.color.arrow_correct : R.color.arrow_wrong);
            tvInferTargetArrow.setText(targetArrow);
            tvInferTargetArrow.setTextColor(feedbackColor);
            tvInferResultArrow.setText(intentArrow);
            tvInferResultArrow.setTextColor(feedbackColor);
            arrowHandler.removeCallbacksAndMessages(null);
            final String nextTarget = currentTarget;
            arrowHandler.postDelayed(() -> {
                tvInferTargetArrow.setText("");
                tvInferResultArrow.setText("");
                arrowHandler.postDelayed(() -> {
                    tvInferTargetArrow.setText(nextTarget != null ? ("LEFT".equals(nextTarget) ? "←" : "→") : "←");
                    tvInferTargetArrow.setTextColor(ContextCompat.getColor(requireContext(), R.color.arrow_target));
                }, 200);
            }, 1500);
        });
    }

    @Override public void onWaveData(int cmd, int ch, float val) {
        if (cmd != EegChannels.waveTypeToCmd(currentWaveType)) return;
        if (isCombinedMode) {
            stripView.addPoint(ch, val);
        } else {
            for (int i = 0; i < 4; i++) {
                if (ch == MI_CHANNELS[i] && waveViews[i] != null) {
                    waveViews[i].addPoint(val);
                    break;
                }
            }
        }
    }
    @Override public void onSpectrumData(int cmd, float[] mags) {}
    @Override public void onFocusData(float attn0, float attn1, float ema0, float ema1, int trend, int instant) {}
    @Override public void onEegFrame(EegFrame frame) {
        if (!isTraining || miMode != 0) return;
        int rawLabel;
        try { rawLabel = Integer.parseInt(frame.getLabel()); } catch (NumberFormatException e) { return; }
        int label = DirLdaTrainer.normalizeLabel(rawLabel);
        if (label < 0) return;
        float[] features = new float[DirLdaTrainer.FEATURE_DIM];
        for (int i = 0; i < DirLdaTrainer.FEATURE_DIM; i++) features[i] = frame.getFeature(i);
        synchronized (dirCollectBuffer) {
            dirCollectBuffer.add(new DirTrainStore.StoredSample(features, label, System.currentTimeMillis()));
        }
    }
    @Override public void onIpcDiag(IpcDiagInfo diag) {}
    @Override public void onDirConfig(String configJson) {}
    @Override public void onModeSetOk(int mode) {}
    @Override public void onDirModelList(String listText) {
        if (tvDirLog != null) appendDirLog(listText.replace("\n", " "));
    }

    private void startInference() {
        if (isInferencing) return;
        isInferencing = true;
        resultCountSinceLastRefresh = 0;
        currentTarget = null;
        InferenceRecordStore.clearAll(requireContext());
        refreshStats();
        arrowHandler.removeCallbacksAndMessages(null);
        tvInferTargetArrow.setText("←");
        tvInferTargetArrow.setTextColor(ContextCompat.getColor(requireContext(), R.color.arrow_target));
        tvInferResultArrow.setText("");
        int mode = spinnerInferMode.getSelectedItemPosition();
        int rounds = mode == 0 ? spinnerInferRounds.getSelectedItemPosition() + 1 : 3;
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_INFER_CFG, new byte[]{(byte) mode, (byte) rounds});
        CommandSender.getInstance().setModeInfer();
        CommandSender.getInstance().startTest();
        btnStartInfer.setEnabled(false);
        btnStopInfer.setEnabled(true);
    }

    private void stopInference() {
        if (!isInferencing) return;
        isInferencing = false;
        arrowHandler.removeCallbacksAndMessages(null);
        tvInferTargetArrow.setText("");
        tvInferResultArrow.setText("");
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_STOP, null);
        TcpServerManager.getInstance().sendDisplayToOutput(EegProtocol.CMD_TRAIN_STOP, null);
        btnStartInfer.setEnabled(true);
        btnStopInfer.setEnabled(false);
    }

    private void refreshStats() {
        java.util.List<InferenceRecordStore.Record> records = InferenceRecordStore.loadAll(requireContext());
        if (getActivity() != null) getActivity().runOnUiThread(() -> {
            tvStatTotal.setText("总推理: " + InferenceRecordStore.countTotal(records));
            float acc = InferenceRecordStore.calcAccuracy(records);
            tvStatAccuracy.setText(String.format("达标率: %.0f%%", acc));
            tvStatAvgConf.setText(String.format("平均置信: %.1f%%", InferenceRecordStore.avgConfidence(records)));
            tvStatLeft.setText("←左: " + InferenceRecordStore.countLeft(records));
            tvStatRight.setText("右→: " + InferenceRecordStore.countRight(records));
        });
    }

    private void exportRecords() {
        java.util.List<InferenceRecordStore.Record> records = InferenceRecordStore.loadAll(requireContext());
        if (records.isEmpty()) return;
        String csv = InferenceRecordStore.exportCsv(records);
        try {
            java.io.File dir = new java.io.File(requireContext().getExternalFilesDir(null), "inference");
            if (!dir.exists()) dir.mkdirs();
            String ts = new java.text.SimpleDateFormat("yyyyMMdd_HHmmss", java.util.Locale.getDefault())
                    .format(new java.util.Date());
            java.io.File file = new java.io.File(dir, "inference_" + ts + ".csv");
            try (java.io.FileOutputStream fos = new java.io.FileOutputStream(file)) {
                fos.write(csv.getBytes("UTF-8"));
            }
        } catch (Exception e) {
            Log.e("MiTraining", "export failed", e);
        }
    }

    private void confirmClearRecords() {
        new AlertDialog.Builder(requireContext())
                .setTitle("清除记录")
                .setMessage("确定清除所有推理记录？")
                .setPositiveButton("确定", (d, w) -> {
                    InferenceRecordStore.clearAll(requireContext());
                    refreshStats();
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void initDirTrainViews(View root) {
        rvDirFiles = root.findViewById(R.id.rv_dir_files);
        btnTrainSelected = root.findViewById(R.id.btn_train_selected);
        btnSendWeight = root.findViewById(R.id.btn_send_weight);
        btnMergeSelected = root.findViewById(R.id.btn_merge_selected);
        btnDeleteSelected = root.findViewById(R.id.btn_delete_selected);
        btnDirBack = root.findViewById(R.id.btn_dir_back);
        btnImportFile = root.findViewById(R.id.btn_import_file);
        btnExportModel = root.findViewById(R.id.btn_export_model);
        btnDeleteModel = root.findViewById(R.id.btn_delete_model);
        btnScanSd = root.findViewById(R.id.btn_scan_sd);
        tvTrainResult = root.findViewById(R.id.tv_train_result);
        tvDirLog = root.findViewById(R.id.tv_log);
        spinnerPatient = root.findViewById(R.id.spinner_patient);
        tvDirLog.setMovementMethod(new android.text.method.ScrollingMovementMethod());
        dirFileAdapter = new DirFileAdapter();
        rvDirFiles.setLayoutManager(new LinearLayoutManager(requireContext()));
        rvDirFiles.setAdapter(dirFileAdapter);
        rvDirModels = root.findViewById(R.id.rv_dir_models);
        dirModelAdapter = new DirModelAdapter();
        rvDirModels.setLayoutManager(new LinearLayoutManager(requireContext()));
        rvDirModels.setAdapter(dirModelAdapter);
        btnTrainSelected.setOnClickListener(v -> trainSelected());
        btnSendWeight.setOnClickListener(v -> sendSelectedModel());
        btnMergeSelected.setOnClickListener(v -> mergeSelected());
        btnDeleteSelected.setOnClickListener(v -> deleteSelected());
        btnDirBack.setOnClickListener(v -> toggleMode());
        btnDeleteModel.setOnClickListener(v -> deleteSelectedModel());
        btnScanSd.setOnClickListener(v -> scanSdModels());
        btnImportFile.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
            intent.setType("*/*");
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            startActivityForResult(intent, REQ_IMPORT_DIR_FILE);
        });
        btnExportModel.setOnClickListener(v -> exportDirModel());
        setupPatientSpinner();
        refreshDirFileList();
        refreshDirModelList();
    }

    private void setupPatientSpinner() {
        loadPatientList();
        refreshPatientSpinner();
    }

    private void loadPatientList() {
        patientList.clear();
        PatientManager pm = PatientManager.getInstance();
        pm.init(requireContext());
        for (String[] p : pm.getPatientList()) patientList.add(new String[]{p[0], p[1]});
        selectedPatientIdx = pm.getSelectedIdx();
    }

    private void savePatientList() {
    }

    private void refreshPatientSpinner() {
        List<String> labels = new ArrayList<>();
        for (String[] p : patientList) labels.add(p[0] + " " + p[1]);
        labels.add("＋ 新增患者");
        android.widget.ArrayAdapter<String> adapter = new android.widget.ArrayAdapter<>(requireContext(), android.R.layout.simple_spinner_item, labels);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinnerPatient.setAdapter(adapter);
        if (selectedPatientIdx >= patientList.size()) selectedPatientIdx = 0;
        spinnerPatient.setSelection(selectedPatientIdx);
        spinnerPatient.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(android.widget.AdapterView<?> parent, View view, int position, long id) {
                if (position == patientList.size()) {
                    showNewPatientDialog();
                } else {
                    selectedPatientIdx = position;
                    PatientManager.getInstance().setSelectedIdx(position);
                }
            }
            @Override
            public void onNothingSelected(android.widget.AdapterView<?> parent) {}
        });
    }

    private void showNewPatientDialog() {
        PatientManager.getInstance().showNewPatientDialog(requireContext(), () -> {
            loadPatientList();
            refreshPatientSpinner();
            appendDirLog("新增患者: " + getCurrentPatientIdStr() + " " + getCurrentPatientName());
        });
    }

    private String getCurrentPatientIdStr() {
        return PatientManager.getInstance().getCurrentPatientId();
    }

    private String getCurrentPatientName() {
        return PatientManager.getInstance().getCurrentPatientName();
    }

    private void refreshDirFileList() {
        Context ctx = getContext();
        if (ctx == null) return;
        dirBgExecutor.execute(() -> {
            List<DirTrainFileManager.SessionInfo> sessions = DirTrainFileManager.listSessions(ctx);
            if (getActivity() != null) {
                getActivity().runOnUiThread(() -> dirFileAdapter.setData(sessions));
            }
        });
    }

    private void importDirFile(Uri uri) {
        Context ctx = getContext();
        if (ctx == null) return;
        dirBgExecutor.execute(() -> {
            try {
                java.io.File dir = DirTrainFileManager.getDir(ctx);
                String ts = new java.text.SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(new java.util.Date());
                String outName = "imported_" + ts + ".csv";
                java.io.File outFile = new java.io.File(dir, outName);
                try (java.io.InputStream is = ctx.getContentResolver().openInputStream(uri);
                     java.io.FileOutputStream fos = new java.io.FileOutputStream(outFile)) {
                    byte[] buf = new byte[4096];
                    int n;
                    while ((n = is.read(buf)) > 0) fos.write(buf, 0, n);
                }
                List<DirTrainStore.StoredSample> test = DirTrainFileManager.loadFile(outFile);
                if (test.isEmpty()) {
                    outFile.delete();
                    if (getActivity() != null) getActivity().runOnUiThread(() -> appendDirLog("导入失败: 文件为空或格式不对"));
                    return;
                }
                if (getActivity() != null) {
                    getActivity().runOnUiThread(() -> {
                        appendDirLog("导入成功: " + outName + " (" + test.size() + "样本)");
                        refreshDirFileList();
                    });
                }
            } catch (Exception e) {
                Log.e("MiTraining", "import error", e);
                if (getActivity() != null) getActivity().runOnUiThread(() -> appendDirLog("导入失败: " + e.getMessage()));
            }
        });
    }

    private void saveDirCollectBuffer() {
        Context ctx = getContext();
        if (ctx == null) return;
        List<DirTrainStore.StoredSample> snapshot;
        synchronized (dirCollectBuffer) {
            if (dirCollectBuffer.isEmpty()) return;
            snapshot = new ArrayList<>(dirCollectBuffer);
            dirCollectBuffer.clear();
        }
        dirBgExecutor.execute(() -> {
            String filename = DirTrainFileManager.saveSession(ctx, snapshot);
            if (getActivity() != null) {
                getActivity().runOnUiThread(() -> {
                    if (filename != null) {
                        appendDirLog("已保存采集数据: " + filename + " (" + snapshot.size() + "样本)");
                        refreshDirFileList();
                    } else {
                        appendDirLog("保存采集数据失败");
                    }
                });
            }
        });
    }

    private void trainSelected() {
        Context ctx = getContext();
        if (ctx == null) return;
        List<DirTrainFileManager.SessionInfo> selected = dirFileAdapter.getSelected();
        if (selected.isEmpty()) { appendDirLog("请先选择文件"); return; }
        appendDirLog("训练 " + selected.size() + " 个文件...");
        tvTrainResult.setText("训练中...");
        dirBgExecutor.execute(() -> {
            List<DirLdaTrainer.Sample> samples = new ArrayList<>();
            for (DirTrainFileManager.SessionInfo info : selected) {
                List<DirTrainStore.StoredSample> stored = DirTrainFileManager.loadFile(info.file);
                for (DirTrainStore.StoredSample s : stored) samples.add(new DirLdaTrainer.Sample(s.features, s.label));
            }
            DirLdaTrainer.TrainResult result = DirLdaTrainer.train(samples);
            if (getActivity() != null) {
                getActivity().runOnUiThread(() -> {
                    if (!result.valid) {
                        tvTrainResult.setText("训练失败: " + result.errorMessage);
                        appendDirLog("训练失败: " + result.errorMessage);
                        return;
                    }
                    lastDirResult = result;
                    StringBuilder sb = new StringBuilder();
                    sb.append(String.format(Locale.getDefault(), "平衡准确率: %.1f%%\n", result.balancedAccuracy * 100));
                    sb.append(String.format(Locale.getDefault(), "正确/总数: %d/%d\n", result.correctCount, result.totalCount));
                    sb.append(String.format(Locale.getDefault(), "LEFT: %d  RIGHT: %d\n", result.leftCount, result.rightCount));
                    sb.append(String.format(Locale.getDefault(), "bias: %.6e\n", result.bias));
                    sb.append("weight[0..5]:\n");
                    for (int i = 0; i < 6; i++) sb.append(String.format(Locale.getDefault(), "  w[%d]=%.6e\n", i, result.weight[i]));
                    tvTrainResult.setText(sb.toString());
                    appendDirLog(String.format(Locale.getDefault(), "训练完成, bAcc=%.1f%%", result.balancedAccuracy * 100));
                    autoExportModel(result);
                });
            }
        });
    }

    private void sendSelectedModel() {
        List<DirModelAdapter.ModelInfo> selected = dirModelAdapter.getSelected();
        if (!selected.isEmpty()) {
            DirModelAdapter.ModelInfo info = selected.get(0);
            DirLdaTrainer.TrainResult r = loadModelFromFile(info.file);
            if (r == null || !r.valid) { appendDirLog("模型加载失败: " + info.name); return; }
            byte[] payload = packModelPushPayload(r, info.patientId, info.patientName);
            appendDirLog("下发模型 " + info.name + " (" + payload.length + "字节)");
            TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_DIR_MODEL_PUSH, payload);
            return;
        }
        if (lastDirResult != null && lastDirResult.valid) {
            byte[] payload = packModelPushPayload(lastDirResult, getCurrentPatientIdStr(), getCurrentPatientName());
            appendDirLog("下发当前训练模型 (" + payload.length + "字节)");
            TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_DIR_MODEL_PUSH, payload);
            return;
        }
        appendDirLog("请先选择模型文件或训练模型");
    }

    private byte[] packModelPushPayload(DirLdaTrainer.TrainResult r, String patientId, String patientName) {
        byte[] modelData = DirLdaTrainer.packWeightPayload(r);
        byte[] payload = new byte[8 + 32 + 4 + modelData.length];
        int offset = 0;
        byte[] idBytes = patientId.getBytes();
        System.arraycopy(idBytes, 0, payload, offset, Math.min(idBytes.length, 8));
        offset += 8;
        byte[] nameBytes = patientName.getBytes();
        System.arraycopy(nameBytes, 0, payload, offset, Math.min(nameBytes.length, 32));
        offset += 32;
        int accBits = Float.floatToIntBits(r.balancedAccuracy);
        payload[offset++] = (byte) (accBits & 0xFF);
        payload[offset++] = (byte) ((accBits >> 8) & 0xFF);
        payload[offset++] = (byte) ((accBits >> 16) & 0xFF);
        payload[offset++] = (byte) ((accBits >> 24) & 0xFF);
        System.arraycopy(modelData, 0, payload, offset, modelData.length);
        return payload;
    }

    private DirLdaTrainer.TrainResult loadModelFromFile(java.io.File file) {
        DirLdaTrainer.TrainResult r = new DirLdaTrainer.TrainResult();
        try (java.io.BufferedReader br = new java.io.BufferedReader(new java.io.FileReader(file))) {
            String line;
            while ((line = br.readLine()) != null) {
                String[] parts = line.split(",");
                if (parts.length < 2) continue;
                String key = parts[0].trim();
                String val = parts[1].trim();
                try {
                    if (key.equals("bias")) r.bias = Float.parseFloat(val);
                    else if (key.startsWith("w") && key.length() > 1) {
                        int idx = Integer.parseInt(key.substring(1));
                        if (idx < r.weight.length) r.weight[idx] = Float.parseFloat(val);
                    } else if (key.startsWith("mean") && key.length() > 4) {
                        int idx = Integer.parseInt(key.substring(4));
                        if (idx < r.mean.length) r.mean[idx] = Float.parseFloat(val);
                    } else if (key.startsWith("scale") && key.length() > 5) {
                        int idx = Integer.parseInt(key.substring(5));
                        if (idx < r.scale.length) r.scale[idx] = Float.parseFloat(val);
                    } else if (key.equals("balanced_accuracy")) r.balancedAccuracy = Float.parseFloat(val);
                    else if (key.equals("shrinkage")) r.shrinkage = Float.parseFloat(val);
                } catch (NumberFormatException ignored) {}
            }
            r.valid = true;
        } catch (java.io.IOException e) {
            Log.e("MiTraining", "loadModel error", e);
            return null;
        }
        return r;
    }

    private void autoExportModel(DirLdaTrainer.TrainResult r) {
        Context ctx = getContext();
        if (ctx == null) return;
        String pidStr = getCurrentPatientIdStr();
        String pname = getCurrentPatientName();
        dirBgExecutor.execute(() -> {
            try {
                java.io.File dir = new java.io.File(ctx.getExternalFilesDir(null), "dir_models");
                if (!dir.exists()) dir.mkdirs();
                String ts = new java.text.SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(new java.util.Date());
                String filename = "dir_model_" + pidStr + "_" + pname + "_" + ts + ".csv";
                java.io.File file = new java.io.File(dir, filename);
                writeModelFile(file, r, pidStr, pname);
                if (getActivity() != null) {
                    getActivity().runOnUiThread(() -> {
                        appendDirLog("模型已自动导出: " + filename);
                        refreshDirModelList();
                    });
                }
            } catch (java.io.IOException e) {
                Log.e("MiTraining", "autoExport error", e);
                if (getActivity() != null) getActivity().runOnUiThread(() -> appendDirLog("自动导出失败: " + e.getMessage()));
            }
        });
    }

    private void writeModelFile(java.io.File file, DirLdaTrainer.TrainResult r, String pidStr, String pname) throws java.io.IOException {
        try (java.io.FileWriter fw = new java.io.FileWriter(file)) {
            fw.append("param,value\n");
            fw.append("patient_id,").append(pidStr).append("\n");
            fw.append("patient_name,").append(pname).append("\n");
            fw.append("bias,").append(String.format(Locale.US, "%.10e", r.bias)).append("\n");
            for (int i = 0; i < r.weight.length; i++)
                fw.append("w").append(String.valueOf(i)).append(",")
                  .append(String.format(Locale.US, "%.10e", r.weight[i])).append("\n");
            for (int i = 0; i < r.mean.length; i++)
                fw.append("mean").append(String.valueOf(i)).append(",")
                  .append(String.format(Locale.US, "%.10e", r.mean[i])).append("\n");
            for (int i = 0; i < r.scale.length; i++)
                fw.append("scale").append(String.valueOf(i)).append(",")
                  .append(String.format(Locale.US, "%.10e", r.scale[i])).append("\n");
            fw.append("balanced_accuracy,").append(String.format(Locale.US, "%.6f", r.balancedAccuracy)).append("\n");
            fw.append("shrinkage,").append(String.format(Locale.US, "%.10e", r.shrinkage)).append("\n");
            fw.append("correct,").append(String.valueOf(r.correctCount)).append("\n");
            fw.append("total,").append(String.valueOf(r.totalCount)).append("\n");
            fw.append("left,").append(String.valueOf(r.leftCount)).append("\n");
            fw.append("right,").append(String.valueOf(r.rightCount)).append("\n");
        }
    }

    private void refreshDirModelList() {
        Context ctx = getContext();
        if (ctx == null) return;
        dirBgExecutor.execute(() -> {
            java.io.File dir = new java.io.File(ctx.getExternalFilesDir(null), "dir_models");
            List<DirModelAdapter.ModelInfo> list = new ArrayList<>();
            if (dir.exists() && dir.isDirectory()) {
                java.io.File[] files = dir.listFiles((d, name) -> name.endsWith(".csv"));
                if (files != null) {
                    java.util.Arrays.sort(files, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));
                    for (java.io.File f : files) {
                        DirModelAdapter.ModelInfo info = parseModelInfo(f);
                        list.add(info);
                    }
                }
            }
            if (getActivity() != null) {
                getActivity().runOnUiThread(() -> dirModelAdapter.setData(list));
            }
        });
    }

    private DirModelAdapter.ModelInfo parseModelInfo(java.io.File file) {
        DirModelAdapter.ModelInfo info = new DirModelAdapter.ModelInfo();
        info.file = file;
        info.name = file.getName();
        info.patientId = "";
        info.patientName = "";
        info.dateStr = new java.text.SimpleDateFormat("MM-dd HH:mm", Locale.getDefault()).format(new java.util.Date(file.lastModified()));
        try (java.io.BufferedReader br = new java.io.BufferedReader(new java.io.FileReader(file))) {
            String line;
            while ((line = br.readLine()) != null) {
                String[] parts = line.split(",");
                if (parts.length < 2) continue;
                String key = parts[0].trim();
                String val = parts[1].trim();
                if (key.equals("patient_id")) info.patientId = val;
                else if (key.equals("patient_name")) info.patientName = val;
                else if (key.equals("balanced_accuracy")) {
                    try { info.balancedAccuracy = Float.parseFloat(val); info.accuracyValid = true; } catch (NumberFormatException ignored) {}
                }
            }
        } catch (java.io.IOException ignored) {}
        return info;
    }

    private void deleteSelectedModel() {
        List<DirModelAdapter.ModelInfo> selected = dirModelAdapter.getSelected();
        if (selected.isEmpty()) { appendDirLog("请先选择模型文件"); return; }
        new AlertDialog.Builder(requireContext())
                .setTitle("删除模型")
                .setMessage("确定删除 " + selected.size() + " 个模型文件？")
                .setPositiveButton("确定", (d, w) -> {
                    for (DirModelAdapter.ModelInfo info : selected) info.file.delete();
                    appendDirLog("已删除 " + selected.size() + " 个模型文件");
                    refreshDirModelList();
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void scanSdModels() {
        appendDirLog("发送SD卡扫描命令...");
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_DIR_MODEL_SCAN, new byte[0]);
    }

    private void exportDirModel() {
        if (lastDirResult == null || !lastDirResult.valid) { appendDirLog("请先训练模型"); return; }
        Context ctx = getContext();
        if (ctx == null) return;
        DirLdaTrainer.TrainResult r = lastDirResult;
        String pidStr = getCurrentPatientIdStr();
        String pname = getCurrentPatientName();
        dirBgExecutor.execute(() -> {
            try {
                java.io.File dir = new java.io.File(ctx.getExternalFilesDir(null), "dir_models");
                if (!dir.exists()) dir.mkdirs();
                String ts = new java.text.SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(new java.util.Date());
                String filename = "dir_model_" + pidStr + "_" + pname + "_" + ts + ".csv";
                java.io.File file = new java.io.File(dir, filename);
                writeModelFile(file, r, pidStr, pname);
                if (getActivity() != null) {
                    getActivity().runOnUiThread(() -> {
                        appendDirLog("模型已导出: " + filename);
                        refreshDirModelList();
                    });
                }
            } catch (java.io.IOException e) {
                Log.e("MiTraining", "export model error", e);
                if (getActivity() != null) getActivity().runOnUiThread(() -> appendDirLog("导出失败: " + e.getMessage()));
            }
        });
    }

    private void mergeSelected() {
        Context ctx = getContext();
        if (ctx == null) return;
        List<DirTrainFileManager.SessionInfo> selected = dirFileAdapter.getSelected();
        if (selected.size() < 2) { appendDirLog("至少选择2个文件才能合并"); return; }
        dirBgExecutor.execute(() -> {
            List<File> files = new ArrayList<>();
            for (DirTrainFileManager.SessionInfo info : selected) files.add(info.file);
            String ts = new java.text.SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(new java.util.Date());
            String outName = "merged_" + ts + ".csv";
            String result = DirTrainFileManager.mergeFiles(ctx, files, outName);
            if (getActivity() != null) {
                getActivity().runOnUiThread(() -> {
                    if (result != null) {
                        appendDirLog("合并完成: " + result);
                        refreshDirFileList();
                    } else {
                        appendDirLog("合并失败");
                    }
                });
            }
        });
    }

    private void deleteSelected() {
        List<DirTrainFileManager.SessionInfo> selected = dirFileAdapter.getSelected();
        if (selected.isEmpty()) { appendDirLog("请先选择文件"); return; }
        new AlertDialog.Builder(requireContext())
                .setTitle("删除文件")
                .setMessage("确定删除 " + selected.size() + " 个文件？")
                .setPositiveButton("确定", (d, w) -> {
                    for (DirTrainFileManager.SessionInfo info : selected) DirTrainFileManager.deleteFile(info.file);
                    appendDirLog("已删除 " + selected.size() + " 个文件");
                    refreshDirFileList();
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void appendDirLog(String msg) {
        String line = dirTimeFmt.format(new java.util.Date()) + " " + msg + "\n";
        dirLogBuilder.append(line);
        if (dirLogBuilder.length() > 4000) dirLogBuilder.delete(0, dirLogBuilder.length() - 4000);
        tvDirLog.setText(dirLogBuilder.toString());
    }
}