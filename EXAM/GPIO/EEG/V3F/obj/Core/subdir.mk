################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
d:/Libraries/Projects/EEG/EXAM/SRC/Core/core_riscv.c 

C_DEPS += \
./Core/core_riscv.d 

OBJS += \
./Core/core_riscv.o 

DIR_OBJS += \
./Core/*.o \

DIR_DEPS += \
./Core/*.d \

DIR_EXPANDS += \
./Core/*.253r.expand \


# Each subdirectory must supply rules for building sources it contributes
Core/core_riscv.o: d:/Libraries/Projects/EEG/EXAM/SRC/Core/core_riscv.c
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -DCore_V3F -DGLXSS_ENABLED -I"d:/Libraries/Projects/EEG/EXAM/SRC/Debug" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Core" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/User" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Hardware" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/HAL" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Attention" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Direction" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Filter" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/USB_Host" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

