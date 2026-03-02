#!/usr/bin/env python3
import argparse
import csv
import datetime as dt
import hashlib
import re
import subprocess
import sys
from pathlib import Path

PRODUCT_SECRET = "LRS-v1-rotate-this-secret"
DEFAULT_DEPLOYMENT_KEY = "lora-default-passphrase"
ADJ = [
    "amber", "brisk", "clear", "delta", "eager", "frost", "grand", "harbor",
    "ivory", "jolly", "keen", "lunar", "maple", "noble", "opal", "proud",
    "quiet", "rapid", "solar", "tidal", "urban", "vivid", "wild", "young",
]
NOUN = [
    "anchor", "bridge", "canyon", "diesel", "engine", "field", "geyser",
    "harvest", "island", "junction", "kernel", "lantern", "meadow",
    "network", "orchard", "pump", "quarry", "relay", "switch", "tank",
    "uplink", "valve", "water", "yard",
]
TAIL = [
    "alpha", "beacon", "cobalt", "drift", "ember", "forge", "grove", "horizon",
    "ion", "jet", "kiln", "leaf", "mesa", "nova", "orbit", "pulse",
    "quest", "ridge", "stone", "trail", "unity", "vista", "wave", "zen",
]


def run(cmd):
    return subprocess.check_output(cmd, text=True, stderr=subprocess.STDOUT)


def read_repo_version(default: str = "0.0.0-dev") -> str:
    try:
        raw = (Path(__file__).resolve().parents[1] / "VERSION").read_text(encoding="utf-8").strip()
        if not raw:
            return default
        return raw[1:] if raw.startswith("v") else raw
    except Exception:
        return default


def derive_password(chip_hex: str) -> str:
    digest = hashlib.sha256(f"{PRODUCT_SECRET}:{chip_hex}".encode()).hexdigest().upper()
    return digest[:12]


def serial_for(chip_hex: str) -> str:
    today = dt.date.today()
    yy = today.year % 100
    ww = int(today.strftime("%W")) + 1
    return f"LRS{yy:02d}{ww:02d}-{chip_hex}"


def parse_chip_id(output: str) -> str:
    m = re.search(r"Chip ID:\s*0x([0-9a-fA-F]+)", output)
    if not m:
        raise ValueError("Could not parse chip id")
    return m.group(1).upper().zfill(8)


def parse_mac(output: str) -> str:
    m = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", output)
    if not m:
        raise ValueError("Could not parse MAC")
    return m.group(1).upper()


def derive_addresses(chip_id_int: int):
    local_addr = (chip_id_int & 0xFF) % 254 + 1
    remote_addr = ((chip_id_int >> 8) & 0xFF) % 254 + 1
    if remote_addr == local_addr:
        remote_addr = (local_addr % 254) + 1
    return local_addr, remote_addr


def three_word_key(seed_text: str) -> str:
    digest = hashlib.sha256(seed_text.encode()).digest()
    a = ADJ[digest[0] % len(ADJ)]
    b = NOUN[digest[1] % len(NOUN)]
    c = TAIL[digest[2] % len(TAIL)]
    return f"{a}-{b}-{c}"


def main():
    ap = argparse.ArgumentParser(description="Flash firmware and output sticker metadata")
    ap.add_argument("--port", required=True)
    ap.add_argument("--env", choices=["lrs_za", "lrs_us", "lrs_eu"], default="lrs_za")
    ap.add_argument("--flash", action="store_true", help="Run pio upload before reading IDs")
    ap.add_argument("--csv", default="factory_sticker.csv")
    ap.add_argument("--batch-id", default=dt.date.today().strftime("%y%m%d"), help="Factory batch/run id used in suggested deployment key seed")
    ap.add_argument("--fleet-key", default="", help="Optional explicit fleet key (shared across a batch); otherwise generated as readable three-word key")
    args = ap.parse_args()

    root = Path(__file__).resolve().parents[1]

    if args.flash:
      print("Flashing firmware...")
      subprocess.check_call([
          "pio", "run", "-e", args.env, "-t", "upload", "--upload-port", args.port
      ], cwd=root)

    chip_out = run([sys.executable, "-m", "esptool", "--port", args.port, "chip_id"])
    mac_out = run([sys.executable, "-m", "esptool", "--port", args.port, "read_mac"])

    chip = parse_chip_id(chip_out)
    mac = parse_mac(mac_out)
    chip_int = int(chip, 16)
    local_addr, remote_addr = derive_addresses(chip_int)
    password = derive_password(chip)
    role = "tx"
    deployment_key = args.fleet_key.strip() or three_word_key(f"{args.batch_id}:{args.env}:{chip}")

    record = {
        "firmware_version": read_repo_version(),
        "serial": serial_for(chip),
        "chip_id": chip,
        "mac": mac,
        "factory_role": role,
        "factory_local_address": local_addr,
        "factory_remote_address": remote_addr,
        "factory_ap_ssid": f"lrs-{chip}",
        "factory_ap_password": password,
        "factory_admin_password": password,
        "deployment_key": deployment_key,
        "deployment_key_is_default": str(deployment_key == DEFAULT_DEPLOYMENT_KEY).lower(),
        "batch_id": args.batch_id,
        "region_env": args.env,
        "date": dt.date.today().isoformat(),
    }

    print("Sticker metadata:")
    for k, v in record.items():
        print(f"  {k}: {v}")

    csv_path = Path(args.csv)
    write_header = not csv_path.exists()
    with csv_path.open("a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(record.keys()))
        if write_header:
            writer.writeheader()
        writer.writerow(record)

    print(f"Appended to {csv_path}")


if __name__ == "__main__":
    main()
