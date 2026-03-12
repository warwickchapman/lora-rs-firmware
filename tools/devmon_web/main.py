#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import json
import os
import re
import subprocess
import sys
import threading
import time
from collections import deque
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Deque, Optional, Tuple

import serial
from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field
from serial.tools import list_ports


DEFAULT_PAIRS = [
    ("", "/dev/tty.usbserial-3"),
    ("", "/dev/tty.usbserial-110"),
    ("", "/dev/tty.usbserial-210"),
]
MAX_LOG_LINES = 800
FIB_RETRY_SECONDS = [1, 1, 2, 3, 5, 8, 13, 21, 34]
_SERIAL_KV_RE = re.compile(r"\b([a-zA-Z_][a-zA-Z0-9_]*)=([^\s]+)")


@dataclass
class SlotState:
    slot_id: int
    ip: str
    port: str
    baud: int = 115200
    ping_state: str = "unknown"
    ping_latency_ms: Optional[float] = None
    ping_last_ok_at: Optional[float] = None
    serial_running: bool = False
    serial_error: Optional[str] = None
    serial_wanted: bool = True
    serial_retry_index: int = 0
    serial_next_retry_at: float = 0.0
    logs: Deque[str] = field(default_factory=lambda: deque(maxlen=MAX_LOG_LINES))

    def to_dict(self) -> dict[str, Any]:
        retry_in_s: Optional[float] = None
        if self.serial_wanted and not self.serial_running:
            retry_in_s = max(0.0, self.serial_next_retry_at - time.time())
        return {
            "slot_id": self.slot_id,
            "ip": self.ip,
            "port": self.port,
            "baud": self.baud,
            "ping": {
                "state": self.ping_state,
                "latency_ms": self.ping_latency_ms,
                "last_ok_at": self.ping_last_ok_at,
            },
            "serial": {
                "running": self.serial_running,
                "error": self.serial_error,
                "wanted": self.serial_wanted,
                "retry_in_s": retry_in_s,
            },
            "logs": list(self.logs),
        }


class SlotUpdate(BaseModel):
    ip: Optional[str] = Field(default=None)
    port: Optional[str] = Field(default=None)
    baud: Optional[int] = Field(default=None, ge=300, le=3000000)


class JsonlRecorder:
    def __init__(self, path: str) -> None:
        self.path = Path(path).expanduser().resolve()
        self._fh: Optional[Any] = None
        self._write_error = False

    def start(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._fh = self.path.open("a", encoding="utf-8", buffering=1)

    def stop(self) -> None:
        if self._fh is None:
            return
        try:
            self._fh.flush()
            self._fh.close()
        finally:
            self._fh = None

    def write(self, payload: dict[str, Any]) -> None:
        if self._fh is None:
            return
        payload = dict(payload)
        payload["recorded_at"] = time.time()
        try:
            self._fh.write(json.dumps(payload, ensure_ascii=True) + "\n")
        except Exception as exc:
            if not self._write_error:
                self._write_error = True
                print(f"[devmon] recorder write failed: {exc}", file=sys.stderr)


_LAT_RE = re.compile(r"time[=<]([0-9.]+)\\s*ms", re.IGNORECASE)
_IP_FROM_LOG_RE = re.compile(r"\bip=((?:\d{1,3}\.){3}\d{1,3})\b", re.IGNORECASE)
_STA_CONNECTED_RE = re.compile(r"\bevent=sta_connected\b", re.IGNORECASE)


def _valid_ipv4(ip: str) -> bool:
    parts = ip.split(".")
    if len(parts) != 4:
        return False
    try:
        nums = [int(p) for p in parts]
    except ValueError:
        return False
    return all(0 <= n <= 255 for n in nums)


def ping_once(ip: str) -> Tuple[str, Optional[float]]:
    if not ip:
        return "unknown", None

    if sys.platform == "darwin":
        cmd = ["ping", "-n", "-c", "1", "-W", "1000", ip]
    else:
        cmd = ["ping", "-n", "-c", "1", "-W", "1", ip]

    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=2.5)
    except Exception:
        return "error", None

    out = (proc.stdout or "") + "\n" + (proc.stderr or "")
    if proc.returncode == 0:
        m = _LAT_RE.search(out)
        latency = float(m.group(1)) if m else None
        return "online", latency

    return "offline", None


