braintest2 dual-core v2.3

Purpose:
- Keep the original full EEG chain on V3F for comparison.
- V5F drains shared raw-frame ring, parses ADS1299 data, preprocesses, runs FFT band features, and exports the full 24-dimensional DIRCSV feature vector.

Expected logs:
[V3F] IPC V5F DIRCSV feature v2.3 enabled
[V3F] V3F keeps full EEG, V5F outputs full 24-dim feature
IPC_V5F_DIR_SEQ=...,CS=OK,PARSE=OK,...,CS_BAD=0,PARSE_BAD=0
V5F_DIRCSV,<fft_seq>,<24 feature values>

Build order:
1. Clean/Build V3F
2. Clean/Build V5F
3. Download V5F merged image
