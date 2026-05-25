#!/usr/bin/env python3
"""Increment the repo dev-build version.

Examples:
  0.9.2-dev  -> 0.9.2~1
  0.9.2~1    -> 0.9.2~2
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


DEV_VERSION_RE = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)(?:~(\d+)|-dev)?$")


def bump_dev_version(version: str) -> str:
    raw = version.strip()
    match = DEV_VERSION_RE.fullmatch(raw)
    if not match:
        raise ValueError(
            f"VERSION '{version}' is not a dev version; expected X.Y.Z-dev or X.Y.Z~N"
        )
    major, minor, patch, build = match.groups()
    next_build = int(build or "0") + 1
    return f"{major}.{minor}.{patch}~{next_build}"


def main() -> int:
    ap = argparse.ArgumentParser(description="Bump repo VERSION to the next X.Y.Z~N dev build.")
    ap.add_argument(
        "--repo-root",
        default=None,
        help="Repository root. Defaults to this script's parent repository.",
    )
    ap.add_argument(
        "--check",
        action="store_true",
        help="Print the next dev version without writing VERSION.",
    )
    args = ap.parse_args()

    repo_root = Path(args.repo_root).resolve() if args.repo_root else Path(__file__).resolve().parents[1]
    version_path = repo_root / "VERSION"
    current = version_path.read_text(encoding="utf-8").strip()
    next_version = bump_dev_version(current)

    if args.check:
        print(next_version)
        return 0

    version_path.write_text(f"{next_version}\n", encoding="utf-8")
    print(f"VERSION: {current} -> {next_version}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
