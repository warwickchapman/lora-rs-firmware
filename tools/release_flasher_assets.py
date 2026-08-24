#!/usr/bin/env python3
"""
Release helper for flasher assets.

Policy encoded:
- GitHub Actions release builds: Windows + Linux only.
- macOS release artifacts: portable ZIPs built locally, uploaded manually.
"""

from __future__ import annotations

import argparse
import json
import shlex
import subprocess
from pathlib import Path
from typing import Iterable, List, Set


REPO_MAIN = "warwickchapman/lora-rs"
REPO_PUBLIC = "warwickchapman/lora-rs-firmware"
WORKFLOW = "package_flasher.yml"


def run(cmd: List[str], capture: bool = False) -> str:
    printable = " ".join(shlex.quote(part) for part in cmd)
    print(f"$ {printable}")
    if capture:
        return subprocess.check_output(cmd, text=True).strip()
    subprocess.check_call(cmd)
    return ""


def normalize_tag(tag_or_version: str) -> str:
    value = tag_or_version.strip()
    if not value:
        raise RuntimeError("Version/tag is empty")
    return value if value.startswith("v") else f"v{value}"


def version_from_tag(tag: str) -> str:
    return tag[1:] if tag.startswith("v") else tag


def expected_assets(version: str) -> Set[str]:
    return {
        f"lrs-firmware-{version}-433_za.bin",
        f"lrs-firmware-{version}-915_us.bin",
        f"thanda-lora-flasher-{version}-macos-arm64-portable.zip",
        f"thanda-lora-flasher-{version}-macos-x86_64-portable.zip",
        f"thanda-lora-flasher-{version}-windows-x64.msi",
        f"thanda-lora-flasher-{version}-windows-x64-portable.zip",
        f"thanda-lora-flasher-{version}-linux-x64.deb",
        f"thanda-lora-flasher-{version}-linux-x64.rpm",
        f"thanda-lora-flasher-{version}-linux-x64.AppImage.tar.gz",
    }


def release_asset_names(repo: str, tag: str) -> Set[str]:
    raw = run(["gh", "release", "view", tag, "--repo", repo, "--json", "assets"], capture=True)
    payload = json.loads(raw)
    assets = payload.get("assets", [])
    return {str(asset.get("name", "")).strip() for asset in assets if asset.get("name")}


def dispatch_ci(tag: str, repo: str, workflow: str) -> None:
    # Release publishing must run from the intended release tag ref.
    # Running from main after post-bump can accidentally publish a *-dev release.
    for platform in ("windows", "linux"):
        run(
            [
                "gh",
                "workflow",
                "run",
                workflow,
                "--repo",
                repo,
                "--ref",
                tag,
                "-f",
                f"platform={platform}",
                "-f",
                "create_release=true",
            ]
        )
    print(f"Dispatched release packaging for windows + linux only (ref={tag}).")


def upload_macos(tag: str, arm64: Path, x64: Path, repos: Iterable[str]) -> None:
    for path in (arm64, x64):
        if not path.exists():
            raise RuntimeError(f"Missing file: {path}")
    for repo in repos:
        run(
            [
                "gh",
                "release",
                "upload",
                tag,
                str(arm64),
                str(x64),
                "--repo",
                repo,
                "--clobber",
            ]
        )
    print("Uploaded local macOS portable ZIPs to both repos.")


def verify(tag: str, repos: Iterable[str]) -> None:
    version = version_from_tag(tag)
    expected = expected_assets(version)
    for repo in repos:
        actual = release_asset_names(repo, tag)
        missing = sorted(expected - actual)
        if missing:
            raise RuntimeError(f"{repo}:{tag} missing assets: {', '.join(missing)}")
        print(f"{repo}:{tag} asset contract OK ({len(expected)} assets)")


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="Flasher release helper (Windows/Linux CI + local macOS upload)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    ap_dispatch = sub.add_parser("dispatch-ci", help="Dispatch Windows + Linux flasher CI release runs.")
    ap_dispatch.add_argument("--tag", required=True, help="Release tag (vX.Y.Z-alpha or X.Y.Z-alpha)")
    ap_dispatch.add_argument("--repo", default=REPO_MAIN)
    ap_dispatch.add_argument("--workflow", default=WORKFLOW)

    ap_upload = sub.add_parser("upload-macos", help="Upload local macOS portable ZIPs to both repos.")
    ap_upload.add_argument("--tag", required=True, help="Release tag (vX.Y.Z-alpha or X.Y.Z-alpha)")
    ap_upload.add_argument("--arm64", required=True, type=Path, help="Path to macOS arm64 portable ZIP")
    ap_upload.add_argument("--x64", required=True, type=Path, help="Path to macOS x86_64 portable ZIP")
    ap_upload.add_argument("--repo-main", default=REPO_MAIN)
    ap_upload.add_argument("--repo-public", default=REPO_PUBLIC)

    ap_verify = sub.add_parser("verify", help="Verify full 9-asset contract in both repos.")
    ap_verify.add_argument("--tag", required=True, help="Release tag (vX.Y.Z-alpha or X.Y.Z-alpha)")
    ap_verify.add_argument("--repo-main", default=REPO_MAIN)
    ap_verify.add_argument("--repo-public", default=REPO_PUBLIC)

    return ap.parse_args()


def main() -> int:
    args = parse_args()
    tag = normalize_tag(args.tag)

    if args.cmd == "dispatch-ci":
        dispatch_ci(tag, args.repo, args.workflow)
        return 0

    if args.cmd == "upload-macos":
        upload_macos(tag, args.arm64, args.x64, [args.repo_main, args.repo_public])
        return 0

    if args.cmd == "verify":
        verify(tag, [args.repo_main, args.repo_public])
        return 0

    raise RuntimeError(f"Unhandled command: {args.cmd}")


if __name__ == "__main__":
    raise SystemExit(main())
