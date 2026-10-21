package com.voice.control;

import android.content.pm.ActivityInfo;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

/**
 * VOICE 配置 App —— 双页面: [参数页(竖屏)] [波形页(横屏)], 底部 Tab 切换。
 * 方向随页面动态切换(configChanges 声明 → 旋转不重建, TCP 连接不断)。
 * 波形页 EEG 同款: X 时间窗可输入调节(1~120s) / Y 固定±500Pa(每10Pa刻度) / 暂停 / 清屏,
 * 阈值虚线(吹按/吹放/滚动)随参数回执联动。
 * 协议 v2 二进制成帧, TCP server(:41002), ESP IP 零手输。
 */
public class MainActivity extends AppCompatActivity implements EspLink.Listener {

    /** 参数表(与 Services/params.c 描述符表一致):
     *  {id, key, 标签, 默认, min, max, 分组, 单位, 说明} */
    private static final Object[][] PARAMS = {
        {0, "joy_deadzone",     "摇杆死区",          500, 0,    4000, "摇杆控制", "%",
                "杆偏移低于此比例视为未推动, 防手抖漂移"},
        {1, "joy_step_div",     "摇杆灵敏度",        128, 16,   1024, "摇杆控制", "倍",
                "倍率越大光标跟手越快"},
        {2, "joy_clamp",        "单帧最大位移",      64,  8,    127,  "摇杆控制", "px/帧",
                "光标单帧限速, 防瞬移跳跃"},
        {3, "press_on",         "吹气按下阈值",      250, 0,    480,  "压差手势", "Pa",
                "s1口吹到该压力→按下左键(点击/拖拽起点)"},
        {4, "press_off",        "吹气释放阈值",      150, 0,    400,  "压差手势", "Pa",
                "吹住时回落到该值→松开左键(须低于按下阈值)"},
        {5, "idle_win",         "跨口空闲窗口",      100, 20,   400,  "压差手势", "Pa",
                "另一口压力须在此窗口内才判单口手势——两口同忙不判, 防双吹误触单吹"},
        {6, "scroll_on",        "滚动进入阈值",      150, 0,    480,  "压差手势", "Pa",
                "两口同时吹/吸到此值→进入双口滚动(吹=上滚 吸=下滚)"},
        {7, "scroll_off",       "滚动退出阈值",      100, 0,    400,  "压差手势", "Pa",
                "滚动中压力回落到此值以下→退出滚动"},
        {9, "vol_tilt",         "音量倾斜阈值",      600, 0,   8000, "压差手势", "ADC计数",
                "音量模式: 摇杆上/右/右上侧倾斜合计超此值→调大音量, 下/左/左下侧→调小(135°对角线划分)"},
        {10, "debounce",        "按下去抖拍数",      2,   1,    10,   "压差手势", "拍(50ms)",
                "吸气类确认去抖: 连续N拍才触发(防压力毛刺)"},
        {11, "home_x_center",   "回主页X定位",       540, 100,  4000, "回主页序列", "px",
                "回主页序列的目标横坐标(按屏幕宽度标定)"},
        {12, "home_y_offset",   "主页键Y偏移",       24,  0,    200,  "回主页序列", "px",
                "冲角后从屏幕顶端下移到主页键的Y计数"},
        {13, "home_flood_ticks","冲角拍数",          12,  4,    60,   "回主页序列", "拍",
                "回主页先把光标冲向屏幕角落的步数(利用系统指针加速快速到边)"},
        {14, "click_hold_ms",   "点击保持时长",      30,  5,    200,  "回主页序列", "ms",
                "模拟点击的按住时长, 太短系统不识别"},
        {15, "locate_ms",       "X/Y定位节奏",       25,  5,    100,  "回主页序列", "ms",
                "回主页每步移动间隔, 太小会被安卓指针加速放大"},
        {16, "pair_debounce",   "双口确认拍数",      2,   1,    10,   "双口仲裁·防误触", "拍(50ms)",
                "两口同向持续N拍才进滚动——双吹误触单吹的第一道防线"},
        {17, "blow_debounce",   "吹按确认拍数",      3,   1,    10,   "双口仲裁·防误触", "拍(50ms)",
                "吹气持续N拍才确认(s1左键按下/s2进音量, 让出双吹仲裁窗口)"},
        {18, "s2_short_ms",     "多任务窗口",        800, 100,  2000, "双口仲裁·防误触", "ms",
                "S2短吸松口在此窗口内→Alt+Tab多任务; 吸更久松口→无动作(防误触)"},
        {19, "cmd_gap_ms",      "异种命令间隔",      500, 100,  2000, "双口仲裁·防误触", "ms",
                "两种不同命令间的最小间隔(连续同类不受限), 被吞的持续意图会在间隔后自动通过"},
        {20, "s1_short_ms",     "长吸回主页时长",    800, 100,  2000, "压差手势", "ms",
                "S1吸住此时长→回主页; 短于此松口=中键返回"},
        {21, "s1_back_min_ms",  "返回最小吸时",      50,  0,    500,  "压差手势", "ms",
                "吸住至少此时长松口才算中键返回, 更短视为压力毛刺吞掉"},
        {22, "vol_rate_ms",     "音量调节间隔",      50,  20,   1000, "压差手势", "ms",
                "音量模式下音量命令的发送间隔, 越小调得越快(默认50ms)"},
    };

