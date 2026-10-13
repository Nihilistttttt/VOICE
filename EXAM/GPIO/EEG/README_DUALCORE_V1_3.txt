Dualcore EEG v1.3 - IPC runtime verification

Purpose:
1. Keep complete EEG acquisition and algorithm chain on V3F.
2. Keep V5F away from ADS1299/SPI3/DMA/DRDY/OLED/USART data streams.
3. Add a light IPC ping/ACK path while EEG is running.

Expected DEBUG serial output:
[V3F] boot
[V3F] SystemCoreClk:100000000
[V3F] wake V5F...
[V3F] V5F HSEM released
[V3F] IPC runtime test enabled
[V3F] EEG run on V3F, V5F IPC ACK only
System Start
DIRCSV,...
IPC_ACK=10,V3F_NOTIFY=10
IPC_ACK=20,V3F_NOTIFY=20
...

Expected waveform:
WIFI serial waveform output remains enabled from v1.2.

Build/download order:
1. Clean V3F
2. Build V3F
3. Clean V5F
4. Build V5F
5. Select V5F project and download merged image

Modified files:
Common/hardware.c
Common/hardware.h
V3F/User/main.c
V5F/User/main.c
V3F/Hardware/Signal_Analysis.c
