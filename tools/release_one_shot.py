#!/usr/bin/env python3
"""
Deterministic end-to-end release orchestrator.

One command for:
1) Firmware release publish (both repos) using exact notes file.
2) Windows/Linux flasher build+publish via CI from the release tag.
3) Local macOS flasher portable ZIP build from the release tag.
4) Upload macOS portable ZIPs to both repos.
5) Full 9-asset verification in both repos.
"""

from __future__ import annotations

import argparse
import calendar
import json
import os
import shlex
import subprocess
import tempfile
import time
from pathlib import Path
from typing import List


REPO = "warwickchapman/lora-rs"
WORKFLOW = "package_flasher.yml"
ESPTOOL_VER = "v5.2.0"


def run(cmd: List[str], cwd: Path | None = None, capture: bool = False) -> str:
    print("$ " + " ".join(shlex.quote(c) for c in cmd))
    if capture:
        return subprocess.check_output(cmd, cwd=str(cwd) if cwd else None, text=True).strip()
    subprocess.check_call(cmd, cwd=str(cwd) if cwd else None)
    return ""


def read_version(root: Path) -> str:
    v = (root / "VERSION").read_text(encoding="utf-8").strip()
    if not v:
        raise RuntimeError("VERSION is empty")
    return v.lstrip("v")


def ensure_clean(root: Path) -> None:
    dirty = run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=root, capture=True)
    if dirty:
        raise RuntimeError("Tracked tree is dirty. Commit/stash first.")


def find_recent_dispatch_runs(repo: str, tag: str, started_after_epoch: float) -> List[int]:
    raw = run(
        [
            "gh",
            "run",
            "list",
            "--repo",
            repo,
            "--workflow",
            WORKFLOW,
            "--limit",
            "30",
            "--json",
            "databaseId,event,headBranch,createdAt",
        ],
        capture=True,
    )
    rows = json.loads(raw)
    ids: List[int] = []
    threshold = started_after_epoch - 120.0
    for row in rows:
        if row.get("event") != "workflow_dispatch":
            continue
        if row.get("headBranch") != tag:
            continue
        created = row.get("createdAt")
        if not created:
            continue
        # ISO UTC -> epoch (no external deps).
        created_epoch = calendar.timegm(time.strptime(created, "%Y-%m-%dT%H:%M:%SZ"))
        if created_epoch < threshold:
            continue
        ids.append(int(row["databaseId"]))
    return ids


def wait_for_ci_runs(repo: str, tag: str, started_after_epoch: float) -> None:
    deadline = time.time() + 900
    run_ids: List[int] = []
    while time.time() < deadline:
        run_ids = find_recent_dispatch_runs(repo, tag, started_after_epoch)
        if len(run_ids) >= 2:
            break
        print("Waiting for CI dispatch runs to appear...")
        time.sleep(8)
    if len(run_ids) < 2:
        raise RuntimeError(f"Could not find both CI dispatch runs for {tag}")

    run_ids = sorted(run_ids, reverse=True)[:2]
    for rid in run_ids:
        run(["gh", "run", "watch", str(rid), "--repo", repo, "--exit-status"])


def prune_old_workflow_runs(repo: str, workflow: str, keep_last: int) -> None:
    """
    Keep only the most recent N runs for the target workflow.
    Deletes older runs only when status is completed.
    """
    if keep_last <= 0:
        print("Skipping workflow run pruning (keep_last <= 0).")
        return

    raw = run(
        [
            "gh",
            "run",
            "list",
            "--repo",
            repo,
            "--workflow",
            workflow,
            "--limit",
            "200",
            "--json",
            "databaseId,status,createdAt",
        ],
        capture=True,
    )
    rows = json.loads(raw)
    rows_sorted = sorted(rows, key=lambda r: r.get("createdAt", ""), reverse=True)
    to_consider = rows_sorted[keep_last:]
    deleted = 0

    for row in to_consider:
        run_id = row.get("databaseId")
        status = row.get("status")
        if not run_id or status != "completed":
            continue
        run(["gh", "run", "delete", str(run_id), "--repo", repo])
        deleted += 1

    print(
        f"Workflow run pruning complete for {workflow}: "
        f"kept latest {keep_last}, deleted {deleted} older completed runs."
    )


def create_portable_zip(app_bundle: Path, output_zip: Path) -> None:
    if output_zip.exists():
        output_zip.unlink()
    run(
        [
            "/usr/bin/ditto",
            "-c",
            "-k",
            "--sequesterRsrc",
            "--keepParent",
            str(app_bundle),
            str(output_zip),
        ]
    )


