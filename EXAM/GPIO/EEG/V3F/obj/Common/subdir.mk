################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/dualcore_ipc.c \
d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/dualcore_ipc_diag.c \
d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/dualcore_ipc_v3f.c \
d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/dualcore_ipc_v5f.c \
d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/dualcore_v5f_dsp.c \
d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/dualcore_v5f_ssvep.c \
d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/eeg_protocol.c \
d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/hardware.c \
d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/ipc_log_v3f.c \
d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/ipc_log_v5f.c 

C_DEPS += \
./Common/dualcore_ipc.d \
./Common/dualcore_ipc_diag.d \
./Common/dualcore_ipc_v3f.d \
./Common/dualcore_ipc_v5f.d \
./Common/dualcore_v5f_dsp.d \
./Common/dualcore_v5f_ssvep.d \
./Common/eeg_protocol.d \
./Common/hardware.d \
./Common/ipc_log_v3f.d \
./Common/ipc_log_v5f.d 

OBJS += \
./Common/dualcore_ipc.o \
./Common/dualcore_ipc_diag.o \
./Common/dualcore_ipc_v3f.o \
./Common/dualcore_ipc_v5f.o \
./Common/dualcore_v5f_dsp.o \
./Common/dualcore_v5f_ssvep.o \
./Common/eeg_protocol.o \
./Common/hardware.o \
./Common/ipc_log_v3f.o \
./Common/ipc_log_v5f.o 

DIR_OBJS += \
./Common/*.o \

DIR_DEPS += \
./Common/*.d \

DIR_EXPANDS += \
./Common/*.253r.expand \


# Each subdirectory must supply rules for building sources it contributes
Common/dualcore_ipc.o: d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/dualcore_ipc.c
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -DCore_V3F -DGLXSS_ENABLED -I"d:/Libraries/Projects/EEG/EXAM/SRC/Debug" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Core" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/User" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Hardware" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/HAL" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Attention" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Direction" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Filter" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/USB_Host" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
Common/dualcore_ipc_diag.o: d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/dualcore_ipc_diag.c
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -DCore_V3F -DGLXSS_ENABLED -I"d:/Libraries/Projects/EEG/EXAM/SRC/Debug" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Core" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/User" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Hardware" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/HAL" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Attention" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Direction" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Filter" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/USB_Host" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
Common/dualcore_ipc_v3f.o: d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/dualcore_ipc_v3f.c
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -DCore_V3F -DGLXSS_ENABLED -I"d:/Libraries/Projects/EEG/EXAM/SRC/Debug" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Core" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/User" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Hardware" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/HAL" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Attention" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Direction" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Filter" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/USB_Host" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
Common/dualcore_ipc_v5f.o: d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/dualcore_ipc_v5f.c
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -DCore_V3F -DGLXSS_ENABLED -I"d:/Libraries/Projects/EEG/EXAM/SRC/Debug" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Core" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/User" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Hardware" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/HAL" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Attention" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Direction" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Filter" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/USB_Host" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
Common/dualcore_v5f_dsp.o: d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/dualcore_v5f_dsp.c
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -DCore_V3F -DGLXSS_ENABLED -I"d:/Libraries/Projects/EEG/EXAM/SRC/Debug" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Core" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/User" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Hardware" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/HAL" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Attention" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Direction" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Filter" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/USB_Host" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
Common/dualcore_v5f_ssvep.o: d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/dualcore_v5f_ssvep.c
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -DCore_V3F -DGLXSS_ENABLED -I"d:/Libraries/Projects/EEG/EXAM/SRC/Debug" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Core" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/User" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Hardware" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/HAL" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Attention" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Direction" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Filter" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/USB_Host" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
Common/eeg_protocol.o: d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/eeg_protocol.c
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -DCore_V3F -DGLXSS_ENABLED -I"d:/Libraries/Projects/EEG/EXAM/SRC/Debug" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Core" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/User" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Hardware" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/HAL" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Attention" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Direction" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Filter" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/USB_Host" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
Common/hardware.o: d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/hardware.c
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -DCore_V3F -DGLXSS_ENABLED -I"d:/Libraries/Projects/EEG/EXAM/SRC/Debug" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Core" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/User" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Hardware" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/HAL" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Attention" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Direction" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Filter" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/USB_Host" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
Common/ipc_log_v3f.o: d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/ipc_log_v3f.c
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -DCore_V3F -DGLXSS_ENABLED -I"d:/Libraries/Projects/EEG/EXAM/SRC/Debug" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Core" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/User" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Hardware" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/HAL" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Attention" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Direction" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Filter" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/USB_Host" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
Common/ipc_log_v5f.o: d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common/ipc_log_v5f.c
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -DCore_V3F -DGLXSS_ENABLED -I"d:/Libraries/Projects/EEG/EXAM/SRC/Debug" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Core" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/User" -I"d:/Libraries/Projects/EEG/EXAM/SRC/Peripheral/inc" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/Common" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Hardware" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/HAL" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/Attention" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Direction" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_FFT" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Filter" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/EEG_Protocol" -I"d:/Libraries/Projects/EEG/EXAM/GPIO/EEG/V3F/USB_Host" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

