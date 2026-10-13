################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Phone/aoa_host.c \
../Phone/mic_hum.c \
../Phone/phone_control.c 

C_DEPS += \
./Phone/aoa_host.d \
./Phone/mic_hum.d \
./Phone/phone_control.d 

OBJS += \
./Phone/aoa_host.o \
./Phone/mic_hum.o \
./Phone/phone_control.o 

DIR_OBJS += \
./Phone/*.o \

DIR_DEPS += \
./Phone/*.d \

DIR_EXPANDS += \
./Phone/*.253r.expand \


# Each subdirectory must supply rules for building sources it contributes
Phone/%.o: ../Phone/%.c
	@	riscv-wch-elf-gcc -march=rv32imac_zba_zbb_zbc_zbs_xw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -DCore_V3F -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Debug" -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Core" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/User" -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/Common" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/Hardware" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/USB_Host" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/Phone" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

