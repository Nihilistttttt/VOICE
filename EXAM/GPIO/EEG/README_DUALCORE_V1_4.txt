braintest2 dualcore v1.4 frame-share verification

Purpose:
- Keep the complete EEG acquisition/filter/FFT/model/waveform path on V3F.
- Keep V5F away from ADS1299, SPI, DMA, OLED and USART data streams.
- V3F copies one 27-byte ADS1299 frame to a SRAM shadow buffer every STEP_SIZE frames.
- V3F passes the shadow-buffer address through IPC_MSG0.
- V5F reads the 27 bytes, computes checksum16, and ACKs checksum + receive count.

Expected DEBUG output:
[V3F] IPC frame-share test enabled
[V3F] EEG run on V3F, V5F frame read only
IPC_FRAME_ACK=10,V3F_TX=11,TX_CS=xxxx,V5F_CS=xxxx

If TX_CS and V5F_CS are normally equal, V5F can read the raw EEG frame copied by V3F.
Waveform output on WIFI/USART2 is unchanged.