    private EspLink mLink;
    private TextView mConnState;
    private TextView mS1Text, mS2Text;
    private TextView mLogText;
    private WaveView mWaveView;
    private View mPageParams, mPageWave;
    private Button mTabParams, mTabWave;
    private EditText mEditX;
    private EditText mEditYStep;
    private EditText mEditYLabels;
    private Button mBtnPause;
    private final int[] mEdits = new int[PARAMS.length];
    private final Handler mUi = new Handler(Looper.getMainLooper());
    private long mLastLog = 0;
    private boolean mShowWave = false;
    private int mLastS1 = Integer.MIN_VALUE, mLastS2 = Integer.MIN_VALUE;
    private final java.util.HashMap<Integer, Integer> mCur = new java.util.HashMap<>();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        for (Object[] row : PARAMS) {
            mCur.put((Integer) row[0], (Integer) row[3]);   /* 参数当前值缓存(阈值联动源) */
        }
        setContentView(buildUi());
        showPage(false);
        /* 自动连接(对齐 EEG 医生端): 启动即监听 41002, 无需手动按钮;
         * 断线后 EspLink.acceptLoop 持续嗅探, ESP 重连即恢复 */
        mLink = new EspLink(this);
        mLink.start();
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
        /* 参数页竖屏(列表滚动), 波形页横屏(EEG 同款宽幅) */
        setRequestedOrientation(wave
                ? ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
                : ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
    }

