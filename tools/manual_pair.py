"""Launch two visible, disposable co-op copies on the normal desktop for manual testing.
Only these two processes are ever closed; they use the dev assets and xsave i3/i4, never the user's install.
Ctrl+C or stop-manual ends only the copies started here.
"""
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

GAME = Path(r"D:\Jogos\xbox-xrevive\Midnight Club 3 DUB Edition Remix")
ASSETS = Path(r"D:\dev\mc3-coop-data\assets")
RUN = Path(r"D:\dev\mc3-coop-data\xrun")
MOD = Path(r"D:\dev\mc3-coop-build\mod")


class Startup(C.Structure):
    _fields_ = [("cb", W.DWORD), ("reserved", W.LPWSTR), ("desktop", W.LPWSTR),
                ("title", W.LPWSTR), ("x", W.DWORD), ("y", W.DWORD),
                ("width", W.DWORD), ("height", W.DWORD), ("charsx", W.DWORD),
                ("charsy", W.DWORD), ("fill", W.DWORD), ("flags", W.DWORD),
                ("show", W.WORD), ("reserved_size", W.WORD), ("reserved2", C.c_void_p),
                ("stdin", W.HANDLE), ("stdout", W.HANDLE), ("stderr", W.HANDLE)]


class Process(C.Structure):
    _fields_ = [("process", W.HANDLE), ("thread", W.HANDLE), ("pid", W.DWORD), ("tid", W.DWORD)]


def runner_exe():
    exes = sorted((p for p in GAME.glob("*.exe") if p.stat().st_size >= 20 << 20), key=lambda p: p.stat().st_mtime)
    if not exes:
        raise SystemExit(f"no xrevive runner in {GAME}")
    return exes[-1]


def stage():
    RUN.mkdir(parents=True, exist_ok=True)
    src = runner_exe()
    dst = RUN / "MC3Runner.exe"
    if not dst.exists() or dst.stat().st_size != src.stat().st_size:
        shutil.copy2(src, dst)
    for f in MOD.glob("*.dll"):
        shutil.copy2(f, RUN / f.name)
    shutil.copy2(Path(__file__).resolve().parent.parent / "mod" / "VkLayer_MC3Coop.json", RUN / "VkLayer_MC3Coop.json")
    return dst


def env_for(instance, port, extra):
    env = dict(os.environ)
    env.update({
        "MC3COOP_INSTANCE": str(instance),
        "MC3COOP_CONTROL": str(port),
        "MC3COOP_TRAFFIC_PHYSICS": "1",  # native traffic bodies/roads for collisions in the manual test
        "XRECOMP_NO_DIALOGS": "1",
        "XRECOMP_LOG_DIR": str(RUN / "logs" / f"runner-{instance}"),
        "XRECOMP_BORDERLESS": "0",
        "XRECOMP_FULLSCREEN": "0",
        "XRECOMP_GPU_BACKEND": "d3d12",
        "XRECOMP_PRESENT_MODE": "immediate",
        "XRECOMP_MC3_MENU_TRACE": "1",
        "XRECOMP_NATIVE_CONTROLLERS": "0",
        "XRECOMP_KEYBOARD": "1",
        "VK_LAYER_PATH": str(RUN),
        "VK_INSTANCE_LAYERS": "VK_LAYER_MC3COOP_overlay",
        "VK_LOADER_LAYERS_ENABLE": "VK_LAYER_MC3COOP_overlay",
    })
    for kv in extra:
        k, v = kv.split("=", 1)
        env[k] = v
    return "".join(f"{k}={v}\0" for k, v in env.items()) + "\0"


def main():
    exe = stage()
    stop = RUN / "stop-manual"
    stop.unlink(missing_ok=True)
    user = C.WinDLL("user32", use_last_error=True)
    kernel = C.WinDLL("kernel32", use_last_error=True)
    kernel.CreateProcessW.argtypes = [W.LPCWSTR, W.LPWSTR, C.c_void_p, C.c_void_p, W.BOOL,
                                      W.DWORD, C.c_void_p, W.LPCWSTR, C.POINTER(Startup), C.POINTER(Process)]
    kernel.GetExitCodeProcess.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
    kernel.TerminateProcess.argtypes = [W.HANDLE, W.UINT]
    kernel.CloseHandle.argtypes = [W.HANDLE]
    processes = []
    try:
        for i, (role, nick, x) in enumerate((("host", "Murilo", 0), ("join:127.0.0.1", "Amigo", 620))):
            instance, port = 3 + i, 45703 + i
            save = Path(r"D:\dev\mc3-coop-data\xsave") / f"i{instance}"
            save.mkdir(parents=True, exist_ok=True)
            (RUN / "logs" / f"runner-{instance}").mkdir(parents=True, exist_ok=True)
            command = [str(exe), "--xbe", str(ASSETS / "default.xbe"), "--game-dir", str(ASSETS),
                       "--save-name", str(save)]
            extra = [f"MC3COOP_COOP={role}", "MC3COOP_PORT=45420", f"MC3COOP_NICK={nick}"]
            block = C.create_unicode_buffer(env_for(instance, port, extra))
            si = Startup()
            # default desktop, normal show, positioned side by side; no focus tricks
            si.cb, si.desktop, si.flags, si.show = C.sizeof(si), None, 0x00000005, 1
            si.x, si.y = x, 40
            pi = Process()
            if not kernel.CreateProcessW(command[0], C.create_unicode_buffer(subprocess.list2cmdline(command)), None,
                                         None, False, 0x00000400, block, str(RUN), C.byref(si), C.byref(pi)):
                raise C.WinError(C.get_last_error())
            kernel.CloseHandle(pi.thread)
            processes.append((pi.process, pi.pid, port))
            print(json.dumps({"pid": pi.pid, "instance": instance, "control": port, "role": role}), flush=True)
        deadline = time.monotonic() + 7200
        while time.monotonic() < deadline and not stop.exists():
            time.sleep(0.5)
    finally:
        for handle, pid, _ in processes:
            code = W.DWORD()
            kernel.GetExitCodeProcess(handle, C.byref(code))
            if code.value == 259:
                kernel.TerminateProcess(handle, 0)
                print(json.dumps({"pid": pid, "terminated": True}), flush=True)
            kernel.CloseHandle(handle)


if __name__ == "__main__":
    main()
