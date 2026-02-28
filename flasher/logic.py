import hashlib
import datetime as dt
import re
import subprocess
import sys
import serial.tools.list_ports
import requests
import os
import pathlib
import json

PRODUCT_SECRET = "LRS-v1-rotate-this-secret"

def derive_password(chip_hex: str) -> str:
    """Derive the WPA2 password from the chip ID."""
    digest = hashlib.sha256(f"{PRODUCT_SECRET}:{chip_hex}".encode()).hexdigest().lower()
    return digest[:8]

def get_serial(chip_hex: str) -> str:
    """Generate the serial number based on date and chip ID."""
    today = dt.date.today()
    yy = today.year % 100
    ww = int(today.strftime("%W")) + 1
    return f"lrs{yy:02d}{ww:02d}-{chip_hex}"

def derive_addresses(chip_hex: str):
    """Derive local and remote addresses from chip ID."""
    chip = int(chip_hex, 16)
    local_addr = (chip & 0xFF) % 254 + 1
    remote_addr = ((chip >> 8) & 0xFF) % 254 + 1
    if remote_addr == local_addr:
        remote_addr = (local_addr % 254) + 1
    return local_addr, remote_addr

def list_serial_ports():
    """List all available serial ports with descriptions."""
    ports = serial.tools.list_ports.comports()
    sorted_ports = sorted(ports, key=lambda p: ("usb" in p.description.lower() or "usb" in p.device.lower()), reverse=True)
    return [{"device": p.device, "description": p.description} for p in sorted_ports]

def parse_chip_id(output: str) -> str:
    match = re.search(r"Chip ID:\s*0x([0-9A-Fa-f]+)", output)
    if not match:
        raise RuntimeError("Unable to parse chip ID from esptool output")
    return match.group(1).lower().zfill(8)

def parse_mac(output: str) -> str:
    match = re.search(r"MAC:\s*([0-9A-Fa-f:]{17})", output)
    if not match:
        raise RuntimeError("Unable to parse MAC from esptool output")
    return match.group(1).lower()

class FlasherLogic:
    def __init__(self, python_path=sys.executable):
        self.python_path = python_path

    def run_esptool(self, args):
        # Add reset strategy for more reliable syncing
        cmd = [self.python_path, "-m", "esptool", "--before", "default_reset", "--after", "hard_reset"] + args
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, check=True)
            return result.stdout
        except subprocess.CalledProcessError as e:
            error_msg = e.stderr if e.stderr else e.stdout
            raise RuntimeError(f"esptool error: {error_msg.strip()}")

    def get_chip_info(self, port):
        chip_out = self.run_esptool(["--port", port, "chip_id"])
        mac_out = self.run_esptool(["--port", port, "read_mac"])
        chip_id = parse_chip_id(chip_out)
        mac = parse_mac(mac_out)
        return {
            "chip_id": chip_id,
            "mac": mac,
            "serial": get_serial(chip_id),
            "password": derive_password(chip_id),
            "local_addr": derive_addresses(chip_id)[0],
            "remote_addr": derive_addresses(chip_id)[1],
            "ssid": f"lrs-{chip_id}"
        }

    def flash_firmware(self, port, baud, firmware_path, callback=None):
        args = ["--port", port, "--baud", str(baud), "write_flash", "0x0", firmware_path]
        process = subprocess.Popen(
            [self.python_path, "-m", "esptool"] + args,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
        )
        for line in process.stdout:
            if callback: callback(line)
        process.wait()
        if process.returncode != 0:
            raise subprocess.CalledProcessError(process.returncode, process.args)

class FirmwareManager:
    def __init__(self, cache_dir="firmware_cache"):
        # IMPORTANT: Pointing to the PUBLIC firmware proxy repository
        # This allows anonymous client access while keeping lora-rs private.
        self.repo = "warwickchapman/lora-rs-firmware"
        self.api_url = f"https://api.github.com/repos/{self.repo}/releases"
        self.cache_dir = pathlib.Path(os.path.dirname(__file__)).absolute() / cache_dir
        self.cache_dir.mkdir(exist_ok=True)

    def get_available_firmwares(self):
        """Fetch releases from the public proxy repo anonymously."""
        try:
            response = requests.get(self.api_url, timeout=5)
            response.raise_for_status()
            releases = response.json()
            return self._parse_releases(releases)
        except Exception as e:
            print(f"Firmware Fetch Error: {e}")
            return []

    def _parse_releases(self, releases):
        firmwares = []
        for rel in releases:
            tag = rel.get("tag_name")
            for asset in rel.get("assets", []):
                if asset["name"].endswith(".bin"):
                    firmwares.append({
                        "name": f"{tag} - {asset['name']}",
                        "tag": tag,
                        "url": asset["browser_download_url"],
                        "filename": asset["name"],
                        "type": "cloud"
                    })
        return firmwares

    def download_firmware(self, url, filename):
        target_path = self.cache_dir / filename
        if target_path.exists():
            return str(target_path)
            
        try:
            # Standard anonymous download for public assets
            response = requests.get(url, stream=True)
            response.raise_for_status()
            with open(target_path, 'wb') as f:
                for chunk in response.iter_content(chunk_size=8192):
                    f.write(chunk)
            return str(target_path)
        except Exception as e:
            raise RuntimeError(f"Download failed: {e}")
