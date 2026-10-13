################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Phone/OLED.c \
../Phone/ads1115.c \
../Phone/aoa_host.c \
../Phone/ch32v30x_usbfs_device.c \
../Phone/diskio.c \
../Phone/ff.c \
../Phone/ffsystem.c \
../Phone/ffunicode.c \
../Phone/joystick.c \
../Phone/mic_hum.c \
../Phone/phone_control.c \
../Phone/pressure.c \
../Phone/sdio_sd.c \
../Phone/sdio_test.c \
../Phone/sw_udisk.c \
../Phone/usb_desc.c \
../Phone/usbhs_dev.c 

C_DEPS += \
./Phone/OLED.d \
./Phone/ads1115.d \
./Phone/aoa_host.d \
./Phone/ch32v30x_usbfs_device.d \
./Phone/diskio.d \
./Phone/ff.d \
./Phone/ffsystem.d \
./Phone/ffunicode.d \
./Phone/joystick.d \
./Phone/mic_hum.d \
./Phone/phone_control.d \
./Phone/pressure.d \
./Phone/sdio_sd.d \
./Phone/sdio_test.d \
./Phone/sw_udisk.d \
./Phone/usb_desc.d \
./Phone/usbhs_dev.d 

OBJS += \
./Phone/OLED.o \
./Phone/ads1115.o \
./Phone/aoa_host.o \
./Phone/ch32v30x_usbfs_device.o \
./Phone/diskio.o \
./Phone/ff.o \
./Phone/ffsystem.o \
./Phone/ffunicode.o \
./Phone/joystick.o \
./Phone/mic_hum.o \
./Phone/phone_control.o \
./Phone/pressure.o \
./Phone/sdio_sd.o \
./Phone/sdio_test.o \
./Phone/sw_udisk.o \
./Phone/usb_desc.o \
./Phone/usbhs_dev.o 

DIR_OBJS += \
./Phone/*.o \

DIR_DEPS += \
./Phone/*.d \

DIR_EXPANDS += \
./Phone/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
Phone/%.o: ../Phone/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/Libraries/Projects/VOICE/VOICE_V307/Debug" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Core" -I"d:/Libraries/Projects/VOICE/VOICE_V307/User" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Peripheral/inc" -I"d:/Libraries/Projects/VOICE/VOICE_V307/USB_Host" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Common" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Phone" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

