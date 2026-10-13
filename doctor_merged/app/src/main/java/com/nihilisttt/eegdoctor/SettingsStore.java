package com.nihilisttt.eegdoctor;

import android.content.Context;
import android.content.SharedPreferences;

public class SettingsStore {
    private static final String PREFS_NAME = "eeg_doctor_settings";

    private static final String KEY_WAVE_STEP = "wave_step";
    private static final String KEY_WAVE_STEP_UNIT = "wave_step_unit";
    private static final String KEY_WAVE_LABEL_COUNT = "wave_label_count";
    private static final String KEY_WAVE_X_MAX = "wave_x_max";
    private static final String KEY_SPEC_RANGE = "spec_range";
    private static final String KEY_SPEC_UNIT = "spec_unit";
    private static final String KEY_WAVE_CH_A = "wave_ch_a";
    private static final String KEY_WAVE_CH_B = "wave_ch_b";
    private static final String KEY_WAVE_TYPE_A = "wave_type_a";
    private static final String KEY_WAVE_TYPE_B = "wave_type_b";
    private static final String KEY_SPEC_TYPE_A = "spec_type_a";
    private static final String KEY_SPEC_TYPE_B = "spec_type_b";
    private static final String KEY_WAVE_MODE_A = "wave_mode_a";
    private static final String KEY_WAVE_MODE_B = "wave_mode_b";

    private static final String KEY_WC_CH_A = "wc_ch_a";
    private static final String KEY_WC_CH_B = "wc_ch_b";
    private static final String KEY_SC_CH_A = "sc_ch_a";
    private static final String KEY_SC_CH_B = "sc_ch_b";

    private static final String KEY_FOCUS_LABEL_COUNT = "focus_label_count";
    private static final String KEY_FOCUS_Y_RANGE = "focus_y_range";
    private static final String KEY_FOCUS_X_POINTS = "focus_x_points";

    private static final String KEY_DISPLAY_OUTPUT = "display_output";

    public static final int DISPLAY_OUTPUT_PATIENT = 0;
    public static final int DISPLAY_OUTPUT_GLXSS = 1;

    private static SharedPreferences getPrefs(Context ctx) {
        return ctx.getApplicationContext().getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
    }

    public static int getWaveStep(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_WAVE_STEP, def);
    }
    public static void setWaveStep(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_WAVE_STEP, val).apply();
    }

    public static String getWaveStepUnit(Context ctx, String def) {
        return getPrefs(ctx).getString(KEY_WAVE_STEP_UNIT, def);
    }
    public static void setWaveStepUnit(Context ctx, String val) {
        getPrefs(ctx).edit().putString(KEY_WAVE_STEP_UNIT, val).apply();
    }

    public static int getWaveLabelCount(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_WAVE_LABEL_COUNT, def);
    }
    public static void setWaveLabelCount(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_WAVE_LABEL_COUNT, val).apply();
    }

    public static float getWaveXMax(Context ctx, float def) {
        return getPrefs(ctx).getFloat(KEY_WAVE_X_MAX, def);
    }
    public static void setWaveXMax(Context ctx, float val) {
        getPrefs(ctx).edit().putFloat(KEY_WAVE_X_MAX, val).apply();
    }

    public static float getSpecRange(Context ctx, float def) {
        return getPrefs(ctx).getFloat(KEY_SPEC_RANGE, def);
    }
    public static void setSpecRange(Context ctx, float val) {
        getPrefs(ctx).edit().putFloat(KEY_SPEC_RANGE, val).apply();
    }

    public static String getSpecUnit(Context ctx, String def) {
        return getPrefs(ctx).getString(KEY_SPEC_UNIT, def);
    }
    public static void setSpecUnit(Context ctx, String val) {
        getPrefs(ctx).edit().putString(KEY_SPEC_UNIT, val).apply();
    }

    public static int getFocusLabelCount(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_FOCUS_LABEL_COUNT, def);
    }
    public static void setFocusLabelCount(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_FOCUS_LABEL_COUNT, val).apply();
    }

    public static float getFocusYRange(Context ctx, float def) {
        return getPrefs(ctx).getFloat(KEY_FOCUS_Y_RANGE, def);
    }
    public static void setFocusYRange(Context ctx, float val) {
        getPrefs(ctx).edit().putFloat(KEY_FOCUS_Y_RANGE, val).apply();
    }

    public static int getFocusXPoints(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_FOCUS_X_POINTS, def);
    }
    public static void setFocusXPoints(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_FOCUS_X_POINTS, val).apply();
    }

    public static int getWaveChA(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_WAVE_CH_A, def);
    }
    public static void setWaveChA(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_WAVE_CH_A, val).apply();
    }

    public static int getWaveChB(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_WAVE_CH_B, def);
    }
    public static void setWaveChB(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_WAVE_CH_B, val).apply();
    }

    public static int getWaveTypeA(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_WAVE_TYPE_A, def);
    }
    public static void setWaveTypeA(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_WAVE_TYPE_A, val).apply();
    }

    public static int getWaveTypeB(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_WAVE_TYPE_B, def);
    }
    public static void setWaveTypeB(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_WAVE_TYPE_B, val).apply();
    }

    public static int getSpecTypeA(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_SPEC_TYPE_A, def);
    }
    public static void setSpecTypeA(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_SPEC_TYPE_A, val).apply();
    }

    public static int getSpecTypeB(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_SPEC_TYPE_B, def);
    }
    public static void setSpecTypeB(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_SPEC_TYPE_B, val).apply();
    }

    public static int getWaveModeA(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_WAVE_MODE_A, def);
    }
    public static void setWaveModeA(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_WAVE_MODE_A, val).apply();
    }

    public static int getWaveModeB(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_WAVE_MODE_B, def);
    }
    public static void setWaveModeB(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_WAVE_MODE_B, val).apply();
    }

    public static int getWcChA(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_WC_CH_A, def);
    }
    public static void setWcChA(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_WC_CH_A, val).apply();
    }

    public static int getWcChB(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_WC_CH_B, def);
    }
    public static void setWcChB(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_WC_CH_B, val).apply();
    }

    public static int getScChA(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_SC_CH_A, def);
    }
    public static void setScChA(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_SC_CH_A, val).apply();
    }

    public static int getScChB(Context ctx, int def) {
        return getPrefs(ctx).getInt(KEY_SC_CH_B, def);
    }
    public static void setScChB(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_SC_CH_B, val).apply();
    }

    public static int getDisplayOutput(Context ctx) {
        return getPrefs(ctx).getInt(KEY_DISPLAY_OUTPUT, DISPLAY_OUTPUT_PATIENT);
    }
    public static void setDisplayOutput(Context ctx, int val) {
        getPrefs(ctx).edit().putInt(KEY_DISPLAY_OUTPUT, val).apply();
    }

    public static boolean isGlxssOutput(Context ctx) {
        return getDisplayOutput(ctx) == DISPLAY_OUTPUT_GLXSS;
    }
}