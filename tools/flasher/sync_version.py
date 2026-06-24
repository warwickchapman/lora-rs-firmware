#!/usr/bin/env python3
"""
Synchronize flasher package versions from the repo VERSION file.

Targets:
- tools/flasher/package.json
- tools/flasher/package-lock.json
- tools/flasher/src-tauri/tauri.conf.json
- tools/flasher/src-tauri/Cargo.toml
- tools/flasher/src-tauri/Cargo.lock
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def read_repo_version(repo_root: Path) -> str:
    raw = (repo_root / "VERSION").read_text(encoding="utf-8").strip()
    if not raw:
        raise RuntimeError("VERSION is empty")
    return raw[1:] if raw.startswith("v") else raw


def package_version_for_release(version: str) -> str:
    """
    Repo dev builds use operator-facing X.Y.Z~N strings. npm, Cargo, and Tauri
    need SemVer-compatible package versions, so map those to X.Y.Z-dev.N.
    """
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)~(\d+)", version)
    if match:
        major, minor, patch, build = match.groups()
        return f"{major}.{minor}.{patch}-dev.{build}"
    return version


def update_package_json(path: Path, version: str) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    data["version"] = version
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def update_tauri_conf(path: Path, version: str) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    data["version"] = version
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def update_package_lock(path: Path, version: str) -> None:
    if not path.exists():
        return
    data = json.loads(path.read_text(encoding="utf-8"))
    data["version"] = version
    root_package = data.get("packages", {}).get("")
    if isinstance(root_package, dict):
        root_package["version"] = version
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def update_cargo_toml(path: Path, version: str) -> None:
    text = path.read_text(encoding="utf-8")
    match = re.search(r'(?m)^version = ".*"$', text)
    if not match:
        raise RuntimeError(f"Failed to find version field in {path}")
    current_line = match.group(0)
    desired_line = f'version = "{version}"'
    if current_line == desired_line:
        return
    text_new = text[: match.start()] + desired_line + text[match.end() :]
    path.write_text(text_new, encoding="utf-8")


def update_cargo_lock(path: Path, version: str) -> None:
    if not path.exists():
        return
    text = path.read_text(encoding="utf-8")
    pattern = r'(?m)^(\[\[package\]\]\s*\nname\s*=\s*"thanda-lora-flasher"\s*\nversion\s*=\s*")[^"]*(")'
    new_text, count = re.subn(pattern, rf'\g<1>{version}\g<2>', text)
    if count != 1:
        raise RuntimeError(f"Failed to update thanda-lora-flasher package in {path}")
    path.write_text(new_text, encoding="utf-8")


def read_package_json_version(path: Path) -> str:
    data = json.loads(path.read_text(encoding="utf-8"))
    return str(data.get("version", "")).strip()


def read_tauri_conf_version(path: Path) -> str:
    data = json.loads(path.read_text(encoding="utf-8"))
    return str(data.get("version", "")).strip()


def read_cargo_toml_version(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    match = re.search(r'(?m)^version = "(.*)"$', text)
    if not match:
        raise RuntimeError(f"Failed to find version field in {path}")
    return match.group(1).strip()


def read_cargo_lock_version(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    pattern = r'(?m)^\[\[package\]\]\s*\nname\s*=\s*"thanda-lora-flasher"\s*\nversion\s*=\s*"(.*?)"'
    match = re.search(pattern, text)
    if not match:
        raise RuntimeError(f"Failed to find thanda-lora-flasher package in {path}")
    return match.group(1).strip()


def read_package_lock_version(path: Path) -> str:
    data = json.loads(path.read_text(encoding="utf-8"))
    return str(data.get("version", "")).strip()


def check_versions(repo_root: Path, display_version: str, package_version: str) -> int:
    flasher_root = repo_root / "tools" / "flasher"
    checks = [
        ("package.json", read_package_json_version(flasher_root / "package.json"), package_version),
        ("tauri.conf.json", read_tauri_conf_version(flasher_root / "src-tauri" / "tauri.conf.json"), package_version),
        ("Cargo.toml", read_cargo_toml_version(flasher_root / "src-tauri" / "Cargo.toml"), package_version),
    ]

    cargo_lock_path = flasher_root / "src-tauri" / "Cargo.lock"
    if cargo_lock_path.exists():
        checks.append(("Cargo.lock", read_cargo_lock_version(cargo_lock_path), package_version))

    lock_path = flasher_root / "package-lock.json"
    if lock_path.exists():
        checks.append(("package-lock.json", read_package_lock_version(lock_path), package_version))

    mismatches = [(name, current, expected) for (name, current, expected) in checks if current != expected]
    if not mismatches:
        print(
            "Version sync check passed: "
            f"display={display_version}, package={package_version}, "
            f"lockfile_checked={'yes' if lock_path.exists() else 'no'}"
        )
        return 0

    print("Version sync check failed. Mismatches:")
    for name, current, expected in mismatches:
        print(f"  - {name}: current={current or '<empty>'} expected={expected}")
    print("Fix with: python3 tools/flasher/sync_version.py")
    if lock_path.exists():
        print("Then refresh lockfile: cd tools/flasher && npm install")
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description="Sync flasher version fields from VERSION.")
    ap.add_argument(
        "--repo-root",
        default=None,
        help="Path to repository root (defaults to auto-detect from script location).",
    )
    ap.add_argument(
        "--windows-msi-safe",
        action="store_true",
        help="Use numeric-only package version for tauri/Cargo (MSI requirement).",
    )
    ap.add_argument(
        "--check",
        action="store_true",
        help="Check versions only (no writes). Exit non-zero when drift is detected.",
    )
    args = ap.parse_args()

    repo_root = (
        Path(args.repo_root).resolve()
        if args.repo_root
        else Path(__file__).resolve().parents[2]
    )
    flasher_root = repo_root / "tools" / "flasher"

    release_version = read_repo_version(repo_root)
    semver_package_version = package_version_for_release(release_version)
    package_version = (
        semver_package_version.split("-", 1)[0]
        if args.windows_msi_safe
        else semver_package_version
    )

    if args.check:
        return check_versions(repo_root, release_version, package_version)

    update_package_json(flasher_root / "package.json", package_version)
    update_package_lock(flasher_root / "package-lock.json", package_version)
    update_tauri_conf(flasher_root / "src-tauri" / "tauri.conf.json", package_version)
    update_cargo_toml(flasher_root / "src-tauri" / "Cargo.toml", package_version)
    update_cargo_lock(flasher_root / "src-tauri" / "Cargo.lock", package_version)

    print(
        f"Synchronized flasher versions: release={release_version}, "
        f"package={package_version}, windows_msi_safe={args.windows_msi_safe}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