    /* ================= 参数页 ================= */
    private View buildParamsPage() {
        int pad = dp(10);
        LinearLayout left = new LinearLayout(this);
        left.setOrientation(LinearLayout.VERTICAL);
        left.setPadding(pad, pad, pad, dp(4));

        mConnState = new TextView(this);
        mConnState.setText("嗅探中 · 等待 ESP 连入 41002...");
        mConnState.setTextSize(13);
        mConnState.setTypeface(null, Typeface.BOLD);
        mConnState.setPadding(dp(6), 0, 0, dp(6));
        left.addView(mConnState);

        LinearLayout rows = new LinearLayout(this);
        rows.setOrientation(LinearLayout.VERTICAL);
        /* 按分组生成卡片: 同组参数进同一张白底圆角卡片, 组间 10dp 间距,
         * 组内行间细分隔线 —— 分类差异一眼可辨 */
        LinearLayout card = null;
        String curGroup = "";
        boolean firstInCard = true;
        for (int i = 0; i < PARAMS.length; i++) {
            String group = (String) PARAMS[i][6];
            if (card == null || !group.equals(curGroup)) {
                curGroup = group;
                LinearLayout.LayoutParams clp = new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT);
                clp.setMargins(dp(2), 0, dp(2), dp(10));
                card = buildGroupCard(group, clp);
                rows.addView(card);
                firstInCard = true;
            }
            if (!firstInCard) {
                View div = new View(this);
                div.setBackgroundColor(0xFFF0F1F5);
                LinearLayout.LayoutParams dlp = new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT, dp(1));
                dlp.setMargins(dp(6), 0, dp(6), 0);
                div.setLayoutParams(dlp);
                card.addView(div);
            }
            firstInCard = false;
            card.addView(buildParamRow(i));
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
            if (mLink != null && mLink.isClientConnected()) mLink.sendCmdAsync(0);
            else toast("ESP 未连入");
        });
        cmdRow.addView(save);
        Button factory = smallBtn("恢复出厂");
        factory.setOnClickListener(v -> {
            if (mLink != null && mLink.isClientConnected()) mLink.sendCmdAsync(1);
            else toast("ESP 未连入");
        });
        cmdRow.addView(factory);
        left.addView(cmdRow);
        return left;
    }

    /** 分组卡片: 白底圆角 + 左色条 + 组名标题 */
    private LinearLayout buildGroupCard(String title, LinearLayout.LayoutParams lp) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xFFFFFFFF);
        bg.setCornerRadius(dp(12));
        bg.setStroke(dp(1), 0xFFE2E6ED);
        card.setBackground(bg);
        card.setPadding(dp(8), dp(8), dp(8), dp(4));
        card.setLayoutParams(lp);

        LinearLayout head = new LinearLayout(this);
        head.setOrientation(LinearLayout.HORIZONTAL);
        head.setGravity(Gravity.CENTER_VERTICAL);
        View bar = new View(this);
        bar.setBackgroundColor(0xFF3D5AFE);
        LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(dp(4), dp(14));
        blp.setMargins(dp(6), 0, dp(8), 0);
        bar.setLayoutParams(blp);
        head.addView(bar);
        TextView t = new TextView(this);
        t.setText(title);
        t.setTextSize(13);
        t.setTypeface(null, Typeface.BOLD);
        t.setTextColor(0xFF3949AB);
        head.addView(t);
        card.addView(head);
        return card;
    }

    /** 参数行: 标签(含单位) | 值 | 发 | 默
     *  显示换算: id=1 灵敏度显示倍率, id=0 死区显示杆量百分比, 其余原值 */
    private View buildParamRow(int i) {
        int id = (Integer) PARAMS[i][0];
        boolean isSens = (id == 1);
        boolean isDead = (id == 0);
        /* 单位并入标签: "吹气按下阈值 (Pa)" —— 不占额外列, 布局不变 */
        String label = PARAMS[i][2] + " (" + PARAMS[i][7] + ")";
        int def = (Integer) PARAMS[i][3];
        int min = (Integer) PARAMS[i][4];
        int max = (Integer) PARAMS[i][5];

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(6), dp(3), dp(6), dp(3));

        /* 名称区: 主标签(含单位) + 灰色小字说明 —— 一眼可懂 */
        LinearLayout nameBox = new LinearLayout(this);
        nameBox.setOrientation(LinearLayout.VERTICAL);
        nameBox.setLayoutParams(new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.35f));

        TextView name = new TextView(this);
        name.setText(label);
        name.setTextSize(12);
        nameBox.addView(name);

        TextView desc = new TextView(this);
        desc.setText((String) PARAMS[i][8]);
        desc.setTextSize(10);
        desc.setTextColor(0xFF8A919E);
        nameBox.addView(desc);
        row.addView(nameBox);

        EditText val = new EditText(this);
        val.setText(isSens ? stepDivToSens(def)
                : isDead ? deadToPct(def) : String.valueOf(def));
        val.setSingleLine();
        val.setTextSize(12);
        val.setInputType(android.text.InputType.TYPE_CLASS_NUMBER
                | android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL);
        val.setLayoutParams(new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 0.75f));
        val.setId(View.generateViewId());
        /* 键盘"完成"键收起软键盘——防焦点滞留的输入框与回执刷新打架 */
        val.setImeOptions(EditorInfo.IME_ACTION_DONE);
        val.setOnEditorActionListener((v, actionId, ev) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE) {
                v.clearFocus();
                hideKb(v);
                return true;
            }
            return false;
        });
        mEdits[i] = val.getId();
        row.addView(val);

        Button send = smallBtn("发");
        send.setOnClickListener(v -> {
            val.clearFocus();          /* 发送即收键盘, 防焦点错乱 */
            hideKb(val);
            try {
                int value;
                if (isSens) {
                    double s = Double.parseDouble(val.getText().toString());
                    value = sensToStepDiv(s);
                } else if (isDead) {
                    double p = Double.parseDouble(val.getText().toString());
                    value = pctToDead(p);
                } else {
                    value = Integer.parseInt(val.getText().toString());
                }
                if (value < min) value = min;
                if (value > max) value = max;
                val.setText(isSens ? stepDivToSens(value)
                        : isDead ? deadToPct(value) : String.valueOf(value));
                if (mLink != null && mLink.isClientConnected()) {
                    mLink.sendParamAsync(id, value);
                } else {
                    toast("ESP 未连入");
                }
            } catch (NumberFormatException ex) {
                toast("数值非法");
            }
        });
        row.addView(send);

        Button defBtn = smallBtn("默");
        defBtn.setOnClickListener(v -> val.setText(
                isSens ? stepDivToSens(def)
                : isDead ? deadToPct(def) : String.valueOf(def)));
        row.addView(defBtn);
        return row;
    }

    /* ================= 波形页(独立页面 + EEG 可控项) ================= */
    private View buildWavePage() {
        int pad = dp(6);
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setBackgroundColor(0xFFF5F6F8);   /* 亮色页面(与绘图区一致) */
        page.setPadding(pad, dp(4), pad, dp(2));

        /* 控制条: X 时间窗输入(秒) / 暂停 / 清屏 */
        LinearLayout ctrl = new LinearLayout(this);
        ctrl.setOrientation(LinearLayout.HORIZONTAL);
        mEditX = new EditText(this);
        mEditX.setText("10");
        mEditX.setHint("1~120");
        mEditX.setSingleLine();
        mEditX.setTextSize(12);
        mEditX.setTextColor(0xFF263238);
        mEditX.setHintTextColor(0xFF808A9F);
        mEditX.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
        LinearLayout.LayoutParams elp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 0.35f);
        elp.setMargins(dp(4), 0, dp(4), 0);
        elp.gravity = Gravity.CENTER_VERTICAL;
        mEditX.setLayoutParams(elp);
        mEditX.setOnEditorActionListener((v, actionId, event) -> {
            applyWaveConfig();
            return true;
        });
        ctrl.addView(lab(this, "X秒"));
        ctrl.addView(mEditX);

        /* Y 轴配置: 步长(Pa/格) + 标签数(3~21), 范围=步长×(标签数-1)/2 */
        mEditYStep = new EditText(this);
        mEditYStep.setSingleLine();
        mEditYStep.setTextSize(12);
        mEditYStep.setTextColor(0xFF263238);
        mEditYStep.setHintTextColor(0xFF808A9F);
        mEditYStep.setInputType(android.text.InputType.TYPE_CLASS_NUMBER
                | android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL);
        mEditYStep.setHint("格Pa");
        mEditYStep.setLayoutParams(elp);
        mEditYStep.setOnEditorActionListener((v, actionId, event) -> {
            applyWaveConfig();
            return true;
        });

        mEditYLabels = new EditText(this);
        mEditYLabels.setSingleLine();
        mEditYLabels.setTextSize(12);
        mEditYLabels.setTextColor(0xFF263238);
        mEditYLabels.setHintTextColor(0xFF808A9F);
        mEditYLabels.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
        mEditYLabels.setHint("标签");
        mEditYLabels.setLayoutParams(elp);
        mEditYLabels.setOnEditorActionListener((v, actionId, event) -> {
            applyWaveConfig();
            return true;
        });

        /* 控件入列顺序: 标签在前、输入框在后(创建已全部完成, 此处只挂树) */
        ctrl.addView(lab(this, "Y格Pa"));
        ctrl.addView(mEditYStep);
        ctrl.addView(lab(this, "Y标签"));
        ctrl.addView(mEditYLabels);
        ctrl.addView(lab(this, "  "));

        Button applyX = waveBtn("应用");
        applyX.setOnClickListener(v -> applyWaveConfig());
        ctrl.addView(applyX);
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

        /* S1/S2 数值行(压缩高度: 字号 14sp, 紧凑间距) */
        LinearLayout chips = new LinearLayout(this);
        chips.setOrientation(LinearLayout.HORIZONTAL);
        chips.setPadding(dp(2), 0, dp(2), 0);
        mS1Text = chip(chips, 0xFFD32F2F);
        TextView sp = new TextView(this);
        sp.setText("   ");
        chips.addView(sp);
        mS2Text = chip(chips, 0xFF1976D2);
        page.addView(chips);

        mWaveView = new WaveView(this);
        LinearLayout.LayoutParams wlp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        wlp.setMargins(0, dp(2), 0, 0);
        mWaveView.setLayoutParams(wlp);
        page.addView(mWaveView);

        /* 恢复上次波形显示配置(X 窗/Y 步长/Y 标签数) */
        restoreWaveConfig();

        /* 阈值线初值 */
        mUi.post(() -> {
            if (mWaveView != null) updateThresholds();
        });
        return page;
    }

    /** 控制条内的小文字标签(灰底深字, 告诉用户每个输入框调什么) */
    private TextView lab(android.content.Context c, String s) {
        TextView t = new TextView(c);
        t.setText(s);
        t.setTextSize(11);
        t.setTextColor(0xFF546E7A);
        t.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                android.view.ViewGroup.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.setMargins(dp(6), 0, 0, 0);
        lp.gravity = Gravity.CENTER_VERTICAL;
        t.setLayoutParams(lp);
        return t;
    }

    private Button waveBtn(String text) {
        Button b = new Button(this);
        b.setText(text);
        b.setTextSize(12);
        b.setAllCaps(false);
        b.setMinHeight(0);
        b.setPadding(dp(10), dp(3), dp(10), dp(3));
        b.setBackgroundResource(R.drawable.bg_btn_outline);
        b.setTextColor(0xFF37474F);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                android.view.ViewGroup.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.setMargins(dp(2), 0, dp(2), 0);
        b.setLayoutParams(lp);
        return b;
    }

    private TextView chip(LinearLayout parent, int color) {
        TextView t = new TextView(this);
        t.setText("-- Pa");
        t.setTextSize(14);
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
        b.setMinWidth(dp(40));    /* 竖屏窄幅下防止默认 minWidth 挤爆行 */
        b.setPadding(dp(10), dp(4), dp(10), dp(4));
        return b;
    }

    private void hideKb(View v) {
        InputMethodManager imm =
                (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (imm != null) imm.hideSoftInputFromWindow(v.getWindowToken(), 0);
    }

    /* ---- 灵敏度语义翻转: App 显示倍率(越大越快), 固件存 step_div(除数) ---- */
    private String stepDivToSens(int sd) {
        return String.format(java.util.Locale.US, "%.2f", 128.0 / Math.max(16, sd));
    }

    private int sensToStepDiv(double sens) {
        double sd = 128.0 / Math.max(0.125, sens);
        return (int) Math.round(sd);
    }

    /* ---- 摇杆死区: App 显示杆量百分比, 固件存 ADC 计数 ----
     * 基准 8800 counts = 摇杆中值(3.3V 分压半量程 ≈1.65V)到边缘的满偏偏移
     * (ADS1115 ±6.144V/16bit, 中值运行时校准 ≈8800) */
    private static final int JOY_HALF_ADC = 8800;

    private String deadToPct(int adc) {
        return String.format(java.util.Locale.US, "%.1f", adc * 100.0 / JOY_HALF_ADC);
    }

    private int pctToDead(double pct) {
        return (int) Math.round(pct * JOY_HALF_ADC / 100.0);
    }

    private int dp(int v) {
        return (int) (getResources().getDisplayMetrics().density * v);
    }

    /** 波形显示配置应用: X 时间窗(1~120s) + Y 步长(Pa/格) + Y 标签数(3~21)。
     *  Y 半量程 = 步长×(标签数-1)/2; 全部持久化, 下次启动自动恢复。 */
    private void applyWaveConfig() {
        if (mWaveView == null) return;
        android.content.SharedPreferences sp =
                getSharedPreferences("wave", MODE_PRIVATE);
        try {
            int sec = Integer.parseInt(mEditX.getText().toString().trim());
            if (sec < 1) sec = 1;
            if (sec > 120) sec = 120;
            mEditX.setText(String.valueOf(sec));

            float step = Float.parseFloat(mEditYStep.getText().toString().trim());
            if (step < 1f) step = 1f;
            if (step > 10000f) step = 10000f;
            mEditYStep.setText(step == Math.floor(step)
                    ? String.valueOf((int) step) : String.valueOf(step));

            int labels = Integer.parseInt(mEditYLabels.getText().toString().trim());
            if (labels < 3) labels = 3;
            if (labels > 41) labels = 41;
            mEditYLabels.setText(String.valueOf(labels));

            mWaveView.setXSeconds(sec);
            mWaveView.setYAxis(step, labels);

            sp.edit().putInt("xSec", sec)
                     .putFloat("yStep", step)
                     .putInt("yLabels", labels).apply();
        } catch (NumberFormatException ex) {
            toast("请输入数字: X=秒, Y步长=Pa/格, 标签数=3~21");
        }
    }

    /** 恢复上次保存的波形显示配置(无存档时默认 X=10s/步长 100/标签 11) */
    private void restoreWaveConfig() {
        android.content.SharedPreferences sp =
                getSharedPreferences("wave", MODE_PRIVATE);
        int sec = sp.getInt("xSec", 10);
        float step = sp.getFloat("yStep", 100f);
        int labels = sp.getInt("yLabels", 11);
        mEditX.setText(String.valueOf(sec));
        mEditYStep.setText(step == Math.floor(step)
                ? String.valueOf((int) step) : String.valueOf(step));
        mEditYLabels.setText(String.valueOf(labels));
        mEditX.post(() -> applyWaveConfig());
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
        mUi.post(() -> mConnState.setText("已连接 · ESP " + ip + " (协议 v2)"));
        /* 连入即拉取全量参数: 固件回推 PARAM_ECHO 逐条刷新显示
         * (含 SD 存储值; 断线重连会再次触发, 始终与固件一致) */
        if (mLink != null) mLink.sendCmdAsync(2);
    }

    @Override
    public void onClientDisconnected() {
        mUi.post(() -> mConnState.setText("已断开 · 嗅探中, 等待 ESP 重连 41002..."));
    }

    @Override
    public void onWave(int s1, int s2) {
        mUi.post(() -> {
            if (mWaveView != null) mWaveView.addSample(s1, s2);
            if (s1 != mLastS1) { mLastS1 = s1; mS1Text.setText("S1 " + s1 + " Pa"); }
            if (s2 != mLastS2) { mLastS2 = s2; mS2Text.setText("S2 " + s2 + " Pa"); }
        });
    }

    @Override
    public void onParamEcho(int id, int value) {
        mUi.post(() -> {
            mCur.put(id, value);
            if (id == 3 || id == 4 || id == 6 || id == 7) {
                updateThresholds();                   /* 阈值线联动 */
            }
            for (int i = 0; i < PARAMS.length; i++) {
                if ((Integer) PARAMS[i][0] == id) {
                    boolean isSens = (id == 1);
                    boolean isDead = (id == 0);
                    EditText v = findViewById(mEdits[i]);
                    /* 正在编辑的框不被回执覆盖——否则键盘弹起时 setText
                     * 重置文本, 部分机型表现为"失焦无法修改" */
                    if (v != null && !v.hasFocus()) {
                        String txt = isSens ? stepDivToSens(value)
                                : isDead ? deadToPct(value)
                                : String.valueOf(value);
                        v.setText(txt);
                    }
                    appendLog("回执: " + PARAMS[i][1] + " = " + value);
                    break;
                }
            }
        });
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
