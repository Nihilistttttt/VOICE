Dual-core EEG v2.2 - V5F FFT feature verification

Purpose:
- V3F still keeps the complete original EEG chain for safety and comparison.
- V3F sends every ADS1299 raw frame to V5F through shared slots + IPC.
- V5F parses ADS1299 raw frame, converts to voltage, performs drift removal, 50 Hz notch and bandpass.
- V5F accumulates 256-point windows with 125-sample hop and extracts 24-direction features based on theta/mu/beta ratios.
- V5F does not initialize or use ADS1299, SPI, DMA, OLED or USART.

Expected DEBUG output:
[V3F] IPC V5F FFT feature v2.2 enabled
[V3F] V3F keeps full EEG, V5F preprocesses+extracts FFT features
IPC_V5F_FFT_SEQ=...,CS=OK,PARSE=OK,V5F_SAMP=...,V5F_STEP=...,V5F_FFT=...,FEAT=OK,F0=...,F1=...,F2=...,F3=...,F16=...,F18=...,CS_BAD=0,PARSE_BAD=0

Key checks:
- CS must stay OK.
- PARSE must stay OK.
- CS_BAD and PARSE_BAD should stay 0.
- V5F_SAMP should keep increasing.
- V5F_FFT should start increasing after the first 256 samples and then about every 125 samples.
- FEAT should change from WAIT to OK after the first V5F FFT window.
