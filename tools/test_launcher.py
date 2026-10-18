"""The packaged zip as a friend would use it, on an invisible desktop: a test game folder laid out like xrevive's
(extracted = the dev copy of the game files, the runner, a test save), the zip extracted in its own folder, then
MC3Coop.exe started with the test channel and mute variables (inherited by the runner it starts). Prints what the
mod logged and takes a picture with the F10 panel open. Only the runner whose command line points at the test game
folder is ever closed.
python tools/test_launcher.py [seconds]
"""
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ctl import Copy  # noqa: E402

DATA = Path(r"D:\dev\mc3-coop-data")
GAME = DATA / "testgame" / "Midnight Club 3 DUB Edition Remix"
PKG = DATA / "pkgtest"
ZIP = Path(r"D:\dev\mc3-coop-build\MC3Coop-test.zip")
REAL = Path(r"D:\Jogos\xbox-xrevive\Midnight Club 3 DUB Edition Remix")


class Startup(C.Structure):
    _fields_ = [("cb", W.DWORD), ("reserved", W.LPWSTR), ("desktop", W.LPWSTR),
                ("title", W.LPWSTR), ("x", W.DWORD), ("y", W.DWORD),
                ("width", W.DWORD), ("height", W.DWORD), ("charsx", W.DWORD),
                ("charsy", W.DWORD), ("fill", W.DWORD), ("flags", W.DWORD),
                ("show", W.WORD), ("reserved_size", W.WORD), ("reserved2", C.c_void_p),
                ("stdin", W.HANDLE), ("stdout", W.HANDLE), ("stderr", W.HANDLE)]


class Process(C.Structure):
    _fields_ = [("process", W.HANDLE), ("thread", W.HANDLE), ("pid", W.DWORD), ("tid", W.DWORD)]


def setup():
    GAME.mkdir(parents=True, exist_ok=True)
    if not (GAME / "extracted").exists():
        subprocess.run(["cmd", "/c", "mklink", "/J", str(GAME / "extracted"), str(DATA / "assets")], check=True,
                       capture_output=True)
    runner = sorted(REAL.glob(".xrevive-*.exe"))[-1]  # read only: copied into the test game folder
    if not (GAME / runner.name).exists():
        shutil.copy2(runner, GAME / runner.name)
    saves = GAME / "SaveData" / "Midnight Club 3_ DUB edition"
    if not saves.exists():
        shutil.copytree(DATA / "xsave" / "i3", saves)
    if PKG.exists():
        shutil.rmtree(PKG)
    with zipfile.ZipFile(ZIP) as z:
        z.extractall(PKG)
    (PKG / "MC3Coop" / "MC3Coop.ini").write_text(f"game={GAME}\n", encoding="utf-16")


def main():
    seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 90
    setup()
    exe = PKG / "MC3Coop" / "MC3Coop.exe"
    env = dict(os.environ, MC3COOP_GAME=str(GAME), MC3COOP_CONTROL="45709", MC3COOP_MUTE="1", MC3COOP_INSTANCE="9", XRECOMP_NO_DIALOGS="1",
               XRECOMP_PRESENT_MODE="immediate", XRECOMP_FULLSCREEN="0", XRECOMP_BORDERLESS="0")
    block = C.create_unicode_buffer("".join(f"{k}={v}\0" for k, v in env.items()) + "\0")
    user = C.WinDLL("user32", use_last_error=True)
    kernel = C.WinDLL("kernel32", use_last_error=True)
    user.CreateDesktopW.restype = W.HANDLE
    user.CreateDesktopW.argtypes = [W.LPCWSTR, C.c_void_p, C.c_void_p, W.DWORD, W.DWORD, C.c_void_p]
    kernel.CreateProcessW.argtypes = [W.LPCWSTR, W.LPWSTR, C.c_void_p, C.c_void_p, W.BOOL, W.DWORD, C.c_void_p,
                                     W.LPCWSTR, C.POINTER(Startup), C.POINTER(Process)]
    name = "MC3CoopLauncherTest-" + str(time.time_ns())
    desktop = user.CreateDesktopW(name, None, None, 0, 0x01FF, None)
    si = Startup()
    si.cb, si.desktop = C.sizeof(si), name
    pi = Process()
    if not kernel.CreateProcessW(str(exe), C.create_unicode_buffer(f'"{exe}"'), None, None, False, 0x400, block,
                                 str(exe.parent), C.byref(si), C.byref(pi)):
        raise C.WinError(C.get_last_error())
    print("launcher pid", pi.pid)
    try:
        time.sleep(3)
        import psutil
        for p in psutil.process_iter(["name", "cmdline"]):  # the runner must be on the test game folder, or stop now
            cmd = " ".join(p.info["cmdline"] or [])
            if p.info["name"] == "MC3Runner.exe" and str(PKG) in cmd and str(GAME) not in cmd:
                p.terminate()
                raise SystemExit(f"the launcher did not use the test game folder: {cmd}")
        c = Copy(45709)
        end = time.time() + 60
        while time.time() < end:
            try:
                c.command("ping")
                break
            except Exception:
                time.sleep(1)
        time.sleep(20)
        print(c.command("state"))
        c.command("key f10 100")
        time.sleep(1.5)
        print(c.shot("launcher-panel"))
        print(c.command("coop online host"))
        for _ in range(10):
            time.sleep(2)
            s = c.command("coop session")
            if "code=" in s and "code= " not in s:
                break
        print(s)
        time.sleep(max(0, seconds - 50))
    finally:
        import psutil
        for p in psutil.process_iter(["name", "cmdline"]):
            cmd = " ".join(p.info["cmdline"] or [])
            if p.info["name"] == "MC3Runner.exe" and str(PKG) in cmd:  # only the copy this test started
                p.terminate()
                print("closed test runner", p.pid)
        log = PKG / "MC3Coop" / "logs" / "mc3coop-9.log"
        if log.exists():
            print(log.read_text(errors="replace"))
        user.CloseDesktop(desktop)


if __name__ == "__main__":
    main()
