"""Compile/run the real mod against synthetic Xbox RAM. Never opens a game, runner, window or network session."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'out' / 'codex-tests'
OUT.mkdir(parents=True, exist_ok=True)
vswhere = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath'], text=True).strip()
script = OUT / 'run.cmd'
script.write_text(f'''@echo off
call "{vs}\\Common7\\Tools\\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul || exit /b 1
"{vs}\\VC\\Tools\\Llvm\\x64\\bin\\clang-cl.exe" /nologo /std:c++20 /EHsc /MT /W4 /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS tools\\tests\\coop_memory_tests.cpp /Fo"{OUT}\\memory-tests.obj" /Fe"{OUT}\\memory-tests.exe" /link /SUBSYSTEM:CONSOLE || exit /b 1
"{OUT}\\memory-tests.exe"
''', encoding='utf-8')
subprocess.run(['cmd', '/c', str(script)], cwd=ROOT, check=True)