def build_local_macos_zips(root: Path, tag: str) -> tuple[Path, Path]:
    run(["git", "fetch", "origin", "--tags"], cwd=root)
    tmp_wt = Path(tempfile.mkdtemp(prefix="lrs-rel-macos-"))
    run(["git", "worktree", "add", "--detach", str(tmp_wt), tag], cwd=root)

    version = (tmp_wt / "VERSION").read_text(encoding="utf-8").strip().lstrip("v")
    flasher = tmp_wt / "tools" / "flasher"
    src_tauri = flasher / "src-tauri"

    run(["python3", "tools/flasher/sync_version.py", "--check"], cwd=tmp_wt)
    run(["npm", "install"], cwd=flasher)
    run(["npm", "run", "build"], cwd=flasher)

    resources = src_tauri / "resources"
    resources.mkdir(parents=True, exist_ok=True)
    (resources / "VERSION").write_text(f"{version}\n", encoding="utf-8")

    arm_tmp = Path(tempfile.mkdtemp(prefix="esptool-arm64-"))
    amd_tmp = Path(tempfile.mkdtemp(prefix="esptool-amd64-"))

    run(
        [
            "curl",
            "-fL",
            "-o",
            str(src_tauri / "esptool-arm64.tar.gz"),
            f"https://github.com/espressif/esptool/releases/download/{ESPTOOL_VER}/esptool-{ESPTOOL_VER}-macos-arm64.tar.gz",
        ]
    )
    run(["tar", "-xzf", str(src_tauri / "esptool-arm64.tar.gz"), "-C", str(arm_tmp)])
    arm_bin = run(["/bin/zsh", "-lc", f"find {shlex.quote(str(arm_tmp))} -name esptool -type f | head -n1"], capture=True)
    run(["cp", arm_bin, str(src_tauri / "esptool-aarch64-apple-darwin")])
    run(["chmod", "+x", str(src_tauri / "esptool-aarch64-apple-darwin")])

    run(
        [
            "curl",
            "-fL",
            "-o",
            str(src_tauri / "esptool-amd64.tar.gz"),
            f"https://github.com/espressif/esptool/releases/download/{ESPTOOL_VER}/esptool-{ESPTOOL_VER}-macos-amd64.tar.gz",
        ]
    )
    run(["tar", "-xzf", str(src_tauri / "esptool-amd64.tar.gz"), "-C", str(amd_tmp)])
    amd_bin = run(["/bin/zsh", "-lc", f"find {shlex.quote(str(amd_tmp))} -name esptool -type f | head -n1"], capture=True)
    run(["cp", amd_bin, str(src_tauri / "esptool-x86_64-apple-darwin")])
    run(["chmod", "+x", str(src_tauri / "esptool-x86_64-apple-darwin")])

    run(["python3", "sync_version.py", "--check"], cwd=flasher)
    run(["npm", "run", "tauri", "build", "--", "--target", "aarch64-apple-darwin"], cwd=flasher)
    run(["npm", "run", "tauri", "build", "--", "--target", "x86_64-apple-darwin"], cwd=flasher)

    out_dir = tmp_wt / "release-macos"
    out_dir.mkdir(parents=True, exist_ok=True)
    arm_zip = out_dir / f"thanda-lora-flasher-{version}-macos-arm64-portable.zip"
    x64_zip = out_dir / f"thanda-lora-flasher-{version}-macos-x86_64-portable.zip"

    arm_bundle = src_tauri / "target" / "aarch64-apple-darwin" / "release" / "bundle" / "macos"
    x64_bundle = src_tauri / "target" / "x86_64-apple-darwin" / "release" / "bundle" / "macos"
    create_portable_zip(arm_bundle / "Thanda LoRa Flasher.app", arm_zip)
    create_portable_zip(x64_bundle / "Thanda LoRa Flasher.app", x64_zip)

    return arm_zip, x64_zip

def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="One-shot deterministic release runner.")
    ap.add_argument("--notes-file", required=True, type=Path, help="Exact release notes Markdown file to publish.")
    ap.add_argument("--title", required=True, help="Exact release title (e.g. 'v0.9.0-beta Flex').")
    ap.add_argument(
        "--keep-workflow-runs",
        type=int,
        default=10,
        help="Keep only this many latest GitHub Actions runs for package_flasher.yml (default: 10, 0 disables pruning).",
    )
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parents[1]
    os.chdir(root)

    ensure_clean(root)
    version = read_version(root)
    tag = f"v{version}"

    if not args.notes_file.exists():
        raise RuntimeError(f"Notes file not found: {args.notes_file}")

    run(["python3", "tools/flasher/sync_version.py", "--check"], cwd=root)

    run(
        [
            "python3",
            "tools/release_manager.py",
            "--notes-file",
            str(args.notes_file),
            "--title",
            args.title,
            "--verify-firmware-only-assets",
        ],
        cwd=root,
    )

    dispatch_start = time.time()
    run(["python3", "tools/release_flasher_assets.py", "dispatch-ci", "--tag", tag], cwd=root)
    wait_for_ci_runs(REPO, tag, dispatch_start)

    arm_zip, x64_zip = build_local_macos_zips(root, tag)
    run(
        [
            "python3",
            "tools/release_flasher_assets.py",
            "upload-macos",
            "--tag",
            tag,
            "--arm64",
            str(arm_zip),
            "--x64",
            str(x64_zip),
        ],
        cwd=root,
    )

    run(["python3", "tools/release_flasher_assets.py", "verify", "--tag", tag], cwd=root)
    prune_old_workflow_runs(REPO, WORKFLOW, args.keep_workflow_runs)

    print("\nDeterministic release completed successfully.")
    print(f"- tag: {tag}")
    print(f"- title: {args.title}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=os.sys.stderr)
        raise SystemExit(1)
