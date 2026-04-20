#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Optional


KV_RE = re.compile(r"\b([a-zA-Z_][a-zA-Z0-9_]*)=([^\s]+)")


@dataclass
class KeyEvent:
    ts: float
    slot_id: int
    ip: str
    event: str
    path: str
    status: Optional[int]
    phase: str
    line: str


def iso(ts: float) -> str:
    return datetime.fromtimestamp(ts, tz=timezone.utc).isoformat()


def parse_markers(line: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for key, val in KV_RE.findall(line):
        if key in {"event", "path", "status", "phase", "ip", "has_cookie", "dur_ms", "heap_free", "max_free_block"}:
            out[key] = val
    return out


def iter_key_events(rows: Iterable[dict[str, Any]]) -> Iterable[KeyEvent]:
    for row in rows:
        row_type = str(row.get("type", ""))
        if row_type == "devmon_event":
            evt = row.get("event")
            if not isinstance(evt, dict) or evt.get("type") != "serial_line":
                continue
            line = str(evt.get("line", ""))
            markers = row.get("markers")
            if not isinstance(markers, dict):
                markers = parse_markers(line)
            ts = float(row.get("recorded_at", 0.0) or 0.0)
            slot = row.get("slot")
            slot_id = int(evt.get("slot_id", -1))
            ip = ""
            if isinstance(slot, dict):
                ip = str(slot.get("ip", "") or "")
            yield key_from(ts, slot_id, ip, markers, line)
            continue

        if row_type == "serial_line":
            line = str(row.get("line", ""))
            markers = parse_markers(line)
            ts = float(row.get("recorded_at", 0.0) or 0.0)
            slot_id = int(row.get("slot_id", -1))
            ip = str(row.get("ip", "") or "")
            yield key_from(ts, slot_id, ip, markers, line)


def key_from(ts: float, slot_id: int, ip: str, markers: dict[str, Any], line: str) -> KeyEvent:
    status_raw = markers.get("status")
    status: Optional[int] = None
    if status_raw is not None:
        try:
            status = int(str(status_raw))
        except ValueError:
            status = None
    return KeyEvent(
        ts=ts,
        slot_id=slot_id,
        ip=ip or str(markers.get("ip", "") or ""),
        event=str(markers.get("event", "")),
        path=str(markers.get("path", "")),
        status=status,
        phase=str(markers.get("phase", "")),
        line=line,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze devmon JSONL for web-session instability patterns.")
    parser.add_argument("--in", dest="in_path", required=True, help="input JSONL path")
    parser.add_argument("--context-window-s", type=float, default=20.0, help="seconds of context before each auth failure")
    parser.add_argument("--max-context-events", type=int, default=10, help="max context lines per auth failure")
    args = parser.parse_args()

    in_path = Path(args.in_path).expanduser().resolve()
    rows: list[dict[str, Any]] = []
    with in_path.open("r", encoding="utf-8") as fh:
        for raw in fh:
            raw = raw.strip()
            if not raw:
                continue
            try:
                rows.append(json.loads(raw))
            except json.JSONDecodeError:
                continue

    key_events = sorted(iter_key_events(rows), key=lambda e: e.ts)
    auth_invalid = [e for e in key_events if e.event == "auth_cookie_invalid"]
    login_ok = [e for e in key_events if e.event == "login_ok"]
    request_401 = [e for e in key_events if e.event == "request" and e.status == 401]
    slow_web_tick = [e for e in key_events if e.event == "slow_phase" and e.phase == "web_tick"]
    low_heap = [e for e in key_events if e.event in {"low_heap", "api_low_heap_reject"}]
    prov_compact = [e for e in key_events if e.event == "provisioning_status_compact"]

    print(f"Input: {in_path}")
    print(f"Rows: {len(rows)}")
    print(f"Parsed serial key-events: {len(key_events)}")
    print(f"auth_cookie_invalid: {len(auth_invalid)}")
    print(f"request status=401: {len(request_401)}")
    print(f"login_ok: {len(login_ok)}")
    print(f"slow_phase web_tick: {len(slow_web_tick)}")
    print(f"low-heap family events: {len(low_heap)}")
    print(f"provisioning_status_compact: {len(prov_compact)}")

    if request_401:
        by_path: dict[str, int] = {}
        for e in request_401:
            by_path[e.path] = by_path.get(e.path, 0) + 1
        print("401 paths:")
        for path, count in sorted(by_path.items(), key=lambda p: (-p[1], p[0])):
            print(f"  {path or '(unknown)'}: {count}")

    if not auth_invalid:
        return 0

    print("")
    print("Auth-failure context:")
    for idx, bad in enumerate(auth_invalid, start=1):
        print(f"[{idx}] {iso(bad.ts)} slot={bad.slot_id} ip={bad.ip or '-'}")
        window_start = bad.ts - max(1.0, args.context_window_s)
        context = [
            e for e in key_events
            if e.ts >= window_start and e.ts <= bad.ts and e.slot_id == bad.slot_id
            and e.event in {
                "request",
                "auth_cookie_invalid",
                "login_ok",
                "slow_phase",
                "low_heap",
                "api_low_heap_reject",
                "provisioning_status_compact",
            }
        ]
        if len(context) > args.max_context_events:
            context = context[-args.max_context_events:]
        for e in context:
            rel = bad.ts - e.ts
            print(f"  -{rel:5.2f}s event={e.event} path={e.path or '-'} status={e.status if e.status is not None else '-'} phase={e.phase or '-'}")
        print("")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
