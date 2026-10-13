package com.nihilisttt.eegdoctor;

import com.nihilisttt.eegdoctor.R;

import android.content.Intent;
import android.util.Log;
import android.os.Bundle;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;

public class HomeActivity extends AppCompatActivity implements TcpServerManager.ConnectionListener {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (getSupportActionBar() != null) getSupportActionBar().hide();
        setContentView(R.layout.activity_home);

        TcpServerManager.getInstance().start();
        TcpServerManager.getInstance().setAppContext(getApplicationContext());
        TcpServerManager.getInstance().addConnectionListener(this);
        TrainingModeCoordinator.getInstance().activate(TrainingModeCoordinator.Mode.NONE);

        findViewById(R.id.card_impedance).setOnClickListener(v -> launchMonitor(0));
        findViewById(R.id.card_realtime).setOnClickListener(v -> launchMonitor(1));
        findViewById(R.id.card_focus).setOnClickListener(v -> launchMonitor(2));
        findViewById(R.id.card_wave_compare).setOnClickListener(v -> launchMonitor(3));
        findViewById(R.id.card_training).setOnClickListener(v -> launchMonitor(4));
        findViewById(R.id.card_mi_train).setOnClickListener(v -> launchMonitor(5));
        findViewById(R.id.card_posture).setOnClickListener(v -> launchMonitor(6));
        findViewById(R.id.card_system_config).setOnClickListener(v -> launchMonitor(7));
        findViewById(R.id.card_offline_bdf).setOnClickListener(v -> launchMonitor(8));
        findViewById(R.id.card_game).setOnClickListener(v -> launchMonitor(9));

    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        TcpServerManager.getInstance().removeConnectionListener(this);
    }

    private void launchMonitor(int page) {
        Intent intent = new Intent(this, MainActivity.class);
        intent.putExtra("initial_page", page);
        startActivity(intent);
    }

    @Override
    public void onDeviceConnected(boolean connected) {}

    @Override
    public void onPatientConnected(boolean connected) {
        // State is restored by TcpServerManager after the 41004 handshake.
    }
}