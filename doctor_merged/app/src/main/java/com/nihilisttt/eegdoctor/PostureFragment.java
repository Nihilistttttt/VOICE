package com.nihilisttt.eegdoctor;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public class PostureFragment extends Fragment {

    private static final int MAX_LOG_LINES = 200;
    private static final int COLOR_ALERT = 0xFFFF1744;

    private static class LogEntry {
        final String text;
        final boolean isAlert;

        LogEntry(String text, boolean isAlert) {
            this.text = text;
            this.isAlert = isAlert;
        }
    }

    private static final List<LogEntry> sharedLog = new ArrayList<>();
    private static String sharedPosture = "UNKNOWN";
    private static int sharedTurnCount = 0;
    private static boolean staticListenerRegistered = false;

    private static String postureToChinese(String posture) {
        switch (posture) {
            case "SUPINE": return "仰卧";
            case "PRONE": return "俯卧";
            case "LEFT": return "左侧卧";
            case "RIGHT": return "右侧卧";
            case "SITTING": return "坐姿";
            default: return posture;
        }
    }

    private static void appendToSharedLog(String event, boolean isAlert) {
        String time = new SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(new Date());
        String line = "[" + time + "] " + event;
        sharedLog.add(0, new LogEntry(line, isAlert));
        while (sharedLog.size() > MAX_LOG_LINES) {
            sharedLog.remove(sharedLog.size() - 1);
        }
    }

    private static final Handler staticHandler = new Handler(Looper.getMainLooper());
    private static PostureFragment activeInstance = null;

    private static void notifyInstance() {
        staticHandler.post(() -> {
            if (activeInstance != null && activeInstance.viewReady) {
                activeInstance.updatePostureDisplay();
                activeInstance.refreshLogView();
            }
        });
    }

    private static final DataListener staticListener = new DataListener() {
        @Override
        public void onPostureState(String posture, int turnCount) {
            String oldPosture = sharedPosture;
            sharedPosture = posture;
            sharedTurnCount = turnCount;
            if (!oldPosture.equals(posture)) {
                appendToSharedLog("姿态变更: " + postureToChinese(oldPosture) + " → " + postureToChinese(posture), false);
            }
            notifyInstance();
        }

        @Override
        public void onTurnEvent(String from, String to) {
            appendToSharedLog("翻身: " + postureToChinese(from) + " → " + postureToChinese(to), false);
            notifyInstance();
        }

        @Override
        public void onFallEvent() {
            appendToSharedLog("⚠ 坠床警报!", true);
            notifyInstance();
        }

        @Override
        public void onNoTurnAlert(long durationMin) {
            appendToSharedLog("久未翻身: " + durationMin + "分钟", true);
            notifyInstance();
        }

        @Override public void onWaveData(int cmd, int ch, float val) {}
        @Override public void onSpectrumData(int cmd, float[] mags) {}
        @Override public void onFocusData(float attn0, float attn1, float ema0, float ema1, int trend, int instant) {}
        @Override public void onEegFrame(EegFrame frame) {}
        @Override public void onIpcDiag(IpcDiagInfo diag) {}
        @Override public void onDirConfig(String configJson) {}
        @Override public void onTaskStart(String side) {}
        @Override public void onTaskDone() {}
        @Override public void onReadyTrain() {}
        @Override public void onReadyTest() {}
        @Override public void onModeSetOk(int mode) {}
    };

    public static int getSharedTurnCount() { return sharedTurnCount; }
    public static String getSharedPosture() { return sharedPosture; }

    private TextView tvPostureState;
    private TextView tvTurnCount;
    private TextView tvEventLog;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean viewReady = false;

    private static void ensureStaticListener() {
        if (!staticListenerRegistered) {
            DataDispatcher.getInstance().addListener(staticListener);
            staticListenerRegistered = true;
        }
    }

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        ensureStaticListener();

        View root = inflater.inflate(R.layout.fragment_posture, container, false);

        tvPostureState = root.findViewById(R.id.tv_posture_state);
        tvTurnCount = root.findViewById(R.id.tv_turn_count);
        tvEventLog = root.findViewById(R.id.tv_event_log);

        updatePostureDisplay();
        refreshLogView();

        viewReady = true;
        activeInstance = this;
        return root;
    }

    @Override
    public void onResume() {
        super.onResume();
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_DISPLAY_CFG,
                EegChannels.buildAllNoneDisplayConfigData());
        updatePostureDisplay();
        refreshLogView();
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        viewReady = false;
        if (activeInstance == this) activeInstance = null;
        handler.removeCallbacksAndMessages(null);
    }

    private static String postureIcon(String posture) {
        switch (posture) {
            case "SUPINE": return "⬆";
            case "PRONE": return "⬇";
            case "LEFT": return "⬅";
            case "RIGHT": return "➡";
            case "SITTING": return "⏫";
            default: return "?";
        }
    }

    private static int postureColor(String posture) {
        switch (posture) {
            case "SUPINE": return 0xFF4CAF50;
            case "PRONE": return 0xFFFF9800;
            case "LEFT": return 0xFF2196F3;
            case "RIGHT": return 0xFF2196F3;
            case "SITTING": return 0xFF9C27B0;
            default: return 0xFF9E9E9E;
        }
    }

    private void updatePostureDisplay() {
        if (!viewReady || tvPostureState == null) return;
        tvPostureState.setText(postureIcon(sharedPosture) + " " + postureToChinese(sharedPosture));
        tvPostureState.setTextColor(postureColor(sharedPosture));
        if (tvTurnCount != null) {
            tvTurnCount.setText("翻身: " + sharedTurnCount);
        }
    }

    private void refreshLogView() {
        if (!viewReady || tvEventLog == null) return;
        if (sharedLog.isEmpty()) {
            tvEventLog.setText("等待姿态事件...");
            return;
        }
        SpannableStringBuilder ssb = new SpannableStringBuilder();
        for (int i = 0; i < sharedLog.size(); i++) {
            LogEntry entry = sharedLog.get(i);
            int start = ssb.length();
            ssb.append(entry.text);
            if (i < sharedLog.size() - 1) ssb.append("\n");
            if (entry.isAlert) {
                ssb.setSpan(new ForegroundColorSpan(COLOR_ALERT), start, ssb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            }
        }
        tvEventLog.setText(ssb);
    }
}
