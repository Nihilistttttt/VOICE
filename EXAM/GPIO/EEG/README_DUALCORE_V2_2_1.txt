braintest2 dual-core v2.2.1

Purpose:
- Keep V3F complete EEG path as reference.
- V3F writes every ADS1299 frame into a 64-slot shared ring.
- IPC is used as a doorbell; V5F drains all pending frames instead of processing only the latest IPC message.
- V5F parses, preprocesses, accumulates 256-point windows, and extracts FFT direction features.

Expected:
- CS_BAD should stay 0.
- PARSE_BAD should stay 0 or not increase after initial startup.
- V5F_SAMP should track IPC_V5F_FFT_SEQ closely.
