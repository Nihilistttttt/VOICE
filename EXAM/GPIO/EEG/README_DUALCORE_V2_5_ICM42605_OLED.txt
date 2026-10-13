DualCore v2.5 - V5F EEG inference + ICM42605 SPI OLED display

Based on v2.4 V5F inference version.

Function split:
- V3F:
  1) ADS1299 SPI3 + DMA + DRDY acquisition
  2) Keeps original EEG chain and upper-computer waveform/DIRCSV for comparison
  3) Shares raw EEG frames to V5F through shared ring + IPC
  4) Runs ICM42605 attitude task on SPI4 + DMA2 and displays X/Y/Z angle offset on SPI OLED

- V5F:
  1) Drains shared EEG ring
  2) Parses ADS1299 frames
  3) Runs preprocessing + FFT feature extraction
  4) Runs LEFT/RIGHT inference and reports result back to V3F

ICM42605 placement:
- ICM42605 is intentionally placed on V3F, not V5F.
- Reason: SPI4 and SPI OLED are physical peripherals, and V5F should remain focused on EEG feature/inference verification.
- ICM reads are non-blocking after init: TIM2 only sets flags, SPI4 reads use DMA2_Channel1/2, OLED refresh is done in the main loop at low frequency.

ICM42605 pins from the tested gyro project:
- SCK  : PE2
- MISO : PE13
- MOSI : PE14
- CS   : PE3 software GPIO

ICM42605 resources:
- SPI4
- DMA2_Channel1 RX
- DMA2_Channel2 TX
- TIM2 1 ms tick
- SPI OLED display

Expected DEBUG output:
- [V3F] boot
- [V3F] SystemCoreClk:100000000
- [V3F] wake V5F...
- [V3F] V5F HSEM released
- [V3F] IPC V5F inference v2.4 enabled
- System Start
- ICM42605_INIT=0 if ICM init succeeds
- DIRCSV / IPC_V5F_INF / V5F_RESULT output continues

Expected SPI OLED output:
- During init: ICM INIT / KEEP STILL / SET ZERO POINT
- After init: X/Y/Z angle offset in degrees

Build/download order:
1. Clean V3F
2. Build V3F
3. Clean V5F
4. Build V5F
5. Select V5F project and download the merged image
