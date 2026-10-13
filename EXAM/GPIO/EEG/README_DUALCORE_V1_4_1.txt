braintest2 dualcore v1.4.1 - strict frame-share checksum verification

Goal:
- Keep complete EEG acquisition/filter/FFT/DIRCSV/waveform path on V3F.
- Keep V5F away from ADS1299/SPI/DMA/OLED/USART data streams.
- V3F copies one 27-byte ADS1299 raw frame into shared frame slots every STEP_SIZE frames.
- V3F sends the slot address to V5F through IPC_MSG0.
- V5F reads the frame and returns ACK: high16=V5F checksum, low16=source frame sequence.
- V3F compares V5F checksum with its stored checksum for the same ACK sequence.

Expected DEBUG log:
[V3F] IPC frame-share v1.4.1 strict checksum enabled
[V3F] EEG run on V3F, V5F frame read + strict ACK only
IPC_FRAME_ACK_SEQ=10,V3F_TX=11,TX_CS=0576,V5F_CS=0576,OK,OK=10,BAD=0

Note:
- V3F_TX may still be one ahead of ACK_SEQ. This is normal.
- The key check is TX_CS == V5F_CS and BAD remains 0.
- Waveform output remains on WIFI/USART2.
