#!/bin/bash
# 手势状态机宿主单测: 编译 Services/gesture_sm.c + Framework/smf.c（均为纯逻辑,
# 无硬件依赖）跑断言。SMF 化重构后本测试一字未改, 全过即等价性证明。
# 2026-10-15 目录重构: Common/gesture_sm→Services/, Common/smf→Framework/
# 注: 原生 Windows 程序路径必须 D:/... 形式（POSIX /d/... 会报 No such file）。
set -e

GCC="D:/InstallData/EmbTools/STM32/MinGW/bin/gcc.exe"
ROOT="D:/Libraries/Projects/VOICE"

"$GCC" -Wall -Wextra -std=c99 -O1 \
  -I"$ROOT/VOICE_V307/Services" \
  -I"$ROOT/VOICE_V307/Framework" \
  "$ROOT/hosttest/test_gesture_sm.c" \
  "$ROOT/VOICE_V307/Services/gesture_sm.c" \
  "$ROOT/VOICE_V307/Services/params.c" \
  "$ROOT/VOICE_V307/Framework/smf.c" \
  -o "$ROOT/hosttest/test_gesture_sm.exe"

"$ROOT/hosttest/test_gesture_sm.exe"
