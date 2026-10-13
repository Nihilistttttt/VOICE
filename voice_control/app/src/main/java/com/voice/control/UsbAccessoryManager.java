package com.voice.control;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbAccessory;
import android.hardware.usb.UsbManager;
import android.os.Handler;
import android.os.Looper;
import android.os.Build;
import android.util.Log;

import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

public class UsbAccessoryManager {
    private static final String TAG = "UsbAccessory";
    private static final String ACTION_USB_PERMISSION = "com.voice.control.USB_PERMISSION";

    private volatile Context context;
    private final UsbManager usbManager;
    private volatile Callback callback;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private volatile UsbAccessory accessory;
    private volatile android.os.ParcelFileDescriptor pfd;
    private volatile FileInputStream inputStream;
    private volatile FileOutputStream outputStream;
    private Thread readThread;
    private volatile boolean running = false;
    private volatile boolean permissionRequested = false;

    public interface Callback {
        void onConnected();
        void onDisconnected();
        void onCommandReceived(int command);
        void onEnergyReceived(int meanA, int meanB);
        void onCollectResult(int judge1, int ticks1, int peak1, int avg1,
                             int judge2, int ticks2, int peak2, int avg2, int gap);
    }

    public UsbAccessoryManager(Context context, Callback callback) {
        this.context = context;
        this.callback = callback;
        this.usbManager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
    }

    public void setCallback(Context ctx, Callback cb) {
        if (ctx != null) this.context = ctx;
        this.callback = cb;
    }

    public void start() {
        running = true;
        IntentFilter filter = new IntentFilter();
        filter.addAction(UsbManager.ACTION_USB_ACCESSORY_ATTACHED);
        filter.addAction(UsbManager.ACTION_USB_ACCESSORY_DETACHED);
        filter.addAction(ACTION_USB_PERMISSION);
        try { context.unregisterReceiver(usbReceiver); } catch (Exception e) { }
        if (Build.VERSION.SDK_INT >= 33) {
            context.registerReceiver(usbReceiver, filter, Context.RECEIVER_EXPORTED);
        } else {
            context.registerReceiver(usbReceiver, filter);
        }

        if (readThread == null || !readThread.isAlive()) {
            readThread = new Thread(this::readLoop, "UsbAccessoryRead");
            readThread.start();
        }

        tryConnect();
    }

    public void stop() {
        running = false;
        try { context.unregisterReceiver(usbReceiver); } catch (Exception e) { }
        closeStream();
    }

    public boolean isRunning() {
        return running;
    }

