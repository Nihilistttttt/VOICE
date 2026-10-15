################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/OLED.c \
../Drivers/ads1115.c \
../Drivers/aoa_host.c \
../Drivers/ch32v30x_usbfs_device.c \
../Drivers/joystick.c \
../Drivers/pressure.c \
../Drivers/sdio_sd.c \
../Drivers/sdio_test.c \
../Drivers/serial.c \
../Drivers/sw_udisk.c \
../Drivers/usb_desc.c \
../Drivers/usbhs_dev.c 

C_DEPS += \
./Drivers/OLED.d \
./Drivers/ads1115.d \
./Drivers/aoa_host.d \
./Drivers/ch32v30x_usbfs_device.d \
./Drivers/joystick.d \
./Drivers/pressure.d \
./Drivers/sdio_sd.d \
./Drivers/sdio_test.d \
./Drivers/serial.d \
./Drivers/sw_udisk.d \
./Drivers/usb_desc.d \
./Drivers/usbhs_dev.d 

OBJS += \
./Drivers/OLED.o \
./Drivers/ads1115.o \
./Drivers/aoa_host.o \
./Drivers/ch32v30x_usbfs_device.o \
./Drivers/joystick.o \
./Drivers/pressure.o \
./Drivers/sdio_sd.o \
./Drivers/sdio_test.o \
./Drivers/serial.o \
./Drivers/sw_udisk.o \
./Drivers/usb_desc.o \
./Drivers/usbhs_dev.o 

DIR_OBJS += \
./Drivers/*.o \

DIR_DEPS += \
./Drivers/*.d \

DIR_EXPANDS += \
./Drivers/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
Drivers/%.o: ../Drivers/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/Libraries/Projects/VOICE/VOICE_V307/Debug" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Core" -I"d:/Libraries/Projects/VOICE/VOICE_V307/User" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Peripheral/inc" -I"d:/Libraries/Projects/VOICE/VOICE_V307/USB_Host" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Common" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Drivers" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Framework" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Middleware" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Services" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

