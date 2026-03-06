#!/usr/bin/env python3
import argparse
import base64
import hashlib
import http.client
import os
import subprocess
import sys
from typing import Dict, List

PRODUCT_SECRET = "LRS-v1-rotate-this-secret"
DEFAULT_ESPOTA = "/Users/warwick/.platformio/packages/framework-arduinoespressif8266/tools/espota.py"
DEFAULT_FW = "/Users/warwick/Code/LoRa/lora_rs/.pio/build/lrs_za/firmware.bin"

# Edit these lists as needed.
LOCATIONS: Dict[str, List[Dict[str, str]]] = {
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
        # Populate with the subset you take home.
        # {"host": "lrs-00fc4f9c", "ip": "192.168.0.170"},
    ],
}


def chip_id_from_host(host: str) -> str:
    # host format: lrs-<hex> or lrs_<hex>
    parts = host.replace("_", "-").split("-")
    if len(parts) < 2:
        raise ValueError(f"Invalid host format: {host}")
    chip = parts[-1].lower().strip()
    if not all(c in "0123456789abcdef" for c in chip):
        raise ValueError(f"Invalid chip id in host: {host}")
    return chip.zfill(8)


def ota_password(chip_id_hex: str) -> str:
    payload = f"{PRODUCT_SECRET}:{chip_id_hex}"
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()[:8]


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


def run_ota_http(ip: str, admin_pass: str, firmware: str, flags: Dict[str, str]) -> None:
    with open(firmware, "rb") as f:
        fw_bytes = f.read()
    content_type, body = build_multipart(
        flags, "firmware", os.path.basename(firmware), fw_bytes
    )
    conn = http.client.HTTPConnection(ip, 80, timeout=30)
    auth = ("admin:" + admin_pass).encode("utf-8")
    headers = {
        "Content-Type": content_type,
        "Content-Length": str(len(body)),
        "Authorization": "Basic " + base64.b64encode(auth).decode("utf-8"),
    }
    conn.request("POST", "/api/ota", body=body, headers=headers)
    resp = conn.getresponse()
    if resp.status < 200 or resp.status >= 300:
        raise RuntimeError(f"HTTP OTA failed: {resp.status} {resp.reason}")
    resp.read()
    conn.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Batch OTA uploader")
    parser.add_argument("--location", required=True, choices=sorted(LOCATIONS.keys()))
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
        help="Admin password for /api/ota when using flags",
    )
    args = parser.parse_args()

    devices = LOCATIONS.get(args.location, [])
    if not devices:
        print(f"No devices configured for location '{args.location}'.")
        return 1

    if not os.path.isfile(args.fw):
        print(f"Firmware not found: {args.fw}")
        return 1
    if not os.path.isfile(args.espota):
        print(f"espota.py not found: {args.espota}")
        return 1

    failures = 0
    use_http = args.factory_reset or args.keep_wifi or args.keep_fleet
    if use_http and not args.admin_pass:
        print("Flags requested but --admin-pass not provided.")
        return 1

    for dev in devices:
        host = dev["host"]
        ip = dev["ip"]
        try:
            chip = chip_id_from_host(host)
            pw = ota_password(chip)
        except ValueError as exc:
            print(f"SKIP {host} ({ip}): {exc}")
            failures += 1
            continue

        print(f"OTA {host} {ip} chip={chip} pw={pw}")
        if args.dry_run:
            continue
        try:
            if use_http:
                flags = {
                    "factory_reset_after_update": "1" if args.factory_reset else "0",
                    "keep_wifi_credentials_after_update": "1" if args.keep_wifi else "0",
                    "keep_shared_fleet_key_after_update": "1" if args.keep_fleet else "0",
                }
                run_ota_http(ip, args.admin_pass, args.fw, flags)
            else:
                run_ota(args.espota, ip, pw, args.fw, args.port)
            print(f"OK  {host} {ip}")
        except subprocess.CalledProcessError as exc:
            print(f"FAIL {host} {ip}: {exc}")
            failures += 1
        except Exception as exc:
            print(f"FAIL {host} {ip}: {exc}")
            failures += 1

    return 0 if failures == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
