"""Windows UI integration and optional 10 minute soak; no third party Python packages."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
user = C.WinDLL("user32", use_last_error=True)
kernel = C.WinDLL("kernel32", use_last_error=True)
user.GetDlgItem.argtypes = [W.HWND, C.c_int]
user.GetDlgItem.restype = W.HWND
user.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
user.SetWindowTextW.argtypes = [W.HWND, W.LPCWSTR]
user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user.SendMessageW.restype = W.LPARAM
user.ShowWindow.argtypes = [W.HWND, C.c_int]
user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
CALLBACK = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
user.EnumWindows.argtypes = [CALLBACK, W.LPARAM]


def wait(test, timeout=10, message="condition"):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        result = test()
        if result:
            return result
        time.sleep(0.05)
    raise AssertionError("Timed out: " + message)


def window_for(pid):
    found = []
    @CALLBACK
    def visit(hwnd, unused):
        value = W.DWORD()
        user.GetWindowThreadProcessId(hwnd, C.byref(value))
        if value.value == pid and user.GetDlgItem(hwnd, 1017):
            found.append(hwnd)
        return True
    user.EnumWindows(visit, 0)
    return found[0] if found else None


def settings_for(pid):
    found = []
    @CALLBACK
    def visit(hwnd, unused):
        value = W.DWORD()
        user.GetWindowThreadProcessId(hwnd, C.byref(value))
        if value.value == pid and user.GetDlgItem(hwnd, 1020):
            found.append(hwnd)
        return True
    user.EnumWindows(visit, 0)
    return found[0] if found else None


def text(hwnd, control):
    buf = C.create_unicode_buffer(2048)
    user.SendMessageW(user.GetDlgItem(hwnd, control), 0xD, len(buf), C.addressof(buf))
    return buf.value


def click(hwnd, control):
    user.PostMessageW(hwnd, 0x111, control, user.GetDlgItem(hwnd, control))


def set_text(hwnd, control, value):
    buffer = C.create_unicode_buffer(value)
    user.SendMessageW(user.GetDlgItem(hwnd, control), 0xC, 0, C.addressof(buffer))


def requests(root):
    path = root / "requests.jsonl"
    if not path.exists():
        return []
    return [json.loads(x) for x in path.read_text(encoding="utf-8").splitlines() if x]


def polls(root):
    return [r for r in requests(root) if r["method"] == "account/rateLimits/read"]


class PROCESSENTRY32W(C.Structure):
    _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD), ("th32ProcessID", W.DWORD),
                ("th32DefaultHeapID", C.c_size_t), ("th32ModuleID", W.DWORD),
                ("cntThreads", W.DWORD), ("th32ParentProcessID", W.DWORD),
                ("pcPriClassBase", W.LONG), ("dwFlags", W.DWORD), ("szExeFile", W.WCHAR * 260)]


kernel.CreateToolhelp32Snapshot.argtypes = [W.DWORD, W.DWORD]
kernel.CreateToolhelp32Snapshot.restype = W.HANDLE
kernel.Process32FirstW.argtypes = [W.HANDLE, C.POINTER(PROCESSENTRY32W)]
kernel.Process32NextW.argtypes = [W.HANDLE, C.POINTER(PROCESSENTRY32W)]
kernel.CloseHandle.argtypes = [W.HANDLE]
kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
kernel.OpenProcess.restype = W.HANDLE
kernel.GetProcessTimes.argtypes = [W.HANDLE] + [C.POINTER(W.FILETIME)] * 4
kernel.GetProcessHandleCount.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
kernel.GetExitCodeProcess.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]


class MEMORY(C.Structure):
    _fields_ = [("cb", W.DWORD), ("PageFaultCount", W.DWORD)] + [(n, C.c_size_t) for n in
        ("PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage",
         "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]


psapi = C.WinDLL("psapi")
psapi.GetProcessMemoryInfo.argtypes = [W.HANDLE, C.POINTER(MEMORY), W.DWORD]


def tree(pid):
    snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
    entry = PROCESSENTRY32W()
    entry.dwSize = C.sizeof(entry)
    items = []
    ok = kernel.Process32FirstW(snapshot, C.byref(entry))
    while ok:
        items.append((entry.th32ProcessID, entry.th32ParentProcessID))
        ok = kernel.Process32NextW(snapshot, C.byref(entry))
    kernel.CloseHandle(snapshot)
    ids = {pid}
    while True:
        expanded = ids | {child for child, parent in items if parent in ids}
        if expanded == ids:
            break
        ids = expanded
    return ids & {child for child, parent in items}


def metrics(pid):
    ids = tree(pid)
    values = {"time": time.time(), "processes": len(ids), "private_bytes": 0, "cpu_seconds": 0, "handles": 0}
    for member in ids:
        handle = kernel.OpenProcess(0x410, False, member)
        if not handle:
            continue
        memory = MEMORY()
        memory.cb = C.sizeof(memory)
        if psapi.GetProcessMemoryInfo(handle, C.byref(memory), C.sizeof(memory)):
            values["private_bytes"] += memory.PrivateUsage
        dates = [W.FILETIME() for unused in range(4)]
        if kernel.GetProcessTimes(handle, *[C.byref(d) for d in dates]):
            values["cpu_seconds"] += sum((d.dwHighDateTime << 32) + d.dwLowDateTime for d in dates[2:]) / 1e7
        count = W.DWORD()
        if kernel.GetProcessHandleCount(handle, C.byref(count)):
            values["handles"] += count.value
        kernel.CloseHandle(handle)
    return values


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--soak-seconds", type=int, default=0)
    args = parser.parse_args()
    root = ROOT / "tests" / "runtime"
    root.mkdir(exist_ok=True)
    # Only known test artifacts inside this fixed directory are reset.
    for name in ("requests.jsonl", "control.json", "CodexUsage.ini"):
        path = root / name
        if path.is_file():
            path.unlink()
    shutil.copy2(ROOT / "bin/x64/Release/CodexUsage.exe", root / "CodexUsage.exe")
    python = sys.executable
    (root / "codex.cmd").write_text('@"' + python + '" "' + str(ROOT / "tests/fake_server.py") + '"\r\n', encoding="utf-8")
    env = os.environ.copy()
    env["PATH"] = str(root) + os.pathsep + env["PATH"]
    env["CODEX_USAGE_TEST_ROOT"] = str(root)
    process = None
    seen = set()
    report = {"checks": [], "samples": [], "real_service": False}

    def check(value, label):
        if not value:
            raise AssertionError(label)
        report["checks"].append(label)
        print("PASS " + label, flush=True)

    def state(**values):
        path = root / "control.tmp"
        path.write_text(json.dumps(values), encoding="utf-8")
        path.replace(root / "control.json")

    def launch():
        p = subprocess.Popen([str(root / "CodexUsage.exe")], cwd=root, env=env)
        h = wait(lambda: window_for(p.pid), message="dialog startup")
        user.ShowWindow(h, 0)
        return p, h

    def close(p, h):
        seen.update(tree(p.pid))
        start = time.monotonic()
        user.PostMessageW(h, 0x10, 0, 0)
        p.wait(timeout=5)
        check(time.monotonic() - start < 3, "close completes within three seconds")
        time.sleep(0.2)
        alive = set()
        for pid in seen:
            handle = kernel.OpenProcess(0x1000, False, pid)
            if handle:
                code = W.DWORD()
                if kernel.GetExitCodeProcess(handle, C.byref(code)) and code.value == 259:
                    alive.add(pid)
                kernel.CloseHandle(handle)
        check(not alive, "no process remains after close")

    try:
        state(split=True, stderr=True)
        process, hwnd = launch()
        wait(lambda: "75.0%" in text(hwnd, 1002), message="initial data")
        check("37.5%" in text(hwnd, 1006), "weekly remaining percentage")
        check(text(hwnd, 1009).endswith("9"), "server total count retained independently of one detail")
        check(user.SendMessageW(user.GetDlgItem(hwnd, 1011), 0x1004, 0, 0) == 1, "one detail row shown")
        check(text(hwnd, 1001) == "5 小时额度" and text(hwnd, 1005) == "每周额度"
              and text(hwnd, 1012) == "刷新间隔（秒）" and text(hwnd, 1019) == "设置",
              "main window labels display Chinese")
        list_handle = user.GetDlgItem(hwnd, 1011)
        check(user.SendMessageW(list_handle, 0x1028, 0, 0) >= 1, "first detail row fully visible")
        check(text(hwnd, 1013) == "3", "missing config defaults to three seconds")
        check(not user.GetDlgItem(hwnd, 1016) and not user.GetDlgItem(hwnd, 1018), "removed unused status rows")
        old_font = user.SendMessageW(user.GetDlgItem(hwnd, 1002), 0x31, 0, 0)
        click(hwnd, 1019)
        dialog = wait(lambda: settings_for(process.pid), message="font settings dialog")
        check(text(dialog, 1) == "确定" and text(dialog, 2) == "取消", "settings buttons display Chinese")
        check(text(dialog, 1020) == "9" and text(dialog, 1021) == "9", "font sizes default to nine")
        set_text(dialog, 1020, "12")
        set_text(dialog, 1021, "11")
        click(dialog, 1)
        wait(lambda: not settings_for(process.pid), message="font settings closed")
        check(user.SendMessageW(user.GetDlgItem(hwnd, 1002), 0x31, 0, 0) != old_font, "font applied immediately")
        saved = (root / "CodexUsage.ini").read_text()
        check("FiveHourFontSize=12" in saved and "WeeklyFontSize=11" in saved, "both font sizes saved")
        check(not polls(root)[0]["params"]["excludeResetCreditDetails"], "startup asks for full details")
        wait(lambda: len(polls(root)) >= 2, timeout=8, message="default polling")
        items = polls(root)
        check(2.8 <= items[1]["time"] - items[0]["time"] <= 5, "three second default polling")
        check(items[1]["params"]["excludeResetCreditDetails"], "ordinary polls exclude details")
        check(user.SendMessageW(user.GetDlgItem(hwnd, 1011), 0x1004, 0, 0) == 1, "ordinary polls retain details")
        set_text(hwnd, 1013, "1")
        click(hwnd, 1014)
        wait(lambda: "RefreshIntervalSeconds=1" in (root / "CodexUsage.ini").read_text(), message="save interval")
        check("RefreshIntervalSeconds=1" in (root / "CodexUsage.ini").read_text(), "applied interval saved beside executable")
        before = len(polls(root))
        wait(lambda: len(polls(root)) >= before + 2, timeout=4, message="updated polling interval")
        items = polls(root)
        check(0.9 <= items[-1]["time"] - items[-2]["time"] < 2, "changed interval applies immediately")
        state(count=8)
        wait(lambda: text(hwnd, 1009).endswith("8"), message="changed count")
        wait(lambda: not polls(root)[-1]["params"]["excludeResetCreditDetails"], timeout=4, message="count triggers full refresh")
        check(True, "count change immediately refreshes details")
        state(delay=2)
        before = len(polls(root))
        click(hwnd, 1015)
        wait(lambda: len(polls(root)) > before, message="slow refresh starts")
        for unused in range(30):
            click(hwnd, 1015)
        time.sleep(4.3)
        delta = polls(root)[before:]
        check(len(delta) == 2, "thirty clicks coalesce into one pending refresh")
        check(all(not p["params"]["excludeResetCreditDetails"] for p in delta), "manual refreshes request full details")
        state(error=True)
        wait(lambda: "数据未更新" in text(hwnd, 1017), timeout=8, message="failure state")
        check("75.0%" in text(hwnd, 1002), "failure preserves last successful data")
        state(exit=True)
        click(hwnd, 1015)
        wait(lambda: "管道" in text(hwnd, 1017), message="process exit detected")
        state()
        click(hwnd, 1015)
        wait(lambda: text(hwnd, 1017) == "已连接", message="immediate manual reconnect")
        check(True, "process exit reconnects and manual refresh bypasses backoff")
        # Long configured waits must still close promptly and persist across restart.
        set_text(hwnd, 1013, "3600")
        click(hwnd, 1014)
        time.sleep(0.2)
        close(process, hwnd)
        process, hwnd = launch()
        wait(lambda: "75.0%" in text(hwnd, 1002), message="restart data")
        check(text(hwnd, 1013) == "3600", "saved settings restored after restart")
        click(hwnd, 1019)
        dialog = wait(lambda: settings_for(process.pid), message="font settings reopened")
        check(text(dialog, 1020) == "12" and text(dialog, 1021) == "11", "font sizes restored after restart")
        click(dialog, 2)
        wait(lambda: not settings_for(process.pid), message="font settings cancelled")
        # A notification stream cannot postpone the 15-second request timeout.
        state(notifications=True)
        before = len(polls(root))
        click(hwnd, 1015)
        wait(lambda: len(polls(root)) > before, message="notification flood starts")
        started = time.monotonic()
        wait(lambda: "15 秒" in text(hwnd, 1017), timeout=18, message="request timeout")
        check(14 <= time.monotonic() - started <= 17, "notification traffic cannot extend request timeout")
        state()
        click(hwnd, 1015)
        wait(lambda: text(hwnd, 1017) == "已连接", message="timeout recovery")
        if args.soak_seconds:
            set_text(hwnd, 1013, "3")
            click(hwnd, 1014)
            # Warm up before the measurement to exclude startup allocations.
            time.sleep(4)
            start = time.monotonic()
            while True:
                sample = metrics(process.pid)
                sample["elapsed"] = round(time.monotonic() - start, 3)
                sample["polls"] = len(polls(root))
                report["samples"].append(sample)
                seen.update(tree(process.pid))
                (root / "validation.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
                print("SAMPLE " + json.dumps(sample), flush=True)
                if time.monotonic() - start >= args.soak_seconds:
                    break
                time.sleep(min(30, args.soak_seconds - (time.monotonic() - start)))
            samples = report["samples"]
            check(max(x["processes"] for x in samples) == min(x["processes"] for x in samples), "process count remains constant during soak")
            check(samples[-1]["private_bytes"] - samples[0]["private_bytes"] < 8 * 1024 * 1024, "private memory growth below eight MiB")
            full = [p for p in polls(root) if not p["params"]["excludeResetCreditDetails"] and p["time"] >= samples[0]["time"]]
            check(len(full) >= args.soak_seconds // 65, "full details refreshed every sixty seconds")
        # Closing during a blocked request must interrupt the wait and kill the tree.
        state(hang=True)
        before = len(polls(root))
        click(hwnd, 1015)
        wait(lambda: len(polls(root)) > before, message="blocked request")
        close(process, hwnd)
        process = None
        check(not any("forbidden" in r for r in requests(root)), "only handshake and read-only query methods used")
        report["passed"] = True
    except Exception as error:
        report["passed"] = False
        report["error"] = str(error)
        raise
    finally:
        if process and process.poll() is None:
            hwnd = window_for(process.pid)
            if hwnd:
                user.PostMessageW(hwnd, 0x10, 0, 0)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True)
        (root / "validation.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
