$env:PATH = "D:\InstallData\EmbTools\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC\bin;D:\InstallData\EmbTools\MounRiver\MounRiver_Studio2\resources\app\resources\win32\others\Build_Tools\Make\bin;" + $env:PATH
Set-Location "D:\Libraries\Projects\VOICE\VOICE_V307"
make -C obj all