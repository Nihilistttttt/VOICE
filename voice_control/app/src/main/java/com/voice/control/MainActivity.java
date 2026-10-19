package com.voice.control;

import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.PixelFormat;
import android.graphics.Point;
import android.graphics.drawable.Drawable;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.provider.Settings;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.HorizontalScrollView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.ArrayAdapter;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.recyclerview.widget.RecyclerView;
import androidx.viewpager2.widget.ViewPager2;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class MainActivity extends AppCompatActivity implements UsbAccessoryManager.Callback {

    private static final String TAG = "VoiceCtrl";

    private static final int STATE_SELECT = 0;
    private static final int STATE_CONTROL = 1;

    public static final int CMD_ENTER = 2;
    public static final int CMD_BACK  = 3;
    public static final int CMD_PLAY  = 4;
    public static final int CMD_NEXT  = 5;
    public static final int CMD_PREV  = 6;
    public static final int CMD_VOL_UP = 7;
    public static final int CMD_VOL_DN = 8;
    public static final int CMD_RETURN = 9;
    public static final int CMD_MODE = 10;
    public static final int CMD_LEFT = 11;
    public static final int CMD_RIGHT = 12;
    public static final int CMD_VOL_MODE = 13;

    private static final int CAT_ALL     = -1;
    private static final int CAT_MUSIC   = 0;
    private static final int CAT_VIDEO   = 1;
    private static final int CAT_NOVEL   = 2;
    private static final int CAT_ADAPTED = 3;

    private static final String[] CAT_NAMES = {"音乐", "视频", "小说", "适配"};

    private static final String[][] KNOWN_APPS = {
        {"com.netease.cloudmusic", "网易云音乐",   "0"},
        {"com.tencent.qqmusic",    "QQ音乐",       "0"},
        {"com.kugou.android",      "酷狗音乐",     "0"},
        {"com.kugou.android.lite", "酷狗概念版",   "0"},
        {"com.baidu.music",        "百度音乐",     "0"},
        {"com.miui.player",        "小米音乐",     "0"},
        {AppConstants.BILIBILI,        "哔哩哔哩",     "1"},
        {"com.qiyi.video",         "爱奇艺",       "1"},
        {"com.tencent.qqlive",     "腾讯视频",     "1"},
        {"com.youku.phone",        "优酷",         "1"},
        {AppConstants.XIMALAYA, "喜马拉雅",  "2"},
        {AppConstants.WECHAT,          "微信",         "3"},
        {"com.dragon.read",        "番茄免费小说", "2"},
        {"com.qidian.QDReader",    "起点读书",     "2"},
    };

    private static class AppEntry {
        String pkg;
        String name;
        int cat;
        Drawable icon;
    }

    private int state = STATE_SELECT;
    private int selectedApp = 0;
    private int currentCat = CAT_ALL;
    private String currentName = "";
    private String currentPkg = "";
    private int currentType = CAT_MUSIC;
    private boolean debugMode = false;
    private boolean volumeMode = false;

    private final List<AppEntry> allApps = new ArrayList<>();
    private final List<AppEntry> filteredApps = new ArrayList<>();

    private TextView tvStatus, tvGuide, tvControlHeader, tvNoApp;
    private LinearLayout selectArea, appListContainer, categoryTabs;
    private HorizontalScrollView appScroll;
    private TextView[] tabViews;
    private AudioManager audioManager;
    private static UsbAccessoryManager sUsbManager;
    private UsbAccessoryManager usbManager;
    private SharedPreferences prefs;

    private BlowBarView blowBar;
    private SeekBar seekThreshold, seekShortMax, seekMidMax, seekComboWait, seekLongRepeat, seekMinGap;
    private TextView tvThresholdVal, tvShortMaxVal, tvMidMaxVal, tvComboWaitVal, tvLongRepeatVal, tvMinGapVal;
    private Spinner spinCollectType;
    private Button btnCollect;
    private TextView tvCollectResult;
    private TextView tvCollectHistory;
    private TextView tvCollectAvg;

    private static final String[] COLLECT_NAMES = {
        "MIC_A 短吹 (ENTER)",
        "MIC_A 中吹 (NEXT)",
        "MIC_A 长吹 (NEXT连续)",
        "MIC_A 短短 (PLAY)",
        "MIC_A 短短短 (VOL_MODE)",
        "MIC_B 短吹 (BACK)",
        "MIC_B 中吹 (PREV)",
        "MIC_B 长吹 (PREV连续)",
        "MIC_B 短短 (MODE)",
        "MIC_B 短短短 (VOL_MODE)",
        "跨麦 左短+右短 (LEFT)",
        "跨麦 右短+左短 (RIGHT)",
    };
    private static final String[] JUDGE_NAMES = {"", "短吹", "中吹", "长吹"};
    private final StringBuilder collectHistory = new StringBuilder();
    private final android.os.Handler collectTimeoutHandler = new android.os.Handler();
    private final Runnable collectTimeoutRunnable = this::cancelCollect;
    private void cancelCollect() {
        collectTimeoutHandler.removeCallbacks(collectTimeoutRunnable);
        btnCollect.setText("开始采集");
        btnCollect.setEnabled(true);
    }

    private static class CollectStats {
        final java.util.List<int[]> records = new java.util.ArrayList<>();
    }
    private final CollectStats[] collectStats = new CollectStats[COLLECT_NAMES.length];
    { for (int i = 0; i < collectStats.length; i++) collectStats[i] = new CollectStats(); }

    private static MainActivity instance;
    private View floatView;
    private WindowManager.LayoutParams floatLp;
    private View floatViewRight;
    private WindowManager.LayoutParams floatLpRight;
    private float dragDownX, dragDownY;
    private int dragStartX, dragStartY;
    private View learnFloatView;
    private WindowManager windowManager;
    private final Handler handler = new Handler();
    private final Handler backHandler = new Handler();

    private final BilibiliAdapter biliAdapter = new BilibiliAdapter(this);
    private final XimalayaAdapter ximaAdapter = new XimalayaAdapter(this);
    private final WechatAdapter wechatAdapter = new WechatAdapter(this);

    private AppAdapter getAdapter(String pkg) {
        if (pkg.equals(AppConstants.BILIBILI)) return biliAdapter;
        if (pkg.equals(AppConstants.XIMALAYA)) return ximaAdapter;
        if (pkg.equals(AppConstants.WECHAT) || VoiceAccessibilityService.isWechatCallPanelOpen()) return wechatAdapter;
        return null;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        instance = this;
        setContentView(R.layout.activity_main);

        ViewPager2 viewPager = findViewById(R.id.viewPager);
        View pageControl = getLayoutInflater().inflate(R.layout.page_control, viewPager, false);
        View pageSettings = getLayoutInflater().inflate(R.layout.page_settings, viewPager, false);

        bindControlPageViews(pageControl);
        bindSettingsPageViews(pageSettings);

        viewPager.setAdapter(new RecyclerView.Adapter<RecyclerView.ViewHolder>() {
            @NonNull
            @Override
            public RecyclerView.ViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
                View v = viewType == 0 ? pageControl : pageSettings;
                if (v.getParent() != null) ((ViewGroup) v.getParent()).removeView(v);
                return new RecyclerView.ViewHolder(v) {};
            }
            @Override
            public void onBindViewHolder(@NonNull RecyclerView.ViewHolder holder, int position) {}
            @Override
            public int getItemCount() { return 2; }
            @Override
            public int getItemViewType(int position) { return position; }
        });

        audioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        if (sUsbManager == null) {
            sUsbManager = new UsbAccessoryManager(this, this);
            sUsbManager.start();
        } else {
            sUsbManager.setCallback(this, this);
            if (!sUsbManager.isRunning()) {
                sUsbManager.start();
            }
        }
        usbManager = sUsbManager;
        prefs = getSharedPreferences("voice_ctrl", MODE_PRIVATE);

        scanApps();
        refreshFilter();
        checkPermissions();

        viewPager.registerOnPageChangeCallback(new ViewPager2.OnPageChangeCallback() {
            @Override
            public void onPageSelected(int position) {
                if (position != 1) cancelCollect();
                if (usbManager != null && usbManager.isConnected()) {
                    usbManager.sendData(new byte[]{(byte)(position == 1 ? 0xFD : 0xFC)});
                }
            }
        });

        tvStatus.setText("正在检测USB连接...");
    }

    private void bindControlPageViews(View page) {
        tvStatus = page.findViewById(R.id.tvStatus);
        tvGuide = page.findViewById(R.id.tvGuide);
        tvControlHeader = page.findViewById(R.id.tvControlHeader);
        tvNoApp = page.findViewById(R.id.tvNoApp);
        selectArea = page.findViewById(R.id.selectArea);
        appListContainer = page.findViewById(R.id.appListContainer);
        appScroll = page.findViewById(R.id.appScroll);
        categoryTabs = page.findViewById(R.id.categoryTabs);
        tabViews = new TextView[]{
            page.findViewById(R.id.tabAll),
            page.findViewById(R.id.tabMusic),
            page.findViewById(R.id.tabVideo),
            page.findViewById(R.id.tabNovel),
            page.findViewById(R.id.tabAdapted)
        };
        Button btnTestShort = page.findViewById(R.id.btnTestShort);
        Button btnTestMid = page.findViewById(R.id.btnTestMid);
        Button btnTestLong = page.findViewById(R.id.btnTestLong);
        Button btnTestTriple = page.findViewById(R.id.btnTestTriple);
        Button btnLearn = page.findViewById(R.id.btnLearn);
        Button btnLearnFs = page.findViewById(R.id.btnLearnFs);
        Button btnRescan = page.findViewById(R.id.btnRescan);
        Button btnAddApp = page.findViewById(R.id.btnAddApp);
        Button btnDebug = page.findViewById(R.id.btnDebug);
        Button btnCalibrate = page.findViewById(R.id.btnCalibrate);

        btnTestShort.setOnClickListener(v -> onCommand(CMD_NEXT));
        btnTestMid.setOnClickListener(v -> onCommand(CMD_ENTER));
        btnTestLong.setOnClickListener(v -> onCommand(CMD_PLAY));
        btnTestTriple.setOnClickListener(v -> onCommand(CMD_VOL_MODE));
        btnDebug.setOnClickListener(v -> {
            debugMode = !debugMode;
            btnDebug.setText(debugMode ? "调试开" : "调试关");
            if (state == STATE_CONTROL) {
                removeFloatWindow();
                showFloatWindow();
            }
        });
        btnLearn.setOnClickListener(v -> startLearnCurrent());
        btnLearnFs.setOnClickListener(v -> startLearnFullscreen());
        btnRescan.setOnClickListener(v -> { scanApps(); refreshFilter(); });
        btnAddApp.setOnClickListener(v -> showAddAppDialog());
        btnCalibrate.setOnClickListener(v -> showCalibration());

        for (int i = 0; i < tabViews.length; i++) {
            final int cat = i - 1;
            tabViews[i].setOnClickListener(v -> switchCategory(cat));
        }
    }

    private void sendParam(int id, int value) {
        if (usbManager != null && usbManager.isConnected()) {
            usbManager.sendData(new byte[]{(byte)0xFB, (byte)id,
                (byte)(value >> 8), (byte)(value & 0xFF)});
        }
    }

    private void bindSettingsPageViews(View page) {
        blowBar = page.findViewById(R.id.blowBar);
        btnCollect = page.findViewById(R.id.btnCollect);
        tvCollectResult = page.findViewById(R.id.tvCollectResult);
        tvCollectAvg = page.findViewById(R.id.tvCollectAvg);
        seekThreshold = page.findViewById(R.id.seekThreshold);
        seekShortMax = page.findViewById(R.id.seekShortMax);
        seekMidMax = page.findViewById(R.id.seekMidMax);
        seekComboWait = page.findViewById(R.id.seekComboWait);
        seekLongRepeat = page.findViewById(R.id.seekLongRepeat);
        seekMinGap = page.findViewById(R.id.seekMinGap);
        tvThresholdVal = page.findViewById(R.id.tvThresholdVal);
        tvShortMaxVal = page.findViewById(R.id.tvShortMaxVal);
        tvMidMaxVal = page.findViewById(R.id.tvMidMaxVal);
        tvComboWaitVal = page.findViewById(R.id.tvComboWaitVal);
        tvLongRepeatVal = page.findViewById(R.id.tvLongRepeatVal);
        tvMinGapVal = page.findViewById(R.id.tvMinGapVal);
        tvCollectHistory = page.findViewById(R.id.tvCollectHistory);

        seekThreshold.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar sb, int prog, boolean fromUser) {
                tvThresholdVal.setText(String.valueOf(prog));
                if (blowBar != null) blowBar.setThreshold(prog);
            }
            public void onStartTrackingTouch(SeekBar sb) {}
            public void onStopTrackingTouch(SeekBar sb) { sendParam(0, sb.getProgress()); }
        });
        seekShortMax.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar sb, int prog, boolean fromUser) {
                tvShortMaxVal.setText(prog + "t (~" + (prog * 8) + "ms)");
            }
            public void onStartTrackingTouch(SeekBar sb) {}
            public void onStopTrackingTouch(SeekBar sb) { sendParam(1, sb.getProgress()); }
        });
        seekMidMax.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar sb, int prog, boolean fromUser) {
                tvMidMaxVal.setText(prog + "t (~" + (prog * 8) + "ms)");
            }
            public void onStartTrackingTouch(SeekBar sb) {}
            public void onStopTrackingTouch(SeekBar sb) { sendParam(4, sb.getProgress()); }
        });
        seekComboWait.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar sb, int prog, boolean fromUser) {
                tvComboWaitVal.setText(prog + "t (~" + (prog * 8) + "ms)");
            }
            public void onStartTrackingTouch(SeekBar sb) {}
            public void onStopTrackingTouch(SeekBar sb) { sendParam(2, sb.getProgress()); }
        });
        seekLongRepeat.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar sb, int prog, boolean fromUser) {
                tvLongRepeatVal.setText(prog + "t (~" + (prog * 8) + "ms)");
            }
            public void onStartTrackingTouch(SeekBar sb) {}
            public void onStopTrackingTouch(SeekBar sb) { sendParam(3, sb.getProgress()); }
        });
        seekMinGap.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar sb, int prog, boolean fromUser) {
                tvMinGapVal.setText(prog + "t (~" + (prog * 8) + "ms)");
            }
            public void onStartTrackingTouch(SeekBar sb) {}
            public void onStopTrackingTouch(SeekBar sb) { sendParam(5, sb.getProgress()); }
        });

        spinCollectType = page.findViewById(R.id.spinCollectType);
        spinCollectType.setAdapter(new ArrayAdapter<>(this,
            android.R.layout.simple_spinner_dropdown_item, COLLECT_NAMES));
        spinCollectType.setOnItemSelectedListener(new Spinner.OnItemSelectedListener() {
            public void onItemSelected(android.widget.AdapterView<?> p, View v, int pos, long id) {
                cancelCollect();
            }
            public void onNothingSelected(android.widget.AdapterView<?> p) {}
        });

        btnCollect.setOnClickListener(v -> {
            if (usbManager != null && usbManager.isConnected()) {
                usbManager.sendData(new byte[]{(byte)0xFA});
                btnCollect.setText("采集中...");
                btnCollect.setEnabled(false);
                tvCollectResult.setText("请吹气，MCU判断中...");
                collectTimeoutHandler.postDelayed(collectTimeoutRunnable, 5000);
            }
        });

        Button btnClearStats = page.findViewById(R.id.btnClearStats);
        btnClearStats.setOnClickListener(v -> {
            cancelCollect();
            for (CollectStats s : collectStats) s.records.clear();
            collectHistory.setLength(0);
            tvCollectHistory.setText("");
            tvCollectResult.setText("已清空统计");
            tvCollectAvg.setText("暂无统计数据");
        });
    }

    private static long avgNoExtremes(java.util.List<Integer> vals) {
        int n = vals.size();
        if (n == 0) return 0;
        if (n <= 2) {
            long sum = 0;
            for (int v : vals) sum += v;
            return sum / n;
        }
        int min = Integer.MAX_VALUE, max = Integer.MIN_VALUE;
        long sum = 0;
        for (int v : vals) { sum += v; if (v < min) min = v; if (v > max) max = v; }
        return (sum - min - max) / (n - 2);
    }

    @Override
    public void onEnergyReceived(int meanA, int meanB) {
        runOnUiThread(() -> {
            if (blowBar != null) blowBar.addSample(meanA, meanB);
        });
    }

    @Override
    public void onCollectResult(int j1, int t1, int p1, int a1,
                                int j2, int t2, int p2, int a2, int gap) {
        runOnUiThread(() -> {
            cancelCollect();

            String type = spinCollectType.getSelectedItem().toString();
            int typeIdx = spinCollectType.getSelectedItemPosition();

            StringBuilder sb = new StringBuilder();
            sb.append(type).append("\n");
            sb.append("  峰值=").append(p1).append(" 均值=").append(a1)
              .append(" ").append(t1).append("t(").append(t1 * 8).append("ms) ")
              .append(JUDGE_NAMES[j1]).append("\n");
            if (j2 > 0) {
                sb.append("  峰值=").append(p2).append(" 均值=").append(a2)
                  .append(" ").append(t2).append("t(").append(t2 * 8).append("ms) ")
                  .append(JUDGE_NAMES[j2]).append("\n");
                sb.append("  间隔=").append(gap).append("t(").append(gap * 8).append("ms)\n");
            }
            tvCollectResult.setText(sb.toString());

            CollectStats st = collectStats[typeIdx];
            st.records.add(new int[]{p1, a1, t1, j1, p2, a2, t2, j2, gap});

            int n = st.records.size();
            java.util.List<Integer> p1s = new java.util.ArrayList<>(), a1s = new java.util.ArrayList<>(), t1s = new java.util.ArrayList<>();
            java.util.List<Integer> p2s = new java.util.ArrayList<>(), a2s = new java.util.ArrayList<>(), t2s = new java.util.ArrayList<>(), gaps = new java.util.ArrayList<>();
            for (int[] r : st.records) {
                p1s.add(r[0]); a1s.add(r[1]); t1s.add(r[2]);
                if (r[7] > 0) { p2s.add(r[4]); a2s.add(r[5]); t2s.add(r[6]); gaps.add(r[8]); }
            }
            StringBuilder avg = new StringBuilder();
            avg.append(type).append("  x").append(n).append(" (去极端)\n");
            avg.append("  峰值=").append(avgNoExtremes(p1s))
               .append(" 均值=").append(avgNoExtremes(a1s))
               .append(" ").append(avgNoExtremes(t1s)).append("t\n");
            if (!p2s.isEmpty()) {
                avg.append("  峰值=").append(avgNoExtremes(p2s))
                   .append(" 均值=").append(avgNoExtremes(a2s))
                   .append(" ").append(avgNoExtremes(t2s)).append("t\n");
                avg.append("  间隔=").append(avgNoExtremes(gaps)).append("t\n");
            }
            tvCollectAvg.setText(avg.toString());

            collectHistory.insert(0, sb.toString());
            if (collectHistory.length() > 2000) collectHistory.setLength(2000);
            tvCollectHistory.setText(collectHistory.toString());
        });
    }

    private void scanApps() {
        allApps.clear();
        PackageManager pm = getPackageManager();
        Set<String> seen = new HashSet<>();

        for (String[] app : KNOWN_APPS) {
            String pkg = app[0];
            if (seen.contains(pkg)) continue;
            seen.add(pkg);
            if (prefs.getBoolean("app_" + pkg + "_disabled", false)) continue;
            try {
                pm.getPackageInfo(pkg, 0);
                if (pm.getLaunchIntentForPackage(pkg) != null) {
                    AppEntry e = new AppEntry();
                    e.pkg = pkg;
                    e.name = app[1];
                    e.cat = Integer.parseInt(app[2]);
                    e.icon = pm.getApplicationIcon(pkg);
                    allApps.add(e);
                }
            } catch (PackageManager.NameNotFoundException ignored) {}
        }

        Map<String, ?> saved = prefs.getAll();
        for (String key : saved.keySet()) {
            if (!key.startsWith("app_") || !key.endsWith("_name")) continue;
            String pkg = key.substring(4, key.length() - 5);
            if (seen.contains(pkg)) continue;
            seen.add(pkg);
            try {
                pm.getPackageInfo(pkg, 0);
                if (pm.getLaunchIntentForPackage(pkg) != null) {
                    AppEntry e = new AppEntry();
                    e.pkg = pkg;
                    e.name = (String) saved.get(key);
                    e.cat = prefs.getInt("app_" + pkg + "_cat", CAT_MUSIC);
                    e.icon = pm.getApplicationIcon(pkg);
                    allApps.add(e);
                }
            } catch (PackageManager.NameNotFoundException ignored) {}
        }

        Collections.sort(allApps, (a, b) -> Integer.compare(a.cat, b.cat));
        Log.i(TAG, "扫描完成，共 " + allApps.size() + " 个应用");
    }

    private void refreshFilter() {
        filteredApps.clear();
        for (AppEntry app : allApps) {
            if (currentCat == CAT_ALL || app.cat == currentCat) {
                filteredApps.add(app);
            }
        }
        if (selectedApp >= filteredApps.size()) selectedApp = 0;
        buildAppList();
        updateUI();
    }

    private void switchCategory(int cat) {
        currentCat = cat;
        selectedApp = 0;
        for (int i = 0; i < tabViews.length; i++) {
            if (i - 1 == cat) {
                tabViews[i].setBackgroundResource(R.drawable.bg_app_selected);
                tabViews[i].setTextColor(0xFFFFFFFF);
            } else {
                tabViews[i].setBackgroundResource(R.drawable.bg_app_normal);
                tabViews[i].setTextColor(0xFF333333);
            }
        }
        refreshFilter();
    }

    private void buildAppList() {
        appListContainer.removeAllViews();
        if (filteredApps.isEmpty()) {
            tvNoApp.setVisibility(View.VISIBLE);
            appScroll.setVisibility(View.GONE);
            return;
        }
        tvNoApp.setVisibility(View.GONE);
        appScroll.setVisibility(View.VISIBLE);

        float dp = getResources().getDisplayMetrics().density;
        int iconSize = (int)(52 * dp);
        int itemWidth = (int)(84 * dp);
        int pad = (int)(8 * dp);

        for (int i = 0; i < filteredApps.size(); i++) {
            AppEntry app = filteredApps.get(i);

            LinearLayout item = new LinearLayout(this);
            item.setOrientation(LinearLayout.VERTICAL);
            item.setGravity(Gravity.CENTER);
            item.setPadding(pad, pad, pad, pad);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(itemWidth, LinearLayout.LayoutParams.WRAP_CONTENT);
            lp.setMargins(pad, 0, pad, 0);
            item.setLayoutParams(lp);

            ImageView iconView = new ImageView(this);
            iconView.setImageDrawable(app.icon);
            iconView.setLayoutParams(new LinearLayout.LayoutParams(iconSize, iconSize));
            item.addView(iconView);

            TextView nameView = new TextView(this);
            nameView.setText(app.name);
            nameView.setTextSize(12);
            nameView.setMaxLines(1);
            nameView.setGravity(Gravity.CENTER);
            nameView.setTextColor(0xFF333333);
            nameView.setPadding(0, (int)(4 * dp), 0, 0);
            item.addView(nameView);

            TextView catView = new TextView(this);
            catView.setText(CAT_NAMES[app.cat]);
            catView.setTextSize(10);
            catView.setGravity(Gravity.CENTER);
            catView.setTextColor(app.cat == CAT_ADAPTED ? 0xFFE65100 : 0xFF888888);
            catView.setPadding(0, (int)(2 * dp), 0, 0);
            item.addView(catView);

            final int index = i;
            item.setOnClickListener(v -> { selectedApp = index; updateUI(); });
            appListContainer.addView(item);
        }
    }

    private boolean isAccessibilityEnabled() {
        String service = getPackageName() + "/" + VoiceAccessibilityService.class.getName();
        String enabled = Settings.Secure.getString(getContentResolver(),
                Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES);
        return enabled != null && enabled.contains(service);
    }

    private void checkPermissions() {
        if (!isAccessibilityEnabled()) {
            new AlertDialog.Builder(this)
                .setTitle("需要无障碍服务")
                .setMessage("适配类应用的播放控制和返回需要开启无障碍服务。\n\n点击确定前往设置。")
                .setPositiveButton("确定", (d, w) -> {
                    Intent intent = new Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS);
                    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    startActivity(intent);
                })
                .setNegativeButton("以后再说", null)
                .show();
        } else if (!Settings.canDrawOverlays(this)) {
            new AlertDialog.Builder(this)
                .setTitle("需要悬浮窗权限")
                .setMessage("控制应用时需要悬浮窗显示状态。\n\n点击确定前往设置。")
                .setPositiveButton("确定", (d, w) -> {
                    startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                        Uri.parse("package:" + getPackageName()))
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                })
                .setNegativeButton("以后再说", null)
                .show();
        }
    }

    private void updateUI() {
        if (filteredApps.isEmpty()) {
            selectArea.setVisibility(View.VISIBLE);
            tvControlHeader.setVisibility(View.GONE);
            tvGuide.setText("当前分类没有可用应用。\n请安装应用或点击\"添加应用\"。");
            return;
        }
        if (state == STATE_SELECT) {
            selectArea.setVisibility(View.VISIBLE);
            tvControlHeader.setVisibility(View.GONE);
            for (int i = 0; i < appListContainer.getChildCount(); i++) {
                appListContainer.getChildAt(i).setBackgroundResource(
                    i == selectedApp ? R.drawable.bg_app_selected : R.drawable.bg_app_normal);
            }
            AppEntry app = filteredApps.get(selectedApp);
            tvGuide.setText("NEXT(5)   切换选中软件\n" +
                "PREV(6)   反向切换软件\n" +
                "ENTER(2)  确认打开选中的软件\n\n" +
                "当前选中: " + app.name + " [" + CAT_NAMES[app.cat] + "]\n" +
                "共 " + filteredApps.size() + " 个应用");
            scrollToSelected();
        } else {
            selectArea.setVisibility(View.GONE);
            tvControlHeader.setVisibility(View.VISIBLE);
            tvControlHeader.setText("正在控制: " + currentName + " [" + CAT_NAMES[currentType] + "]");
            String playHint = currentType == CAT_ADAPTED ?
                "PLAY(4)   播放/暂停 (无障碍点击)" : "PLAY(4)   播放/暂停 (媒体键)";
            tvGuide.setText(playHint + "\n" +
                "NEXT(5)   下一首\n" +
                "PREV(6)   上一首\n" +
                "BACK(3)   返回选择软件\n\n" +
                "正在控制: " + currentName);
        }
    }

    private void scrollToSelected() {
        appScroll.post(() -> {
            if (appListContainer.getChildCount() == 0) return;
            if (selectedApp >= appListContainer.getChildCount()) return;
            View child = appListContainer.getChildAt(selectedApp);
            int target = child.getLeft() - (appScroll.getWidth() - child.getWidth()) / 2;
            appScroll.smoothScrollTo(Math.max(0, target), 0);
        });
    }

    private void openApp(int slot) {
        if (slot < 0 || slot >= filteredApps.size()) return;
        AppEntry app = filteredApps.get(slot);
        Intent intent = getPackageManager().getLaunchIntentForPackage(app.pkg);
        if (intent == null) {
            Toast.makeText(this, "无法打开 " + app.name, Toast.LENGTH_LONG).show();
            return;
        }
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(intent);
        currentName = app.name;
        currentPkg = app.pkg;
        currentType = app.cat;
        state = STATE_CONTROL;
        updateUI();
        tvStatus.setText("已打开 " + app.name);
        showFloatWindow();
        if (app.pkg.equals(AppConstants.BILIBILI) && VoiceAccessibilityService.isAvailable()) {
            VoiceAccessibilityService.resetCardList();
        }
        if (app.cat == CAT_ADAPTED) {
            if (VoiceAccessibilityService.isAvailable()) {
                VoiceAccessibilityService.tryClickPlay(app.pkg);
            } else if (isAccessibilityEnabled()) {
                Toast.makeText(this, "无障碍服务未连接，请关闭后重新开启", Toast.LENGTH_LONG).show();
            }
        }
    }

    public void sendMediaKey(int keyCode, String name) {
        audioManager.dispatchMediaKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, keyCode));
        audioManager.dispatchMediaKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, keyCode));
        Log.i(TAG, "发送: " + name);
        tvStatus.setText("已发送: " + name);
    }

    public void setStatusText(String s) { tvStatus.setText(s); }
    public Handler getHandler() { return handler; }
    public Handler getBackHandler() { return backHandler; }

    @Override
    public void onConnected() {
        runOnUiThread(() -> {
            try {
                tvStatus.setText("USB已连接，请哼声操作");
                String pkg = VoiceAccessibilityService.getForegroundPkg();
                Log.i(TAG, "onConnected foregroundPkg=" + pkg);
                if (!pkg.isEmpty()) {
                    for (String[] known : KNOWN_APPS) {
                        if (known[0].equals(pkg)) {
                            currentPkg = known[0];
                            currentName = known[1];
                            currentType = Integer.parseInt(known[2]);
                            state = STATE_CONTROL;
                            updateUI();
                            tvStatus.setText("USB已连接，正在控制: " + currentName);
                            showFloatWindow();
                            Log.i(TAG, "Auto-enter CONTROL: " + currentName);
                            break;
                        }
                    }
                }
            } catch (Exception e) {
                Log.e(TAG, "onConnected crash", e);
            }
        });
    }

    @Override
    public void onDisconnected() {
        runOnUiThread(() -> tvStatus.setText("USB未连接"));
        if (usbManager != null) {
            usbManager.stop();
        }
        finish();
    }

    @Override
    public void onCommandReceived(int command) {
        runOnUiThread(() -> onCommand(command));
    }

    private boolean tryAutoControl() {
        String pkg = VoiceAccessibilityService.getForegroundPkg();
        if (pkg.isEmpty()) return false;
        for (String[] known : KNOWN_APPS) {
            if (known[0].equals(pkg)) {
                currentPkg = known[0];
                currentName = known[1];
                currentType = Integer.parseInt(known[2]);
                state = STATE_CONTROL;
                updateUI();
                showFloatWindow();
                tvStatus.setText("自动控制: " + currentName);
                Log.i(TAG, "Auto-enter CONTROL: " + currentName + " (" + pkg + ")");
                return true;
            }
        }
        return false;
    }

    private void autoSwitchByForeground() {
        String fgPkg = VoiceAccessibilityService.getForegroundPkg();
        if (fgPkg.isEmpty() || fgPkg.equals(getPackageName())) return;
        if (fgPkg.equals(currentPkg)) return;
        for (String[] known : KNOWN_APPS) {
            if (known[0].equals(fgPkg)) {
                Log.i(TAG, "前台切换: " + currentPkg + " → " + fgPkg);
                if (currentPkg.equals(AppConstants.WECHAT) && !fgPkg.equals(AppConstants.WECHAT)) {
                    VoiceAccessibilityService.resetWechatNavMode();
                }
                currentPkg = known[0];
                currentName = known[1];
                currentType = Integer.parseInt(known[2]);
                updateUI();
                tvStatus.setText("自动切换: " + currentName);
                break;
            }
        }
    }

    private void onCommand(int cmd) {
        Log.i(TAG, "命令: " + cmd + " 状态: " + (state == STATE_SELECT ? "SELECT" : "CONTROL"));
        if (state == STATE_SELECT) {
            if (cmd == CMD_PLAY || cmd == CMD_BACK || cmd == CMD_VOL_UP || cmd == CMD_VOL_DN) {
                if (tryAutoControl()) {
                    onCommand(cmd);
                    return;
                }
            }
            if (filteredApps.isEmpty()) {
                tvStatus.setText("没有可选应用");
                return;
            }
            switch (cmd) {
                case CMD_NEXT:
                    selectedApp = (selectedApp + 1) % filteredApps.size();
                    updateUI();
                    tvStatus.setText("切换到: " + filteredApps.get(selectedApp).name);
                    break;
                case CMD_PREV:
                    selectedApp = (selectedApp - 1 + filteredApps.size()) % filteredApps.size();
                    updateUI();
                    tvStatus.setText("切换到: " + filteredApps.get(selectedApp).name);
                    break;
                case CMD_ENTER:
                    openApp(selectedApp);
                    break;
                default:
                    tvStatus.setText("选择阶段忽略命令: " + cmd);
                    break;
            }
        } else {
            autoSwitchByForeground();
            if (VoiceAccessibilityService.detectAndEnterCallMode()) {
                tvStatus.setText("微信来电，自动切换控制");
                return;
            }
            boolean a11y = VoiceAccessibilityService.isAvailable();
            Log.i(TAG, "CONTROL: cmd=" + cmd + " pkg=" + currentPkg + " a11y=" + a11y + " type=" + currentType);
            AppAdapter adapter = getAdapter(currentPkg);
            if (adapter != null && a11y && adapter.handleCommand(cmd)) {
                return;
            }
            handleDefaultCommand(cmd);
        }
    }

    private void handleDefaultCommand(int cmd) {
        switch (cmd) {
            case CMD_PLAY:
                sendMediaKey(KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE, "播放/暂停");
                break;
            case CMD_NEXT:
                sendMediaKey(KeyEvent.KEYCODE_MEDIA_NEXT, "下一首");
                break;
            case CMD_PREV:
                sendMediaKey(KeyEvent.KEYCODE_MEDIA_PREVIOUS, "上一首");
                break;
            case CMD_ENTER:
                sendMediaKey(KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE, "播放/暂停");
                break;
            case CMD_BACK:
                returnToSelect();
                break;
            case CMD_VOL_UP:
                audioManager.adjustVolume(AudioManager.ADJUST_RAISE, AudioManager.FLAG_SHOW_UI);
                tvStatus.setText("音量+");
                break;
            case CMD_VOL_DN:
                audioManager.adjustVolume(AudioManager.ADJUST_LOWER, AudioManager.FLAG_SHOW_UI);
                tvStatus.setText("音量-");
                break;
            case CMD_RETURN:
                returnToSelect();
                tvStatus.setText("返回选择软件");
                break;
            case CMD_MODE:
                tvStatus.setText("模式切换(当前App不支持)");
                break;
            case CMD_LEFT:
                GestureHelper.doSwipe(0.5f, 0.8f, 0.5f, 0.2f, null);
                tvStatus.setText("向上翻页");
                break;
            case CMD_RIGHT:
                GestureHelper.doSwipe(0.5f, 0.2f, 0.5f, 0.8f, null);
                tvStatus.setText("向下翻页");
                break;
            case CMD_VOL_MODE:
                volumeMode = !volumeMode;
                Toast.makeText(this, volumeMode ? "已进入音量调节模式" : "已退出音量调节模式", Toast.LENGTH_SHORT).show();
                tvStatus.setText(volumeMode ? "音量模式" : "正常模式");
                break;
            default:
                tvStatus.setText("控制阶段忽略命令: " + cmd);
                break;
        }
    }

    private void killBackgroundApps() {
        ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        for (AppEntry app : allApps) {
            am.killBackgroundProcesses(app.pkg);
        }
        Log.i(TAG, "已清理所有应用后台");
    }

    public void returnToSelect() {
        state = STATE_SELECT;
        updateUI();
        removeFloatWindow();
        if (VoiceAccessibilityService.isAvailable()) {
            VoiceAccessibilityService.cancelTryClick();
            VoiceAccessibilityService.removeHighlight();
        }
        killBackgroundApps();
        Intent back = new Intent(this, MainActivity.class);
        back.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        startActivity(back);
        tvStatus.setText("已返回选择软件，已清理后台");
    }

    private void startLearnCurrent() {
        if (filteredApps.isEmpty()) return;
        if (!isAccessibilityEnabled()) {
            Toast.makeText(this, "请先开启无障碍服务", Toast.LENGTH_LONG).show();
            startActivity(new Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            return;
        }
        if (!VoiceAccessibilityService.isAvailable()) {
            Toast.makeText(this, "无障碍服务已开启但未连接，请关闭后重新开启", Toast.LENGTH_LONG).show();
            startActivity(new Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            return;
        }
        AppEntry app = filteredApps.get(selectedApp);
        VoiceAccessibilityService.startLearning(app.pkg, 0);
        Intent launch = getPackageManager().getLaunchIntentForPackage(app.pkg);
        if (launch != null) {
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(launch);
            tvStatus.setText("学习中：请点击" + app.name + "的播放键");
            Toast.makeText(this, "已打开" + app.name + "，请点击播放键", Toast.LENGTH_LONG).show();
        } else {
            Toast.makeText(this, "无法打开" + app.name, Toast.LENGTH_LONG).show();
        }
    }

    private void startLearnFullscreen() {
        if (filteredApps.isEmpty()) return;
        if (!VoiceAccessibilityService.isAvailable()) {
            Toast.makeText(this, "请先开启无障碍服务", Toast.LENGTH_LONG).show();
            return;
        }
        AppEntry app = filteredApps.get(selectedApp);
        Intent launch = getPackageManager().getLaunchIntentForPackage(app.pkg);
        if (launch != null) {
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(launch);
            showLearnFloatWindow(app.pkg, app.name);
            Toast.makeText(this, "已打开" + app.name + "，请点开始收集", Toast.LENGTH_LONG).show();
        } else {
            Toast.makeText(this, "无法打开" + app.name, Toast.LENGTH_LONG).show();
        }
    }

    private void showLearnFloatWindow(String pkg, String name) {
        if (!Settings.canDrawOverlays(this)) {
            Toast.makeText(this, "请先开启悬浮窗权限", Toast.LENGTH_LONG).show();
            return;
        }
        if (learnFloatView != null) return;
        windowManager = (WindowManager) getSystemService(Context.WINDOW_SERVICE);

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.HORIZONTAL);
        layout.setBackgroundColor(0xCC000000);
        layout.setPadding(8, 8, 8, 8);

        Button btnStart = new Button(this);
        btnStart.setText("开始收集");
        btnStart.setTextSize(11);
        btnStart.setOnClickListener(v -> {
            VoiceAccessibilityService.startLearningManual(pkg, 1);
            Toast.makeText(this, "开始收集，请点全屏按钮", Toast.LENGTH_SHORT).show();
        });

        Button btnEnd = new Button(this);
        btnEnd.setText("结束收集");
        btnEnd.setTextSize(11);
        btnEnd.setOnClickListener(v -> {
            VoiceAccessibilityService.stopLearningManual();
            removeLearnFloatWindow();
        });

        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.setMargins(4, 0, 4, 0);
        layout.addView(btnStart, lp);
        layout.addView(btnEnd, lp);

        learnFloatView = layout;
        WindowManager.LayoutParams p = new WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT, WindowManager.LayoutParams.WRAP_CONTENT,
            Build.VERSION.SDK_INT >= 26 ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                                       : WindowManager.LayoutParams.TYPE_PHONE,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE, PixelFormat.TRANSLUCENT);
        p.gravity = Gravity.TOP | Gravity.CENTER_HORIZONTAL;
        p.y = 50;
        windowManager.addView(learnFloatView, p);
    }

    private void removeLearnFloatWindow() {
        if (learnFloatView != null && windowManager != null) {
            windowManager.removeView(learnFloatView);
            learnFloatView = null;
        }
    }

    private boolean isPredefinedApp(String pkg) {
        for (String[] app : KNOWN_APPS) {
            if (app[0].equals(pkg)) return true;
        }
        return false;
    }

    private void showAddAppDialog() {
        PackageManager pm = getPackageManager();
        Intent main = new Intent(Intent.ACTION_MAIN, null);
        main.addCategory(Intent.CATEGORY_LAUNCHER);
        List<ResolveInfo> ris = pm.queryIntentActivities(main, 0);

        List<ResolveInfo> available = new ArrayList<>();
        for (ResolveInfo ri : ris) {
            String pkg = ri.activityInfo.packageName;
            if (!pkg.equals(getPackageName()) &&
                (ri.activityInfo.applicationInfo.flags & ApplicationInfo.FLAG_SYSTEM) == 0) {
                available.add(ri);
            }
        }
        Collections.sort(available, (a, b) ->
            a.loadLabel(pm).toString().compareTo(b.loadLabel(pm).toString()));

        if (available.isEmpty()) {
            Toast.makeText(this, "没有可添加的应用", Toast.LENGTH_SHORT).show();
            return;
        }

        String[] names = new String[available.size()];
        boolean[] checked = new boolean[available.size()];
        for (int i = 0; i < available.size(); i++) {
            ResolveInfo ri = available.get(i);
            names[i] = ri.loadLabel(pm).toString();
            String pkg = ri.activityInfo.packageName;
            if (isPredefinedApp(pkg)) {
                checked[i] = !prefs.getBoolean("app_" + pkg + "_disabled", false);
            } else {
                checked[i] = prefs.contains("app_" + pkg + "_name");
            }
        }

        AlertDialog dialog = new AlertDialog.Builder(this)
            .setTitle("勾选要控制的应用（取消勾选可删除）")
            .setMultiChoiceItems(names, checked, (d, which, isChecked) -> {
                ResolveInfo ri = available.get(which);
                String pkg = ri.activityInfo.packageName;
                String name = ri.loadLabel(pm).toString();
                if (isChecked) {
                    if (isPredefinedApp(pkg)) {
                        prefs.edit().remove("app_" + pkg + "_disabled").apply();
                        scanApps();
                        refreshFilter();
                    } else {
                        showSelectCategoryDialog(pkg, name, () -> {
                            scanApps();
                            refreshFilter();
                        }, () -> {
                            checked[which] = false;
                            ((AlertDialog) d).getListView().setItemChecked(which, false);
                        });
                    }
                } else {
                    if (isPredefinedApp(pkg)) {
                        prefs.edit().putBoolean("app_" + pkg + "_disabled", true).apply();
                    } else {
                        prefs.edit()
                            .remove("app_" + pkg + "_name")
                            .remove("app_" + pkg + "_cat")
                            .apply();
                    }
                    scanApps();
                    refreshFilter();
                }
            })
            .setPositiveButton("完成", null)
            .create();
        dialog.show();
    }

    private void showSelectCategoryDialog(String pkg, String name,
            Runnable onSuccess, Runnable onCancel) {
        new AlertDialog.Builder(this)
            .setTitle(name + " 的类型")
            .setItems(CAT_NAMES, (d, which) -> {
                prefs.edit()
                    .putString("app_" + pkg + "_name", name)
                    .putInt("app_" + pkg + "_cat", which)
                    .apply();
                Toast.makeText(this, "已添加 " + name + " 到" + CAT_NAMES[which], Toast.LENGTH_SHORT).show();
                if (onSuccess != null) onSuccess.run();
            })
            .setNegativeButton("取消", (d, w) -> {
                if (onCancel != null) onCancel.run();
            })
            .setOnCancelListener(d -> {
                if (onCancel != null) onCancel.run();
            })
            .show();
    }

    private CalibrationLineView[] calibLines = null;
    private CalibrationVLineView[] calibVLines = null;
    private Button btnShowCalib = null;
    private Button btnSaveCalibGlobal = null;
    private Button btnSaveCalibApp = null;

    private void showCalibration() {
        if (!Settings.canDrawOverlays(this)) {
            Toast.makeText(this, "请先开启悬浮窗权限", Toast.LENGTH_LONG).show();
            startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:" + getPackageName())));
            return;
        }
        if (calibLines != null) {
            removeCalibration();
            return;
        }

        WindowManager wm = (WindowManager) getSystemService(Context.WINDOW_SERVICE);
        Point size = new Point();
        wm.getDefaultDisplay().getRealSize(size);
        int screenW = size.x;
        int screenH = size.y;

        int topLine, botLine, centerTop, centerBot;
        String curPkg = VoiceAccessibilityService.getForegroundPkg();
        String curAppNameTmp = curPkg;
        for (AppEntry app : allApps) {
            if (app.pkg.equals(curPkg)) { curAppNameTmp = app.name; break; }
        }
        final String curAppName = curAppNameTmp;
        {
            int defTop = curPkg.equals(AppConstants.WECHAT) ? 226 : 240;
            int defBot = curPkg.equals(AppConstants.WECHAT) ? screenH - 150 : screenH - 100;
            int defCT = defTop + (defBot - defTop) * 3 / 8;
            int defCB = defTop + (defBot - defTop) * 5 / 8;
            topLine = prefs.getInt("calib_top_" + curPkg, prefs.getInt("calib_top", defTop));
            botLine = prefs.getInt("calib_bot_" + curPkg, prefs.getInt("calib_bot", defBot));
            centerTop = prefs.getInt("calib_centerTop_" + curPkg, prefs.getInt("calib_centerTop", defCT));
            centerBot = prefs.getInt("calib_centerBot_" + curPkg, prefs.getInt("calib_centerBot", defCB));
        }

        int lineH = 70;
        calibLines = new CalibrationLineView[4];
        int[] colors = {0xFFFF0000, 0xFFFF0000, 0xFF0000FF, 0xFF0000FF};
        String[] labels = {"上边界", "下边界", "中心上", "中心下"};
        int[] initYs = {topLine, botLine, centerTop, centerBot};

        for (int i = 0; i < 4; i++) {
            WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                screenW, lineH,
                Build.VERSION.SDK_INT >= 26 ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                                            : WindowManager.LayoutParams.TYPE_PHONE,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);
            lp.gravity = Gravity.TOP | Gravity.LEFT;
            lp.x = 0;
            CalibrationLineView line = new CalibrationLineView(this, labels[i], colors[i], wm, lp, initYs[i], screenH);
            wm.addView(line, lp);
            calibLines[i] = line;
        }

        int leftLine = prefs.getInt("calib_left_" + curPkg, prefs.getInt("calib_left", screenW / 4));
        int rightLine = prefs.getInt("calib_right_" + curPkg, prefs.getInt("calib_right", screenW * 3 / 4));
        int vLineW = 70;
        calibVLines = new CalibrationVLineView[2];
        int[] vColors = {0xFF00FF00, 0xFF00FF00};
        String[] vLabels = {"左边界", "右边界"};
        int[] vInitXs = {leftLine, rightLine};
        for (int i = 0; i < 2; i++) {
            WindowManager.LayoutParams vlp = new WindowManager.LayoutParams(
                vLineW, screenH,
                Build.VERSION.SDK_INT >= 26 ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                                            : WindowManager.LayoutParams.TYPE_PHONE,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);
            vlp.gravity = Gravity.TOP | Gravity.LEFT;
            vlp.y = 0;
            CalibrationVLineView vline = new CalibrationVLineView(this, vLabels[i], vColors[i], wm, vlp, vInitXs[i], screenW);
            wm.addView(vline, vlp);
            calibVLines[i] = vline;
        }

    }

    private void saveCalibration(boolean isGlobal) {
        if (calibLines == null || calibVLines == null) {
            Toast.makeText(this, "请先显示边界线", Toast.LENGTH_SHORT).show();
            return;
        }
        int t = calibLines[0].getLineY();
        int b = calibLines[1].getLineY();
        int ct = calibLines[2].getLineY();
        int cb = calibLines[3].getLineY();
        int l = calibVLines[0].getLineX();
        int r = calibVLines[1].getLineX();
        if (t >= b || ct >= cb || ct < t || cb > b) {
            Toast.makeText(this, "位置不合理", Toast.LENGTH_LONG).show();
            return;
        }
        if (l >= r) {
            Toast.makeText(this, "左边界必须小于右边界", Toast.LENGTH_LONG).show();
            return;
        }
        String prefix;
        if (isGlobal) {
            prefix = "";
        } else {
            String pkg = VoiceAccessibilityService.getForegroundPkg();
            if (pkg.isEmpty()) {
                Toast.makeText(this, "未检测到前台App", Toast.LENGTH_SHORT).show();
                return;
            }
            prefix = "_" + pkg;
        }
        prefs.edit()
            .putInt("calib_top" + prefix, t)
            .putInt("calib_bot" + prefix, b)
            .putInt("calib_centerTop" + prefix, ct)
            .putInt("calib_centerBot" + prefix, cb)
            .putInt("calib_left" + prefix, l)
            .putInt("calib_right" + prefix, r)
            .apply();
        String msg = isGlobal ? "全局" : "当前App";
        removeCalibration();
        updateCalibButtons();
        Log.i(TAG, msg + "校准已保存: " + t + "/" + ct + "/" + cb + "/" + b);
        Toast.makeText(this, msg + "校准已保存: " + t + "/" + ct + "/" + cb + "/" + b, Toast.LENGTH_LONG).show();
    }

    private void updateCalibButtons() {
        if (btnShowCalib == null) return;
        boolean showing = calibLines != null;
        btnShowCalib.setText(showing ? "隐藏边界" : "显示边界");
        btnSaveCalibGlobal.setEnabled(showing);
        btnSaveCalibApp.setEnabled(showing);
    }

    private void removeCalibration() {
        if (calibLines == null) return;
        WindowManager wm = (WindowManager) getSystemService(Context.WINDOW_SERVICE);
        for (CalibrationLineView l : calibLines) {
            if (l != null) wm.removeView(l);
        }
        if (calibVLines != null) {
            for (CalibrationVLineView vl : calibVLines) {
                if (vl != null) wm.removeView(vl);
            }
            calibVLines = null;
        }
        calibLines = null;
    }

    private void showFloatWindow() {
        if (!Settings.canDrawOverlays(this)) {
            Toast.makeText(this, "请先开启悬浮窗权限", Toast.LENGTH_LONG).show();
            startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:" + getPackageName())));
            return;
        }
        if (floatView != null) return;
        windowManager = (WindowManager) getSystemService(Context.WINDOW_SERVICE);

        if (debugMode) {
            LinearLayout layout = new LinearLayout(this);
            layout.setOrientation(LinearLayout.VERTICAL);
            layout.setBackgroundColor(0xCC000000);
            layout.setPadding(2, 2, 2, 2);

            String[] labels = {"上", "确", "下", "返", "播"};
            int[] cmds = {CMD_PREV, CMD_ENTER, CMD_NEXT, CMD_BACK, CMD_PLAY};
            for (int i = 0; i < labels.length; i++) {
                Button btn = new Button(this);
                btn.setText(labels[i]);
                btn.setTextSize(8);
                btn.setMinWidth(0); btn.setPadding(6, 0, 6, 0);
                final int cmd = cmds[i];
                btn.setOnClickListener(v -> onCommand(cmd));
                layout.addView(btn);
            }

            Button btnScan = new Button(this);
            btnScan.setText("扫描"); btnScan.setTextSize(8); btnScan.setMinWidth(0); btnScan.setPadding(6, 0, 6, 0);
            btnScan.setOnClickListener(v -> VoiceAccessibilityService.scanAllFocusable());
            Button btnDump = new Button(this);
            btnDump.setText("dump"); btnDump.setTextSize(8); btnDump.setMinWidth(0); btnDump.setPadding(6, 0, 6, 0);
            btnDump.setOnClickListener(v -> VoiceAccessibilityService.dumpAllNodes());
            Button btnMark = new Button(this);
            btnMark.setText(VoiceAccessibilityService.isShowMarkerEnabled() ? "标记:开" : "标记:关");
            btnMark.setTextSize(8); btnMark.setMinWidth(0); btnMark.setPadding(6, 0, 6, 0);
            btnMark.setOnClickListener(v -> { VoiceAccessibilityService.toggleShowMarker(); btnMark.setText(VoiceAccessibilityService.isShowMarkerEnabled() ? "标记:开" : "标记:关"); });
            Button btnSwipe = new Button(this);
            btnSwipe.setText("滑试"); btnSwipe.setTextSize(8); btnSwipe.setMinWidth(0); btnSwipe.setPadding(6, 0, 6, 0);
            btnSwipe.setOnClickListener(v -> VoiceAccessibilityService.testSwipe());
            Button btnExtractId = new Button(this);
            btnExtractId.setText("提取id"); btnExtractId.setTextSize(8); btnExtractId.setMinWidth(0); btnExtractId.setPadding(6, 0, 6, 0);
            btnExtractId.setOnClickListener(v -> { String result = VoiceAccessibilityService.extractWechatIds(); Toast.makeText(this, result, Toast.LENGTH_LONG).show(); });
            btnShowCalib = new Button(this);
            btnShowCalib.setText("边界"); btnShowCalib.setTextSize(8); btnShowCalib.setMinWidth(0); btnShowCalib.setPadding(6, 0, 6, 0);
            btnShowCalib.setOnClickListener(v -> { showCalibration(); updateCalibButtons(); });
            btnSaveCalibGlobal = new Button(this);
            btnSaveCalibGlobal.setText("存全"); btnSaveCalibGlobal.setTextSize(8); btnSaveCalibGlobal.setMinWidth(0); btnSaveCalibGlobal.setPadding(6, 0, 6, 0); btnSaveCalibGlobal.setEnabled(false);
            btnSaveCalibGlobal.setOnClickListener(v -> saveCalibration(true));
            btnSaveCalibApp = new Button(this);
            btnSaveCalibApp.setText("存个"); btnSaveCalibApp.setTextSize(8); btnSaveCalibApp.setMinWidth(0); btnSaveCalibApp.setPadding(6, 0, 6, 0); btnSaveCalibApp.setEnabled(false);
            btnSaveCalibApp.setOnClickListener(v -> saveCalibration(false));

            for (Button b : new Button[]{btnScan, btnDump, btnMark, btnSwipe, btnExtractId, btnShowCalib, btnSaveCalibGlobal, btnSaveCalibApp}) {
                layout.addView(b);
            }
            floatView = layout;
        } else {
            TextView tv = new TextView(this);
            tv.setText("控制中: " + currentName + "\nPLAY=暂停 NEXT=下首 PREV=上首 BACK=返回");
            tv.setTextSize(14);
            tv.setTextColor(0xFFFFFFFF);
            tv.setBackgroundColor(0x99000000);
            tv.setPadding(20, 12, 20, 12);
            floatView = tv;
        }

        android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
        int defX = dm.widthPixels - 80;
        floatView = wrapWithDragHandle(floatView, "float_x", "float_y", defX, 50);
    }

    private View wrapWithDragHandle(View content, String keyX, String keyY, int defX, int defY) {
        LinearLayout container = new LinearLayout(this);
        container.setOrientation(LinearLayout.VERTICAL);
        container.setBackgroundColor(0xCC000000);
        TextView dragHandle = new TextView(this);
        dragHandle.setText("✥");
        dragHandle.setTextColor(0xFFCCCCCC);
        dragHandle.setTextSize(9);
        dragHandle.setGravity(Gravity.CENTER);
        dragHandle.setPadding(6, 1, 6, 1);
        container.addView(dragHandle);
        container.addView(content);

        int savedX = prefs.getInt(keyX, defX);
        int savedY = prefs.getInt(keyY, defY);
        WindowManager.LayoutParams p = new WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT, WindowManager.LayoutParams.WRAP_CONTENT,
            Build.VERSION.SDK_INT >= 26 ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                                       : WindowManager.LayoutParams.TYPE_PHONE,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE, PixelFormat.TRANSLUCENT);
        p.gravity = Gravity.TOP | Gravity.LEFT;
        p.x = savedX;
        p.y = savedY;
        if (floatLp == null) {
            floatLp = p;
            floatView = container;
        } else {
            floatLpRight = p;
            floatViewRight = container;
        }
        windowManager.addView(container, p);

        dragHandle.setOnTouchListener((v, event) -> {
            switch (event.getAction()) {
                case MotionEvent.ACTION_DOWN:
                    dragDownX = event.getRawX();
                    dragDownY = event.getRawY();
                    dragStartX = p.x;
                    dragStartY = p.y;
                    return true;
                case MotionEvent.ACTION_MOVE:
                    p.x = dragStartX + (int) (event.getRawX() - dragDownX);
                    p.y = dragStartY + (int) (event.getRawY() - dragDownY);
                    windowManager.updateViewLayout(container, p);
                    return true;
                case MotionEvent.ACTION_UP:
                    prefs.edit().putInt(keyX, p.x).putInt(keyY, p.y).apply();
                    return true;
            }
            return false;
        });
        return container;
    }

    private void removeFloatWindow() {
        if (floatView != null && windowManager != null) {
            windowManager.removeView(floatView);
            floatView = null;
            floatLp = null;
        }
        if (floatViewRight != null && windowManager != null) {
            windowManager.removeView(floatViewRight);
            floatViewRight = null;
            floatLpRight = null;
        }
    }

    public static void setFloatTouchable(boolean touchable) {
        if (instance == null || instance.windowManager == null) return;
        int baseFlags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
        if (!touchable) baseFlags |= WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
        if (instance.floatLp != null && instance.floatView != null) {
            instance.floatLp.flags = baseFlags;
            try {
                instance.windowManager.updateViewLayout(instance.floatView, instance.floatLp);
            } catch (Exception e) { }
        }
        if (instance.floatLpRight != null && instance.floatViewRight != null) {
            instance.floatLpRight.flags = baseFlags;
            try {
                instance.windowManager.updateViewLayout(instance.floatViewRight, instance.floatLpRight);
            } catch (Exception e) { }
        }
    }


    @Override
    protected void onDestroy() {
        super.onDestroy();
        removeFloatWindow();
        removeLearnFloatWindow();
        removeCalibration();
        sUsbManager.setCallback(null, null);
        instance = null;
    }

    @Override
    protected void onResume() {
        super.onResume();
        checkPermissions();
        List<VoiceAccessibilityService.EventRecord> events = VoiceAccessibilityService.consumeLearningEvents();
        if (events != null) {
            showLearnResultDialog(events);
        }
    }

    private void showLearnResultDialog(List<VoiceAccessibilityService.EventRecord> events) {
        String learnedPkg = VoiceAccessibilityService.getLearnedPkg();
        int learnMode = VoiceAccessibilityService.consumeLearnMode();
        String prefix = learnMode == 1 ? "fs_" : "";
        String btnName = learnMode == 1 ? "全屏键" : "播放键";
        String appNameTmp = learnedPkg;
        for (AppEntry app : allApps) {
            if (app.pkg.equals(learnedPkg)) { appNameTmp = app.name; break; }
        }
        final String appName = appNameTmp;

        if (events.isEmpty()) {
            Toast.makeText(this, appName + " 未捕获到任何点击", Toast.LENGTH_LONG).show();
            return;
        }

        List<VoiceAccessibilityService.EventRecord> merged = new ArrayList<>();
        List<Integer> mergeCount = new ArrayList<>();
        for (VoiceAccessibilityService.EventRecord r : events) {
            boolean found = false;
            for (int j = 0; j < merged.size(); j++) {
                VoiceAccessibilityService.EventRecord m = merged.get(j);
                if (m.text.equals(r.text) && m.desc.equals(r.desc) && m.viewId.equals(r.viewId) && m.eventType == r.eventType) {
                    mergeCount.set(j, mergeCount.get(j) + 1);
                    found = true;
                    break;
                }
            }
            if (!found) { merged.add(r); mergeCount.add(1); }
        }

        long firstTs = merged.get(0).timestamp;
        String[] items = new String[merged.size()];
        for (int i = 0; i < merged.size(); i++) {
            VoiceAccessibilityService.EventRecord r = merged.get(i);
            double sec = (r.timestamp - firstTs) / 1000.0;
            String info = !r.text.isEmpty() ? r.text : (!r.desc.isEmpty() ? r.desc : r.viewId);
            if (info.isEmpty()) info = "type=" + r.eventType;
            int cnt = mergeCount.get(i);
            items[i] = String.format("%d. [%.1fs] %s%s", i + 1, sec, info, cnt > 1 ? " (x" + cnt + ")" : "");
        }
        new AlertDialog.Builder(this)
            .setTitle(appName + " - 选择" + btnName)
            .setItems(items, (d, which) -> {
                VoiceAccessibilityService.EventRecord r = merged.get(which);
                prefs.edit()
                    .putString(prefix + "text_" + learnedPkg, r.text)
                    .putString(prefix + "desc_" + learnedPkg, r.desc)
                    .putString(prefix + "id_" + learnedPkg, r.viewId)
                    .apply();
                String info = !r.text.isEmpty() ? r.text : (!r.desc.isEmpty() ? r.desc : r.viewId);
                Log.i(TAG, btnName + "特征: pkg=" + learnedPkg + " text=[" + r.text + "] desc=[" + r.desc + "] id=[" + r.viewId + "] type=" + r.eventType);
                tvStatus.setText(appName + " " + btnName + "学习完成: " + info);
                Toast.makeText(this, appName + " " + btnName + "已保存", Toast.LENGTH_LONG).show();
            })
            .setNegativeButton("都不是，重试", (d, w) -> {
                Toast.makeText(this, "请重新点击学习键", Toast.LENGTH_SHORT).show();
            })
            .show();
    }
}
