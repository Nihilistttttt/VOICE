################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Hardware/Serial.c \
../Hardware/voice_protocol.c 

C_DEPS += \
./Hardware/Serial.d \
./Hardware/voice_protocol.d 

OBJS += \
./Hardware/Serial.o \
./Hardware/voice_protocol.o 

DIR_OBJS += \
./Hardware/*.o \

DIR_DEPS += \
./Hardware/*.d \

DIR_EXPANDS += \
./Hardware/*.253r.expand \


# Each subdirectory must supply rules for building sources it contributes
Hardware/%.o: ../Hardware/%.c
	@	riscv-wch-elf-gcc -march=rv32imac_zba_zbb_zbc_zbs_xw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -DCore_V3F -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Debug" -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Core" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/User" -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/Common" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/Hardware" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/USB_Host" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/Phone" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