    private final BroadcastReceiver usbReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (ACTION_USB_PERMISSION.equals(action)) {
                UsbAccessory acc = (UsbAccessory) intent.getParcelableExtra(UsbManager.EXTRA_ACCESSORY);
                if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false) && acc != null) {
                    permissionRequested = false;
                    openAccessory(acc);
                }
            } else if (UsbManager.ACTION_USB_ACCESSORY_ATTACHED.equals(action)) {
                Log.i(TAG, "Accessory attached broadcast");
                permissionRequested = false;
                bringActivityToFront();
                tryConnect();
            } else if (UsbManager.ACTION_USB_ACCESSORY_DETACHED.equals(action)) {
                Log.i(TAG, "Accessory detached broadcast");
                permissionRequested = false;
                closeStream();
                mainHandler.post(() -> { if (callback != null) callback.onDisconnected(); });
            }
        }
    };

    private void requestPermission(UsbAccessory acc) {
        Log.i(TAG, "Requesting permission for accessory");
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 31) {
            flags |= PendingIntent.FLAG_MUTABLE;
        }
        PendingIntent pi = PendingIntent.getBroadcast(context, 0,
                new Intent(ACTION_USB_PERMISSION), flags);
        usbManager.requestPermission(acc, pi);
    }

    private void bringActivityToFront() {
        String pkg = context.getPackageName();
        Intent intent = new Intent();
        intent.setClassName(pkg, pkg + ".MainActivity");
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        try {
            context.startActivity(intent);
        } catch (Exception e) {
            Log.e(TAG, "Failed to bring activity to front", e);
        }
    }

    private synchronized void openAccessory(UsbAccessory acc) {
        if (inputStream != null) return;
        Log.i(TAG, "Opening: " + acc.getManufacturer() + "/" + acc.getModel());
        android.os.ParcelFileDescriptor newPfd = usbManager.openAccessory(acc);
        if (newPfd == null) {
            Log.e(TAG, "openAccessory returned null");
            return;
        }
        accessory = acc;
        pfd = newPfd;
        inputStream = new FileInputStream(pfd.getFileDescriptor());
        outputStream = new FileOutputStream(pfd.getFileDescriptor());
        mainHandler.post(() -> { if (callback != null) callback.onConnected(); });
    }

    private synchronized void closeStream() {
        try { if (inputStream != null) inputStream.close(); } catch (Exception e) { }
        try { if (outputStream != null) outputStream.close(); } catch (Exception e) { }
        try { if (pfd != null) pfd.close(); } catch (Exception e) { }
        inputStream = null;
        outputStream = null;
        pfd = null;
        accessory = null;
    }

    private void tryConnect() {
        if (inputStream != null) return;
        UsbAccessory[] accessories = usbManager.getAccessoryList();
        if (accessories == null || accessories.length == 0) return;
        UsbAccessory acc = accessories[0];
        synchronized (this) {
            if (inputStream != null) return;
            Log.i(TAG, "Trying openAccessory directly");
            android.os.ParcelFileDescriptor newPfd = usbManager.openAccessory(acc);
            if (newPfd != null) {
                Log.i(TAG, "openAccessory success (no dialog needed)");
                accessory = acc;
                pfd = newPfd;
                inputStream = new FileInputStream(pfd.getFileDescriptor());
                outputStream = new FileOutputStream(pfd.getFileDescriptor());
                mainHandler.post(() -> { if (callback != null) callback.onConnected(); });
                return;
            }
        }
        if (!permissionRequested) {
            Log.i(TAG, "openAccessory failed, requesting permission");
            permissionRequested = true;
            requestPermission(acc);
        }
    }

    private void readLoop() {
        byte[] buffer = new byte[256];
        int parseState = 0;
        byte[] energyBuf = new byte[4];
        int energyIdx = 0;
        byte[] collectBuf = new byte[13];
        int collectIdx = 0;
        while (running) {
            if (inputStream == null) {
                tryConnect();
                try { Thread.sleep(500); } catch (InterruptedException e) { break; }
                continue;
            }
            try {
                int len = inputStream.read(buffer);
                if (len > 0) {
                    for (int i = 0; i < len; i++) {
                        int b = buffer[i] & 0xFF;
                        if (parseState == 0) {
                            if (b == 0xFE) {
                                parseState = 1;
                                energyIdx = 0;
                            } else if (b == 0xFA) {
                                parseState = 2;
                                collectIdx = 0;
                            } else {
                                final int cmd = b;
                                Log.i(TAG, "Command: " + cmd);
                                mainHandler.post(() -> { if (callback != null) callback.onCommandReceived(cmd); });
                            }
                        } else if (parseState == 1) {
                            energyBuf[energyIdx++] = (byte) b;
                            if (energyIdx >= 4) {
                                final int meanA = ((energyBuf[0] & 0xFF) << 8) | (energyBuf[1] & 0xFF);
                                final int meanB = ((energyBuf[2] & 0xFF) << 8) | (energyBuf[3] & 0xFF);
                                mainHandler.post(() -> { if (callback != null) callback.onEnergyReceived(meanA, meanB); });
                                parseState = 0;
                            }
                        } else {
                            collectBuf[collectIdx++] = (byte) b;
                            if (collectIdx >= 13) {
                                final int j1 = collectBuf[0] & 0xFF;
                                final int t1 = collectBuf[1] & 0xFF;
                                final int p1 = ((collectBuf[2] & 0xFF) << 8) | (collectBuf[3] & 0xFF);
                                final int a1 = ((collectBuf[4] & 0xFF) << 8) | (collectBuf[5] & 0xFF);
                                final int j2 = collectBuf[6] & 0xFF;
                                final int t2 = collectBuf[7] & 0xFF;
                                final int p2 = ((collectBuf[8] & 0xFF) << 8) | (collectBuf[9] & 0xFF);
                                final int a2 = ((collectBuf[10] & 0xFF) << 8) | (collectBuf[11] & 0xFF);
                                final int gap = collectBuf[12] & 0xFF;
                                mainHandler.post(() -> { if (callback != null) callback.onCollectResult(j1, t1, p1, a1, j2, t2, p2, a2, gap); });
                                parseState = 0;
                            }
                        }
                    }
                }
            } catch (IOException e) {
                if (running) Log.e(TAG, "Read error, will retry", e);
                closeStream();
                permissionRequested = false;
                mainHandler.post(() -> { if (callback != null) callback.onDisconnected(); });
                try { Thread.sleep(500); } catch (InterruptedException e2) { break; }
            }
        }
    }

    public void sendData(byte[] data) {
        if (outputStream == null) return;
        try {
            outputStream.write(data);
            Log.i(TAG, "Sent " + data.length + " bytes");
        } catch (IOException e) {
            Log.e(TAG, "Send error", e);
        }
    }

    public boolean isConnected() {
        return inputStream != null;
    }
}
