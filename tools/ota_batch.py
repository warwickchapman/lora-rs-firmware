#!/usr/bin/env python3
import argparse
import hashlib
import http.client
import json
import os
import re
import subprocess
import sys
from typing import Any, Dict, List, Tuple

PRODUCT_SECRET = "LRS-v1-rotate-this-secret"
DEFAULT_ESPOTA = "/Users/warwick/.platformio/packages/framework-arduinoespressif8266/tools/espota.py"
DEFAULT_FW = "/Users/warwick/Code/LoRa/lora_rs/.pio/build/lrs_za/firmware.bin"

# Edit these lists as needed.
LOCATIONS: Dict[str, List[Dict[str, Any]]] = {
    "office": [
        {"host": "lrs-00af8e6", "ip": "192.168.133.21"},
        {"host": "lrs-0029ca6f", "ip": "192.168.133.22"},
        {"host": "lrs-00fc4f9f", "ip": "192.168.133.23"},
        {"host": "lrs-00fc4f9e", "ip": "192.168.133.24"},
        {"host": "lrs-00fc4f9d", "ip": "192.168.133.25"},
        {"host": "lrs-004a9753", "ip": "192.168.133.26"},
        {"host": "lrs-004a9c27", "ip": "192.168.133.27"},
        {"host": "lrs-00fc4f9c", "ip": "192.168.133.28"},
        {"host": "lrs-0048d1bb", "ip": "192.168.133.29"},
    ],
    "home": [
        {"host": "lrs-004a9753", "ip": "192.168.0.170"},
        {"host": "lrs-0048d1bb", "ip": "192.168.0.235", "factory_reset": True, "admin_pass": "ota"},
        {"host": "lrs-00fc4f9c", "ip": "192.168.0.234", "factory_reset": True, "admin_pass": "ota"},
    ],
}


def normalize_chip_id(value: str) -> str:
    raw = value.strip().lower()
    if raw.startswith("lrs-"):
        raw = raw[4:]
    if raw.startswith("lrs_"):
        raw = raw[4:]
    if raw.startswith("0x"):
        raw = raw[2:]
    if not re.fullmatch(r"[0-9a-f]{1,8}", raw):
        raise ValueError(f"Invalid chip id or host: {value}")
    return raw.zfill(8)


def chip_id_from_host(host: str) -> str:
    return normalize_chip_id(host)


def ota_password(chip_id_hex: str) -> str:
    payload = f"{PRODUCT_SECRET}:{chip_id_hex}"
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()[:8]


def normalize_host_label(value: str) -> str:
    chip = normalize_chip_id(value)
    return f"lrs-{chip}"


def parse_cli_target(spec: str) -> Dict[str, Any]:
    raw = spec.strip()
    if not raw:
        raise ValueError("Empty device spec")

    label = raw
    target = raw
    if "=" in raw:
        label, target = raw.split("=", 1)
    elif "@" in raw:
        label, target = raw.split("@", 1)

    label = label.strip()
    target = target.strip()
    if not label:
        raise ValueError(f"Missing device label in spec: {spec}")
    if not target:
        raise ValueError(f"Missing target host/IP in spec: {spec}")

    chip = normalize_chip_id(label)
    host = normalize_host_label(label)
    return {
        "host": host,
        "ip": target,
        "chip_id": chip,
    }


def resolve_devices(args: argparse.Namespace) -> Tuple[List[Dict[str, Any]], str]:
    devices: List[Dict[str, Any]] = []
    source = ""
    if args.devices:
        for spec in args.devices:
            devices.append(parse_cli_target(spec))
        source = "cli"
    elif args.location:
        devices = list(LOCATIONS.get(args.location, []))
        source = f"location:{args.location}"
    return devices, source


def run_ota(espota: str, ip: str, password: str, firmware: str, port: int) -> None:
    cmd = [sys.executable, espota, "-i", ip, "-p", str(port), "-a", password, "-f", firmware]
    subprocess.run(cmd, check=True)

def build_multipart(fields, file_field, filename, file_bytes):
    boundary = "----lrs-ota-boundary-1"
    lines = []
    for name, value in fields.items():
        lines.append(f"--{boundary}")
        lines.append(f'Content-Disposition: form-data; name="{name}"')
        lines.append("")
        lines.append(str(value))
    lines.append(f"--{boundary}")
    lines.append(
        f'Content-Disposition: form-data; name="{file_field}"; filename="{filename}"'
    )
    lines.append("Content-Type: application/octet-stream")
    lines.append("")
    body = "\r\n".join(lines).encode("utf-8") + b"\r\n" + file_bytes + b"\r\n"
    body += f"--{boundary}--\r\n".encode("utf-8")
    content_type = f"multipart/form-data; boundary={boundary}"
    return content_type, body


