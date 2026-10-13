################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/Common/Config.c \
d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/Common/hal_gpio.c \
d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/Common/hardware.c 

C_DEPS += \
./Common/Config.d \
./Common/hal_gpio.d \
./Common/hardware.d 

OBJS += \
./Common/Config.o \
./Common/hal_gpio.o \
./Common/hardware.o 

DIR_OBJS += \
./Common/*.o \

DIR_DEPS += \
./Common/*.d \

DIR_EXPANDS += \
./Common/*.253r.expand \


# Each subdirectory must supply rules for building sources it contributes
Common/Config.o: d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/Common/Config.c
	@	riscv-wch-elf-gcc -march=rv32imac_zba_zbb_zbc_zbs_xw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -DCore_V3F -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Debug" -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Core" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/User" -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/Common" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/Hardware" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/USB_Host" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/Phone" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
Common/hal_gpio.o: d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/Common/hal_gpio.c
	@	riscv-wch-elf-gcc -march=rv32imac_zba_zbb_zbc_zbs_xw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -DCore_V3F -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Debug" -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Core" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/User" -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/Common" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/Hardware" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/USB_Host" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/Phone" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
Common/hardware.o: d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/Common/hardware.c
	@	riscv-wch-elf-gcc -march=rv32imac_zba_zbb_zbc_zbs_xw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -DCore_V3F -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Debug" -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Core" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/User" -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/Common" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/Hardware" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/USB_Host" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/Phone" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

