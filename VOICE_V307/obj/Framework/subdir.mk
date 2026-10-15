################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Framework/smf.c 

C_DEPS += \
./Framework/smf.d 

OBJS += \
./Framework/smf.o 

DIR_OBJS += \
./Framework/*.o \

DIR_DEPS += \
./Framework/*.d \

DIR_EXPANDS += \
./Framework/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
Framework/%.o: ../Framework/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/Libraries/Projects/VOICE/VOICE_V307/Debug" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Core" -I"d:/Libraries/Projects/VOICE/VOICE_V307/User" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Peripheral/inc" -I"d:/Libraries/Projects/VOICE/VOICE_V307/USB_Host" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Common" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Drivers" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Framework" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Middleware" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Services" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

