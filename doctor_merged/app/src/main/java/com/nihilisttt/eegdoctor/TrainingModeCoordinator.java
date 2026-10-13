package com.nihilisttt.eegdoctor;

import android.util.Log;

import java.util.EnumMap;
import java.util.Map;

/**
 * Process-wide arbitration for the two active training paradigms.
 * Only one of MI or SSVEP may own timers, analysis workers, or device control at a time.
 */
public final class TrainingModeCoordinator {
    private static final String TAG = "TrainingMode";
    private static final TrainingModeCoordinator INSTANCE = new TrainingModeCoordinator();

    public enum Mode { NONE, SSVEP, MI }

    public interface ModeController {
        void stopForModeSwitch(Mode nextMode);
    }

    private final Map<Mode, ModeController> controllers = new EnumMap<>(Mode.class);
    private Mode activeMode = Mode.NONE;

    private TrainingModeCoordinator() {}

    public static TrainingModeCoordinator getInstance() { return INSTANCE; }

    public synchronized Mode getActiveMode() { return activeMode; }

    public synchronized void registerController(Mode mode, ModeController controller) {
        if (mode == null || mode == Mode.NONE || controller == null) return;
        controllers.put(mode, controller);
    }

    public synchronized void unregisterController(Mode mode, ModeController controller) {
        if (mode == null || controller == null) return;
        if (controllers.get(mode) == controller) controllers.remove(mode);
    }

    public void activate(Mode requestedMode) {
        if (requestedMode == null) requestedMode = Mode.NONE;
        final Mode previousMode;
        final ModeController previousController;
        synchronized (this) {
            if (activeMode == requestedMode) return;
            previousMode = activeMode;
            previousController = controllers.get(previousMode);
            activeMode = requestedMode;
        }
        Log.i(TAG, "mode switch: " + previousMode + " -> " + requestedMode);
        if (previousController != null) {
            try {
                previousController.stopForModeSwitch(requestedMode);
            } catch (Exception e) {
                Log.e(TAG, "failed to stop previous mode " + previousMode, e);
            }
        }
    }

    public synchronized void release(Mode mode) {
        if (mode != null && activeMode == mode) activeMode = Mode.NONE;
    }
}
