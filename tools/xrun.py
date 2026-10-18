"""Disposable copies of the xrevive runner with the co-op mod, on an isolated invisible desktop, muted.

The runner exe is copied from the game folder (never run from there, never changed); the game files are the dev
copy (D:\\dev\\mc3-coop-data\\assets), so nothing the runner writes lands in the user's install. Each copy has its
own save folder (xsave\\i<N>), mod log (logs\\mc3coop-<N>.log), runner log dir and control port.
Only the processes started here are ever closed, by their own handles.

python tools/xrun.py --copies 1 --seconds 600
python tools/xrun.py --copies 2 --env "MC3COOP_COOP=host" --env2 "MC3COOP_COOP=join:127.0.0.1"
A file named stop-tests in the run folder ends the run early.
"""
import argparse
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
EOS_CONFIG = Path(r"C:\dev\black-coop\out\build\release\coop-online.toml")


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
    # the runner beside the game (read only, copied): .xrevive-<sha>.exe before xrevive's October 2026 update,
    # "<game name>.exe" since; a top level .exe of 20 MB or more
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
    if EOS_CONFIG.exists() and not (RUN / "coop-online.toml").exists():
        shutil.copy2(EOS_CONFIG, RUN / "coop-online.toml")
    return dst


def env_for(instance, port, extra):
    env = dict(os.environ)
    env.update({
        "MC3COOP_INSTANCE": str(instance),
        "MC3COOP_CONTROL": str(port),
        "MC3COOP_MUTE": "1",
        "XRECOMP_NO_DIALOGS": "1",
        "XRECOMP_LOG_DIR": str(RUN / "logs" / f"runner-{instance}"),
        "XRECOMP_BORDERLESS": "0",
        "XRECOMP_FULLSCREEN": "0",
        # the Vulkan backend does not present on an invisible desktop; D3D12 does, through the mod's DXGI hooks
        "XRECOMP_GPU_BACKEND": "d3d12",
        "XRECOMP_PRESENT_MODE": "immediate",
        "XRECOMP_MC3_MENU_TRACE": "1",
        "XRECOMP_NATIVE_CONTROLLERS": "0",  # the mod's game-thread hook runs only then (docs/TESTE-ISOLADO)
        # the mod's Vulkan layer: the panel and the captures
        "VK_LAYER_PATH": str(RUN),
        "VK_INSTANCE_LAYERS": "VK_LAYER_MC3COOP_overlay",
        "VK_LOADER_LAYERS_ENABLE": "VK_LAYER_MC3COOP_overlay",
    })
    for kv in extra:
        k, v = kv.split("=", 1)
        env[k] = v
    return "".join(f"{k}={v}\0" for k, v in env.items()) + "\0"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--copies", type=int, default=1)
    p.add_argument("--first-instance", type=int, default=3)
    p.add_argument("--first-port", type=int, default=45703)
    p.add_argument("--seconds", type=int, default=1800)
    p.add_argument("--env", action="append", default=[], help="KEY=VALUE for every copy")
    p.add_argument("--env1", action="append", default=[], help="KEY=VALUE for the first copy only")
    p.add_argument("--env2", action="append", default=[], help="KEY=VALUE for the second copy only")
    args = p.parse_args()
    exe = stage()
    stop = RUN / "stop-tests"
    stop.unlink(missing_ok=True)
    user = C.WinDLL("user32", use_last_error=True)
    kernel = C.WinDLL("kernel32", use_last_error=True)
    user.CreateDesktopW.argtypes = [W.LPCWSTR, C.c_void_p, C.c_void_p, W.DWORD, W.DWORD, C.c_void_p]
    user.CreateDesktopW.restype = W.HANDLE
    user.CloseDesktop.argtypes = [W.HANDLE]
    kernel.CreateProcessW.argtypes = [W.LPCWSTR, W.LPWSTR, C.c_void_p, C.c_void_p, W.BOOL,
                                     W.DWORD, C.c_void_p, W.LPCWSTR, C.POINTER(Startup), C.POINTER(Process)]
    kernel.GetExitCodeProcess.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
    kernel.TerminateProcess.argtypes = [W.HANDLE, W.UINT]
    kernel.CloseHandle.argtypes = [W.HANDLE]
    desktop_name = "MC3CoopTest-" + str(time.time_ns())
    desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x01FF, None)
    if not desktop:
        raise C.WinError(C.get_last_error())
    processes = []
    try:
        for i in range(args.copies):
            instance, port = args.first_instance + i, args.first_port + i
            save = Path(r"D:\dev\mc3-coop-data\xsave") / f"i{instance}"
            save.mkdir(parents=True, exist_ok=True)
            (RUN / "logs" / f"runner-{instance}").mkdir(parents=True, exist_ok=True)
            command = [str(exe), "--xbe", str(ASSETS / "default.xbe"), "--game-dir", str(ASSETS), "--save-name", str(save)]
            extra = args.env + (args.env1 if i == 0 else args.env2 if i == 1 else [])
            block = C.create_unicode_buffer(env_for(instance, port, extra))
            si = Startup()
            # shown normally, on the invisible desktop: a hidden window makes the renderer stop presenting
            si.cb, si.desktop, si.flags, si.show = C.sizeof(si), desktop_name, 0, 0
            pi = Process()
            CREATE_UNICODE_ENVIRONMENT = 0x400
            if not kernel.CreateProcessW(command[0], C.create_unicode_buffer(subprocess.list2cmdline(command)), None, None,
                                         False, CREATE_UNICODE_ENVIRONMENT, block, str(RUN), C.byref(si), C.byref(pi)):
                raise C.WinError(C.get_last_error())
            kernel.CloseHandle(pi.thread)
            processes.append((pi.process, pi.pid, port))
            print(json.dumps({"pid": pi.pid, "instance": instance, "control": port}), flush=True)
        deadline = time.monotonic() + args.seconds
        exited = set()
        while time.monotonic() < deadline and not stop.exists():
            for handle, pid, port in processes:
                code = W.DWORD()
                kernel.GetExitCodeProcess(handle, C.byref(code))
                if code.value != 259 and pid not in exited:
                    exited.add(pid)
                    print(json.dumps({"pid": pid, "control": port, "exit": f"0x{code.value:08X}"}), flush=True)
            if len(exited) == len(processes):
                break
            time.sleep(0.5)
    finally:
        for handle, pid, _ in processes:
            code = W.DWORD()
            kernel.GetExitCodeProcess(handle, C.byref(code))
            if code.value == 259:
                kernel.TerminateProcess(handle, 0)
                print(json.dumps({"pid": pid, "terminated": True}), flush=True)
            kernel.CloseHandle(handle)
        user.CloseDesktop(desktop)


if __name__ == "__main__":
    main()
