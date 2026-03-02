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
import io
from contextlib import redirect_stdout, redirect_stderr
import threading
import serial
from typing import Optional, Union, List, Dict
# We use subprocess to call standalone esptool binaries to avoid PyInstaller recursion bugs

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
    def _get_esptool_path(self):
        """Determine the path to the bundled or system esptool binary."""
        # 1. Check for bundled binary in _MEIPASS (PyInstaller)
        meipass = getattr(sys, "_MEIPASS", None)
        if meipass:
            # We bundle as 'bin/esptool' (mac/linux) or 'bin/esptool.exe' (win)
            binary = "esptool.exe" if os.name == "nt" else "esptool"
            bundled_path = os.path.join(meipass, "bin", binary)
            if os.path.exists(bundled_path):
                return bundled_path
        
        # 2. Fallback to system path (development)
        return "esptool.py" if os.name != "nt" else "esptool.exe"

    def run_esptool(self, args):
        esptool_bin = self._get_esptool_path()
        full_args = [esptool_bin, "--before", "default_reset", "--after", "hard_reset"] + args
        
        try:
            # Use subprocess to avoid the recursion bug in PyInstaller's importer
            result = subprocess.run(
                full_args,
                capture_output=True,
                text=True,
                check=False
            )
            output = result.stdout + "\n" + result.stderr
            if result.returncode != 0:
                raise RuntimeError(f"esptool error {result.returncode}:\n{output}")
            return output
        except FileNotFoundError:
            raise RuntimeError(f"esptool binary not found: {esptool_bin}")
        except Exception as e:
            raise RuntimeError(f"esptool exception: {str(e)}")

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
        esptool_bin = self._get_esptool_path()
        args = [esptool_bin, "--port", port, "--baud", str(baud), "write_flash", "0x0", firmware_path]
        
        try:
            # Forward output to callback in real-time
            process = subprocess.Popen(
                args,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                universal_newlines=True
            )
            
            for line in process.stdout:
                if callback:
                    callback(line)
            
            process.wait()
            if process.returncode != 0:
                raise RuntimeError("Flash failed")
                
        except Exception as e:
            raise RuntimeError(f"Flash error: {e}")

class SerialMonitor:
    def __init__(self, port, baud=115200, callback=None):
        self.port = port
        self.baud = baud
        self.callback = callback
        self.running = False
        self._thread: Optional[threading.Thread] = None
        self._serial: Optional[serial.Serial] = None

    def start(self):
        if self.running: return
        self.running = True
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        self.running = False
        ser = self._serial
        if ser:
            try: 
                self._serial = None
                ser.close()
            except: pass

    def _run(self):
        try:
            self._serial = serial.Serial(self.port, self.baud, timeout=0.1)
            ser = self._serial
            while self.running and ser:
                if ser.in_waiting:
                    line = ser.readline().decode('utf-8', errors='replace')
                    if self.callback and line:
                        self.callback(line)
        except Exception as e:
            if self.callback:
                self.callback(f"Monitor Error: {e}")
        finally:
            self.stop()

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
