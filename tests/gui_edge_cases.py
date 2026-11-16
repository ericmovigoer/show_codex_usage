"""Additional UI validation, isolated from gui_validation.py's soak process."""
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import gui_validation as g

root = g.ROOT / "tests/runtime-extra"
root.mkdir(exist_ok=True)
for name in ("CodexUsage.ini", "control.json", "requests.jsonl"):
    path = root / name
    if path.is_file():
        path.unlink()
    elif path.is_dir():
        path.rmdir()
shutil.copy2(g.ROOT / "bin/x64/Release/CodexUsage.exe", root / "CodexUsage.exe")
(root / "codex.cmd").write_text('@"' + g.sys.executable + '" "' + str(g.ROOT / "tests/fake_server.py") + '"\r\n', encoding="utf-8")
env = os.environ.copy()
env["PATH"] = str(root) + os.pathsep + env["PATH"]
env["CODEX_USAGE_TEST_ROOT"] = str(root)
results = []


def state(**values):
    temporary = root / "control.tmp"
    temporary.write_text(json.dumps(values), encoding="utf-8")
    temporary.replace(root / "control.json")


def check(condition, message):
    assert condition, message
    results.append(message)
    print("PASS " + message, flush=True)


def popup(pid, caption):
    found = []
    @g.CALLBACK
    def visit(hwnd, unused):
        value = W.DWORD()
        g.user.GetWindowThreadProcessId(hwnd, C.byref(value))
        buffer = C.create_unicode_buffer(256)
        g.user.GetWindowTextW(hwnd, buffer, len(buffer))
        if value.value == pid and buffer.value == caption:
            found.append(hwnd)
        return True
    g.user.EnumWindows(visit, 0)
    return found[0] if found else None


def dismiss(pid, caption):
    dialog = g.wait(lambda: popup(pid, caption), message="validation popup")
    button = g.user.GetDlgItem(dialog, 1) or g.user.GetDlgItem(dialog, 2)
    g.user.PostMessageW(button, 0xF5, 0, 0)
    g.wait(lambda: not popup(pid, caption), message="dismiss popup")


state()
process = subprocess.Popen([str(root / "CodexUsage.exe")], cwd=root, env=env)
try:
    hwnd = g.wait(lambda: g.window_for(process.pid))
    g.user.ShowWindow(hwnd, 0)
    g.wait(lambda: "75.0%" in g.text(hwnd, 1002))
    g.set_text(hwnd, 1013, "1.5")
    g.click(hwnd, 1014)
    dismiss(process.pid, "刷新间隔无效")
    check(not (root / "CodexUsage.ini").exists(), "invalid input does not create settings or close dialog")
    g.set_text(hwnd, 1013, "2")
    # Use an actual queued Enter key so MFC dialog keyboard handling is exercised.
    g.user.PostMessageW(g.user.GetDlgItem(hwnd, 1013), 0x100, 0xD, 0)
    g.wait(lambda: (root / "CodexUsage.ini").exists())
    check("RefreshIntervalSeconds=2" in (root / "CodexUsage.ini").read_text(), "Enter immediately applies and saves interval")
    (root / "CodexUsage.ini").unlink()
    (root / "CodexUsage.ini").mkdir()
    g.set_text(hwnd, 1013, "1")
    g.click(hwnd, 1014)
    dismiss(process.pid, "无法保存设置")
    before = len(g.polls(root))
    g.wait(lambda: len(g.polls(root)) >= before + 2, timeout=4)
    rows = g.polls(root)
    check(0.9 <= rows[-1]["time"] - rows[-2]["time"] < 1.7, "failed INI save still applies interval for current run")
    (root / "CodexUsage.ini").rmdir()
    state(null_details=True)
    g.click(hwnd, 1015)
    g.wait(lambda: "未提供重置明细" in g.text(hwnd, 1017))
    check(g.user.SendMessageW(g.user.GetDlgItem(hwnd, 1011), 0x1004, 0, 0) == 1, "null full details preserves prior list")
    state(missing=True)
    g.click(hwnd, 1015)
    g.wait(lambda: "未提供" in g.text(hwnd, 1009))
    check("未提供" in g.text(hwnd, 1002), "missing percentages display unknown rather than zero")
    check(not g.user.SendMessageW(g.user.GetDlgItem(hwnd, 1003), 0x1008, 0, 0), "unknown progress shows no numeric value")
    state(incompatible=True)
    g.click(hwnd, 1015)
    g.wait(lambda: "接口不兼容" in g.text(hwnd, 1017))
    check("数据未更新" in g.text(hwnd, 1017), "malformed interface is distinct from missing data")
    state()
    g.click(hwnd, 1015)
    g.wait(lambda: g.text(hwnd, 1017) == "已连接")
    g.set_text(hwnd, 1013, "3600")
    g.click(hwnd, 1014)
    before = len(g.polls(root))
    g.click(hwnd, 1015)
    g.wait(lambda: len(g.polls(root)) > before)
    anchor = g.polls(root)[-1]["time"]
    time.sleep(10)
    check(len(g.polls(root)) == before + 1, "long user interval does not cause fast polls")
    g.wait(lambda: len(g.polls(root)) >= before + 2, timeout=55, message="independent full detail timer")
    rows = g.polls(root)
    check(59 <= rows[-1]["time"] - anchor <= 63 and not rows[-1]["params"]["excludeResetCreditDetails"], "details still refresh after sixty seconds at a 3600 second interval")
    state(error=True)
    before = len(g.polls(root))
    g.click(hwnd, 1015)
    g.wait(lambda: len(g.polls(root)) >= before + 5, timeout=50, message="backoff progression")
    rows = g.polls(root)[before:before + 5]
    gaps = [rows[i + 1]["time"] - rows[i]["time"] for i in range(4)]
    check(all(expected <= actual <= expected + 2 for actual, expected in zip(gaps, [3, 6, 12, 24])), "consecutive failures back off by 3, 6, 12, and 24 seconds")
    state()
    g.click(hwnd, 1015)
    g.wait(lambda: g.text(hwnd, 1017) == "已连接", timeout=4)
    check(True, "manual retry bypasses sixty second failure backoff")
    report = {"passed": True, "checks": results, "backoff_gaps": gaps}
except Exception as error:
    report = {"passed": False, "checks": results, "error": str(error)}
    raise
finally:
    children = g.tree(process.pid)
    g.user.PostMessageW(g.window_for(process.pid), 0x10, 0, 0)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True)
    time.sleep(0.2)
    alive = []
    for pid in children:
        handle = g.kernel.OpenProcess(0x1000, False, pid)
        if handle:
            code = W.DWORD()
            if g.kernel.GetExitCodeProcess(handle, C.byref(code)) and code.value == 259:
                alive.append(pid)
            g.kernel.CloseHandle(handle)
    report["remaining_processes"] = alive
    (root / "validation.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
