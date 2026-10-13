package com.nihilisttt.eegdoctor;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

public class DataDispatcher {
    private static final String TAG = "DataDispatcher";
    private static final DataDispatcher INSTANCE = new DataDispatcher();
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final List<DataListener> listeners = new CopyOnWriteArrayList<>();

    private static final int WAVE_BATCH_SIZE = 8;
    private final int[] waveBatchCmd = new int[WAVE_BATCH_SIZE];
    private final int[] waveBatchCh = new int[WAVE_BATCH_SIZE];
    private final float[] waveBatchVal = new float[WAVE_BATCH_SIZE];
    private int waveBatchCount = 0;
    private boolean waveBatchScheduled = false;

    public static DataDispatcher getInstance() { return INSTANCE; }

    public void addListener(DataListener listener) {
        listeners.add(listener);
        Log.d(TAG, "addListener: " + listener.getClass().getSimpleName() + ", total=" + listeners.size());
    }
    public void removeListener(DataListener listener) {
        listeners.remove(listener);
        Log.d(TAG, "removeListener: " + listener.getClass().getSimpleName() + ", total=" + listeners.size());
    }

    public void postWaveData(int cmd, int ch, float val) {
        synchronized (this) {
            waveBatchCmd[waveBatchCount] = cmd;
            waveBatchCh[waveBatchCount] = ch;
            waveBatchVal[waveBatchCount] = val;
            waveBatchCount++;
            if (waveBatchCount >= WAVE_BATCH_SIZE) {
                flushWaveBatch();
                return;
            }
            if (!waveBatchScheduled) {
                waveBatchScheduled = true;
                mainHandler.post(this::flushWaveBatch);
            }
        }
    }

    private void flushWaveBatch() {
        int count;
        int[] cmds;
        int[] chs;
        float[] vals;
        synchronized (this) {
            count = waveBatchCount;
            cmds = new int[count];
            chs = new int[count];
            vals = new float[count];
            System.arraycopy(waveBatchCmd, 0, cmds, 0, count);
            System.arraycopy(waveBatchCh, 0, chs, 0, count);
            System.arraycopy(waveBatchVal, 0, vals, 0, count);
            waveBatchCount = 0;
            waveBatchScheduled = false;
        }
        for (DataListener l : listeners) {
            for (int i = 0; i < count; i++) {
                try { l.onWaveData(cmds[i], chs[i], vals[i]); }
                catch (Exception e) { Log.e(TAG, "onWaveData error in " + l.getClass().getSimpleName(), e); }
            }
        }
    }

    public void postSpectrumData(int cmd, float[] mags) {
        mainHandler.post(() -> {
            for (DataListener l : listeners) {
                try { l.onSpectrumData(cmd, mags); }
                catch (Exception e) { Log.e(TAG, "onSpectrumData error in " + l.getClass().getSimpleName(), e); }
            }
        });
    }

    public void postFocusData(float attn0, float attn1, float ema0, float ema1, int trend, int instant) {
        FocusHistoryStore.getInstance().addPoint(ema0, ema1);
        mainHandler.post(() -> {
            for (DataListener l : listeners) {
                try { l.onFocusData(attn0, attn1, ema0, ema1, trend, instant); }
                catch (Exception e) { Log.e(TAG, "onFocusData error in " + l.getClass().getSimpleName(), e); }
            }
        });
    }

    public void postEegFrame(EegFrame frame) {
        mainHandler.post(() -> {
            for (DataListener l : listeners) {
                try { l.onEegFrame(frame); }
                catch (Exception e) { Log.e(TAG, "onEegFrame error in " + l.getClass().getSimpleName(), e); }
            }
        });
    }

    public void postInferenceResult(InferenceResult result) {
        mainHandler.post(() -> {
            for (DataListener l : listeners) {
                try { l.onInferenceResult(result); }
                catch (Exception e) { Log.e(TAG, "onInferenceResult error in " + l.getClass().getSimpleName(), e); }
            }
        });
    }

    public void postIpcDiag(IpcDiagInfo diag) {
        mainHandler.post(() -> {
            for (DataListener l : listeners) {
                try { l.onIpcDiag(diag); }
                catch (Exception e) { Log.e(TAG, "onIpcDiag error in " + l.getClass().getSimpleName(), e); }
            }
        });
    }

    public void postDirConfig(String configJson) {
        mainHandler.post(() -> {
            for (DataListener l : listeners) {
                try { l.onDirConfig(configJson); }
                catch (Exception e) { Log.e(TAG, "onDirConfig error in " + l.getClass().getSimpleName(), e); }
            }
        });
    }