class SerialReader(threading.Thread):
    def __init__(
        self,
        slot_id: int,
        port: str,
        baud: int,
        loop: asyncio.AbstractEventLoop,
        event_queue: "asyncio.Queue[dict[str, Any]]",
    ) -> None:
        super().__init__(daemon=True)
        self.slot_id = slot_id
        self.port = port
        self.baud = baud
        self.loop = loop
        self.event_queue = event_queue
        self.stop_event = threading.Event()

    def _emit(self, event: dict[str, Any]) -> None:
        def _put() -> None:
            self.event_queue.put_nowait(event)

        self.loop.call_soon_threadsafe(_put)

    def stop(self) -> None:
        self.stop_event.set()

    def run(self) -> None:
        self._emit(
            {
                "type": "serial_status",
                "slot_id": self.slot_id,
                "running": True,
                "error": None,
            }
        )
        try:
            with serial.Serial(self.port, self.baud, timeout=0.25) as ser:
                pending = ""
                while not self.stop_event.is_set():
                    chunk = ser.read(ser.in_waiting or 1)
                    if not chunk:
                        continue
                    text = chunk.decode(errors="replace")
                    pending += text
                    while "\n" in pending:
                        line, pending = pending.split("\n", 1)
                        self._emit(
                            {
                                "type": "serial_line",
                                "slot_id": self.slot_id,
                                "line": line.rstrip("\r"),
                            }
                        )
                if pending.strip():
                    self._emit(
                        {
                            "type": "serial_line",
                            "slot_id": self.slot_id,
                            "line": pending.rstrip("\r"),
                        }
                    )
        except Exception as exc:
            self._emit(
                {
                    "type": "serial_status",
                    "slot_id": self.slot_id,
                    "running": False,
                    "error": str(exc),
                }
            )
            return

        self._emit(
            {
                "type": "serial_status",
                "slot_id": self.slot_id,
                "running": False,
                "error": None,
            }
        )


