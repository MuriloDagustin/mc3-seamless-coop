"""Own a test process tree on a never-activated Windows desktop. No global process cleanup."""
import ctypes as C
from ctypes import wintypes as W
import os
import subprocess
import time

from xrun import Startup, Process


class BasicLimits(C.Structure):
    _fields_ = [('process_time', C.c_longlong), ('job_time', C.c_longlong), ('flags', W.DWORD),
                ('min_working', C.c_size_t), ('max_working', C.c_size_t), ('active', W.DWORD),
                ('affinity', C.c_size_t), ('priority', W.DWORD), ('scheduling', W.DWORD)]


class IoCounters(C.Structure):
    _fields_ = [(n, C.c_ulonglong) for n in ('read_ops', 'write_ops', 'other_ops', 'read_bytes', 'write_bytes', 'other_bytes')]


class ExtendedLimits(C.Structure):
    _fields_ = [('basic', BasicLimits), ('io', IoCounters), ('process_memory', C.c_size_t),
                ('job_memory', C.c_size_t), ('peak_process', C.c_size_t), ('peak_job', C.c_size_t)]


class IsolatedJob:
    def __init__(self):
        self.k = C.WinDLL('kernel32', use_last_error=True)
        self.u = C.WinDLL('user32', use_last_error=True)
        self.k.CreateJobObjectW.argtypes = [C.c_void_p, W.LPCWSTR]
        self.k.CreateJobObjectW.restype = W.HANDLE
        self.k.SetInformationJobObject.argtypes = [W.HANDLE, C.c_int, C.c_void_p, W.DWORD]
        self.k.AssignProcessToJobObject.argtypes = [W.HANDLE, W.HANDLE]
        self.k.CreateProcessW.argtypes = [W.LPCWSTR, W.LPWSTR, C.c_void_p, C.c_void_p, W.BOOL, W.DWORD,
                                        C.c_void_p, W.LPCWSTR, C.POINTER(Startup), C.POINTER(Process)]
        self.k.ResumeThread.argtypes = [W.HANDLE]
        self.k.ResumeThread.restype = W.DWORD
        self.k.CloseHandle.argtypes = [W.HANDLE]
        self.k.TerminateProcess.argtypes = [W.HANDLE, W.UINT]
        self.k.GetExitCodeProcess.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
        self.u.CreateDesktopW.argtypes = [W.LPCWSTR, C.c_void_p, C.c_void_p, W.DWORD, W.DWORD, C.c_void_p]
        self.u.CreateDesktopW.restype = W.HANDLE
        self.u.CloseDesktop.argtypes = [W.HANDLE]
        self.name = 'MC3CoopPackageTest-' + str(time.time_ns())
        self.desktop = self.u.CreateDesktopW(self.name, None, None, 0, 0x01FF, None)
        if not self.desktop:
            raise C.WinError(C.get_last_error())
        self.job = self.k.CreateJobObjectW(None, None)
        self.processes = []
        limits = ExtendedLimits()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE: ONLY this owned tree
        if not self.job or not self.k.SetInformationJobObject(self.job, 9, C.byref(limits), C.sizeof(limits)):
            self.close()
            raise C.WinError(C.get_last_error())

    def start(self, exe, args=(), env=None):
        environment = dict(os.environ) if env is None else dict(env)
        block = C.create_unicode_buffer(''.join(f'{k}={v}\0' for k, v in sorted(environment.items())) + '\0')
        si, pi = Startup(), Process()
        si.cb, si.desktop = C.sizeof(si), self.name
        cmd = C.create_unicode_buffer(subprocess.list2cmdline([str(exe), *map(str, args)]))
        if not self.k.CreateProcessW(str(exe), cmd, None, None, False, 0x400 | 0x4 | 0x4000,
                                     block, str(exe.parent), C.byref(si), C.byref(pi)):
            raise C.WinError(C.get_last_error())
        try:
            if not self.k.AssignProcessToJobObject(self.job, pi.process):
                raise C.WinError(C.get_last_error())
            if self.k.ResumeThread(pi.thread) == 0xFFFFFFFF:
                raise C.WinError(C.get_last_error())
        except BaseException:
            self.k.TerminateProcess(pi.process, 1)  # retained handle of the process we just created
            self.k.CloseHandle(pi.process)
            raise
        finally:
            self.k.CloseHandle(pi.thread)
        self.processes.append(pi)
        return pi

    def exit_code(self, process):
        result = W.DWORD()
        if not self.k.GetExitCodeProcess(process.process, C.byref(result)):
            raise C.WinError(C.get_last_error())
        return result.value

    def messages(self, process):
        """Read only this launcher's dialogs on the private desktop; never sends input."""
        callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        self.u.EnumDesktopWindows.argtypes = [W.HANDLE, callback, W.LPARAM]
        self.u.EnumChildWindows.argtypes = [W.HWND, callback, W.LPARAM]
        self.u.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
        self.u.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
        self.u.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
        result = []
        @callback
        def child(hwnd, unused):
            text = C.create_unicode_buffer(8192)
            self.u.GetWindowTextW(hwnd, text, len(text))
            if text.value:
                result.append(text.value)
            return True
        @callback
        def window(hwnd, unused):
            pid = W.DWORD()
            self.u.GetWindowThreadProcessId(hwnd, C.byref(pid))
            kind = C.create_unicode_buffer(128)
            self.u.GetClassNameW(hwnd, kind, len(kind))
            if pid.value == process.pid and kind.value == "#32770":
                child(hwnd, 0)
                self.u.EnumChildWindows(hwnd, child, 0)
            return True
        self.u.EnumDesktopWindows(self.desktop, window, 0)
        return result

    def close(self):
        if getattr(self, 'job', None):
            self.k.CloseHandle(self.job)
            self.job = None
        for pi in getattr(self, 'processes', []):
            self.k.CloseHandle(pi.process)
        self.processes = []
        if getattr(self, 'desktop', None):
            self.u.CloseDesktop(self.desktop)
            self.desktop = None

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        self.close()
