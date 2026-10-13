################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################
-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include User/subdir.mk
-include USB_Host/subdir.mk
-include Startup/subdir.mk
-include Protocol/subdir.mk
-include Peripheral/src/subdir.mk
-include Hardware/FATFS/subdir.mk
-include Hardware/subdir.mk
-include HAL/subdir.mk
-include FFT/subdir.mk
-include EEG_Protocol/subdir.mk
-include EEG_Filter/subdir.mk
-include EEG_FFT/subdir.mk
-include EEG_Direction/subdir.mk
-include Debug/subdir.mk
-include Core/subdir.mk
-include Common/subdir.mk
-include Attention/subdir.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(ASM_UPPER_DEPS)),)
-include $(ASM_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 
SECONDARY_FLASH += \
GPIO_Toggle_V3F.bin \
GPIO_Toggle_V3F.hex \

SECONDARY_LIST += \
GPIO_Toggle_V3F.lst \

SECONDARY_SIZE += \
GPIO_Toggle_V3F.siz \


# All Target
all: 
	$(MAKE) --no-print-directory main-build 

main-build: GPIO_Toggle_V3F.elf secondary-outputs

# Tool invocations
GPIO_Toggle_V3F.elf: $(OBJS) $(USER_OBJS_ESCAPE)
	@	riscv-wch-elf-gcc -march=rv32imafc_zba_zbb_zbc_zbs_xw -mabi=ilp32f -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -fsingle-precision-constant -Wunused -Wuninitialized -g -T "d:\Libraries\Projects\EEG\EXAM\SRC\Ld\V3F\Link_v3f.ld" -nostartfiles -Xlinker --gc-sections -Wl,-Map,"GPIO_Toggle_V3F.map" --specs=nano.specs --specs=nosys.specs -o "GPIO_Toggle_V3F.elf" $(OBJS) $(LIBS)
GPIO_Toggle_V3F.bin: GPIO_Toggle_V3F.elf
	@	riscv-wch-elf-objcopy -O binary "GPIO_Toggle_V3F.elf" "GPIO_Toggle_V3F.bin"
GPIO_Toggle_V3F.hex: GPIO_Toggle_V3F.elf
	@	riscv-wch-elf-objcopy -O ihex "GPIO_Toggle_V3F.elf" "GPIO_Toggle_V3F.hex"
GPIO_Toggle_V3F.lst: GPIO_Toggle_V3F.elf
	@	riscv-wch-elf-objdump --all-headers --demangle --disassemble -M xw "GPIO_Toggle_V3F.elf" > "GPIO_Toggle_V3F.lst"
GPIO_Toggle_V3F.siz: GPIO_Toggle_V3F.elf
	riscv-wch-elf-size --format=berkeley "GPIO_Toggle_V3F.elf"

# Other Targets
clean:
	-$(RM) $(DIR_OBJS) $(SECONDARY_FLASH)$(SECONDARY_LIST)$(SECONDARY_SIZE) GPIO_Toggle_V3F.elf
	-$(RM) $(DIR_EXPANDS) $(CALLGRAPH_DOT)
	-$(RM) $(DIR_DEPS)
secondary-outputs: $(SECONDARY_FLASH) $(SECONDARY_LIST) $(SECONDARY_SIZE)

.PHONY: all clean dependents

-include ../makefile.targets