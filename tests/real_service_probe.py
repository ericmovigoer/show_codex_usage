"""Read-only smoke check against the installed CLI; does not change CODEX_HOME."""
import json
import subprocess
import time
import gui_validation as g

process = subprocess.Popen([str(g.ROOT / "bin/x64/Release/CodexUsage.exe")], cwd=g.ROOT)
report = {}
try:
    hwnd = g.wait(lambda: g.window_for(process.pid))
    g.user.ShowWindow(hwnd, 0)
    def terminal_status():
        status = g.text(hwnd, 1017)
        return (status == "已连接" or "未提供重置明细" in status or
                (not status.startswith("正在连接") and ("暂无数据" in status or "数据未更新" in status)))
    g.wait(terminal_status, timeout=20)
    report = {"success": g.text(hwnd, 1017) == "已连接", "status": g.text(hwnd, 1017),
              "five_hour": g.text(hwnd, 1002), "weekly": g.text(hwnd, 1006),
              "reset_count": g.text(hwnd, 1009), "last_update": g.text(hwnd, 1018)}
finally:
    before = g.tree(process.pid)
    hwnd = g.window_for(process.pid)
    if hwnd:
        g.user.PostMessageW(hwnd, 0x10, 0, 0)
    process.wait(timeout=5)
    time.sleep(0.2)
    report["remaining_tree_processes"] = len(g.tree(process.pid))
    (g.ROOT / "tests/real_service_result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report))
