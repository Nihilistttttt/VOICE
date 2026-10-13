################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../USB_Host/ch32v30x_usbhs_host.c \
../USB_Host/usb_host_hid.c \
../USB_Host/usb_host_hub.c 

C_DEPS += \
./USB_Host/ch32v30x_usbhs_host.d \
./USB_Host/usb_host_hid.d \
./USB_Host/usb_host_hub.d 

OBJS += \
./USB_Host/ch32v30x_usbhs_host.o \
./USB_Host/usb_host_hid.o \
./USB_Host/usb_host_hub.o 

DIR_OBJS += \
./USB_Host/*.o \

DIR_DEPS += \
./USB_Host/*.d \

DIR_EXPANDS += \
./USB_Host/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
USB_Host/%.o: ../USB_Host/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/Libraries/Projects/VOICE/VOICE_V307/Debug" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Core" -I"d:/Libraries/Projects/VOICE/VOICE_V307/User" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Peripheral/inc" -I"d:/Libraries/Projects/VOICE/VOICE_V307/USB_Host" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Common" -I"d:/Libraries/Projects/VOICE/VOICE_V307/Phone" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

