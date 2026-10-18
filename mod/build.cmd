@echo off
rem builds the mod as xinput1_4.dll into D:\dev\mc3-coop-build\mod (the runner loads it from its own folder)
setlocal
set "PF86=%ProgramFiles(x86)%"
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR set "VSDIR=C:\Program Files\Microsoft Visual Studio\18\Community"
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
set "PATH=%VSDIR%\VC\Tools\Llvm\x64\bin;%PATH%"
cd /d "%~dp0"
set "OUT=D:\dev\mc3-coop-build\mod"
if not exist "%OUT%" mkdir "%OUT%"
set "EOS=C:\dev\eos-sdk\SDK"
set "EOSFLAGS="
if exist "%EOS%\Include\eos_sdk.h" set "EOSFLAGS=-DMC3_WITH_EOS=1 -I%EOS%\Include %EOS%\Lib\EOSSDK-Win64-Shipping.lib"
clang-cl /nologo /O2 /std:c++20 /EHsc /MT /LD /W3 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE ^
  /Ithird_party /Ithird_party\imgui /Isrc /Isrc\shim src\*.cpp src\net\*.cpp ^
  third_party\imgui\imgui.cpp third_party\imgui\imgui_draw.cpp third_party\imgui\imgui_tables.cpp ^
  third_party\imgui\imgui_widgets.cpp third_party\imgui\backends\imgui_impl_dx12.cpp ^
  third_party\imgui\backends\imgui_impl_win32.cpp /Fo"%OUT%\\" /Fe"%OUT%\xinput1_4.dll" %EOSFLAGS% ^
  /link /DEF:xinput1_4.def ws2_32.lib ole32.lib user32.lib gdi32.lib advapi32.lib d3d12.lib dxgi.lib d3dcompiler.lib dwmapi.lib || exit /b 1
if exist "%EOS%\Bin\EOSSDK-Win64-Shipping.dll" copy /y "%EOS%\Bin\EOSSDK-Win64-Shipping.dll" "%OUT%\" >nul
echo built %OUT%\xinput1_4.dll