def login_and_get_cookie(ip: str, admin_pass: str) -> str:
    conn = http.client.HTTPConnection(ip, 80, timeout=15)
    payload = json.dumps({"password": admin_pass}).encode("utf-8")
    headers = {"Content-Type": "application/json", "Content-Length": str(len(payload))}
    conn.request("POST", "/api/login", body=payload, headers=headers)
    resp = conn.getresponse()
    if resp.status < 200 or resp.status >= 300:
        resp.read()
        conn.close()
        raise RuntimeError(f"Login failed: {resp.status} {resp.reason}")
    set_cookie = resp.getheader("Set-Cookie") or ""
    resp.read()
    conn.close()
    parts = set_cookie.split(";")
    cookie = ""
    for part in parts:
        part = part.strip()
        if part.startswith("lrs_session="):
            cookie = part
            break
    if not cookie:
        raise RuntimeError("Login failed: missing session cookie")
    return cookie


def run_ota_http(ip: str, admin_pass: str, firmware: str, flags: Dict[str, str]) -> None:
    cookie = login_and_get_cookie(ip, admin_pass)
    with open(firmware, "rb") as f:
        fw_bytes = f.read()
    content_type, body = build_multipart(
        flags, "firmware", os.path.basename(firmware), fw_bytes
    )
    conn = http.client.HTTPConnection(ip, 80, timeout=30)
    headers = {
        "Content-Type": content_type,
        "Content-Length": str(len(body)),
        "Cookie": cookie,
    }
    conn.request("POST", "/api/ota", body=body, headers=headers)
    resp = conn.getresponse()
    if resp.status < 200 or resp.status >= 300:
        raise RuntimeError(f"HTTP OTA failed: {resp.status} {resp.reason}")
    resp.read()
    conn.close()


def as_bool(value: Any, default: bool = False) -> bool:
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        v = value.strip().lower()
        if v in ("1", "true", "yes", "on"):
            return True
        if v in ("0", "false", "no", "off", ""):
            return False
    return default


def describe_device_plan(dev: Dict[str, Any], args: argparse.Namespace) -> Dict[str, Any]:
    host = str(dev.get("host", "")).strip()
    ip = str(dev.get("ip", host)).strip()
    chip = str(dev.get("chip_id", "")).strip() or chip_id_from_host(host)
    pw = ota_password(chip)
    dev_factory_reset = as_bool(dev.get("factory_reset"), args.factory_reset)
    dev_keep_wifi = as_bool(dev.get("keep_wifi"), args.keep_wifi)
    dev_keep_fleet = as_bool(dev.get("keep_fleet"), args.keep_fleet)
    dev_use_http = dev_factory_reset or dev_keep_wifi or dev_keep_fleet
    dev_admin_pass = str(dev.get("admin_pass", args.admin_pass or "")).strip()
    ota_mode = "HTTP OTA" if dev_use_http else "ESPOTA"
    return {
        "host": host,
        "ip": ip,
        "chip": chip,
        "password": pw,
        "factory_reset": dev_factory_reset,
        "keep_wifi": dev_keep_wifi,
        "keep_fleet": dev_keep_fleet,
        "use_http": dev_use_http,
        "admin_pass": dev_admin_pass,
        "ota_mode": ota_mode,
    }


def print_device_plan(plan: Dict[str, Any], attempt_label: str = "") -> None:
    prefix = f"{attempt_label} " if attempt_label else ""
    print(
        f"{prefix}{plan['ota_mode']} {plan['host']} {plan['ip']} chip={plan['chip']} pw={plan['password']} "
        f"factory_reset={1 if plan['factory_reset'] else 0} "
        f"keep_wifi={1 if plan['keep_wifi'] else 0} keep_fleet={1 if plan['keep_fleet'] else 0}"
    )


def execute_device_plan(plan: Dict[str, Any], args: argparse.Namespace) -> None:
    if plan["use_http"]:
        if not plan["admin_pass"]:
            raise RuntimeError(
                "HTTP OTA requested but admin password is missing (set --admin-pass or device admin_pass)"
            )
        flags = {
            "factory_reset_after_update": "1" if plan["factory_reset"] else "0",
            "keep_wifi_credentials_after_update": "1" if plan["keep_wifi"] else "0",
            "keep_shared_fleet_key_after_update": "1" if plan["keep_fleet"] else "0",
        }
        admin_pass = plan["password"] if plan["admin_pass"] == "ota" else plan["admin_pass"]
        run_ota_http(plan["ip"], admin_pass, args.fw, flags)
    else:
        run_ota(args.espota, plan["ip"], plan["password"], args.fw, args.port)


