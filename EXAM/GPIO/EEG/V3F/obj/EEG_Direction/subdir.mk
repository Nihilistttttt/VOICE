################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../EEG_Direction/eeg_direction_collect.c \
../EEG_Direction/eeg_direction_feature.c \
../EEG_Direction/eeg_direction_infer.c \
../EEG_Direction/eeg_infer_glxss.c 

C_DEPS += \
./EEG_Direction/eeg_direction_collect.d \
./EEG_Direction/eeg_direction_feature.d \
./EEG_Direction/eeg_direction_infer.d \
./EEG_Direction/eeg_infer_glxss.d 

OBJS += \
./EEG_Direction/eeg_direction_collect.o \
./EEG_Direction/eeg_direction_feature.o \
./EEG_Direction/eeg_direction_infer.o \
./EEG_Direction/eeg_infer_glxss.o 

DIR_OBJS += \
./EEG_Direction/*.o \

DIR_DEPS += \
./EEG_Direction/*.d \

DIR_EXPANDS += \
./EEG_Direction/*.253r.expand \


# Each subdirectory must supply rules for building sources it contributes
EEG_Direction/%.o: ../EEG_Direction/%.c
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -DCore_V3F -DGLXSS_ENABLED -I"d:/Libraries/Projects/EEG/EXAM/SRC/Debug" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Core" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/User" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Hardware" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/HAL" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Attention" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Direction" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Filter" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/USB_Host" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

