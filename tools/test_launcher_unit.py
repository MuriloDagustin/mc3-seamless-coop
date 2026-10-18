"""Compile/test launcher logic using synthetic files; never starts a runner, game, UAC dialog or firewall rule.

Win32 dialogs, process launch, Desktop and elevation are intercepted by the C++ harness.
Only system PowerShell executes for real, to create diagnostic ZIPs inside this workspace.
This is distinct from test_launcher.py, which exercises the packaged launcher with the game.
"""
from pathlib import Path
import os
import subprocess
import time
import zipfile


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out" / "codex-tests" / "launcher-unit"
FIXTURES = OUT / "fixtures" / str(time.time_ns())
AUTHORIZED_GAME = r"D:\dev\mc3-coop-data\testgame\Midnight Club 3 DUB Edition Remix"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    FIXTURES.mkdir(parents=True, exist_ok=True)
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    vs = subprocess.check_output(
        [str(vswhere), "-latest", "-products", "*", "-property", "installationPath"], text=True
    ).strip()
    if not vs:
        raise RuntimeError("Visual Studio não encontrado pelo vswhere.")
    script = OUT / "run.cmd"
    script.write_text(f'''@echo off
call "{vs}\\Common7\\Tools\\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul || exit /b 1
"{vs}\\VC\\Tools\\Llvm\\x64\\bin\\clang-cl.exe" /nologo /std:c++20 /EHsc /MT /W4 /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS tools\\tests\\launcher_tests.cpp /Fo"{OUT}\\launcher-tests.obj" /Fe"{OUT}\\launcher-tests.exe" /link /SUBSYSTEM:CONSOLE user32.lib shell32.lib ole32.lib || exit /b 1
"{OUT}\\launcher-tests.exe" "{FIXTURES}"
''', encoding="utf-8")
    env = dict(os.environ, MC3COOP_GAME=AUTHORIZED_GAME)
    subprocess.run(["cmd", "/c", str(script)], cwd=ROOT, env=env, check=True)
    archives = list(FIXTURES.rglob("*.zip"))
    if len(archives) < 3:
        raise AssertionError("Faltam ZIPs dos casos de logs vazio, completo ou parcial.")
    exported = set()
    for archive in archives:
        with zipfile.ZipFile(archive) as z:
            if z.testzip() is not None:
                raise AssertionError("ZIP de diagnóstico corrompido.")
            names = z.namelist()
            if any(name.lower().endswith(".toml") for name in names):
                raise AssertionError("Configuração TOML exportada pelo coletor.")
            for name in names:
                # The only secret in the fixture is a fake marker; never read any real configuration.
                if b"DO_NOT_EXPORT" in z.read(name):
                    raise AssertionError("Marcador privado da fixture exportado pelo coletor.")
                exported.add(Path(name.replace("\\", "/")).name)
    if not {"mod.log", "rúnner.log", "Log_File_For_Tera.log"}.issubset(exported):
        raise AssertionError("O ZIP completo não reuniu todos os logs de teste.")
    print(f"PASS: {len(archives)} ZIPs íntegros, sem TOML; runner novo/legado e seleção por data; jogo nunca iniciado.")


if __name__ == "__main__":
    main()
