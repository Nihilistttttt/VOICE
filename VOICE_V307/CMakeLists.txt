cmake_minimum_required(VERSION 3.16)
project(VOICE_CH32V307 C ASM)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
enable_language(ASM)

# ---- 源文件: 与 MounRiver obj/subdir.mk 等价(全量编译, #if 由源码门控) ----
file(GLOB_RECURSE APP_C CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/User/*.c
    ${CMAKE_SOURCE_DIR}/Services/*.c
    ${CMAKE_SOURCE_DIR}/Drivers/*.c
    ${CMAKE_SOURCE_DIR}/Common/*.c
    ${CMAKE_SOURCE_DIR}/Framework/*.c
    ${CMAKE_SOURCE_DIR}/Middleware/*.c
    ${CMAKE_SOURCE_DIR}/Peripheral/src/*.c
    ${CMAKE_SOURCE_DIR}/Core/*.c
    ${CMAKE_SOURCE_DIR}/Debug/*.c
    ${CMAKE_SOURCE_DIR}/USB_Host/*.c)

set(STARTUP ${CMAKE_SOURCE_DIR}/Startup/startup_ch32v30x_D8C.S)

add_executable(VOICE.elf ${APP_C} ${STARTUP})

# ---- 头文件路径(与 MounRiver 一致) ----
target_include_directories(VOICE.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/Debug
    ${CMAKE_SOURCE_DIR}/Core
    ${CMAKE_SOURCE_DIR}/User
    ${CMAKE_SOURCE_DIR}/Peripheral/inc
    ${CMAKE_SOURCE_DIR}/USB_Host
    ${CMAKE_SOURCE_DIR}/Common
    ${CMAKE_SOURCE_DIR}/Drivers
    ${CMAKE_SOURCE_DIR}/Framework
    ${CMAKE_SOURCE_DIR}/Middleware
    ${CMAKE_SOURCE_DIR}/Services)

# ---- 编译选项(与 MounRiver 一致) ----
target_compile_options(VOICE.elf PRIVATE
    -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore
    -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char
    -ffunction-sections -fdata-sections -fno-common
    -Wunused -Wuninitialized -g -std=gnu99)

target_compile_definitions(VOICE.elf PRIVATE
    DEBUG=1)

# ---- 链接(与 MounRiver 一致) ----
target_link_options(VOICE.elf PRIVATE
    -march=rv32imacxw -mabi=ilp32 -msave-restore
    -T ${CMAKE_SOURCE_DIR}/Ld/Link.ld
    -nostartfiles
    -Xlinker --gc-sections
    -Wl,-Map,VOICE.map
    --specs=nano.specs --specs=nosys.specs)

# ---- 产物: HEX(Burn 到 CH32) ----
find_program(OBJCOPY riscv-none-embed-objcopy.exe
    PATHS "${TOOLBIN}" NO_DEFAULT_PATH)
add_custom_command(TARGET VOICE.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex VOICE.elf VOICE.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary VOICE.elf VOICE.bin
    COMMENT "objcopy -> VOICE.hex / VOICE.bin")
