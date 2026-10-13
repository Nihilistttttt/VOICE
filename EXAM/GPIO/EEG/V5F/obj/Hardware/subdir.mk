################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Hardware/glxss_me.c \
../Hardware/glxss_me_ch32h417.c 

C_DEPS += \
./Hardware/glxss_me.d \
./Hardware/glxss_me_ch32h417.d 

OBJS += \
./Hardware/glxss_me.o \
./Hardware/glxss_me_ch32h417.o 

DIR_OBJS += \
./Hardware/*.o \

DIR_DEPS += \
./Hardware/*.d \

DIR_EXPANDS += \
./Hardware/*.253r.expand \


# Each subdirectory must supply rules for building sources it contributes
Hardware/%.o: ../Hardware/%.c
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -DCore_V5F -DGLXSS_ENABLED -I"d:/Libraries/Projects/EEG/EXAM/SRC/Debug" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Core" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V5F/User" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V5F/Hardware" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V5F/USB_Host" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

