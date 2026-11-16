"""Only implements the three allowed read-only protocol methods for GUI tests."""
import json
import os
from pathlib import Path
import sys
import time

sys.stdin.reconfigure(encoding="utf-8")
sys.stdout.reconfigure(encoding="utf-8", newline="\n")
root = Path(os.environ["CODEX_USAGE_TEST_ROOT"])


def record(value):
    with (root / "requests.jsonl").open("a", encoding="utf-8") as stream:
        stream.write(json.dumps({"time": time.time(), "pid": os.getpid(), **value}) + "\n")


def send(value, split=False):
    data = json.dumps(value, ensure_ascii=False) + "\n"
    if split:
        for pos in range(0, len(data), 17):
            sys.stdout.write(data[pos:pos + 17])
            sys.stdout.flush()
    else:
        sys.stdout.write(data)
        sys.stdout.flush()


for line in sys.stdin:
    req = json.loads(line)
    method = req["method"]
    record({"method": method, "params": req.get("params")})
    if method == "initialize":
        send({"id": req["id"], "result": {"userAgent": "fake-codex/0.169.0"}})
    elif method == "initialized":
        continue
    elif method == "account/rateLimits/read":
        control = root / "control.json"
        state = json.loads(control.read_text(encoding="utf-8")) if control.exists() else {}
        if state.get("exit"):
            sys.exit(7)
        if state.get("hang"):
            time.sleep(120)
        if state.get("notifications"):
            until = time.monotonic() + 20
            while time.monotonic() < until:
                send({"method": "test/notification", "params": {}})
                time.sleep(0.005)
        time.sleep(state.get("delay", 0))
        if state.get("error"):
            send({"id": req["id"], "error": {"code": -32000, "message": "simulated network outage"}})
            continue
        if state.get("stderr"):
            sys.stderr.write("simulated diagnostic\n" * 10000)
            sys.stderr.flush()
        full = not req.get("params", {}).get("excludeResetCreditDetails", False)
        credit = {"id": "fixture", "resetType": "codexRateLimits", "status": "available",
                  "grantedAt": 1700000000, "expiresAt": None,
                  "title": "Codex rate limit reset", "description": "测试说明"}
        result = {"rateLimits": None, "rateLimitsByLimitId": {"codex": {
            "primary": {"usedPercent": 25, "windowDurationMins": 300, "resetsAt": 1800000000},
            "secondary": {"usedPercent": 62.5, "windowDurationMins": 10080, "resetsAt": 1800600000}}},
            "rateLimitResetCredits": {"availableCount": state.get("count", 9),
                                      "credits": [credit] if full else None}}
        if state.get("null_details"):
            result["rateLimitResetCredits"]["credits"] = None
        if state.get("missing"):
            result = {"rateLimits": None, "rateLimitResetCredits": None}
        if state.get("incompatible"):
            result = {"rateLimitResetCredits": {"availableCount": "invalid"}}
        # An unrelated response and notification in the same pipe exercise ID matching.
        send({"id": 999999, "result": {}})
        send({"method": "account/rateLimits/updated", "params": {}})
        send({"id": req["id"], "result": result}, state.get("split", False))
    else:
        record({"forbidden": method})
        raise RuntimeError("Unexpected non-read-only method: " + method)
