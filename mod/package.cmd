@echo off
rem builds the mod and the launcher, then the test zip MC3Coop-test.zip in D:\dev\mc3-coop-build (local only:
rem it carries coop-online.toml with the EOS client credentials, never commit or publish it)
setlocal
cd /d "%~dp0"
call "%~dp0build.cmd" || exit /b 1
cd /d "%~dp0"
set "PF86=%ProgramFiles(x86)%"
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR set "VSDIR=C:\Program Files\Microsoft Visual Studio\18\Community"
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
set "PATH=%VSDIR%\VC\Tools\Llvm\x64\bin;%PATH%"
set "OUT=D:\dev\mc3-coop-build\mod"
set "PKG=D:\dev\mc3-coop-build\package\MC3Coop"
clang-cl /nologo /O2 /std:c++20 /EHsc /MT /DUNICODE /D_UNICODE launcher\launcher.cpp /Fo"%OUT%\\" /Fe"%OUT%\MC3Coop.exe" ^
  /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib ole32.lib || exit /b 1
if exist "%PKG%" rd /s /q "%PKG%"
mkdir "%PKG%"
copy /y "%OUT%\MC3Coop.exe" "%PKG%\" >nul
copy /y "%OUT%\xinput1_4.dll" "%PKG%\" >nul
copy /y "%OUT%\EOSSDK-Win64-Shipping.dll" "%PKG%\" >nul
copy /y "README-JOGAR.txt" "%PKG%\" >nul
copy /y "..\third_party\NOTICES.txt" "%PKG%\" >nul 2>nul
if exist "C:\dev\black-coop\out\build\release\coop-online.toml" copy /y "C:\dev\black-coop\out\build\release\coop-online.toml" "%PKG%\" >nul
del /q "D:\dev\mc3-coop-build\MC3Coop-test.zip" 2>nul
powershell -NoProfile -Command "Compress-Archive -Path '%PKG%' -DestinationPath 'D:\dev\mc3-coop-build\MC3Coop-test.zip'" || exit /b 1
echo packaged D:\dev\mc3-coop-build\MC3Coop-test.zip
