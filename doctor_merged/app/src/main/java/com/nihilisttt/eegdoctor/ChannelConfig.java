package com.nihilisttt.eegdoctor;

import com.nihilisttt.eegdoctor.R;

import java.util.ArrayList;
import java.util.List;

public class ChannelConfig {

    public enum ChannelType {
        EEG,
        EOG,
        EMG,
        ECG,
        AUX
    }

    private String label;
    private String electrodeName;
    private ChannelType type;
    private int colorResId;
    private boolean visible;
    private float yRange;
    private String unit;
    private int cmdCode;

    public ChannelConfig(String label, String electrodeName, ChannelType type,
                         int colorResId, float yRange, String unit, int cmdCode) {
        this.label = label;
        this.electrodeName = electrodeName;
        this.type = type;
        this.colorResId = colorResId;
        this.visible = true;
        this.yRange = yRange;
        this.unit = unit;
        this.cmdCode = cmdCode;
    }

    public String getLabel() { return label; }
    public String getElectrodeName() { return electrodeName; }
    public ChannelType getType() { return type; }
    public int getColorResId() { return colorResId; }
    public boolean isVisible() { return visible; }
    public void setVisible(boolean visible) { this.visible = visible; }
    public float getYRange() { return yRange; }
    public void setYRange(float yRange) { this.yRange = yRange; }
    public String getUnit() { return unit; }
    public void setUnit(String unit) { this.unit = unit; }
    public int getCmdCode() { return cmdCode; }

    public static List<ChannelConfig> getDefaultDualChannel() {
        List<ChannelConfig> channels = new ArrayList<>();
        channels.add(new ChannelConfig("CH 0", "Fp1", ChannelType.EEG,
                R.color.wave_ch0, 0.5f, "mV", EegChannels.CMD_WAVE_RAW));
        channels.add(new ChannelConfig("CH 1", "Fp2", ChannelType.EEG,
                R.color.wave_ch1, 0.5f, "mV", EegChannels.CMD_WAVE_RAW));
        return channels;
    }

    public static List<ChannelConfig> getDefault8Channel() {
        List<ChannelConfig> channels = new ArrayList<>();
        String[] electrodes = EegChannels.NAMES;
        int[] colors = {
            R.color.wave_ch0, R.color.wave_ch1, R.color.wave_filtered,
            R.color.focus_line, R.color.spectrum_bar, R.color.accent_info,
            R.color.accent_success, R.color.accent_warning
        };
        for (int i = 0; i < electrodes.length; i++) {
            channels.add(new ChannelConfig("CH " + i, electrodes[i], ChannelType.EEG,
                    colors[i % colors.length], 0.5f, "uV", EegChannels.CMD_WAVE_RAW));
        }
        return channels;
    }

    public static List<ChannelConfig> getStandard10_20() {
        List<ChannelConfig> channels = new ArrayList<>();
        String[] electrodes = {"Fp1", "Fp2", "F3", "F4", "C3", "C4", "P3", "P4", "O1", "O2"};
        int[] colors = {
            R.color.wave_ch0, R.color.wave_ch1, R.color.wave_filtered,
            R.color.focus_line, R.color.spectrum_bar, R.color.accent_info,
            R.color.accent_success, R.color.accent_warning, R.color.accent_error,
            R.color.brand_primary
        };
        for (int i = 0; i < electrodes.length; i++) {
            channels.add(new ChannelConfig("CH " + i, electrodes[i], ChannelType.EEG,
                    colors[i % colors.length], 0.5f, "mV", EegChannels.CMD_WAVE_RAW));
        }
        return channels;
    }
}