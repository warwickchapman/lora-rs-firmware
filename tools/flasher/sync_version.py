#!/usr/bin/env python3
"""
Synchronize flasher package versions from the repo VERSION file.

Targets:
- tools/flasher/package.json
- tools/flasher/src-tauri/tauri.conf.json
- tools/flasher/src-tauri/Cargo.toml
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


def update_package_json(path: Path, version: str) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    data["version"] = version
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def update_tauri_conf(path: Path, version: str) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    data["version"] = version
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
    args = ap.parse_args()

    repo_root = (
        Path(args.repo_root).resolve()
        if args.repo_root
        else Path(__file__).resolve().parents[2]
    )
    flasher_root = repo_root / "tools" / "flasher"

    release_version = read_repo_version(repo_root)
    package_version = release_version.split("-", 1)[0] if args.windows_msi_safe else release_version

    update_package_json(flasher_root / "package.json", release_version)
    update_tauri_conf(flasher_root / "src-tauri" / "tauri.conf.json", package_version)
    update_cargo_toml(flasher_root / "src-tauri" / "Cargo.toml", package_version)

    print(
        f"Synchronized flasher versions: release={release_version}, "
        f"package={package_version}, windows_msi_safe={args.windows_msi_safe}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
