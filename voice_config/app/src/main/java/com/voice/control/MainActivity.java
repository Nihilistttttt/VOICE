package com.voice.control;

import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

/**
 * VOICE 配置 App(横屏) —— 双页面: [参数页] [波形页], 底部 Tab 切换。
 * 波形页完全照搬 EEG WaveformView 可控项: X 时间窗 / Y 量程 / 暂停 / 清屏,
 * 阈值虚线(吹按/吹放/滚动)随参数回执联动。
 * 协议 v2 二进制成帧, TCP server(:41002), ESP IP 零手输。
 */
public class MainActivity extends AppCompatActivity implements EspLink.Listener {

    /** 参数表(与 Services/params.c 描述符表一致): {id, key, 标签, 默认, min, max} */
    private static final Object[][] PARAMS = {
        {0, "joy_deadzone",     "摇杆死区",       500, 0,    4000},
        {1, "joy_step_div",     "摇杆灵敏度",     128, 16,   1024},
        {2, "joy_clamp",        "单帧最大位移",   64,  8,    127},
        {3, "press_on",         "吹气按下阈值",   250, 50,   480},
        {4, "press_off",        "吹气释放阈值",   150, 20,   400},
        {5, "idle_win",         "跨口空闲窗口",   100, 20,   400},
        {6, "scroll_on",        "滚动进入阈值",   150, 50,   480},
        {7, "scroll_off",       "滚动退出阈值",   100, 20,   400},
        {8, "long_ms",          "长按判定",       500, 100,  5000},
        {9, "vol_tilt",         "音量倾斜阈值",   600, 100,  8000},
        {10, "debounce",        "按下去抖拍数",   2,   1,    10},
        {11, "home_x_center",   "回主页X定位",    540, 100,  4000},
        {12, "home_y_offset",   "主页键Y偏移",    24,  0,    200},
        {13, "home_flood_ticks","冲角拍数",       12,  4,    60},
        {14, "click_hold_ms",   "点击保持时长",   30,  5,    200},
        {15, "locate_ms",       "X/Y定位节奏",    25,  5,    100},
    };

