#!/usr/bin/env python3
"""Guard Flasher version-file workflow.

This is intentionally a policy checker, not a magic fixer. Flasher version
files are tracked build metadata; when they are dirty they must either be
committed deliberately for an artifact build or restored for ordinary work.
"""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


VERSION_FILES = [
    "VERSION",
    "tools/flasher/package.json",
    "tools/flasher/package-lock.json",
    "tools/flasher/src-tauri/Cargo.toml",
    "tools/flasher/src-tauri/Cargo.lock",
    "tools/flasher/src-tauri/tauri.conf.json",
]


def run_git(repo_root: Path, args: list[str]) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=repo_root,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout


def dirty_version_files(repo_root: Path) -> list[str]:
    output = run_git(repo_root, ["status", "--porcelain", "--", *VERSION_FILES])
    dirty: list[str] = []
    for line in output.splitlines():
        if not line:
            continue
        status = line[:2]
        path = line[3:]
        if " -> " in path:
            path = path.split(" -> ", 1)[1]
        if status == "??" or version_content_changed(repo_root, path):
            dirty.append(path)
    return dirty


def version_content_changed(repo_root: Path, path: str) -> bool:
    if path == "VERSION":
        return True

    diff = run_git(repo_root, ["diff", "HEAD", "--", path])
    changed_lines = [
        line[1:]
        for line in diff.splitlines()
        if line.startswith(("+", "-")) and not line.startswith(("+++", "---"))
    ]
    if path.endswith(".json"):
        return any(re.search(r'"version"\s*:', line) for line in changed_lines)
    if path.endswith((".toml", ".lock")):
        return any(re.search(r"\bversion\s*=", line) for line in changed_lines)
    return bool(changed_lines)


def print_file_list(paths: list[str]) -> None:
    for path in paths:
        print(f"  - {path}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check Flasher version files are not accidentally mixed into unrelated work."
    )
    parser.add_argument(
        "--repo-root",
        default=None,
        help="Repository root. Defaults to this script's grandparent repository.",
    )
    parser.add_argument(
        "--artifact",
        action="store_true",
        help="Require Flasher version files to be clean after an intentional artifact/version commit.",
    )
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve() if args.repo_root else Path(__file__).resolve().parents[2]
    dirty = dirty_version_files(repo_root)

    if not dirty:
        print("Flasher version workflow check passed: no dirty version files.")
        return 0

    print("Flasher version workflow check failed: version files are dirty.")
    print_file_list(dirty)
    print()
    if args.artifact:
        print("Artifact build policy:")
        print("  1. Commit the functional/code change separately.")
        print("  2. Commit these version files in a dedicated version bump commit.")
        print("     Suggested message: chore(version): bump dev build")
    else:
        print("Ordinary work policy:")
        print("  - If this work is not producing a shared/flashed/field-tested artifact, restore these files:")
        print(f"    git restore -- {' '.join(dirty)}")
        print("  - If it is producing an artifact, commit them separately as a version bump.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
