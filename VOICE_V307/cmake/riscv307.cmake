# CH32V307 (RISC-V rv32imacxw) 交叉编译工具链 —— CLion 自动加载
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

set(TOOLBIN "D:/InstallData/EmbTools/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC/bin")
set(CMAKE_C_COMPILER   ${TOOLBIN}/riscv-none-embed-gcc.exe)
set(CMAKE_ASM_COMPILER ${TOOLBIN}/riscv-none-embed-gcc.exe)
set(CMAKE_CXX_COMPILER ${TOOLBIN}/riscv-none-embed-gcc.exe)
set(CMAKE_AR           ${TOOLBIN}/riscv-none-embed-gcc-ar.exe)
set(CMAKE_OBJCOPY      ${TOOLBIN}/riscv-none-embed-objcopy.exe CACHE FILEPATH "")
set(CMAKE_OBJDUMP      ${TOOLBIN}/riscv-none-embed-objdump.exe CACHE FILEPATH "")

# 跳过 CMake 的链接探测(裸机无 os, 链接探测会失败)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
