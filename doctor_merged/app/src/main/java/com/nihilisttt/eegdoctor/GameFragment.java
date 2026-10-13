package com.nihilisttt.eegdoctor;

import com.nihilisttt.eegdoctor.R;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.CountDownTimer;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.content.ContextCompat;
import androidx.fragment.app.Fragment;

import com.google.android.material.button.MaterialButton;

public class GameFragment extends Fragment implements DataListener {

    private static final String TAG = "GameFragment";
    private static final String PREF = "eeg_game_stats";
    private static final String KEY_COUNT = "count";
    private static final String KEY_TOTAL_TIME = "total_time";
    private static final String KEY_LAST_DIST = "last_dist";
    private static final String KEY_LAST_COINS = "last_coins";
    private static final String KEY_LAST_SCORE = "last_score";
    private static final String KEY_BEST_SCORE = "best_score";

    private static final long GAME_DURATION_MS = 60000L;

    private TextView tvStatus;
    private TextView tvCountdown;
    private TextView tvLastDist;
    private TextView tvLastCoins;
    private TextView tvLastScore;
    private TextView tvCount;
    private TextView tvTotalTime;
    private TextView tvBestScore;
    private MaterialButton btnStart;
    private MaterialButton btnStop;
    private MaterialButton btnClearStats;

    private boolean isRunning = false;
    private CountDownTimer countDownTimer;
    private SharedPreferences prefs;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View root = inflater.inflate(R.layout.fragment_game, container, false);

        tvStatus = root.findViewById(R.id.tv_game_status);
        tvCountdown = root.findViewById(R.id.tv_game_countdown);
        tvLastDist = root.findViewById(R.id.tv_game_last_dist);
        tvLastCoins = root.findViewById(R.id.tv_game_last_coins);
        tvLastScore = root.findViewById(R.id.tv_game_last_score);
        tvCount = root.findViewById(R.id.tv_game_count);
        tvTotalTime = root.findViewById(R.id.tv_game_total_time);
        tvBestScore = root.findViewById(R.id.tv_game_best_score);
        btnStart = root.findViewById(R.id.btn_game_start);
        btnStop = root.findViewById(R.id.btn_game_stop);
        btnClearStats = root.findViewById(R.id.btn_game_clear_stats);

        prefs = requireContext().getSharedPreferences(PREF, Context.MODE_PRIVATE);

        btnStart.setOnClickListener(v -> startGame());
        btnStop.setOnClickListener(v -> stopGame());
        btnClearStats.setOnClickListener(v -> clearStats());

        refreshStatsUi();
        return root;
    }

    private void startGame() {
        if (isRunning) return;
        isRunning = true;
        btnStart.setEnabled(false);
        btnStop.setEnabled(true);
        tvStatus.setText("游戏进行中...");
        tvStatus.setTextColor(ContextCompat.getColor(requireContext(), R.color.accent_success));
        tvCountdown.setText("倒计时: 60s");

        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_GAME_START, null);
        Log.i(TAG, "sent CMD_GAME_START");

        countDownTimer = new CountDownTimer(GAME_DURATION_MS, 1000) {
            @Override
            public void onTick(long millisUntilFinished) {
                tvCountdown.setText("倒计时: " + (millisUntilFinished / 1000) + "s");
            }

            @Override
            public void onFinish() {
                tvCountdown.setText("倒计时: 0s");
                tvStatus.setText("等待结果...");
                tvStatus.setTextColor(ContextCompat.getColor(requireContext(), R.color.accent_info));
            }
        }.start();
    }

    private void stopGame() {
        if (!isRunning) return;
        isRunning = false;
        btnStart.setEnabled(true);
        btnStop.setEnabled(false);
        tvStatus.setText("已停止");
        tvStatus.setTextColor(ContextCompat.getColor(requireContext(), R.color.training_rest));
        tvCountdown.setText("倒计时: 60s");
        if (countDownTimer != null) {
            countDownTimer.cancel();
            countDownTimer = null;
        }
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_GAME_STOP, null);
        Log.i(TAG, "sent CMD_GAME_STOP");
    }

    private void onGameFinished(int distance, int coins, int score) {
        if (!isRunning) return;
        isRunning = false;
        btnStart.setEnabled(true);
        btnStop.setEnabled(false);
        if (countDownTimer != null) {
            countDownTimer.cancel();
            countDownTimer = null;
        }
        tvStatus.setText("游戏结束");
        tvStatus.setTextColor(ContextCompat.getColor(requireContext(), R.color.training_rest));
        tvCountdown.setText("倒计时: 0s");

        int count = prefs.getInt(KEY_COUNT, 0) + 1;
        int totalTime = prefs.getInt(KEY_TOTAL_TIME, 0) + 60;
        int bestScore = Math.max(prefs.getInt(KEY_BEST_SCORE, 0), score);
        SharedPreferences.Editor ed = prefs.edit();
        ed.putInt(KEY_COUNT, count);
        ed.putInt(KEY_TOTAL_TIME, totalTime);
        ed.putInt(KEY_LAST_DIST, distance);
        ed.putInt(KEY_LAST_COINS, coins);
        ed.putInt(KEY_LAST_SCORE, score);
        ed.putInt(KEY_BEST_SCORE, bestScore);
        ed.apply();

        refreshStatsUi();
        Log.i(TAG, "game finished: dist=" + distance + " coins=" + coins + " score=" + score);
    }

    private void clearStats() {
        SharedPreferences.Editor ed = prefs.edit();
        ed.clear();
        ed.apply();
        refreshStatsUi();
        tvStatus.setText("统计已清空");
    }

    private void refreshStatsUi() {
        int count = prefs.getInt(KEY_COUNT, 0);
        int totalTime = prefs.getInt(KEY_TOTAL_TIME, 0);
        int bestScore = prefs.getInt(KEY_BEST_SCORE, 0);
        int lastDist = prefs.getInt(KEY_LAST_DIST, -1);
        int lastCoins = prefs.getInt(KEY_LAST_COINS, -1);
        int lastScore = prefs.getInt(KEY_LAST_SCORE, -1);

        tvCount.setText(String.valueOf(count));
        tvTotalTime.setText(totalTime + "s");
        tvBestScore.setText(String.valueOf(bestScore));
        if (lastDist >= 0) {
            tvLastDist.setText(String.valueOf(lastDist));
            tvLastCoins.setText(String.valueOf(lastCoins));
            tvLastScore.setText(String.valueOf(lastScore));
        } else {
            tvLastDist.setText("--");
            tvLastCoins.setText("--");
            tvLastScore.setText("--");
        }
    }

    @Override
    public void onResume() {
        super.onResume();
        DataDispatcher.getInstance().addListener(this);
    }

    @Override
    public void onPause() {
        super.onPause();
        DataDispatcher.getInstance().removeListener(this);
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        if (countDownTimer != null) {
            countDownTimer.cancel();
            countDownTimer = null;
        }
    }

    @Override
    public void onGameResult(int distance, int coins, int score) {
        if (getActivity() != null) {
            getActivity().runOnUiThread(() -> onGameFinished(distance, coins, score));
        }
    }

    @Override
    public void onWaveData(int cmd, int ch, float val) {}

    @Override
    public void onSpectrumData(int cmd, float[] mags) {}

    @Override
    public void onFocusData(float attn0, float attn1, float ema0, float ema1, int trend, int instant) {}
}
