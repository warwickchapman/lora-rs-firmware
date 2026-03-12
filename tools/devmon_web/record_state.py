#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import signal
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


def fetch_json(url: str, timeout_s: float) -> dict[str, Any]:
    req = urllib.request.Request(url, method="GET")
    with urllib.request.urlopen(req, timeout=timeout_s) as resp:
        return json.loads(resp.read().decode("utf-8"))


def overlap_tail_to_head(prev: list[str], curr: list[str]) -> int:
    max_n = min(len(prev), len(curr))
    for n in range(max_n, 0, -1):
        if prev[-n:] == curr[:n]:
            return n
    return 0


def append_jsonl(fh: Any, row: dict[str, Any]) -> None:
    fh.write(json.dumps(row, ensure_ascii=True) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Record devmon_web state/logs from a running instance.")
    parser.add_argument("--base-url", default="http://127.0.0.1:8787", help="devmon_web base URL")
    parser.add_argument("--out", required=True, help="JSONL output path")
    parser.add_argument("--interval-ms", type=int, default=700, help="poll interval in milliseconds")
    parser.add_argument("--timeout-ms", type=int, default=2500, help="HTTP timeout in milliseconds")
    args = parser.parse_args()

    out_path = Path(args.out).expanduser().resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)

    state_url = args.base_url.rstrip("/") + "/api/state"
    interval_s = max(0.2, float(args.interval_ms) / 1000.0)
    timeout_s = max(0.5, float(args.timeout_ms) / 1000.0)

    stop = False

    def on_sigint(_: int, __: Any) -> None:
        nonlocal stop
        stop = True

    signal.signal(signal.SIGINT, on_sigint)
    signal.signal(signal.SIGTERM, on_sigint)

    prev_logs: dict[int, list[str]] = {0: [], 1: [], 2: []}
    append_jsonl_row_count = 0

    with out_path.open("a", encoding="utf-8", buffering=1) as fh:
        append_jsonl(
            fh,
            {
                "type": "recorder_start",
                "mode": "api_state_poll",
                "base_url": args.base_url,
                "recorded_at": time.time(),
            },
        )
        append_jsonl_row_count += 1

        while not stop:
            poll_started = time.time()
            try:
                snapshot = fetch_json(state_url, timeout_s=timeout_s)
            except urllib.error.URLError as exc:
                append_jsonl(
                    fh,
                    {
                        "type": "poll_error",
                        "error": str(exc),
                        "recorded_at": time.time(),
                    },
                )
                append_jsonl_row_count += 1
                sleep_s = max(0.05, interval_s - (time.time() - poll_started))
                time.sleep(sleep_s)
                continue

            now = time.time()
            append_jsonl(
                fh,
                {
                    "type": "poll_snapshot",
                    "recorded_at": now,
                    "state": snapshot,
                },
            )
            append_jsonl_row_count += 1

            slots = snapshot.get("slots")
            if isinstance(slots, list):
                for slot in slots:
                    if not isinstance(slot, dict):
                        continue
                    slot_id = int(slot.get("slot_id", -1))
                    if slot_id < 0:
                        continue
                    logs = slot.get("logs")
                    if not isinstance(logs, list):
                        continue
                    cur_lines = [str(x) for x in logs]
                    prev = prev_logs.get(slot_id, [])
                    overlap = overlap_tail_to_head(prev, cur_lines) if prev else 0
                    new_lines = cur_lines if not prev else cur_lines[overlap:]
                    for line in new_lines:
                        append_jsonl(
                            fh,
                            {
                                "type": "serial_line",
                                "recorded_at": now,
                                "slot_id": slot_id,
                                "ip": slot.get("ip", ""),
                                "port": slot.get("port", ""),
                                "line": line,
                            },
                        )
                        append_jsonl_row_count += 1
                    prev_logs[slot_id] = cur_lines

            sleep_s = max(0.05, interval_s - (time.time() - poll_started))
            time.sleep(sleep_s)

        append_jsonl(
            fh,
            {
                "type": "recorder_stop",
                "rows_written": append_jsonl_row_count,
                "recorded_at": time.time(),
            },
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
