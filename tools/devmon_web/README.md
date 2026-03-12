# LoRa DevMon Web (MVP)

Local bench dashboard for 3 paired devices (IP + serial port).

## Features

- 3 stacked device rows (paired layout, full-width logs)
- Ping health badge per device (`online`, `offline`, `error`)
- Serial port picker from discovered local ports
- Live serial log stream per device (WebSocket)
- Editable IP/port/baud per device
- Auto serial reconnect with Fibonacci retry (1,1,2,3,5,8...s)
- `Connect` resets retry backoff to 1s and retries immediately

## Run

From repo root:

```bash
python3 -m pip install -r tools/devmon_web/requirements.txt
python3 tools/devmon_web/main.py
```

Then open:

- http://127.0.0.1:8787

## Optional startup pairs

You can override defaults with three `--pair` entries:

```bash
python3 tools/devmon_web/main.py \
  --pair 192.168.0.170 /dev/tty.usbserial-210 \
  --pair 192.168.0.234 /dev/tty.usbserial-210 \
  --pair 192.168.0.235 /dev/tty.usbserial-110
```

## Recording for session analysis

### Quick decision

- Preferred for direct monitoring: start `devmon_web` with `--record-jsonl` (single process for UI + logging).
- If `devmon_web` is already running without that flag: use `record_state.py` as a no-restart attach recorder.
- `analyze_session_jsonl.py` is passive analysis only. It does **not** start or run an agent.

### Mode A: built-in recorder (recommended)

Use this when launching devmon from scratch.

```bash
python3 tools/devmon_web/main.py \
  --record-jsonl /tmp/devmon-session.jsonl
```

What this gives you:
- normal browser UI at `http://127.0.0.1:8787`
- continuous JSONL logging from the same process
- best signal quality for live triage

### Mode B: attach recorder to an already-running devmon (no restart)

Use this when devmon is already running and you want continuous capture immediately.

```bash
python3 tools/devmon_web/record_state.py \
  --base-url http://127.0.0.1:8787 \
  --out /tmp/devmon-session.jsonl
```

Notes:
- this is a fallback when you cannot restart `devmon_web`
- it polls `/api/state` and appends JSONL snapshots + serial lines

### Analyze captured JSONL

```bash
python3 tools/devmon_web/analyze_session_jsonl.py \
  --in /tmp/devmon-session.jsonl
```

This summarizes:
- `auth_cookie_invalid`
- `request ... status=401`
- `login_ok`
- `slow_phase phase=web_tick`
- low-heap family events
- per-failure context timeline

### Live monitoring workflow

`analyze_session_jsonl.py` does not run continuously by itself. For live consumption during an active run, re-run it periodically while recording is active:

```bash
while true; do
  clear
  python3 tools/devmon_web/analyze_session_jsonl.py --in /tmp/devmon-session.jsonl
  sleep 5
done
```

This gives near-live summaries while the UI test is running.

## Notes

- This MVP uses `pyserial` directly (not `platformio device monitor`) for stable embedding.
- Duplicate port selection is allowed, but two readers cannot successfully open the same serial port at once.
- Serial logs are non-wrapping for readability; use horizontal scroll for long lines.
