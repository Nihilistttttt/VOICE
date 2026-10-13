package com.nihilisttt.eegdoctor;

import com.nihilisttt.eegdoctor.R;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import android.util.Log;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.core.content.ContextCompat;

import com.google.android.material.textfield.TextInputEditText;

import com.google.android.material.button.MaterialButtonToggleGroup;

public class ConfigFragment extends Fragment implements TcpServerManager.ConnectionListener {

    private TextView tvDisplayHint;
    private TextView tvMcuStatus;
    private TextView tvPatientStatus;
    private TextView tvGlxssStatus;
    private View dotMcu;
    private View dotPatient;
    private View dotGlxss;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View root = inflater.inflate(R.layout.fragment_config, container, false);

        TextInputEditText etSsid = root.findViewById(R.id.et_wifi_ssid);
        TextInputEditText etPassword = root.findViewById(R.id.et_wifi_password);

        root.findViewById(R.id.btn_wifi_add).setOnClickListener(v -> {
            String ssid = etSsid.getText() != null ? etSsid.getText().toString().trim() : "";
            String pwd = etPassword.getText() != null ? etPassword.getText().toString().trim() : "";
            Log.i("Config", "btn_wifi_add clicked, ssid=[" + ssid + "]");
            if (ssid.isEmpty()) {
                Toast.makeText(requireContext(), "请输入SSID", Toast.LENGTH_SHORT).show();
                return;
            }
            TcpServerManager.getInstance().sendWifiAdd(ssid, pwd);
            Toast.makeText(requireContext(), "WiFi配置已发送: " + ssid, Toast.LENGTH_SHORT).show();
        });

        root.findViewById(R.id.btn_wifi_delete).setOnClickListener(v -> {
            String ssid = etSsid.getText() != null ? etSsid.getText().toString().trim() : "";
            Log.i("Config", "btn_wifi_delete clicked, ssid=[" + ssid + "]");
            if (ssid.isEmpty()) {
                Toast.makeText(requireContext(), "请输入要删除的SSID", Toast.LENGTH_SHORT).show();
                return;
            }
            TcpServerManager.getInstance().sendWifiDelete(ssid);
            Toast.makeText(requireContext(), "WiFi删除已发送: " + ssid, Toast.LENGTH_SHORT).show();
        });

        MaterialButtonToggleGroup tgDisplay = root.findViewById(R.id.tg_display_output);
        if (tgDisplay != null) {
            int current = SettingsStore.getDisplayOutput(requireContext());
            if (current == SettingsStore.DISPLAY_OUTPUT_GLXSS) {
                tgDisplay.check(R.id.btn_display_glxss);
            } else {
                tgDisplay.check(R.id.btn_display_patient);
            }
            tgDisplay.addOnButtonCheckedListener((group, checkedId, isChecked) -> {
                if (!isChecked) return;
                if (checkedId == R.id.btn_display_glxss) {
                    SettingsStore.setDisplayOutput(requireContext(), SettingsStore.DISPLAY_OUTPUT_GLXSS);
                } else {
                    SettingsStore.setDisplayOutput(requireContext(), SettingsStore.DISPLAY_OUTPUT_PATIENT);
                }
                updateHint();
            });
        }

        tvDisplayHint = root.findViewById(R.id.tv_display_hint);
        tvMcuStatus = root.findViewById(R.id.tv_mcu_status);
        tvPatientStatus = root.findViewById(R.id.tv_patient_status);
        tvGlxssStatus = root.findViewById(R.id.tv_glxss_status);
        dotMcu = root.findViewById(R.id.dot_mcu);
        dotPatient = root.findViewById(R.id.dot_patient);
        dotGlxss = root.findViewById(R.id.dot_glxss);

        TcpServerManager.getInstance().addConnectionListener(this);
        updateHint();
        updateStatusDots();

        return root;
    }

    @Override
    public void onDestroyView() {
        TcpServerManager.getInstance().removeConnectionListener(this);
        super.onDestroyView();
    }

    @Override
    public void onResume() {
        super.onResume();
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_DISPLAY_CFG,
                EegChannels.buildAllNoneDisplayConfigData());
        updateStatusDots();
    }

    @Override
    public void onDeviceConnected(boolean connected) {
        if (getActivity() != null) getActivity().runOnUiThread(this::updateStatusDots);
    }

    @Override
    public void onPatientConnected(boolean connected) {
        if (getActivity() != null) getActivity().runOnUiThread(this::updateStatusDots);
    }

    private void updateHint() {
        if (tvDisplayHint == null || getContext() == null) return;
        boolean glxss = SettingsStore.isGlxssOutput(requireContext());
        if (glxss) {
            tvDisplayHint.setText("AR眼镜模式：刺激画面通过MCU发送至AR眼镜，无需患者端安卓连接");
            tvDisplayHint.setTextColor(ContextCompat.getColor(getContext(), R.color.accent_info));
        } else {
            tvDisplayHint.setText("患者端安卓模式：刺激画面发送至患者端安卓APP，需41004端口连接");
            tvDisplayHint.setTextColor(ContextCompat.getColor(getContext(), R.color.text_secondary));
        }
    }

    private void updateStatusDots() {
        if (tvMcuStatus == null || getContext() == null) return;
        TcpServerManager tcp = TcpServerManager.getInstance();
        boolean mcuConn = tcp.isDeviceConnected();
        boolean patientConn = tcp.isPatientControlConnected();
        boolean glxss = SettingsStore.isGlxssOutput(requireContext());

        tvMcuStatus.setText(mcuConn ? "MCU: 已连接" : "MCU: 未连接");
        dotMcu.setBackgroundColor(ContextCompat.getColor(getContext(),
                mcuConn ? R.color.accent_success : R.color.accent_error));

        tvPatientStatus.setText(patientConn ? "患者端安卓: 已连接" : "患者端安卓: 未连接");
        dotPatient.setBackgroundColor(ContextCompat.getColor(getContext(),
                patientConn ? R.color.accent_success : R.color.accent_error));

        if (glxss) {
            tvGlxssStatus.setText(mcuConn ? "AR眼镜: 可用（MCU已连接）" : "AR眼镜: 不可用（MCU未连接）");
            dotGlxss.setBackgroundColor(ContextCompat.getColor(getContext(),
                    mcuConn ? R.color.accent_success : R.color.accent_warning));
        } else {
            tvGlxssStatus.setText("AR眼镜: 未启用");
            dotGlxss.setBackgroundColor(ContextCompat.getColor(getContext(), R.color.text_secondary));
        }
    }
}
