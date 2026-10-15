#!/bin/bash
# 手势状态机宿主单测: 编译 Common/gesture_sm.c（纯逻辑, 无硬件依赖）跑断言。
# 注: 原生 Windows 程序路径必须 D:/... 形式（POSIX /d/... 会报 No such file）。
set -e

GCC="D:/InstallData/EmbTools/STM32/MinGW/bin/gcc.exe"
ROOT="D:/Libraries/Projects/VOICE"

"$GCC" -Wall -Wextra -std=c99 -O1 \
  -I"$ROOT/VOICE_V307/Common" \
  "$ROOT/hosttest/test_gesture_sm.c" \
  "$ROOT/VOICE_V307/Common/gesture_sm.c" \
  -o "$ROOT/hosttest/test_gesture_sm.exe"

"$ROOT/hosttest/test_gesture_sm.exe"
