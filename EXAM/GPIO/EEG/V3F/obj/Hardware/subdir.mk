################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Hardware/ADS1299.c \
../Hardware/Config.c \
../Hardware/Direction_Classifier.c \
../Hardware/Direction_Model.c \
../Hardware/ICM42605.c \
../Hardware/OLED.c \
../Hardware/Serial.c \
../Hardware/Signal_Analysis.c \
../Hardware/Timer_1ms.c \
../Hardware/W25Q64.c \
../Hardware/dir_model_sd.c \
../Hardware/eeg_record.c \
../Hardware/glxss_me.c \
../Hardware/glxss_me_ch32h417.c \
../Hardware/glxss_sd_fw.c \
../Hardware/max98357a.c \
../Hardware/patient_monitor.c \
../Hardware/posture_detect.c \
../Hardware/sd_card.c \
../Hardware/wav_player.c 

C_DEPS += \
./Hardware/ADS1299.d \
./Hardware/Config.d \
./Hardware/Direction_Classifier.d \
./Hardware/Direction_Model.d \
./Hardware/ICM42605.d \
./Hardware/OLED.d \
./Hardware/Serial.d \
./Hardware/Signal_Analysis.d \
./Hardware/Timer_1ms.d \
./Hardware/W25Q64.d \
./Hardware/dir_model_sd.d \
./Hardware/eeg_record.d \
./Hardware/glxss_me.d \
./Hardware/glxss_me_ch32h417.d \
./Hardware/glxss_sd_fw.d \
./Hardware/max98357a.d \
./Hardware/patient_monitor.d \
./Hardware/posture_detect.d \
./Hardware/sd_card.d \
./Hardware/wav_player.d 

OBJS += \
./Hardware/ADS1299.o \
./Hardware/Config.o \
./Hardware/Direction_Classifier.o \
./Hardware/Direction_Model.o \
./Hardware/ICM42605.o \
./Hardware/OLED.o \
./Hardware/Serial.o \
./Hardware/Signal_Analysis.o \
./Hardware/Timer_1ms.o \
./Hardware/W25Q64.o \
./Hardware/dir_model_sd.o \
./Hardware/eeg_record.o \
./Hardware/glxss_me.o \
./Hardware/glxss_me_ch32h417.o \
./Hardware/glxss_sd_fw.o \
./Hardware/max98357a.o \
./Hardware/patient_monitor.o \
./Hardware/posture_detect.o \
./Hardware/sd_card.o \
./Hardware/wav_player.o 

DIR_OBJS += \
./Hardware/*.o \

DIR_DEPS += \
./Hardware/*.d \

DIR_EXPANDS += \
./Hardware/*.253r.expand \


# Each subdirectory must supply rules for building sources it contributes
Hardware/%.o: ../Hardware/%.c
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -DCore_V3F -DGLXSS_ENABLED -I"d:/Libraries/Projects/EEG/EXAM/SRC/Debug" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Core" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/User" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Hardware" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/HAL" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Attention" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Direction" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Filter" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/USB_Host" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

