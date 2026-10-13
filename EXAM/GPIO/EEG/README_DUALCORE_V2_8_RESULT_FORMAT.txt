v2.8_RESULT_FORMAT

Based on v2.7 structure version.

Changes:
1. In INFER mode, DEBUG UART no longer prints V5F_RESULT CSV/debug fields.
2. INFER mode prints only:
   --------------------------------
   Current : LEFT/RIGHT/UNKNOWN
   Left    : xx.xx %
   Right   : xx.xx %
   --------------------------------
3. Output period is unchanged: every 500 ACKs, about 2 seconds at 250 Hz.
4. COLLECT mode is unchanged: LEFT/REST 5s/RIGHT/REST 5s loop and DIRCSV output.
5. The uploaded classifier/model files were copied into V3F/Hardware with canonical names.
6. Direction_Model_4ch_Weights.h was also copied to Common/DualCore_Direction_Model_4ch_Weights.h so V5F inference uses the same new weights.

Build order:
1. Clean V3F
2. Build V3F
3. Clean V5F
4. Build V5F
5. Download V5F merged image