    public void postTaskStart(String side) {
        mainHandler.post(() -> {
            Log.d(TAG, "postTaskStart: " + side);
            for (DataListener l : listeners) {
                try { l.onTaskStart(side); }
                catch (Exception e) { Log.e(TAG, "onTaskStart error in " + l.getClass().getSimpleName(), e); }
            }
        });
    }

    public void postTaskDone() {
        mainHandler.post(() -> {
            Log.d(TAG, "postTaskDone");
            for (DataListener l : listeners) {
                try { l.onTaskDone(); }
                catch (Exception e) { Log.e(TAG, "onTaskDone error in " + l.getClass().getSimpleName(), e); }
            }
        });
    }

    public void postReadyTrain() {
        mainHandler.post(() -> {
            Log.d(TAG, "postReadyTrain");
            for (DataListener l : listeners) {
                try { l.onReadyTrain(); }
                catch (Exception e) { Log.e(TAG, "onReadyTrain error in " + l.getClass().getSimpleName(), e); }
            }
        });
    }

    public void postReadyTest() {
        mainHandler.post(() -> {
            Log.d(TAG, "postReadyTest");
            for (DataListener l : listeners) {
                try { l.onReadyTest(); }
                catch (Exception e) { Log.e(TAG, "onReadyTest error in " + l.getClass().getSimpleName(), e); }
            }
        });
    }

    public void postModeSetOk(int mode) {
        mainHandler.post(() -> {
            Log.d(TAG, "postModeSetOk: mode=" + mode);
            for (DataListener l : listeners) {
                try { l.onModeSetOk(mode); }
                catch (Exception e) { Log.e(TAG, "onModeSetOk error in " + l.getClass().getSimpleName(), e); }
            }
        });
    }

    public void postTurnEvent(String from, String to) {
        mainHandler.post(() -> {
            Log.d(TAG, "postTurnEvent: " + from + " -> " + to);
            for (DataListener l : listeners) {
                try { l.onTurnEvent(from, to); }
                catch (Exception e) { Log.e(TAG, "onTurnEvent error", e); }
            }
        });
    }

    public void postFallEvent() {
        mainHandler.post(() -> {
            Log.d(TAG, "postFallEvent");
            for (DataListener l : listeners) {
                try { l.onFallEvent(); }
                catch (Exception e) { Log.e(TAG, "onFallEvent error", e); }
            }
        });
    }

    public void postNoTurnAlert(long durationMin) {
        mainHandler.post(() -> {
            Log.d(TAG, "postNoTurnAlert: " + durationMin + " min");
            for (DataListener l : listeners) {
                try { l.onNoTurnAlert(durationMin); }
                catch (Exception e) { Log.e(TAG, "onNoTurnAlert error", e); }
            }
        });
    }

    public void postPostureState(String posture, int turnCount) {
        mainHandler.post(() -> {
            Log.d(TAG, "postPostureState: " + posture + " turns=" + turnCount);
            for (DataListener l : listeners) {
                try { l.onPostureState(posture, turnCount); }
                catch (Exception e) { Log.e(TAG, "onPostureState error", e); }
            }
        });
    }


    public void postSsvepResult(SsvepResult result) {
        mainHandler.post(() -> {
            Log.d(TAG, "postSsvepResult: freq=" + result.getFreq() + " ratio=" + result.getRatio());
            for (DataListener l : listeners) {
                try { l.onSsvepResult(result); }
                catch (Exception e) { Log.e(TAG, "onSsvepResult error", e); }
            }
        });
    }

    public void postSsvepProgress(SsvepProgress progress) {
        mainHandler.post(() -> {
            for (DataListener l : listeners) {
                try { l.onSsvepProgress(progress); }
                catch (Exception e) { Log.e(TAG, "onSsvepProgress error", e); }
            }
        });
    }

    public void postImpedanceResult(float[] kohm, boolean biasConnected) {
        mainHandler.post(() -> {
            for (DataListener l : listeners) {
                try { l.onImpedanceResult(kohm, biasConnected); }
                catch (Exception e) { Log.e(TAG, "onImpedanceResult error", e); }
            }
        });
    }

    public void postDirModelList(String listText) {
        mainHandler.post(() -> {
            for (DataListener l : listeners) {
                try { l.onDirModelList(listText); }
                catch (Exception e) { Log.e(TAG, "onDirModelList error", e); }
            }
        });
    }

    public void postGameResult(int distance, int coins, int score) {
        mainHandler.post(() -> {
            Log.d(TAG, "postGameResult: dist=" + distance + " coins=" + coins + " score=" + score);
            for (DataListener l : listeners) {
                try { l.onGameResult(distance, coins, score); }
                catch (Exception e) { Log.e(TAG, "onGameResult error", e); }
            }
        });
    }

}

