import datetime as dt
import hashlib
import re
import subprocess
import sys

Import("env")

PRODUCT_SECRET = "LRS-v1-rotate-this-secret"


def _derive_password(chip_hex: str) -> str:
    digest = hashlib.sha256(f"{PRODUCT_SECRET}:{chip_hex}".encode()).hexdigest().lower()
    # ESP8266 SoftAP WPA2 requires at least 8 chars.
    return digest[:8]


def _serial_for(chip_hex: str) -> str:
    today = dt.date.today()
    yy = today.year % 100
    ww = int(today.strftime("%W")) + 1
    return f"lrs{yy:02d}{ww:02d}-{chip_hex}"


def _derive_addresses(chip_hex: str):
    chip = int(chip_hex, 16)
    local_addr = (chip & 0xFF) % 254 + 1
    remote_addr = ((chip >> 8) & 0xFF) % 254 + 1
    if remote_addr == local_addr:
        remote_addr = (local_addr % 254) + 1
    return local_addr, remote_addr


def _run(*args) -> str:
    return subprocess.check_output([sys.executable, "-m", "esptool", *args], text=True, stderr=subprocess.STDOUT)


def _looks_like_serial_port(port: str) -> bool:
    p = (port or "").strip().lower()
    if p.startswith("/dev/"):
        return True
    if re.fullmatch(r"com\d+", p):
        return True
    return False


def _parse_chip_id(output: str) -> str:
    match = re.search(r"Chip ID:\s*0x([0-9A-Fa-f]+)", output)
    if not match:
        raise RuntimeError("Unable to parse chip ID from esptool output")
    return match.group(1).lower().zfill(8)


def _parse_mac(output: str) -> str:
    match = re.search(r"MAC:\s*([0-9A-Fa-f:]{17})", output)
    if not match:
        raise RuntimeError("Unable to parse MAC from esptool output")
    return match.group(1).lower()


def _print_sticker_values(port: str):
    chip_out = _run("--port", port, "chip_id")
    mac_out = _run("--port", port, "read_mac")

    chip = _parse_chip_id(chip_out)
    mac = _parse_mac(mac_out)
    role = "tx"
    ssid = f"lrs-{chip}"
    password = _derive_password(chip)
    local_addr, remote_addr = _derive_addresses(chip)

    print("\n=== LRS Sticker Values ===")
    print(f"serial: { _serial_for(chip) }")
    print(f"chip_id: {chip}")
    print(f"mac: {mac}")
    print(f"factory_role: {role}")
    print(f"factory_local_address: {local_addr}")
    print(f"factory_remote_address: {remote_addr}")
    print(f"factory_ap_ssid: {ssid}")
    print(f"factory_ap_password: {password}")
    print(f"factory_admin_password: {password}")
    print("==========================\n")


def _post_upload(source, target, env, **kwargs):
    upload_port = env.subst("$UPLOAD_PORT")
    if not upload_port:
        print("[sticker] Upload port not set, skipping sticker output")
        return

    upload_protocol = env.subst("$UPLOAD_PROTOCOL").strip().lower()
    if upload_protocol == "espota" or not _looks_like_serial_port(upload_port):
        print("[sticker] OTA/non-serial upload detected; skipping esptool sticker read (serial-only)")
        print("[sticker] Use Flasher over USB serial to read current device identity/settings")
        return

    try:
        _print_sticker_values(upload_port)
    except Exception as exc:
        print(f"[sticker] Failed to read sticker values: {exc}")


# Ensure callback runs for normal firmware upload task in PlatformIO UI/CLI.
env.AddPostAction("upload", _post_upload)
