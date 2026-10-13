################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../USB_Host/ch32h417_usbfs_host.c \
../USB_Host/ch32h417_usbhs_host.c \
../USB_Host/usb_host_hid.c \
../USB_Host/usb_host_hub.c 

C_DEPS += \
./USB_Host/ch32h417_usbfs_host.d \
./USB_Host/ch32h417_usbhs_host.d \
./USB_Host/usb_host_hid.d \
./USB_Host/usb_host_hub.d 

OBJS += \
./USB_Host/ch32h417_usbfs_host.o \
./USB_Host/ch32h417_usbhs_host.o \
./USB_Host/usb_host_hid.o \
./USB_Host/usb_host_hub.o 

DIR_OBJS += \
./USB_Host/*.o \

DIR_DEPS += \
./USB_Host/*.d \

DIR_EXPANDS += \
./USB_Host/*.253r.expand \


# Each subdirectory must supply rules for building sources it contributes
USB_Host/%.o: ../USB_Host/%.c
	@	riscv-wch-elf-gcc -march=rv32imac_zba_zbb_zbc_zbs_xw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -DCore_V3F -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Debug" -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Core" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/User" -I"d:/Libraries/Projects/VOICE/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/Common" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/Hardware" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/USB_Host" -I"d:/Libraries/Projects/VOICE/EXAM/GPIO/VOICE/V3F/Phone" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

