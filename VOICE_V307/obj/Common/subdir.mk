################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Common/Config.c \
../Common/gesture_sm.c \
../Common/hal_gpio.c \
../Common/serial.c \
../Common/smf.c 

C_DEPS += \
./Common/Config.d \
./Common/gesture_sm.d \
./Common/hal_gpio.d \
./Common/serial.d \
./Common/smf.d 

OBJS += \
./Common/Config.o \
./Common/gesture_sm.o \
./Common/hal_gpio.o \
./Common/serial.o \
./Common/smf.o 

DIR_OBJS += \
./Common/*.o \

DIR_DEPS += \
./Common/*.d \

DIR_EXPANDS += \
./Common/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
Common/%.o: ../Common/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/Libraries/Projects/VOICE/VOICE_V307/Debug" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Core" -I"d:/Libraries/Projects/VOICE/VOICE_V307/User" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Peripheral/inc" -I"d:/Libraries/Projects/VOICE/VOICE_V307/USB_Host" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Common" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Phone" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