class DevmonManager:
    def __init__(self) -> None:
        self.loop: Optional[asyncio.AbstractEventLoop] = None
        self.event_queue: Optional["asyncio.Queue[dict[str, Any]]"] = None
        self.clients: set[WebSocket] = set()
        self.slots: list[SlotState] = [
            SlotState(slot_id=i, ip=ip, port=port) for i, (ip, port) in enumerate(DEFAULT_PAIRS)
        ]
        self.available_ports: list[str] = []
        self.serial_threads: dict[int, SerialReader] = {}
        self._tasks: list[asyncio.Task[Any]] = []
        self._stopping = False
        self.recorder: Optional[JsonlRecorder] = None

    def set_pairs(self, pairs: list[Tuple[str, str]]) -> None:
        if len(pairs) != 3:
            raise ValueError("exactly 3 pairs required")
        for idx, (ip, port) in enumerate(pairs):
            slot = self.slots[idx]
            slot.ip = ip
            slot.port = port

    def set_jsonl_record_path(self, path: str) -> None:
        self.recorder = JsonlRecorder(path)

    async def start(self) -> None:
        self.loop = asyncio.get_running_loop()
        self.event_queue = asyncio.Queue()
        self.available_ports = self._discover_ports()
        if self.recorder is not None:
            self.recorder.start()
            self.recorder.write(
                {
                    "type": "devmon_start",
                    "state": self.snapshot(),
                }
            )
        self._tasks = [
            asyncio.create_task(self._event_dispatch_loop(), name="event-dispatch"),
            asyncio.create_task(self._port_poll_loop(), name="port-poll"),
            asyncio.create_task(self._ping_loop(), name="ping-poll"),
            asyncio.create_task(self._serial_retry_loop(), name="serial-retry"),
        ]

    async def stop(self) -> None:
        self._stopping = True
        for slot_id in range(3):
            await self.stop_serial(slot_id)
        for t in self._tasks:
            t.cancel()
        for t in self._tasks:
            try:
                await t
            except asyncio.CancelledError:
                pass
        if self.recorder is not None:
            self.recorder.write({"type": "devmon_stop"})
            self.recorder.stop()

    def snapshot(self) -> dict[str, Any]:
        return {
            "slots": [slot.to_dict() for slot in self.slots],
            "ports": self.available_ports,
            "server_time": time.time(),
        }

    def _discover_ports(self) -> list[str]:
        ports = sorted({p.device for p in list_ports.comports() if p.device})
        return ports

    async def _port_poll_loop(self) -> None:
        while not self._stopping:
            ports = await asyncio.to_thread(self._discover_ports)
            if ports != self.available_ports:
                self.available_ports = ports
                await self._emit({"type": "ports", "ports": ports})
            await asyncio.sleep(2.0)

    async def _ping_loop(self) -> None:
        while not self._stopping:
            for slot in self.slots:
                state, latency = await asyncio.to_thread(ping_once, slot.ip)
                event = {
                    "type": "ping",
                    "slot_id": slot.slot_id,
                    "state": state,
                    "latency_ms": latency,
                    "at": time.time(),
                }
                await self._emit(event)
            await asyncio.sleep(1.0)

    async def _serial_retry_loop(self) -> None:
        while not self._stopping:
            now = time.time()
            for slot in self.slots:
                if not slot.serial_wanted or slot.serial_running:
                    continue

                worker = self.serial_threads.get(slot.slot_id)
                if worker and worker.is_alive():
                    continue

                if now < slot.serial_next_retry_at:
                    continue

                try:
                    await self.start_serial(slot.slot_id, reset_backoff=False)
                except HTTPException as exc:
                    slot.serial_error = str(exc.detail)
                    self._schedule_next_retry(slot)
                    await self._broadcast(
                        {
                            "type": "serial_status",
                            "slot_id": slot.slot_id,
                            "running": False,
                            "error": slot.serial_error,
                            "wanted": slot.serial_wanted,
                            "retry_in_s": max(0.0, slot.serial_next_retry_at - time.time()),
                        }
                    )
            await asyncio.sleep(0.25)

    async def _event_dispatch_loop(self) -> None:
        while not self._stopping:
            if self.event_queue is None:
                await asyncio.sleep(0.1)
                continue
            event = await self.event_queue.get()
            self._apply_event(event)
            await self._broadcast(event)

    def _apply_event(self, event: dict[str, Any]) -> None:
        et = event.get("type")
        if et == "serial_line":
            slot = self.slots[event["slot_id"]]
            line = str(event.get("line", ""))
            slot.logs.append(line)
            # Only trust DHCP station IP assignment line, not arbitrary log fields.
            m = _IP_FROM_LOG_RE.search(line)
            if m and _STA_CONNECTED_RE.search(line):
                candidate = m.group(1)
                if _valid_ipv4(candidate) and candidate != slot.ip:
                    slot.ip = candidate
                    event["ip_detected"] = candidate
            return

        if et == "serial_status":
            slot = self.slots[event["slot_id"]]
            slot.serial_running = bool(event.get("running", False))
            slot.serial_error = event.get("error")
            if slot.serial_running:
                slot.serial_retry_index = 0
                slot.serial_next_retry_at = 0.0
            elif slot.serial_wanted:
                self._schedule_next_retry(slot)

            worker = self.serial_threads.get(slot.slot_id)
            if worker and not worker.is_alive():
                self.serial_threads.pop(slot.slot_id, None)

            event["wanted"] = slot.serial_wanted
            event["retry_in_s"] = max(0.0, slot.serial_next_retry_at - time.time()) if slot.serial_wanted and not slot.serial_running else None
            return

        if et == "ping":
            slot = self.slots[event["slot_id"]]
            slot.ping_state = str(event.get("state", "unknown"))
            slot.ping_latency_ms = event.get("latency_ms")
            if slot.ping_state == "online":
                slot.ping_last_ok_at = float(event.get("at", time.time()))
            return

    async def _emit(self, event: dict[str, Any]) -> None:
        if self.event_queue is not None:
            await self.event_queue.put(event)

    async def _broadcast(self, event: dict[str, Any]) -> None:
        self._record_event(event)
        if not self.clients:
            return
        stale: list[WebSocket] = []
        for ws in list(self.clients):
            try:
                await ws.send_json(event)
            except Exception:
                stale.append(ws)
        for ws in stale:
            self.clients.discard(ws)

    def _record_event(self, event: dict[str, Any]) -> None:
        if self.recorder is None:
            return
        row: dict[str, Any] = {"type": "devmon_event", "event": dict(event)}
        slot_id_any = event.get("slot_id")
        if isinstance(slot_id_any, int) and 0 <= slot_id_any < len(self.slots):
            slot = self.slots[slot_id_any]
            row["slot"] = {
                "slot_id": slot.slot_id,
                "ip": slot.ip,
                "port": slot.port,
                "baud": slot.baud,
                "serial_running": slot.serial_running,
                "serial_wanted": slot.serial_wanted,
                "ping_state": slot.ping_state,
            }
        if event.get("type") == "serial_line":
            line = str(event.get("line", ""))
            markers: dict[str, str] = {}
            for key, val in _SERIAL_KV_RE.findall(line):
                if key in {
                    "event",
                    "path",
                    "status",
                    "phase",
                    "dur_ms",
                    "heap_free",
                    "heap_frag",
                    "max_free_block",
                    "ip",
                    "has_cookie",
                }:
                    markers[key] = val
            if markers:
                row["markers"] = markers
        self.recorder.write(row)

    async def connect_ws(self, ws: WebSocket) -> None:
        await ws.accept()
        self.clients.add(ws)
        await ws.send_json({"type": "snapshot", "data": self.snapshot()})

    async def disconnect_ws(self, ws: WebSocket) -> None:
        self.clients.discard(ws)

    async def update_slot(self, slot_id: int, update: SlotUpdate) -> dict[str, Any]:
        slot = self._get_slot(slot_id)
        if update.ip is not None:
            slot.ip = update.ip.strip()
        if update.port is not None:
            slot.port = update.port.strip()
        if update.baud is not None:
            slot.baud = update.baud

        await self._broadcast({"type": "slot", "slot": slot.to_dict()})

        if slot.serial_running:
            await self.stop_serial(slot_id, disable_wanted=False)
            await self.start_serial(slot_id, reset_backoff=True)
        elif slot.serial_wanted:
            # Apply config changes quickly when auto-retry is enabled.
            slot.serial_retry_index = 0
            slot.serial_next_retry_at = 0.0

        return slot.to_dict()

    def _get_slot(self, slot_id: int) -> SlotState:
        if slot_id < 0 or slot_id >= len(self.slots):
            raise HTTPException(status_code=404, detail="slot not found")
        return self.slots[slot_id]

    async def clear_logs(self, slot_id: int) -> None:
        slot = self._get_slot(slot_id)
        slot.logs.clear()
        await self._broadcast({"type": "logs_cleared", "slot_id": slot_id})

    def _schedule_next_retry(self, slot: SlotState) -> None:
        delay = FIB_RETRY_SECONDS[min(slot.serial_retry_index, len(FIB_RETRY_SECONDS) - 1)]
        slot.serial_next_retry_at = time.time() + delay
        slot.serial_retry_index = min(slot.serial_retry_index + 1, len(FIB_RETRY_SECONDS) - 1)

    async def start_serial(self, slot_id: int, reset_backoff: bool = True) -> None:
        slot = self._get_slot(slot_id)
        if not slot.port:
            raise HTTPException(status_code=400, detail="serial port is empty")
        slot.serial_wanted = True
        if reset_backoff:
            slot.serial_retry_index = 0
            slot.serial_next_retry_at = 0.0
            slot.serial_error = None

        if slot_id in self.serial_threads and self.serial_threads[slot_id].is_alive():
            return
        if self.loop is None:
            raise HTTPException(status_code=500, detail="manager loop not initialized")
        if self.event_queue is None:
            raise HTTPException(status_code=500, detail="manager queue not initialized")

        worker = SerialReader(slot_id, slot.port, slot.baud, self.loop, self.event_queue)
        self.serial_threads[slot_id] = worker
        worker.start()

    async def stop_serial(self, slot_id: int, disable_wanted: bool = True) -> None:
        slot = self._get_slot(slot_id)
        if disable_wanted:
            slot.serial_wanted = False
            slot.serial_retry_index = 0
            slot.serial_next_retry_at = 0.0

        worker = self.serial_threads.get(slot_id)
        if not worker:
            await self._broadcast(
                {
                    "type": "serial_status",
                    "slot_id": slot_id,
                    "running": False,
                    "error": None,
                    "wanted": slot.serial_wanted,
                    "retry_in_s": None,
                }
            )
            return
        worker.stop()
        await asyncio.to_thread(worker.join, 1.5)
        if worker.is_alive():
            await self._broadcast(
                {
                    "type": "serial_status",
                    "slot_id": slot_id,
                    "running": False,
                    "error": "serial stop timeout",
                    "wanted": slot.serial_wanted,
                    "retry_in_s": None,
                }
            )
        self.serial_threads.pop(slot_id, None)


