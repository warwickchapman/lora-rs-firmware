#!/usr/bin/env python3
import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

PRODUCT_SECRET = "LRS-v1-rotate-this-secret"


def _run_esptool(*args: str) -> str:
    return subprocess.check_output(
        [sys.executable, "-m", "esptool", *args],
        text=True,
        stderr=subprocess.STDOUT,
    )


def _parse_chip_id(text: str) -> str:
    m = re.search(r"Chip ID:\s*0x([0-9A-Fa-f]+)", text)
    if not m:
        raise RuntimeError("Unable to parse Chip ID from esptool output")
    return m.group(1).lower().zfill(8)


def _parse_mac(text: str) -> str:
    m = re.search(r"MAC:\s*([0-9A-Fa-f:]{17})", text)
    if not m:
        raise RuntimeError("Unable to parse MAC from esptool output")
    return m.group(1).lower()


def _derive_password(chip_hex: str) -> str:
    # Must stay aligned with src/config_store.cpp deriveShortPassword().
    digest = hashlib.sha256(f"{PRODUCT_SECRET}:{chip_hex}".encode()).hexdigest().lower()
    return digest[:8]


def _print_identity(port: str) -> tuple[str, str, str]:
    chip_out = _run_esptool("--port", port, "chip_id")
    mac_out = _run_esptool("--port", port, "read_mac")

    chip_hex = _parse_chip_id(chip_out)
    mac = _parse_mac(mac_out)
    password = _derive_password(chip_hex)

    print("\n=== LRS Device Identity ===")
    print(f"port: {port}")
    print(f"chip_id: {chip_hex}")
    print(f"mac: {mac}")
    print(f"ap_ssid: lrs-{chip_hex}")
    print(f"ap_password: {password}")
    print(f"admin_password: {password}")
    print("===========================\n")

    return chip_hex, mac, password


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Read ESP8266 chip identity, flash release firmware, and print derived LRS passwords."
    )
    ap.add_argument("--port", required=True, help="Serial port (example: COM7 or /dev/cu.usbserial-XXXX)")
    ap.add_argument("--bin", dest="firmware", required=True, help="Path to firmware .bin")
    ap.add_argument("--baud", type=int, default=460800, help="Flash baud rate (default: 460800)")
    ap.add_argument("--addr", default="0x00000", help="Flash address (default: 0x00000)")
    ap.add_argument("--no-flash", action="store_true", help="Only print identity/password; do not flash")
    args = ap.parse_args()

    fw = Path(args.firmware)
    if not fw.exists():
        print(f"Firmware file not found: {fw}", file=sys.stderr)
        return 2

    _print_identity(args.port)

    if args.no_flash:
        return 0

    print("Flashing firmware...")
    _run_esptool(
        "--port",
        args.port,
        "--baud",
        str(args.baud),
        "write_flash",
        args.addr,
        str(fw),
    )
    print("Flash complete.")
    _print_identity(args.port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
