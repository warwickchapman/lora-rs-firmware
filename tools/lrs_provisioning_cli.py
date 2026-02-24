#!/usr/bin/env python3
"""
API-first provisioning helper for a USB-powered LRS coordinator.

This script talks to the device HTTP API (typically http://192.168.4.1) and can:
- log in (optionally deriving the default admin password from USB chip ID)
- set a fleet/deployment key on first login
- configure coordinator settings (role, addresses, WiFi)
- run LoRa provisioning discovery + provision-all
- optionally broadcast WiFi credentials to the fleet over LoRa

It is intentionally stdlib-only.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from http.cookiejar import CookieJar
from pathlib import Path
from typing import Any, Dict, Iterable, Optional


SCRIPT_DIR = Path(__file__).resolve().parent
try:
    from factory_provision import derive_password as derive_default_admin_password
    from factory_provision import parse_chip_id as parse_esptool_chip_id
except Exception:  # pragma: no cover - fallback for unexpected import issues
    derive_default_admin_password = None
    parse_esptool_chip_id = None


class ApiError(RuntimeError):
    def __init__(self, method: str, path: str, status: int, body_text: str, body_json: Any = None):
        super().__init__(f"{method} {path} failed: HTTP {status}")
        self.method = method
        self.path = path
        self.status = status
        self.body_text = body_text
        self.body_json = body_json


@dataclass
class ProvisioningConfig:
    host: str = "192.168.4.1"
    scheme: str = "http"
    admin_password: Optional[str] = None
    chip_id: Optional[str] = None
    usb_port: Optional[str] = None
    fleet_key: Optional[str] = None
    role: Optional[str] = "tx"
    local_address: Optional[int] = None
    remote_address: Optional[int] = None
    wifi_sta_ssid: Optional[str] = None
    wifi_sta_password: Optional[str] = None
    ap_always_on: Optional[bool] = None
    estimated_count: int = 10
    retry_once: bool = True
    discovery_timeout_s: int = 120
    provision_timeout_s: int = 300
    poll_interval_s: float = 1.5
    push_fleet_wifi: bool = False
    fleet_wifi_ssid: Optional[str] = None
    fleet_wifi_password: Optional[str] = None


def eprint(*args: Any) -> None:
    print(*args, file=sys.stderr)


def load_profile(path: Path) -> Dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        raw = json.load(f)
    if not isinstance(raw, dict):
        raise ValueError("profile root must be a JSON object")
    return raw


def bool_or_none(v: Any) -> Optional[bool]:
    if v is None:
        return None
    if isinstance(v, bool):
        return v
    if isinstance(v, (int, float)):
        return bool(v)
    s = str(v).strip().lower()
    if s in {"1", "true", "yes", "y", "on"}:
        return True
    if s in {"0", "false", "no", "n", "off"}:
        return False
    raise ValueError(f"invalid boolean value: {v!r}")


def merge_config(args: argparse.Namespace) -> ProvisioningConfig:
    cfg = ProvisioningConfig()
    if args.profile:
        raw = load_profile(Path(args.profile))
        coord = raw.get("coordinator", {}) if isinstance(raw.get("coordinator"), dict) else {}
        prov = raw.get("provisioning", {}) if isinstance(raw.get("provisioning"), dict) else {}
        fwifi = raw.get("fleet_wifi", {}) if isinstance(raw.get("fleet_wifi"), dict) else {}
        cfg.host = str(raw.get("host", cfg.host))
        cfg.scheme = str(raw.get("scheme", cfg.scheme))
        cfg.admin_password = raw.get("admin_password") or cfg.admin_password
        cfg.chip_id = raw.get("chip_id") or cfg.chip_id
        cfg.usb_port = raw.get("usb_port") or cfg.usb_port
        cfg.fleet_key = raw.get("fleet_key") or cfg.fleet_key
        cfg.role = coord.get("role", cfg.role)
        cfg.local_address = coord.get("local_address", cfg.local_address)
        cfg.remote_address = coord.get("remote_address", cfg.remote_address)
        cfg.wifi_sta_ssid = coord.get("wifi_sta_ssid", cfg.wifi_sta_ssid)
        cfg.wifi_sta_password = coord.get("wifi_sta_password", cfg.wifi_sta_password)
        cfg.ap_always_on = bool_or_none(coord.get("ap_always_on", cfg.ap_always_on))
        cfg.estimated_count = int(prov.get("estimated_count", cfg.estimated_count))
        cfg.retry_once = bool_or_none(prov.get("retry_once", cfg.retry_once))
        cfg.discovery_timeout_s = int(prov.get("discovery_timeout_s", cfg.discovery_timeout_s))
        cfg.provision_timeout_s = int(prov.get("provision_timeout_s", cfg.provision_timeout_s))
        cfg.poll_interval_s = float(prov.get("poll_interval_s", cfg.poll_interval_s))
        cfg.push_fleet_wifi = bool_or_none(fwifi.get("enabled", cfg.push_fleet_wifi))
        cfg.fleet_wifi_ssid = fwifi.get("wifi_sta_ssid", cfg.fleet_wifi_ssid)
        cfg.fleet_wifi_password = fwifi.get("wifi_sta_password", cfg.fleet_wifi_password)

    # CLI overrides
    for attr in (
        "host",
        "scheme",
        "admin_password",
        "chip_id",
        "usb_port",
        "fleet_key",
        "role",
        "local_address",
        "remote_address",
        "wifi_sta_ssid",
        "wifi_sta_password",
        "fleet_wifi_ssid",
        "fleet_wifi_password",
    ):
        value = getattr(args, attr, None)
        if value is not None:
            setattr(cfg, attr, value)

    if getattr(args, "ap_always_on", None) is not None:
        cfg.ap_always_on = args.ap_always_on
    if getattr(args, "estimated_count", None) is not None:
        cfg.estimated_count = int(args.estimated_count)
    if getattr(args, "retry_once", None) is not None:
        cfg.retry_once = bool(args.retry_once)
    if getattr(args, "discovery_timeout_s", None) is not None:
        cfg.discovery_timeout_s = int(args.discovery_timeout_s)
    if getattr(args, "provision_timeout_s", None) is not None:
        cfg.provision_timeout_s = int(args.provision_timeout_s)
    if getattr(args, "poll_interval_s", None) is not None:
        cfg.poll_interval_s = float(args.poll_interval_s)
    if getattr(args, "push_fleet_wifi", None) is not None:
        cfg.push_fleet_wifi = bool(args.push_fleet_wifi)

    if cfg.retry_once is None:
        cfg.retry_once = True
    if cfg.push_fleet_wifi is None:
        cfg.push_fleet_wifi = False
    return cfg


def maybe_read_chip_id_via_usb(usb_port: str) -> str:
    if parse_esptool_chip_id is None:
        raise RuntimeError("factory_provision helpers unavailable; cannot derive chip ID from USB")
    cmd = [sys.executable, "-m", "esptool", "--port", usb_port, "chip_id"]
    out = subprocess.check_output(cmd, text=True, stderr=subprocess.STDOUT)
    return parse_esptool_chip_id(out)


def ensure_admin_password(cfg: ProvisioningConfig) -> str:
    if cfg.admin_password:
        return cfg.admin_password
    chip_id = cfg.chip_id
    if not chip_id and cfg.usb_port:
        chip_id = maybe_read_chip_id_via_usb(cfg.usb_port)
        print(f"Derived chip_id from USB port {cfg.usb_port}: {chip_id}")
    if not chip_id:
        raise RuntimeError("No admin password provided. Use --admin-password, --chip-id, or --usb-port.")
    if derive_default_admin_password is None:
        raise RuntimeError("factory_provision helpers unavailable; cannot derive default admin password")
    pwd = derive_default_admin_password(chip_id.upper())
    print(f"Derived default admin password from chip_id {chip_id.upper()}: {pwd}")
    return pwd


class LrsApiClient:
    def __init__(self, base_url: str, timeout_s: float = 8.0):
        self.base_url = base_url.rstrip("/")
        self.timeout_s = timeout_s
        self.cookies = CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.cookies))

    def _request(
        self,
        method: str,
        path: str,
        payload: Optional[Dict[str, Any]] = None,
        timeout_s: Optional[float] = None,
    ) -> tuple[int, str, Any]:
        url = self.base_url + path
        data = None
        headers = {"Cache-Control": "no-store"}
        if payload is not None:
            data = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with self.opener.open(req, timeout=timeout_s or self.timeout_s) as resp:
                body_bytes = resp.read()
                status = int(resp.getcode())
        except urllib.error.HTTPError as exc:
            body_bytes = exc.read()
            status = int(exc.code)
            body_text = body_bytes.decode("utf-8", errors="replace")
            body_json = self._try_json(body_text)
            raise ApiError(method, path, status, body_text, body_json)
        body_text = body_bytes.decode("utf-8", errors="replace")
        body_json = self._try_json(body_text)
        return status, body_text, body_json

    @staticmethod
    def _try_json(body_text: str) -> Any:
        body_text = body_text.strip()
        if not body_text:
            return None
        if not (body_text.startswith("{") or body_text.startswith("[")):
            return None
        try:
            return json.loads(body_text)
        except json.JSONDecodeError:
            return None

    def post_json(self, path: str, payload: Dict[str, Any], timeout_s: Optional[float] = None) -> Any:
        _, _, body_json = self._request("POST", path, payload=payload, timeout_s=timeout_s)
        return body_json

    def get_json(self, path: str, timeout_s: Optional[float] = None) -> Any:
        _, _, body_json = self._request("GET", path, payload=None, timeout_s=timeout_s)
        return body_json

    def post_text(self, path: str, payload: Dict[str, Any], timeout_s: Optional[float] = None) -> str:
        _, body_text, _ = self._request("POST", path, payload=payload, timeout_s=timeout_s)
        return body_text

    def login(self, admin_password: str) -> Dict[str, Any]:
        out = self.post_json("/api/login", {"password": admin_password})
        if not isinstance(out, dict):
            raise RuntimeError("Unexpected /api/login response")
        return out

    def set_fleet_key(self, fleet_key: str) -> Dict[str, Any]:
        out = self.post_json("/api/setup/fleet-key", {"fleet_passphrase": fleet_key})
        if not isinstance(out, dict):
            raise RuntimeError("Unexpected /api/setup/fleet-key response")
        return out

    def patch_settings(self, patch: Dict[str, Any]) -> str:
        return self.post_text("/api/settings", patch)

    def provisioning_start(self, estimated_count: int, retry_once: bool) -> Dict[str, Any]:
        out = self.post_json(
            "/api/provisioning/start",
            {"estimated_count": estimated_count, "retry_once": retry_once},
        )
        if not isinstance(out, dict):
            raise RuntimeError("Unexpected /api/provisioning/start response")
        return out

    def provisioning_status(self) -> Dict[str, Any]:
        out = self.get_json("/api/provisioning/status")
        if not isinstance(out, dict):
            raise RuntimeError("Unexpected /api/provisioning/status response")
        return out

    def provisioning_provision_all(self) -> Dict[str, Any]:
        out = self.post_json("/api/provisioning/provision-all", {})
        if not isinstance(out, dict):
            raise RuntimeError("Unexpected /api/provisioning/provision-all response")
        return out

    def provisioning_cancel(self) -> Dict[str, Any]:
        out = self.post_json("/api/provisioning/cancel", {})
        if not isinstance(out, dict):
            raise RuntimeError("Unexpected /api/provisioning/cancel response")
        return out

    def push_fleet_wifi(self, ssid: str, password: str) -> Dict[str, Any]:
        out = self.post_json(
            "/api/network/provision-fleet",
            {"wifi_sta_ssid": ssid, "wifi_sta_password": password},
        )
        if not isinstance(out, dict):
            raise RuntimeError("Unexpected /api/network/provision-fleet response")
        return out

    def udp_log_start(self, host: str, port: int, ttl_s: int) -> Dict[str, Any]:
        out = self.post_json(
            "/api/logging/udp",
            {"enabled": True, "host": host, "port": int(port), "ttl_s": int(ttl_s)},
        )
        if not isinstance(out, dict):
            raise RuntimeError("Unexpected /api/logging/udp start response")
        return out

    def udp_log_stop(self) -> Dict[str, Any]:
        out = self.post_json("/api/logging/udp", {"enabled": False})
        if not isinstance(out, dict):
            raise RuntimeError("Unexpected /api/logging/udp stop response")
        return out


def is_low_heap_api_error(err: ApiError) -> bool:
    return err.status == 503 and isinstance(err.body_json, dict) and err.body_json.get("error") == "low_heap"


def print_status_summary(status: Dict[str, Any]) -> None:
    sess = status.get("session", {}) if isinstance(status.get("session"), dict) else {}
    state = sess.get("state", "unknown")
    discovered = sess.get("discovered_count", 0)
    selected = sess.get("selected_count", 0)
    verified = sess.get("verified_count", 0)
    failed = sess.get("failed_count", 0)
    conflicts = sess.get("conflict_count", 0)
    total = sess.get("devices_total", 0)
    compact = bool(sess.get("compact", False))
    print(
        f"state={state} discovered={discovered} selected={selected} verified={verified} "
        f"failed={failed} conflicts={conflicts} devices_total={total}"
        + (" compact=1" if compact else "")
    )


def wait_for_state(
    client: LrsApiClient,
    target_states: Iterable[str],
    timeout_s: int,
    poll_interval_s: float,
    fail_states: Iterable[str] = ("error",),
) -> Dict[str, Any]:
    target = set(target_states)
    fail = set(fail_states)
    deadline = time.monotonic() + timeout_s
    last_state: Optional[str] = None
    low_heap_count = 0
    while time.monotonic() < deadline:
        try:
            status = client.provisioning_status()
        except ApiError as err:
            if is_low_heap_api_error(err):
                low_heap_count += 1
                if low_heap_count == 1 or low_heap_count % 10 == 0:
                    e = err.body_json or {}
                    print(
                        f"provisioning/status low_heap (count={low_heap_count}) "
                        f"heap_free={e.get('heap_free','?')} max_free_block={e.get('max_free_block','?')}; retrying..."
                    )
                time.sleep(poll_interval_s)
                continue
            raise

        sess = status.get("session", {}) if isinstance(status.get("session"), dict) else {}
        state = str(sess.get("state", "unknown"))
        if state != last_state:
            print_status_summary(status)
            last_state = state
        if state in fail:
            raise RuntimeError(f"Provisioning entered failure state: {state}")
        if state in target:
            return status
        time.sleep(poll_interval_s)
    raise TimeoutError(f"Timed out waiting for provisioning state {sorted(target)}")


def build_settings_patch(cfg: ProvisioningConfig) -> Dict[str, Any]:
    patch: Dict[str, Any] = {}
    if cfg.role is not None:
        role = str(cfg.role).strip().lower()
        if role not in {"tx", "rx"}:
            raise ValueError("role must be 'tx' or 'rx'")
        patch["role_tx"] = (role == "tx")
    if cfg.local_address is not None:
        patch["local_address"] = int(cfg.local_address)
    if cfg.remote_address is not None:
        patch["remote_address"] = int(cfg.remote_address)
    if cfg.wifi_sta_ssid is not None:
        patch["wifi_sta_ssid"] = str(cfg.wifi_sta_ssid)
    if cfg.wifi_sta_password is not None:
        patch["wifi_sta_password"] = str(cfg.wifi_sta_password)
    if cfg.ap_always_on is not None:
        patch["ap_always_on"] = bool(cfg.ap_always_on)
    return patch


def run_workflow(cfg: ProvisioningConfig) -> int:
    admin_password = ensure_admin_password(cfg)
    base_url = f"{cfg.scheme}://{cfg.host}"
    print(f"Connecting to {base_url}")
    client = LrsApiClient(base_url)

    login = client.login(admin_password)
    setup_required = bool(login.get("setup_required"))
    print(f"Login OK (setup_required={int(setup_required)})")

    if setup_required:
        if not cfg.fleet_key:
            raise RuntimeError("Fleet key setup is required but no fleet key was provided (--fleet-key or profile.fleet_key)")
        out = client.set_fleet_key(cfg.fleet_key)
        if not out.get("ok"):
            raise RuntimeError(f"Fleet key setup failed: {out}")
        print("Fleet key set (first-login setup)")

    patch = build_settings_patch(cfg)
    if patch:
        print("Applying coordinator settings patch:", patch)
        try:
            out = client.patch_settings(patch)
            print(f"/api/settings -> {out.strip()}")
        except ApiError as err:
            if is_low_heap_api_error(err):
                raise RuntimeError("Coordinator settings update rejected due to low heap; reboot and retry without UI connected") from err
            raise

    print(f"Starting provisioning discovery (estimated_count={cfg.estimated_count}, retry_once={int(cfg.retry_once)})")
    start_out = client.provisioning_start(cfg.estimated_count, bool(cfg.retry_once))
    if not start_out.get("ok"):
        raise RuntimeError(f"Provisioning start failed: {start_out}")

    status = wait_for_state(
        client,
        target_states=("ready", "complete"),
        timeout_s=cfg.discovery_timeout_s,
        poll_interval_s=cfg.poll_interval_s,
    )
    sess = status.get("session", {})
    state = str(sess.get("state", "unknown"))
    if state == "complete":
        print("Provisioning already complete (nothing to do).")
    else:
        print("Discovery complete, starting provision-all")
        out = client.provisioning_provision_all()
        if not out.get("ok"):
            raise RuntimeError(f"Provision-all start failed: {out}")
        status = wait_for_state(
            client,
            target_states=("complete",),
            timeout_s=cfg.provision_timeout_s,
            poll_interval_s=cfg.poll_interval_s,
        )

    print("Provisioning complete.")
    print_status_summary(status)
    devices = status.get("devices") if isinstance(status.get("devices"), list) else []
    if devices:
        print("Devices:")
        for d in devices:
            chip = d.get("chip_id_hex") or d.get("chip_id") or "?"
            cur = d.get("current_address", "?")
            new = d.get("assigned_address", "?")
            role = d.get("role", "?")
            st = d.get("state", "?")
            rssi = d.get("rssi", "?")
            conflict = " conflict" if d.get("address_conflict") else ""
            print(f"  {chip} {role} cur={cur} new={new} state={st} rssi={rssi}{conflict}")

    if cfg.push_fleet_wifi:
        ssid = cfg.fleet_wifi_ssid or cfg.wifi_sta_ssid
        password = cfg.fleet_wifi_password if cfg.fleet_wifi_password is not None else (cfg.wifi_sta_password or "")
        if not ssid:
            raise RuntimeError("Fleet WiFi push requested but no SSID provided")
        print(f"Pushing WiFi to fleet (ssid={ssid})")
        out = client.push_fleet_wifi(ssid, password)
        if not out.get("ok"):
            raise RuntimeError(f"Fleet WiFi push failed: {out}")
        packets = out.get("packets")
        print(f"Fleet WiFi provisioning queued (packets={packets})")

    return 0


def cmd_status(cfg: ProvisioningConfig) -> int:
    admin_password = ensure_admin_password(cfg)
    client = LrsApiClient(f"{cfg.scheme}://{cfg.host}")
    client.login(admin_password)
    out = client.provisioning_status()
    print(json.dumps(out, indent=2, sort_keys=True))
    return 0


def cmd_cancel(cfg: ProvisioningConfig) -> int:
    admin_password = ensure_admin_password(cfg)
    client = LrsApiClient(f"{cfg.scheme}://{cfg.host}")
    client.login(admin_password)
    out = client.provisioning_cancel()
    print(json.dumps(out, indent=2, sort_keys=True))
    return 0


def cmd_udp_log_start(cfg: ProvisioningConfig, host: str, port: int, ttl_s: int) -> int:
    admin_password = ensure_admin_password(cfg)
    client = LrsApiClient(f"{cfg.scheme}://{cfg.host}")
    client.login(admin_password)
    out = client.udp_log_start(host=host, port=port, ttl_s=ttl_s)
    print(json.dumps(out, indent=2, sort_keys=True))
    return 0


def cmd_udp_log_stop(cfg: ProvisioningConfig) -> int:
    admin_password = ensure_admin_password(cfg)
    client = LrsApiClient(f"{cfg.scheme}://{cfg.host}")
    client.login(admin_password)
    out = client.udp_log_stop()
    print(json.dumps(out, indent=2, sort_keys=True))
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Provision LoRa devices from one USB-powered LRS coordinator via HTTP API (no web UI required)."
    )
    sub = p.add_subparsers(dest="command", required=False)

    def add_common(sp: argparse.ArgumentParser) -> None:
        sp.add_argument("--profile", help="Path to JSON profile file (CLI args override profile values)")
        sp.add_argument("--host", help="Coordinator host/IP (default from profile or 192.168.4.1)")
        sp.add_argument("--scheme", choices=["http", "https"], help="URL scheme (default http)")
        sp.add_argument("--admin-password", dest="admin_password", help="Admin password for /api/login")
        sp.add_argument("--chip-id", dest="chip_id", help="Chip ID used to derive default admin password")
        sp.add_argument("--usb-port", dest="usb_port", help="USB serial port; derives chip ID via esptool (e.g. /dev/cu.usbserial-2120)")

    runp = sub.add_parser("run", help="Login, configure coordinator, run discovery + provision-all, optional fleet WiFi push")
    add_common(runp)
    runp.add_argument("--fleet-key", dest="fleet_key", help="Fleet/deployment key (required if first-login setup is pending)")
    runp.add_argument("--role", choices=["tx", "rx"], help="Coordinator role (usually tx)")
    runp.add_argument("--local-address", dest="local_address", type=int, help="Coordinator local LoRa address (1-254)")
    runp.add_argument("--remote-address", dest="remote_address", type=int, help="Coordinator remote LoRa address (1-254)")
    runp.add_argument("--wifi-ssid", dest="wifi_sta_ssid", help="Coordinator STA WiFi SSID")
    runp.add_argument("--wifi-password", dest="wifi_sta_password", help="Coordinator STA WiFi password")
    runp.add_argument("--ap-always-on", dest="ap_always_on", action="store_true", default=None, help="Keep AP always on")
    runp.add_argument("--no-ap-always-on", dest="ap_always_on", action="store_false", help="Disable always-on AP")
    runp.add_argument("--estimated-count", type=int, help="Expected device count for provisioning discovery")
    retry = runp.add_mutually_exclusive_group()
    retry.add_argument("--retry-once", dest="retry_once", action="store_true", default=None, help="Enable one discovery retry")
    retry.add_argument("--no-retry-once", dest="retry_once", action="store_false", help="Disable discovery retry")
    runp.add_argument("--discovery-timeout-s", type=int, help="Timeout for discovery phase")
    runp.add_argument("--provision-timeout-s", type=int, help="Timeout for provision-all phase")
    runp.add_argument("--poll-interval-s", type=float, help="Polling interval for provisioning status")
    fw = runp.add_mutually_exclusive_group()
    fw.add_argument("--push-fleet-wifi", dest="push_fleet_wifi", action="store_true", default=None, help="Broadcast WiFi credentials to fleet after provisioning")
    fw.add_argument("--no-push-fleet-wifi", dest="push_fleet_wifi", action="store_false", help="Do not broadcast fleet WiFi")
    runp.add_argument("--fleet-wifi-ssid", dest="fleet_wifi_ssid", help="WiFi SSID to broadcast to fleet (defaults to --wifi-ssid)")
    runp.add_argument("--fleet-wifi-password", dest="fleet_wifi_password", help="WiFi password to broadcast to fleet (defaults to --wifi-password)")

    statusp = sub.add_parser("status", help="Print provisioning session status JSON")
    add_common(statusp)

    cancelp = sub.add_parser("cancel", help="Cancel the current provisioning session")
    add_common(cancelp)

    udpp = sub.add_parser("udp-log-start", help="Enable UDP log mirroring for a limited time (bench debugging)")
    add_common(udpp)
    udpp.add_argument("--udp-host", required=True, help="Destination host/IP for UDP logs (e.g. 192.168.4.2)")
    udpp.add_argument("--udp-port", type=int, default=5514, help="Destination UDP port (default 5514)")
    udpp.add_argument("--ttl-s", type=int, default=300, help="Mirror duration in seconds (default 300, max 1800)")

    udps = sub.add_parser("udp-log-stop", help="Disable UDP log mirroring")
    add_common(udps)

    # Default command = run (for convenience)
    p.set_defaults(command="run")
    return p


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    cfg = merge_config(args)
    try:
        if args.command == "status":
            return cmd_status(cfg)
        if args.command == "cancel":
            return cmd_cancel(cfg)
        if args.command == "udp-log-start":
            return cmd_udp_log_start(cfg, host=args.udp_host, port=args.udp_port, ttl_s=args.ttl_s)
        if args.command == "udp-log-stop":
            return cmd_udp_log_stop(cfg)
        return run_workflow(cfg)
    except ApiError as err:
        eprint(str(err))
        if err.body_json is not None:
            eprint("Response JSON:", json.dumps(err.body_json, indent=2, sort_keys=True))
        elif err.body_text:
            eprint("Response body:", err.body_text.strip())
        return 2
    except subprocess.CalledProcessError as err:
        eprint("Command failed:", " ".join(str(x) for x in err.cmd))
        if getattr(err, "output", None):
            eprint(err.output)
        return 2
    except (RuntimeError, TimeoutError, ValueError) as err:
        eprint("Error:", err)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
