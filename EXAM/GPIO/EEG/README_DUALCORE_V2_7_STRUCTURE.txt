braintest2 dual-core v2.7 structure optimization

Purpose:
- Preserve v2.6 functions and mode behavior.
- Clean up Common/hardware.c so it only keeps the top-level Hardware() mode selector.
- Move the dual-core IPC, shared ring, V5F EEG preprocessing/FFT/feature/inference backend out of hardware.c.

New structure:
Common/hardware.c
    Top-level hardware mode selection only.

Common/hardware.h
    Hardware() declaration. Includes dualcore_ipc.h for backward compatibility.

Common/dualcore_ipc.h
    Public APIs used by V3F/User/main.c, V5F/User/main.c, and V3F/Hardware/Signal_Analysis.c.

Common/dualcore_ipc.c
    Dual-core IPC channel setup, shared ring, V3F send path, V5F drain path, ADS1299 frame parse, V5F preprocessing, FFT feature extraction, and LEFT/RIGHT inference backend.

Run modes are unchanged:
- Edit EEG_APP_MODE in V3F/Hardware/Signal_Analysis.c.
- EEG_APP_MODE_COLLECT: LEFT/RIGHT alternating collection with REST gap.
- EEG_APP_MODE_INFER: V5F inference result every 2 seconds.

Build order:
1. Clean V3F
2. Build V3F
3. Clean V5F
4. Build V5F
5. Download from V5F project so Merge.bin is generated and flashed.
