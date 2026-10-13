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
-include Phone/subdir.mk
-include Peripheral/src/subdir.mk
-include Debug/subdir.mk
-include Core/subdir.mk
-include Common/subdir.mk
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
TEST-INMP441-DOUBLE.hex \

SECONDARY_LIST += \
TEST-INMP441-DOUBLE.lst \

SECONDARY_SIZE += \
TEST-INMP441-DOUBLE.siz \


# All Target
all: 
	$(MAKE) --no-print-directory main-build 

main-build: TEST-INMP441-DOUBLE.elf secondary-outputs

# Tool invocations
TEST-INMP441-DOUBLE.elf: $(OBJS) $(USER_OBJS_ESCAPE)
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -T "d:\Libraries\Projects\VOICE\VOICE_V307\Ld\Link.ld" -nostartfiles -Xlinker --gc-sections -Wl,-Map,"TEST-INMP441-DOUBLE.map" --specs=nano.specs --specs=nosys.specs -o "TEST-INMP441-DOUBLE.elf" $(OBJS) $(LIBS)
TEST-INMP441-DOUBLE.hex: TEST-INMP441-DOUBLE.elf
	@	riscv-none-embed-objcopy -O ihex "TEST-INMP441-DOUBLE.elf" "TEST-INMP441-DOUBLE.hex"
TEST-INMP441-DOUBLE.lst: TEST-INMP441-DOUBLE.elf
	@	riscv-none-embed-objdump --all-headers --demangle --disassemble -M xw "TEST-INMP441-DOUBLE.elf" > "TEST-INMP441-DOUBLE.lst"
TEST-INMP441-DOUBLE.siz: TEST-INMP441-DOUBLE.elf
	riscv-none-embed-size --format=berkeley "TEST-INMP441-DOUBLE.elf"

# Other Targets
clean:
	-$(RM) $(DIR_OBJS) $(SECONDARY_FLASH)$(SECONDARY_LIST)$(SECONDARY_SIZE) TEST-INMP441-DOUBLE.elf
	-$(RM) $(DIR_EXPANDS) $(CALLGRAPH_DOT)
	-$(RM) $(DIR_DEPS)
secondary-outputs: $(SECONDARY_FLASH) $(SECONDARY_LIST) $(SECONDARY_SIZE)

.PHONY: all clean dependents

-include ../makefile.targets