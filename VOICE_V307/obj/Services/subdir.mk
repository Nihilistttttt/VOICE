################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
# 2026-10-15 目录重构: 业务层（gesture_sm 自 Common/ 拆入, phone_control/mic_hum 自 Phone/ 拆入）
# 2026-10-15 参数系统: params/params_sd 新增（SD 持久化按 USE_SDCARD 门控）
# 2026-10-15 调试模块: log/serial_cmd 新增（分级日志 + 串口 CLI）
################################################################################

# Add inputs and outputs from these tool invocations to the build variables
C_SRCS += \
../Services/gesture_sm.c \
../Services/log.c \
../Services/mic_hum.c \
../Services/params.c \
../Services/params_sd.c \
../Services/phone_control.c \
../Services/serial_cmd.c

C_DEPS += \
./Services/gesture_sm.d \
./Services/log.d \
./Services/mic_hum.d \
./Services/params.d \
./Services/params_sd.d \
./Services/phone_control.d \
./Services/serial_cmd.d

OBJS += \
./Services/gesture_sm.o \
./Services/log.o \
./Services/mic_hum.o \
./Services/params.o \
./Services/params_sd.o \
./Services/phone_control.o \
./Services/serial_cmd.o

DIR_OBJS += \
./Services/*.o \

DIR_DEPS += \
./Services/*.d \

DIR_EXPANDS += \
./Services/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
Services/%.o: ../Services/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/Libraries/Projects/VOICE/VOICE_V307/Debug" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Core" -I"d:/Libraries/Projects/VOICE/VOICE_V307/User" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Peripheral/inc" -I"d:/Libraries/Projects/VOICE/VOICE_V307/USB_Host" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Common" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Drivers" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Framework" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Middleware" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Services" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
