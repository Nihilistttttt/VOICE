package com.nihilisttt.eegdoctor;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.SeekBar;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import java.io.InputStream;


public class OfflineBdfFragment extends Fragment {

    private static final int REQ_OPEN_FILE = 1001;
    private static final int PLAY_INTERVAL_MS = 50;

    private EegStripView stripView;
    private TopoMapView topoMapView;
    private TextView tvFilename;
    private TextView tvInfo;
    private TextView tvPosStart;
    private TextView tvPosEnd;
    private SeekBar seekBar;
    private EditText etYRange;
    private EditText etWindow;
    private Button btnPlayPause;
    private Button btnDcRemove;

    private BdfReader bdf;
    private String bdfPath;
    private int windowSamples;
    private int stepSamples;
    private boolean doDcRemove = true;

    private boolean isPlaying = false;
    private final Handler playHandler = new Handler(Looper.getMainLooper());
    private final Runnable playRunnable = new Runnable() {
        @Override
        public void run() {
            if (!isPlaying || bdf == null) return;
            int pos = seekBar.getProgress();
            pos += stepSamples;
            if (pos > seekBar.getMax()) {
                pos = 0;
            }
            seekBar.setProgress(pos);
            playHandler.postDelayed(this, PLAY_INTERVAL_MS);
        }
    };

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View root = inflater.inflate(R.layout.fragment_offline_bdf, container, false);

        stripView = root.findViewById(R.id.eeg_strip_view);
        topoMapView = root.findViewById(R.id.topo_map_view);
        tvFilename = root.findViewById(R.id.tv_bdf_filename);
        tvInfo = root.findViewById(R.id.tv_bdf_info);
        tvPosStart = root.findViewById(R.id.tv_pos_start);
        tvPosEnd = root.findViewById(R.id.tv_pos_end);
        seekBar = root.findViewById(R.id.seekbar_position);
        etYRange = root.findViewById(R.id.et_y_range);
        etWindow = root.findViewById(R.id.et_window);
        btnPlayPause = root.findViewById(R.id.btn_play_pause);
        btnDcRemove = root.findViewById(R.id.btn_dc_remove);
 
        root.findViewById(R.id.btn_open_bdf).setOnClickListener(v -> openFile());

        btnPlayPause.setOnClickListener(v -> togglePlay());

        btnDcRemove.setOnClickListener(v -> {
            doDcRemove = !doDcRemove;
            btnDcRemove.setText(doDcRemove ? "去DC:ON" : "去DC:OFF");
            if (bdf != null) loadSegment(seekBar.getProgress());
        });

        etYRange.setOnEditorActionListener((v, actionId, event) -> {
            updateYRange();
            if (bdf != null) loadSegment(seekBar.getProgress());
            return false;
        });

        etWindow.setOnEditorActionListener((v, actionId, event) -> {
            if (bdf != null) loadSegment(seekBar.getProgress());
            return false;
        });


        seekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                loadSegment(progress);
            }
            @Override
            public void onStartTrackingTouch(SeekBar sb) {}
            @Override
            public void onStopTrackingTouch(SeekBar sb) {}
        });

        stripView.setYRange(100f / 1_000_000f);
        stripView.setXMax(2f);

        return root;
    }

    private void openFile() {
        Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
        intent.setType("*/*");
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        startActivityForResult(intent, REQ_OPEN_FILE);
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_OPEN_FILE || resultCode != Activity.RESULT_OK || data == null) return;
        Uri uri = data.getData();
        if (uri == null) return;

        String displayName = getDisplayName(uri);
        tvFilename.setText(displayName);

        new Thread(() -> {
            String localPath = copyToCache(uri);
            if (localPath == null) {
                showInfo("文件读取失败");
                return;
            }
            BdfReader reader = new BdfReader();
            boolean ok = reader.parse(localPath);
            if (!ok) {
                showInfo("BDF解析失败");
                return;
            }
            bdf = reader;
            bdfPath = localPath;
            requireActivity().runOnUiThread(() -> onBdfLoaded(displayName));
        }).start();
    }

    private String copyToCache(Uri uri) {
        try {
            InputStream is = requireContext().getContentResolver().openInputStream(uri);
            if (is == null) return null;
            java.io.File cacheDir = requireContext().getCacheDir();
            java.io.File tmp = new java.io.File(cacheDir, "offline_bdf.tmp");
            java.io.FileOutputStream fos = new java.io.FileOutputStream(tmp);
            byte[] buf = new byte[8192];
            int n;
            while ((n = is.read(buf)) > 0) fos.write(buf, 0, n);
            fos.close();
            is.close();
            return tmp.getAbsolutePath();
        } catch (Exception e) {
            return null;
        }
    }

    private String getDisplayName(Uri uri) {
        String name = uri.getLastPathSegment();
        if (name == null) name = uri.toString();
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        return name;
    }

    private void onBdfLoaded(String name) {
        if (bdf == null) return;
        int fs = bdf.sampleRate;
        int total = bdf.totalSamples;
        float totalSec = total / (float) fs;

        windowSamples = (int) (getWindowSeconds() * fs);
        stepSamples = Math.max(1, windowSamples / 40);

        int maxPos = Math.max(0, total - windowSamples);
        seekBar.setMax(maxPos);
        seekBar.setProgress(0);

        tvPosEnd.setText(String.format("%.1fs", totalSec));
        tvInfo.setText(String.format("%s | %d通道 | %dHz | %.1fs | %d采样点",
                name, bdf.nChannels, fs, totalSec, total));

        updateYRange();
        loadSegment(0);
    }

    private float getWindowSeconds() {
        try {
            float v = Float.parseFloat(etWindow.getText().toString());
            if (v >= 0.1f && v <= 60f) return v;
        } catch (NumberFormatException ignored) {}
        return 2f;
    }

    private void updateYRange() {
        try {
            int yuv = Integer.parseInt(etYRange.getText().toString());
            if (yuv >= 5 && yuv <= 10000) {
                stripView.setYRange(yuv / 1_000_000f);
            }
        } catch (NumberFormatException ignored) {}
    }

    private void loadSegment(int startPos) {
        if (bdf == null || stripView == null) return;
        int fs = bdf.sampleRate;
        windowSamples = (int) (getWindowSeconds() * fs);
        stepSamples = Math.max(1, windowSamples / 40);

        stripView.clear();
        stripView.setXMax(getWindowSeconds());
        if (topoMapView != null) topoMapView.clear();

        int nCh = bdf.nChannels;
        int len = Math.min(windowSamples, bdf.totalSamples - startPos);
        if (len <= 0) return;

        float[] rmsUv = new float[nCh];
        for (int ch = 0; ch < nCh; ch++) {
            float[] seg = doDcRemove ? bdf.getSegmentHp(ch, startPos, len) : bdf.getSegment(ch, startPos, len);
            float sumSq = 0f;
            for (int i = 0; i < seg.length; i++) {
                stripView.addPoint(ch, seg[i] / 1_000_000f);
                sumSq += seg[i] * seg[i];
            }
            rmsUv[ch] = (float) Math.sqrt(sumSq / seg.length);
        }

        if (topoMapView != null) {
            topoMapView.setChannelAmplitudes(rmsUv);
        }

        float startSec = startPos / (float) fs;
        tvPosStart.setText(String.format("%.1fs", startSec));
    }

    private void togglePlay() {
        if (bdf == null) return;
        isPlaying = !isPlaying;
        btnPlayPause.setText(isPlaying ? "暂停" : "播放");
        if (isPlaying) {
            playHandler.post(playRunnable);
        } else {
            playHandler.removeCallbacks(playRunnable);
        }
    }

    private void showInfo(String msg) {
        if (getActivity() != null) {
            getActivity().runOnUiThread(() -> tvInfo.setText(msg));
        }
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        isPlaying = false;
        playHandler.removeCallbacks(playRunnable);
    }
}