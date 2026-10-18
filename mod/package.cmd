@echo off
rem Pacote local de teste. Contem credenciais EOS: nunca publicar nem adicionar ao git.
setlocal
cd /d "%~dp0"
for %%i in ("%~dp0..\out") do set "PKGROOT=%%~fi"
set "OUT=%PKGROOT%\mod"
set "PKG=%PKGROOT%\package\MC3Coop"
set "MC3COOP_BUILD_OUT=%OUT%"
call "%~dp0build.cmd" || exit /b 1
cd /d "%~dp0"
set "PF86=%ProgramFiles(x86)%"
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR set "VSDIR=C:\Program Files\Microsoft Visual Studio\18\Community"
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul || exit /b 1
set "PATH=%VSDIR%\VC\Tools\Llvm\x64\bin;%PATH%"
clang-cl /nologo /O2 /std:c++20 /EHsc /MT /W4 /utf-8 /DUNICODE /D_UNICODE launcher\launcher.cpp /Fo"%OUT%\\" /Fe"%OUT%\MC3Coop.exe" ^
  /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib ole32.lib || exit /b 1
rem Verifica o destino absoluto antes de qualquer limpeza recursiva, usando um unico shell.
powershell -NoProfile -NonInteractive -Command "$root=[IO.Path]::GetFullPath($env:PKGROOT); $p=[IO.Path]::GetFullPath($env:PKG); if(!$p.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){exit 1}; if(Test-Path -LiteralPath $p){Remove-Item -LiteralPath $p -Recurse -Force}; New-Item -ItemType Directory -Path $p -Force | Out-Null" || exit /b 1
copy /y "%OUT%\MC3Coop.exe" "%PKG%\" >nul || exit /b 1
copy /y "%OUT%\xinput1_4.dll" "%PKG%\" >nul || exit /b 1
if exist "%OUT%\EOSSDK-Win64-Shipping.dll" copy /y "%OUT%\EOSSDK-Win64-Shipping.dll" "%PKG%\" >nul
copy /y "README-JOGAR.txt" "%PKG%\" >nul || exit /b 1
if exist "..\third_party\NOTICES.txt" copy /y "..\third_party\NOTICES.txt" "%PKG%\" >nul
if exist "C:\dev\black-coop\out\build\release\coop-online.toml" copy /y "C:\dev\black-coop\out\build\release\coop-online.toml" "%PKG%\" >nul
powershell -NoProfile -NonInteractive -Command "Compress-Archive -LiteralPath $env:PKG -DestinationPath (Join-Path $env:PKGROOT 'MC3Coop-test.zip') -Force" || exit /b 1
echo Pacote local: %PKGROOT%\MC3Coop-test.zip
echo ATENCAO: contem credenciais. Nao publicar nem adicionar ao git.
