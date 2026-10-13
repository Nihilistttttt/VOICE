braintest2 dual-core v2.4 - V5F inference verification

Purpose:
- V3F still keeps the full original EEG path for comparison and safety.
- V3F writes every ADS1299 raw frame into the shared ring.
- V5F drains the shared ring, parses CP3/CP4/C3/C4, preprocesses, extracts 24-dimensional FFT features, and runs the embedded 4-channel Linear SVM LEFT/RIGHT classifier.
- V3F prints V5F inference diagnostics only. V5F does not touch USART/OLED/SPI/DMA/ADS1299.

Expected DEBUG output:
[V3F] IPC V5F inference v2.4 enabled
V5F_RESULT_HEADER,fft_seq,pred,score_left,score_right,confidence,trained,infer_count
IPC_V5F_INF_SEQ=...,CS=OK,PARSE=OK,FEAT=OK,INFER=OK,PRED=LEFT/RIGHT,L=...,R=...,CONF=...,CS_BAD=0,PARSE_BAD=0
V5F_RESULT,<fft_seq>,LEFT/RIGHT,<score_left>,<score_right>,<confidence>,<trained>,<infer_count>

Build/download order:
1. Clean V3F
2. Build V3F
3. Clean V5F
4. Build V5F
5. Select V5F and download the merged image.
