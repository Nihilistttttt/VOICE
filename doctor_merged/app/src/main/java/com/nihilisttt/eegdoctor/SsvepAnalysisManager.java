package com.nihilisttt.eegdoctor;

import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;

import java.util.Locale;
import java.util.Random;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicLong;

public final class SsvepAnalysisManager {
    private static final String TAG = "SSVEP_FBCCA";
    private static final SsvepAnalysisManager INSTANCE = new SsvepAnalysisManager();

    private final ExecutorService analysisExecutor = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "doctor-fbcca-analysis");
        t.setDaemon(true);
        return t;
    });
    private final FbccaEngine engine = new FbccaEngine();
    private final AtomicLong sessionGeneration = new AtomicLong(0);

    private volatile boolean prepared;
    private volatile boolean running;
    private volatile boolean synthetic;
    private volatile boolean mcuMode;
    private volatile int targetFreqIndex = -1;
    private volatile int selectedWaveCommand;
    private volatile long sessionStartedAtMs;
    private volatile int progressCounter;
    private Thread syntheticThread;
    private final Handler timeoutHandler = new Handler(Looper.getMainLooper());

    private final int[] ssvepChMap;
    private final double[] pendingSample;
    private final boolean[] pendingReady;
    private int pendingCount;
    private long pendingO1ArrivalMs;

    private static final long CHANNEL_PAIR_TIMEOUT_MS = 5;

    private SsvepAnalysisManager() {
        int[] chs = FbccaConfig.SSVEP_CHANNELS;
        ssvepChMap = new int[8];
        for (int i = 0; i < 8; i++) ssvepChMap[i] = -1;
        for (int i = 0; i < chs.length; i++) {
            ssvepChMap[chs[i]] = i;
        }
        pendingSample = new double[chs.length];
        pendingReady = new boolean[chs.length];
        pendingCount = 0;
    }

    public static SsvepAnalysisManager getInstance() { return INSTANCE; }

    public void prepareSession(int freqIndex) {
        stopSyntheticThread();
        final long generation = sessionGeneration.incrementAndGet();
        analysisExecutor.execute(() -> {
            if (generation != sessionGeneration.get()) return;
            engine.reset();
            targetFreqIndex = freqIndex;
            selectedWaveCommand = 0;
            sessionStartedAtMs = System.currentTimeMillis();
            progressCounter = 0;
            synthetic = false;
            prepared = true;
            running = false;
            resetPending();
            postProgress(SsvepProgress.State.PREPARED, 0, 0,
                    "已准备，等待患者端首个刺激帧");
        });
    }

    public void switchFrequency(int freqIndex) {
        analysisExecutor.execute(() -> {
            engine.reset();
            targetFreqIndex = freqIndex;
            progressCounter = 0;
            resetPending();
            postProgress(SsvepProgress.State.COLLECTING, 0, selectedWaveCommand,
                    "已切换目标频率，重新收集");
        });
    }

    public void confirmStimulusStarted(int freqIndex, float refreshRate) {
        final long generation = sessionGeneration.get();
        analysisExecutor.execute(() -> {
            if (generation != sessionGeneration.get()) return;
            if (synthetic) {
                postProgress(SsvepProgress.State.COLLECTING, engine.getBufferedSamples(), -1,
                        String.format(Locale.US, "患者刺激已启动（屏幕 %.2f Hz）；算法自测继续运行", refreshRate));
                return;
            }
            if (!prepared || targetFreqIndex != freqIndex) {
                engine.reset();
                targetFreqIndex = freqIndex;
                prepared = true;
            }
            running = true;
            synthetic = false;
            mcuMode = true;
            selectedWaveCommand = 0;
            sessionStartedAtMs = System.currentTimeMillis();
            resetPending();
            String msg = String.format(Locale.US,
                    "患者刺激已启动（屏幕 %.2f Hz），等待脑电波形", refreshRate);
            postProgress(SsvepProgress.State.WAITING_WAVE_SOURCE, 0, 0, msg);
        });
    }

    public void startSyntheticTest(int freqIndex) {
        stopSyntheticThread();
        final long generation = sessionGeneration.incrementAndGet();
        targetFreqIndex = freqIndex;
        prepared = true;
        running = true;
        synthetic = true;
        mcuMode = false;
        selectedWaveCommand = -1;
        sessionStartedAtMs = System.currentTimeMillis();
        analysisExecutor.execute(() -> {
            if (generation != sessionGeneration.get()) return;
            engine.reset();
            progressCounter = 0;
            postProgress(SsvepProgress.State.COLLECTING, 0, -1,
                    "算法自测：正在生成合成 SSVEP 数据");
        });

        syntheticThread = new Thread(() -> {
            Random random = new Random(20260717L + freqIndex);
            double freq = FbccaConfig.TARGET_FREQS[freqIndex];
            long next = SystemClock.elapsedRealtimeNanos();
            long periodNs = Math.round(1_000_000_000.0 / FbccaConfig.SAMPLE_RATE);
            long sampleIndex = 0;
            while (running && synthetic && generation == sessionGeneration.get()
                    && !Thread.currentThread().isInterrupted()) {
                double t = sampleIndex / FbccaConfig.SAMPLE_RATE;
                double o1Mv = 0.020 * Math.sin(2.0 * Math.PI * freq * t)
                        + 0.010 * Math.sin(2.0 * Math.PI * 2.0 * freq * t + 0.25)
                        + 0.004 * Math.sin(2.0 * Math.PI * 9.0 * t)
                        + random.nextGaussian() * 0.006;
                double ozMv = 0.015 * Math.sin(2.0 * Math.PI * freq * t + 0.4)
                        + 0.008 * Math.sin(2.0 * Math.PI * 2.0 * freq * t + 0.6)
                        + 0.003 * Math.sin(2.0 * Math.PI * 9.0 * t + 0.2)
                        + random.nextGaussian() * 0.005;
                offerSyntheticSample(o1Mv, ozMv, generation);
                sampleIndex++;
                next += periodNs;
                long sleepNs = next - SystemClock.elapsedRealtimeNanos();
                if (sleepNs > 0) {
                    try {
                        long ms = sleepNs / 1_000_000L;
                        int ns = (int) (sleepNs % 1_000_000L);
                        Thread.sleep(ms, ns);
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                        break;
                    }
                } else if (sleepNs < -100_000_000L) {
                    next = SystemClock.elapsedRealtimeNanos();
                }
            }
        }, "doctor-ssvep-synthetic-source");
        syntheticThread.setDaemon(true);
        syntheticThread.start();
    }

    private void offerSyntheticSample(double o1Mv, double ozMv, long generation) {
        analysisExecutor.execute(() -> {
            if (generation != sessionGeneration.get() || !running || !synthetic) return;
            double[] samples = new double[FbccaConfig.SSVEP_CHANNELS.length];
            samples[0] = o1Mv;
            samples[1] = ozMv;
            processSample(samples);
        });
    }

    /** Called by TcpServerManager for each wave sample with its channel index. */
    public void offerWaveSample(int cmd, int ch, float value) {
        if (!running || synthetic || !Float.isFinite(value)) return;
        int idx = (ch >= 0 && ch < ssvepChMap.length) ? ssvepChMap[ch] : -1;
        if (idx < 0) return;

        final long generation = sessionGeneration.get();
        final long now = System.currentTimeMillis();

        if (selectedWaveCommand == 0) {
            if (cmd == FbccaConfig.PREFERRED_WAVE_CMD) {
                selectedWaveCommand = cmd;
            } else if (cmd == FbccaConfig.FALLBACK_WAVE_CMD
                    && now - sessionStartedAtMs >= FbccaConfig.RAW_FALLBACK_DELAY_MS) {
                selectedWaveCommand = cmd;
            } else {
                return;
            }
            postProgress(SsvepProgress.State.COLLECTING, engine.getBufferedSamples(), cmd,
                    String.format(Locale.US, "已选择波形源 CMD=0x%02X", cmd));
        }
        if (cmd != selectedWaveCommand) return;

        final double mv = value * FbccaConfig.INPUT_SCALE_TO_MV;
        analysisExecutor.execute(() -> {
            if (generation != sessionGeneration.get() || !running || synthetic) return;
            pushChannelSample(idx, mv);
        });
    }

    private synchronized void pushChannelSample(int idx, double mv) {
        if (pendingReady[idx]) {
            flushPending();
        }
        pendingSample[idx] = mv;
        pendingReady[idx] = true;
        pendingCount++;

        if (idx == 0 && pendingCount == 1) {
            pendingO1ArrivalMs = System.currentTimeMillis();
            timeoutHandler.removeCallbacks(flushTimeout);
            timeoutHandler.postDelayed(flushTimeout, CHANNEL_PAIR_TIMEOUT_MS);
        }

        if (pendingCount == FbccaConfig.SSVEP_CHANNELS.length) {
            timeoutHandler.removeCallbacks(flushTimeout);
            double[] samples = pendingSample.clone();
            resetPending();
            processSample(samples);
        }
    }

    private final Runnable flushTimeout = () -> {
        synchronized (SsvepAnalysisManager.this) {
            if (pendingCount > 0) {
                flushPending();
            }
        }
    };

    private synchronized void flushPending() {
        double[] samples = new double[FbccaConfig.SSVEP_CHANNELS.length];
        for (int i = 0; i < samples.length; i++) {
            samples[i] = pendingReady[i] ? pendingSample[i] : Double.NaN;
        }
        resetPending();
        processSample(samples);
    }

    private void resetPending() {
        for (int i = 0; i < pendingReady.length; i++) pendingReady[i] = false;
        pendingCount = 0;
    }

    private void processSample(double[] samplesMv) {
        if (mcuMode) return;
        FbccaOutput output = engine.pushSample(samplesMv);
        progressCounter++;
        if (progressCounter % 25 == 0) {
            int buffered = engine.getBufferedSamples();
            postProgress(SsvepProgress.State.COLLECTING, buffered, selectedWaveCommand,
                    buffered < FbccaConfig.WINDOW_SIZE
                            ? "正在收集 5 秒分析窗口"
                            : "窗口已满，等待下一次 2 秒步进分析");
        }
        if (output != null) {
            postProgress(SsvepProgress.State.ANALYZING,
                    engine.getBufferedSamples(), selectedWaveCommand,
                    "FBCCA 第 " + output.sequence + " 轮完成");
            SsvepResult result = SsvepResult.fromFbcca(output, synthetic);
            DataDispatcher.getInstance().postSsvepResult(result);
            TcpServerManager.getInstance().sendDisplayToOutput(
                    EegProtocol.CMD_SSVEP_RESULT, result.toPatientData());
            Log.i(TAG, "result seq=" + result.getSeq()
                    + " raw=" + result.getRawFreq()
                    + " voted=" + result.getFreq()
                    + " ratio=" + result.getRatio());
        }
    }

    public void stopSession() {
        sessionGeneration.incrementAndGet();
        running = false;
        prepared = false;
        synthetic = false;
        mcuMode = false;
        targetFreqIndex = -1;
        selectedWaveCommand = 0;
        stopSyntheticThread();
        analysisExecutor.execute(() -> {
            engine.reset();
            resetPending();
            postProgress(SsvepProgress.State.STOPPED, 0, 0, "SSVEP 分析已停止");
        });
    }

    public void onMcuSsvepResult(int seq, int rawIndex, int votedIndex,
                                 float ratio, float bestScore, float margin,
                                 float[] scores, int[] voteCounts) {
        if (!running || synthetic) return;
        float[] s = scores != null ? scores.clone() : new float[4];
        int[] v = voteCounts != null ? voteCounts.clone() : new int[4];
        analysisExecutor.execute(() -> {
            if (!running || synthetic) return;
            int totalVotes = 0;
            for (int c : v) totalVotes += c;
            int votePct = Math.min(100, Math.round(totalVotes * 100f / FbccaConfig.VOTE_HISTORY_LEN));
            postProgress(SsvepProgress.State.ANALYZING,
                    totalVotes, FbccaConfig.VOTE_HISTORY_LEN, selectedWaveCommand,
                    String.format(Locale.US, "MCU 第 %d 轮：投票 %d/%d（%d%%）",
                            seq, totalVotes, FbccaConfig.VOTE_HISTORY_LEN, votePct));
            SsvepResult result = SsvepResult.fromMcu(seq, rawIndex, votedIndex,
                    ratio, bestScore, margin, s, v);
            DataDispatcher.getInstance().postSsvepResult(result);
            TcpServerManager.getInstance().sendDisplayToOutput(
                    EegProtocol.CMD_SSVEP_RESULT, result.toPatientData());
            Log.i(TAG, "MCU result seq=" + seq
                    + " raw=" + result.getRawFreq()
                    + " voted=" + result.getFreq()
                    + " ratio=" + ratio);
        });
    }

    public boolean isRunning() { return running; }
    public boolean isSynthetic() { return synthetic; }

    private void stopSyntheticThread() {
        Thread thread = syntheticThread;
        syntheticThread = null;
        if (thread != null) thread.interrupt();
    }

    private void postProgress(SsvepProgress.State state,
                              int buffered,
                              int waveCommand,
                              String message) {
        postProgress(state, buffered, FbccaConfig.WINDOW_SIZE, waveCommand, message);
    }

    private void postProgress(SsvepProgress.State state,
                              int buffered,
                              int windowSamples,
                              int waveCommand,
                              String message) {
        DataDispatcher.getInstance().postSsvepProgress(new SsvepProgress(
                state,
                buffered,
                windowSamples,
                engine.getSamplesUntilNextUpdate(),
                waveCommand,
                synthetic,
                message));
    }
}
