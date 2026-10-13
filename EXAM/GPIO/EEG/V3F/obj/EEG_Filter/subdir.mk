################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../EEG_Filter/eeg_ab_extract.c \
../EEG_Filter/eeg_filter.c 

C_DEPS += \
./EEG_Filter/eeg_ab_extract.d \
./EEG_Filter/eeg_filter.d 

OBJS += \
./EEG_Filter/eeg_ab_extract.o \
./EEG_Filter/eeg_filter.o 

DIR_OBJS += \
./EEG_Filter/*.o \

DIR_DEPS += \
./EEG_Filter/*.d \

DIR_EXPANDS += \
./EEG_Filter/*.253r.expand \


# Each subdirectory must supply rules for building sources it contributes
EEG_Filter/%.o: ../EEG_Filter/%.c
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -DCore_V3F -DGLXSS_ENABLED -I"d:/Libraries/Projects/EEG/EXAM/SRC/Debug" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Core" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/User" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Hardware" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/HAL" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Attention" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Direction" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Filter" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/USB_Host" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

