package com.nihilisttt.eegdoctor;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

public class ImpedanceFragment extends Fragment implements DataListener {

    private ImpedanceBarView barView;
    private TextView tvStatus;
    private TextView tvBias;
    private View btnCheck;
    private boolean checking = false;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
                             @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View root = inflater.inflate(R.layout.fragment_impedance, container, false);
        barView = root.findViewById(R.id.impedance_bar_view);
        tvStatus = root.findViewById(R.id.tv_imp_status);
        tvBias = root.findViewById(R.id.tv_imp_bias);
        btnCheck = root.findViewById(R.id.btn_imp_check);
        btnCheck.setOnClickListener(v -> startCheck());
        return root;
    }

    private void startCheck() {
        if (checking) return;
        checking = true;
        btnCheck.setEnabled(false);
        tvStatus.setText("检测中...");
        byte[] payload = new byte[1];
        payload[0] = (byte) EegProtocol.CMD_IMPEDANCE_CHECK;
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_IMPEDANCE_CHECK, new byte[0]);
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
    public void onImpedanceResult(float[] kohm, boolean biasConnected) {
        if (getActivity() == null) return;
        getActivity().runOnUiThread(() -> {
            barView.setImpedance(kohm);
            checking = false;
            btnCheck.setEnabled(true);
            int ok = 0;
            for (float v : kohm) if (v <= 800.0f) ok++;
            tvStatus.setText(ok + "/8 合格");
            tvBias.setText(biasConnected ? "参考正常" : "参考断开");
            tvBias.setTextColor(getResources().getColor(
                    biasConnected ? R.color.accent_success : R.color.accent_error,
                    getActivity().getTheme()));
        });
    }

    @Override

    public void onWaveData(int cmd, int ch, float val) {}
    @Override
    public void onSpectrumData(int cmd, float[] mags) {}
    @Override
    public void onFocusData(float attn0, float attn1, float ema0, float ema1,
                            int trend, int instant) {}
}