    private EspLink mLink;
    private TextView mConnState;
    private TextView mS1Text, mS2Text;
    private TextView mLogText;
    private WaveView mWaveView;
    private View mPageParams, mPageWave;
    private Button mTabParams, mTabWave;
    private Button mBtnX, mBtnY, mBtnPause;
    private final int[] mEdits = new int[PARAMS.length];
    private final Handler mUi = new Handler(Looper.getMainLooper());
    private long mLastLog = 0;
    private boolean mShowWave = false;
    private final java.util.HashMap<Integer, Integer> mCur = new java.util.HashMap<>();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        for (Object[] row : PARAMS) {
            mCur.put((Integer) row[0], (Integer) row[3]);   /* 参数当前值缓存(阈值联动源) */
        }
        setContentView(buildUi());
        showPage(false);
    }

    /* ================= 页面框架 ================= */
    private View buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xFFF4F5F7);

        FrameLayout content = new FrameLayout(this);
        content.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
        mPageParams = buildParamsPage();
        mPageWave = buildWavePage();
        content.addView(mPageParams);
        content.addView(mPageWave);
        root.addView(content);

        /* 底部 Tab */
        LinearLayout tab = new LinearLayout(this);
        tab.setOrientation(LinearLayout.HORIZONTAL);
        tab.setBackgroundColor(0xFF37474F);
        mTabParams = tabBtn("参数设置");
        mTabParams.setOnClickListener(v -> showPage(false));
        mTabWave = tabBtn("实时波形");
        mTabWave.setOnClickListener(v -> showPage(true));
        tab.addView(mTabParams);
        tab.addView(mTabWave);
        root.addView(tab);
        return root;
    }

    private Button tabBtn(String text) {
        Button b = new Button(this);
        b.setText(text);
        b.setTextSize(14);
        b.setBackground(null);
        b.setTextColor(0xFFB0BEC5);
        b.setLayoutParams(new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        return b;
    }

    private void showPage(boolean wave) {
        mShowWave = wave;
        mPageWave.setVisibility(wave ? View.VISIBLE : View.GONE);
        mPageParams.setVisibility(wave ? View.GONE : View.VISIBLE);
        mTabWave.setTextColor(wave ? 0xFFFFFFFF : 0xFFB0BEC5);
        mTabParams.setTextColor(wave ? 0xFFB0BEC5 : 0xFFFFFFFF);
    }

    /* ================= 参数页 ================= */
    private View buildParamsPage() {
        int pad = dp(10);
        LinearLayout left = new LinearLayout(this);
        left.setOrientation(LinearLayout.VERTICAL);
        left.setPadding(pad, pad, pad, dp(4));

        mConnState = new TextView(this);
        mConnState.setText("未连接 —— 点击连接, 监听 41002");
        mConnState.setTextSize(13);
        mConnState.setTypeface(null, Typeface.BOLD);
        mConnState.setPadding(dp(6), 0, 0, dp(6));
        left.addView(mConnState);

        Button connBtn = new Button(this);
        connBtn.setText("连接(监听ESP:41002)");
        connBtn.setTextSize(13);
        connBtn.setBackgroundResource(R.drawable.bg_btn_primary);
        connBtn.setTextColor(0xFFFFFFFF);
        connBtn.setOnClickListener(v -> toggleConnect());
        left.addView(connBtn);

        LinearLayout rows = new LinearLayout(this);
        rows.setOrientation(LinearLayout.VERTICAL);
        for (int i = 0; i < PARAMS.length; i++) {
            rows.addView(buildParamRow(i));
        }
        ScrollView sc = new ScrollView(this);
        sc.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
        sc.addView(rows);
        left.addView(sc);

        LinearLayout cmdRow = new LinearLayout(this);
        cmdRow.setOrientation(LinearLayout.HORIZONTAL);
        cmdRow.setPadding(0, dp(4), 0, 0);
        Button save = smallBtn("保存到SD");
        save.setOnClickListener(v -> {
            if (mLink != null && mLink.isRunning()) mLink.sendCmdAsync(0);
            else toast("未连接");
        });
        cmdRow.addView(save);
        Button factory = smallBtn("恢复出厂");
        factory.setOnClickListener(v -> {
            if (mLink != null && mLink.isRunning()) mLink.sendCmdAsync(1);
            else toast("未连接");
        });
        cmdRow.addView(factory);
        left.addView(cmdRow);
        return left;
    }

    /** 参数行: 标签 | 值 | 发 | 默 */
    private View buildParamRow(int i) {
        int id = (Integer) PARAMS[i][0];
        boolean isSens = (id == 1);
        String label = isSens ? "灵敏度(倍率,越大越快)"
                              : (String) PARAMS[i][2];
        int def = (Integer) PARAMS[i][3];
        int min = (Integer) PARAMS[i][4];
        int max = (Integer) PARAMS[i][5];

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(6), dp(3), dp(6), dp(3));

        TextView name = new TextView(this);
        name.setText(label);
        name.setTextSize(12);
        name.setLayoutParams(new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.35f));
        row.addView(name);

        EditText val = new EditText(this);
        val.setText(isSens ? stepDivToSens(def) : String.valueOf(def));
        val.setSingleLine();
        val.setTextSize(12);
        val.setInputType(android.text.InputType.TYPE_CLASS_NUMBER
                | android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL);
        val.setLayoutParams(new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 0.75f));
        val.setId(View.generateViewId());
        mEdits[i] = val.getId();
        row.addView(val);

        Button send = smallBtn("发");
        send.setOnClickListener(v -> {
            try {
                int value;
                if (isSens) {
                    double s = Double.parseDouble(val.getText().toString());
                    value = sensToStepDiv(s);
                } else {
                    value = Integer.parseInt(val.getText().toString());
                }
                if (value < min) value = min;
                if (value > max) value = max;
                val.setText(isSens ? stepDivToSens(value)
                                   : String.valueOf(value));
                if (mLink != null && mLink.isRunning()) {
                    mLink.sendParamAsync(id, value);
                } else {
                    toast("未连接");
                }
            } catch (NumberFormatException ex) {
                toast("数值非法");
            }
        });
        row.addView(send);

        Button defBtn = smallBtn("默");
        defBtn.setOnClickListener(v -> val.setText(
                isSens ? stepDivToSens(def) : String.valueOf(def)));
        row.addView(defBtn);
        return row;
    }

    /* ================= 波形页(独立页面 + EEG 可控项) ================= */
    private View buildWavePage() {
        int pad = dp(10);
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setBackgroundColor(0xFF14141E);
        page.setPadding(pad, pad, pad, dp(4));

        /* 控制条: X 窗口 / Y 量程 / 暂停 / 清屏 (照搬 EEG 可控项) */
        LinearLayout ctrl = new LinearLayout(this);
        ctrl.setOrientation(LinearLayout.HORIZONTAL);
        mBtnX = waveBtn(getXLabelOf(mWaveView));
        mBtnX.setOnClickListener(v -> {
            if (mWaveView != null) {
                mWaveView.cycleX();
                mBtnX.setText(mWaveView.getXLabel());
            }
        });
        ctrl.addView(mBtnX);
        mBtnY = waveBtn("Y:自动");
        mBtnY.setOnClickListener(v -> {
            if (mWaveView != null) {
                mWaveView.cycleY();
                mBtnY.setText(mWaveView.getYLabel());
            }
        });
        ctrl.addView(mBtnY);
        mBtnPause = waveBtn("暂停");
        mBtnPause.setOnClickListener(v -> {
            if (mWaveView != null) {
                mWaveView.togglePause();
                mBtnPause.setText(mWaveView.isPaused() ? "继续" : "暂停");
            }
        });
        ctrl.addView(mBtnPause);
        Button clear = waveBtn("清屏");
        clear.setOnClickListener(v -> { if (mWaveView != null) mWaveView.clear(); });
        ctrl.addView(clear);
        page.addView(ctrl);

        /* S1/S2 数值行 */
        LinearLayout chips = new LinearLayout(this);
        chips.setOrientation(LinearLayout.HORIZONTAL);
        mS1Text = chip(chips, 0xFFFF5A5A);
        TextView sp = new TextView(this);
        sp.setText("    ");
        chips.addView(sp);
        mS2Text = chip(chips, 0xFF53C7FF);
        page.addView(chips);

        mWaveView = new WaveView(this);
        LinearLayout.LayoutParams wlp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        wlp.setMargins(0, dp(6), 0, 0);
        mWaveView.setLayoutParams(wlp);
        page.addView(mWaveView);

        /* 同步控制条初值 */
        mUi.post(() -> {
            if (mWaveView != null) {
                mBtnX.setText(mWaveView.getXLabel());
                mBtnY.setText(mWaveView.getYLabel());
                updateThresholds();
            }
        });
        return page;
    }

    private String getXLabelOf(WaveView w) { return w == null ? "X:10s" : w.getXLabel(); }

    private Button waveBtn(String text) {
        Button b = new Button(this);
        b.setText(text);
        b.setTextSize(12);
        b.setAllCaps(false);
        b.setMinHeight(0);
        b.setPadding(dp(14), dp(6), dp(14), dp(6));
        b.setBackgroundResource(R.drawable.bg_btn_outline);
        b.setTextColor(0xFFECEFF1);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        lp.setMargins(dp(4), 0, dp(4), 0);
        b.setLayoutParams(lp);
        return b;
    }

    private TextView chip(LinearLayout parent, int color) {
        TextView t = new TextView(this);
        t.setText("-- Pa");
        t.setTextSize(16);
        t.setTypeface(null, Typeface.BOLD);
        t.setTextColor(color);
        parent.addView(t);
        return t;
    }

    private Button smallBtn(String text) {
        Button b = new Button(this);
        b.setText(text);
        b.setTextSize(12);
        b.setMinHeight(0);
        b.setPadding(dp(10), dp(4), dp(10), dp(4));
        return b;
    }

    /* ---- 灵敏度语义翻转: App 显示倍率(越大越快), 固件存 step_div(除数) ---- */
    private String stepDivToSens(int sd) {
        return String.format(java.util.Locale.US, "%.2f", 128.0 / Math.max(16, sd));
    }

    private int sensToStepDiv(double sens) {
        double sd = 128.0 / Math.max(0.125, sens);
        return (int) Math.round(sd);
    }

    private int dp(int v) {
        return (int) (getResources().getDisplayMetrics().density * v);
    }

    private void toggleConnect() {
        if (mLink != null && mLink.isRunning()) {
            mLink.stop();
            mLink = null;
            mConnState.setText("未连接 —— 点击连接, 监听 41002");
        } else {
            mLink = new EspLink(this);
            if (mLink.start()) {
                mConnState.setText("监听中(等待 ESP 连入 41002)...");
            }
        }
    }

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_SHORT).show();
    }

    private void appendLog(String s) {
        long now = System.currentTimeMillis();
        if (now - mLastLog < 200) return;
        mLastLog = now;
        mUi.post(() -> {
            if (mLogText == null) return;        /* UI 未就绪防护 */
            mLogText.setText(s + "\n" + mLogText.getText());
            if (mLogText.length() > 600) {
                mLogText.setText(mLogText.getText().subSequence(0, 300));
            }
        });
    }

    /** 阈值线联动: 波形上的吹按/吹放/滚动线随参数实时移动 */
    private void updateThresholds() {
        if (mWaveView != null) {
            mWaveView.setThresholds(
                    mCur.get(3), mCur.get(4), mCur.get(6), mCur.get(7));
        }
    }

    /* ---- EspLink.Listener ---- */
    @Override
    public void onClientConnected(String ip) {
        mUi.post(() -> mConnState.setText("ESP 已连入: " + ip + " (协议 v2)"));
    }

    @Override
    public void onClientDisconnected() {
        mUi.post(() -> mConnState.setText("ESP 断开, 等待重连..."));
    }

    @Override
    public void onWave(int s1, int s2) {
        if (mWaveView != null) mWaveView.addSample(s1, s2);
        mUi.post(() -> {
            mS1Text.setText("S1 " + s1 + " Pa");
            mS2Text.setText("S2 " + s2 + " Pa");
        });
    }

    @Override
    public void onParamEcho(int id, int value) {
        mCur.put(id, value);
        if (id == 3 || id == 4 || id == 6 || id == 7) {
            updateThresholds();                   /* 阈值线联动 */
        }
        for (int i = 0; i < PARAMS.length; i++) {
            if ((Integer) PARAMS[i][0] == id) {
                boolean isSens = (id == 1);
                EditText v = findViewById(mEdits[i]);
                if (v != null) {
                    String txt = isSens ? stepDivToSens(value)
                                        : String.valueOf(value);
                    mUi.post(() -> v.setText(txt));
                }
                appendLog("回执: " + PARAMS[i][1] + " = " + value);
                break;
            }
        }
    }

    @Override
    public void onLog(String line) {
        appendLog(line);
    }

    @Override
    protected void onDestroy() {
        if (mLink != null) mLink.stop();
        super.onDestroy();
    }
}