def main() -> int:
    parser = argparse.ArgumentParser(description="Batch OTA uploader")
    parser.add_argument("--location", choices=sorted(LOCATIONS.keys()))
    parser.add_argument(
        "--device",
        dest="devices",
        action="append",
        default=[],
        help=(
            "Target a device directly. Accepts lrs-<chipid>, <chipid>, "
            "or label=host-or-ip / label@host-or-ip. Repeat for multiple devices."
        ),
    )
    parser.add_argument("--fw", default=DEFAULT_FW, help="Firmware .bin path")
    parser.add_argument("--espota", default=DEFAULT_ESPOTA, help="Path to espota.py")
    parser.add_argument("--port", type=int, default=8266, help="OTA port")
    parser.add_argument("--dry-run", action="store_true", help="Print actions only")
    parser.add_argument(
        "--factory-reset",
        action="store_true",
        help="Request factory reset after OTA (uses /api/ota)",
    )
    parser.add_argument(
        "--keep-wifi",
        action="store_true",
        help="Keep WiFi credentials after update (uses /api/ota)",
    )
    parser.add_argument(
        "--keep-fleet",
        action="store_true",
        help="Keep shared fleet key after update (uses /api/ota)",
    )
    parser.add_argument(
        "--admin-pass",
        default="",
        help="Admin password for /api/ota when using flags. Use 'ota' to reuse the OTA password per device.",
    )
    args = parser.parse_args()

    devices, source = resolve_devices(args)
    if bool(args.location) == bool(args.devices):
        print("Specify exactly one of --location or one-or-more --device options.")
        return 1
    if not devices:
        print(f"No devices configured for source '{source or 'unspecified'}'.")
        return 1

    if not os.path.isfile(args.fw):
        print(f"Firmware not found: {args.fw}")
        return 1
    if not os.path.isfile(args.espota):
        print(f"espota.py not found: {args.espota}")
        return 1

    failures = 0
    require_http_flags = args.factory_reset or args.keep_wifi or args.keep_fleet
    if require_http_flags and not args.admin_pass:
        print("Flags requested but --admin-pass not provided.")
        return 1

    failed_devices: List[Dict[str, Any]] = []
    for dev in devices:
        try:
            plan = describe_device_plan(dev, args)
        except ValueError as exc:
            host = str(dev.get("host", "")).strip()
            ip = str(dev.get("ip", host)).strip()
            print(f"SKIP {host} ({ip}): {exc}")
            failures += 1
            continue

        print_device_plan(plan)
        if args.dry_run:
            continue
        try:
            execute_device_plan(plan, args)
            print(f"OK  {plan['ota_mode']} {plan['host']} {plan['ip']}")
        except subprocess.CalledProcessError as exc:
            print(f"FAIL {plan['host']} {plan['ip']}: {exc}")
            failed_devices.append(dev)
        except Exception as exc:
            print(f"FAIL {plan['host']} {plan['ip']}: {exc}")
            failed_devices.append(dev)

    for retry_round in range(1, 4):
        if not failed_devices or args.dry_run:
            break
        retry_batch = failed_devices
        failed_devices = []
        print(
            f"Retry round {retry_round}/3 for {len(retry_batch)} failed "
            f"{'device' if len(retry_batch) == 1 else 'devices'}..."
        )
        for dev in retry_batch:
            try:
                plan = describe_device_plan(dev, args)
            except ValueError as exc:
                host = str(dev.get("host", "")).strip()
                ip = str(dev.get("ip", host)).strip()
                print(f"SKIP {host} ({ip}): {exc}")
                failures += 1
                continue
            print_device_plan(plan, attempt_label=f"Retry {retry_round}/3")
            try:
                execute_device_plan(plan, args)
                print(f"OK  {plan['ota_mode']} {plan['host']} {plan['ip']}")
            except subprocess.CalledProcessError as exc:
                print(f"FAIL {plan['host']} {plan['ip']}: {exc}")
                failed_devices.append(dev)
            except Exception as exc:
                print(f"FAIL {plan['host']} {plan['ip']}: {exc}")
                failed_devices.append(dev)

    failures += len(failed_devices)
    if failed_devices:
        print("Giving up on the following devices after 3 retry rounds:")
        for dev in failed_devices:
            host = str(dev.get("host", "")).strip()
            ip = str(dev.get("ip", host)).strip()
            print(f" - {host} {ip}")

    return 0 if failures == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
