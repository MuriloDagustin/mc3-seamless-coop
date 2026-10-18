"""Run disposable MC3 test copies on an isolated, invisible Windows desktop (never switched to).

The game shows its window itself, so a hidden start is not enough; a separate desktop keeps
the user's desktop, focus and keyboard untouched. Every copy is muted and has its own
--instance (save folder hdd<N>, log mc3-<N>). Only processes created here are ever closed.

python tools/testcopies.py --copies 1 --seconds 120                 # one copy, instance 3
python tools/testcopies.py --copies 2 --args "--coop-control {port}" # {port}/{instance} filled in
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

ROOT = Path(__file__).resolve().parent.parent
RUN = Path(r"D:\dev\mc3-coop-data\run")


class Startup(C.Structure):
    _fields_ = [("cb", W.DWORD), ("reserved", W.LPWSTR), ("desktop", W.LPWSTR),
                ("title", W.LPWSTR), ("x", W.DWORD), ("y", W.DWORD),
                ("width", W.DWORD), ("height", W.DWORD), ("charsx", W.DWORD),
                ("charsy", W.DWORD), ("fill", W.DWORD), ("flags", W.DWORD),
                ("show", W.WORD), ("reserved_size", W.WORD), ("reserved2", C.c_void_p),
                ("stdin", W.HANDLE), ("stdout", W.HANDLE), ("stderr", W.HANDLE)]


class Process(C.Structure):
    _fields_ = [("process", W.HANDLE), ("thread", W.HANDLE), ("pid", W.DWORD), ("tid", W.DWORD)]


def stage(directory):
    """Copy the fresh build into the run folder (assets is a junction to the dev copy)."""
    directory.mkdir(parents=True, exist_ok=True)
    out = ROOT / "out" / "build" / "release"
    for name in ["MC3.exe", "EOSSDK-Win64-Shipping.dll"]:
        if (out / name).exists():
            shutil.copy2(out / name, directory / name)
    eos = Path(r"C:\dev\black-coop\out\build\release\coop-online.toml")
    if eos.exists() and not (directory / "coop-online.toml").exists():
        shutil.copy2(eos, directory / "coop-online.toml")
    if not (directory / "assets").exists():
        subprocess.run(["cmd", "/c", "mklink", "/J", str(directory / "assets"),
                        r"D:\dev\mc3-coop-data\assets"], check=True, capture_output=True)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--dir", type=Path, default=RUN)
    p.add_argument("--copies", type=int, default=1)
    p.add_argument("--first-instance", type=int, default=3)
    p.add_argument("--first-port", type=int, default=45703)
    p.add_argument("--args", default="--coop-control {port}")
    p.add_argument("--per-copy", action="append", default=[], help="extra args for copy i (repeatable, in order)")
    p.add_argument("--seconds", type=int, default=1800)
    p.add_argument("--no-stage", action="store_true")
    args = p.parse_args()
    directory = args.dir.resolve()
    if not args.no_stage:
        stage(directory)
    stop = directory / "stop-tests"
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
            extra = args.args.format(port=port, instance=instance).split()
            if i < len(args.per_copy):
                extra += args.per_copy[i].format(port=port, instance=instance).split()
            os.environ["XBE_CAPTURE_DIR"] = f"captures\i{instance}"
            command = [str(directory / "MC3.exe"), "--mute", "--instance", str(instance)] + extra
            si = Startup()
            si.cb, si.desktop, si.flags, si.show = C.sizeof(si), desktop_name, 1, 0
            pi = Process()
            if not kernel.CreateProcessW(command[0], C.create_unicode_buffer(subprocess.list2cmdline(command)),
                                         None, None, False, 0, None, str(directory), C.byref(si), C.byref(pi)):
                raise C.WinError(C.get_last_error())
            kernel.CloseHandle(pi.thread)
            processes.append((pi.process, pi.pid, port))
            print(json.dumps({"pid": pi.pid, "instance": instance, "control": port, "cmd": " ".join(command[1:])}), flush=True)
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
        # only the processes this script created, by their own handles
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