manager = DevmonManager()


@asynccontextmanager
async def lifespan(_: FastAPI):
    await manager.start()
    try:
        yield
    finally:
        await manager.stop()


app = FastAPI(title="LoRa DevMon", version="0.1.0", lifespan=lifespan)

STATIC_DIR = Path(__file__).resolve().parent / "static"
app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")


@app.get("/")
async def index() -> FileResponse:
    return FileResponse(STATIC_DIR / "index.html")


@app.get("/api/state")
async def api_state() -> dict[str, Any]:
    return manager.snapshot()


@app.post("/api/slots/{slot_id}/config")
async def api_slot_config(slot_id: int, update: SlotUpdate) -> dict[str, Any]:
    slot = await manager.update_slot(slot_id, update)
    return {"ok": True, "slot": slot}


@app.post("/api/slots/{slot_id}/serial/start")
async def api_serial_start(slot_id: int) -> dict[str, Any]:
    await manager.start_serial(slot_id)
    return {"ok": True}


@app.post("/api/slots/{slot_id}/serial/stop")
async def api_serial_stop(slot_id: int) -> dict[str, Any]:
    await manager.stop_serial(slot_id)
    return {"ok": True}


@app.post("/api/slots/{slot_id}/logs/clear")
async def api_logs_clear(slot_id: int) -> dict[str, Any]:
    await manager.clear_logs(slot_id)
    return {"ok": True}


@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket) -> None:
    await manager.connect_ws(ws)
    try:
        while True:
            await ws.receive_text()
    except WebSocketDisconnect:
        await manager.disconnect_ws(ws)
    except Exception:
        await manager.disconnect_ws(ws)


if __name__ == "__main__":
    import uvicorn

    parser = argparse.ArgumentParser(description="LoRa DevMon web dashboard")
    parser.add_argument(
        "--pair",
        action="append",
        nargs=2,
        metavar=("IP", "PORT"),
        help="device pair (IP + serial port). Repeat 3 times.",
    )
    parser.add_argument(
        "--record-jsonl",
        default=os.environ.get("DEVMON_RECORD_JSONL", ""),
        help="append monitor events to this JSONL file",
    )
    parser.add_argument("--host", default=os.environ.get("DEVMON_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("DEVMON_PORT", "8787")))
    args = parser.parse_args()

    if args.pair:
        if len(args.pair) != 3:
            raise SystemExit("Provide exactly 3 --pair entries.")
        manager.set_pairs([(ip, port) for ip, port in args.pair])
    if args.record_jsonl:
        manager.set_jsonl_record_path(args.record_jsonl)

    uvicorn.run(app, host=args.host, port=args.port, log_level="info")
