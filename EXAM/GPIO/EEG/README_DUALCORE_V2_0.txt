braintest2 dualcore v2.0 - V5F raw-frame parse verification

Goal:
- Keep the complete EEG function and algorithm chain on V3F for safety.
- Keep ADS1299/SPI/DMA/DRDY/ring_buffer/OLED/USART ownership on V3F.
- V3F shares one 27-byte ADS1299 raw frame with V5F every STEP_SIZE frames.
- V5F parses ADS1299 status/channel codes and converts CH0(CP3) to microvolt_x1000.
- V3F verifies that V5F's parsed CH0 code matches V3F's own parsed CH0 code.

Expected DEBUG serial output:
[V3F] IPC parse v2.0 enabled
[V3F] EEG still on V3F, V5F parse raw frame only
System Start
DIRCSV,...
IPC_PARSE_SEQ=10,V3F_TX=11,CS=OK,PARSE=OK,V3F_CH0=xxxx,V5F_CH0=xxxx,V5F_CH0_uVx1000=xxxx,CS_OK=10,CS_BAD=0,PARSE_OK=10,PARSE_BAD=0

Pass condition:
- CS=OK keeps appearing.
- PARSE=OK keeps appearing.
- CS_BAD=0 and PARSE_BAD=0 stay stable.
- DIRCSV and waveform output are still normal.

Notes:
- This is not the final split yet. Filters, FFT, feature extraction and model inference still run on V3F.
- If this version is stable, the next step is to move a small processing block from V3F to V5F.
