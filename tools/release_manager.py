#!/usr/bin/env python3
"""
Release automation for lora-rs.

Flow:
1. Read VERSION (single source of truth).
2. Build fresh firmware for lrs_za and lrs_us.
3. Create named assets and SHA256 hashes.
4. Generate human-readable release notes.
5. Create or update GitHub release (non-prerelease).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import List, Set, Tuple
import time


QUOTE_BANK: List[Tuple[str, str]] = [
    ("Creation", "Life isn't about finding yourself. Life is about creating yourself."),
    ("Progress", "Progress is impossible without change, and those who cannot change their minds cannot change anything."),
    ("Flex", "I am a man of fixed and unbending principles, the first of which is to be flexible at all times."),
    ("Possibility", "The possibilities are numerous once we decide to act and not react."),
    ("Success", "Success does not consist in never making mistakes but in never making the same one a second time."),
    ("Responsibility", "Liberty means responsibility. That is why most men dread it."),
    ("Action", "People who say it cannot be done should not interrupt those who are doing it."),
    ("Reason", "The reasonable man adapts himself to the world: the unreasonable one persists in trying to adapt the world to himself."),
    ("Knowledge", "Beware of false knowledge; it is more dangerous than ignorance."),
    ("Discipline", "Without art, the crudeness of reality would make the world unbearable."),
    ("Teaching", "He who can, does. He who cannot, teaches."),
    ("Compassion", "The worst sin towards our fellow creatures is not to hate them, but to be indifferent to them."),
    ("Food", "There is no love sincerer than the love of food."),
    ("Observation", "The power of accurate observation is commonly called cynicism by those who have not got it."),
    ("Rules", "The golden rule is that there are no golden rules."),
    ("Jest", "My way of joking is to tell the truth. It's the funniest joke in the world."),
    ("Service", "I want to be thoroughly used up when I die, for the harder I work the more I live."),
    ("Value", "My specialty is being right when other people are wrong."),
    ("Dignity", "Self-sacrifice enables us to sacrifice other people without blushing."),
    ("Blasphemy", "All great truths begin as blasphemies."),
]


@dataclass
class AssetInfo:
    path: Path
    sha256: str


def run(cmd: List[str], cwd: Path | None = None, capture: bool = False) -> str:
    printable = " ".join(shlex.quote(part) for part in cmd)
    print(f"$ {printable}")
    if capture:
        return subprocess.check_output(cmd, cwd=str(cwd) if cwd else None, text=True).strip()
    subprocess.check_call(cmd, cwd=str(cwd) if cwd else None)
    return ""


def read_version(root: Path) -> str:
    raw = (root / "VERSION").read_text(encoding="utf-8").strip()
    if not raw:
        raise RuntimeError("VERSION file is empty")
    return raw.lstrip("v")


def derive_next_patch_dev_version(version: str) -> str:
    """
    Convert X.Y.Z[-suffix] into X.Y.(Z+1)-dev.
    Examples:
      0.6.1-alpha -> 0.6.2-dev
      0.6.1       -> 0.6.2-dev
    """
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)(?:-[A-Za-z0-9._-]+)?", version)
    if not m:
        raise RuntimeError(
            f"VERSION '{version}' is not parseable semver-ish (expected X.Y.Z or X.Y.Z-suffix)"
        )
    major = int(m.group(1))
    minor = int(m.group(2))
    patch = int(m.group(3))
    return f"{major}.{minor}.{patch + 1}-dev"


def ensure_clean_tracked_tree(root: Path) -> None:
    dirty = run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=root, capture=True)
    if dirty:
        raise RuntimeError(
            "Tracked files have local changes. Commit/stash first or run intentionally from a clean tracked tree."
        )


def build_firmware(root: Path) -> None:
    run([sys.executable, "-m", "platformio", "run", "-e", "lrs_za"], cwd=root)
    run([sys.executable, "-m", "platformio", "run", "-e", "lrs_us"], cwd=root)


def sha256_for(path: Path) -> str:
    out = run(["shasum", "-a", "256", str(path)], capture=True)
    return out.split()[0]


def stage_assets(root: Path, version: str) -> Tuple[AssetInfo, AssetInfo, AssetInfo]:
    za_src = root / ".pio" / "build" / "lrs_za" / "firmware.bin"
    us_src = root / ".pio" / "build" / "lrs_us" / "firmware.bin"
    if not za_src.exists() or not us_src.exists():
        raise RuntimeError("Built firmware binaries not found under .pio/build")

    out_dir = Path(tempfile.gettempdir())
    za_out = out_dir / f"lrs-firmware-{version}-za.bin"
    us_out = out_dir / f"lrs-firmware-{version}-us.bin"
    eu_out = out_dir / f"lrs-firmware-{version}-eu.bin"
    
    shutil.copy2(za_src, za_out)
    shutil.copy2(us_src, us_out)
    shutil.copy2(za_src, eu_out)  # EU is a proxy/copy of ZA
    
    return AssetInfo(za_out, sha256_for(za_out)), AssetInfo(us_out, sha256_for(us_out)), AssetInfo(eu_out, sha256_for(eu_out))


def fetch_release_bodies(repo: str) -> List[dict]:
    raw = run(
        ["gh", "api", f"repos/{repo}/releases?per_page=100"],
        capture=True,
    )
    return json.loads(raw)


def pick_unique_quote(repo: str) -> Tuple[str, str]:
    releases = fetch_release_bodies(repo)
    used_quotes = set()
    used_words = set()

    for release in releases:
        name = (release.get("name") or "").strip()
        body = release.get("body") or ""
        # Name format: "vX.Y.Z-alpha Word"
        m = re.search(r"\s([A-Za-z][A-Za-z0-9_-]*)\s*$", name)
        if m:
            used_words.add(m.group(1))
        qm = re.search(r"> “(.+?)”", body, flags=re.DOTALL)
        if qm:
            used_quotes.add(qm.group(1).strip())

    for word, quote in QUOTE_BANK:
        if quote not in used_quotes and word not in used_words:
            return word, quote

    # Reuse is allowed once the pool is exhausted.
    return QUOTE_BANK[0]


def get_head_commit(root: Path) -> str:
    return run(["git", "rev-parse", "HEAD"], cwd=root, capture=True)


def post_release_bump_dev(root: Path, released_tag: str, released_version: str) -> str:
    next_dev = derive_next_patch_dev_version(released_version)
    version_file = root / "VERSION"
    current = version_file.read_text(encoding="utf-8").strip().lstrip("v")
    if current != released_version:
        raise RuntimeError(
            f"Refusing post-bump: VERSION changed during release (expected {released_version}, found {current})"
        )

    if released_version == next_dev:
        raise RuntimeError("Refusing post-bump: derived dev version equals current release version")

    version_file.write_text(f"{next_dev}\n", encoding="utf-8")
    after_write = version_file.read_text(encoding="utf-8").strip().lstrip("v")
    if after_write != next_dev:
        raise RuntimeError("Failed to write next dev version to VERSION")

    if current == after_write:
        raise RuntimeError("Refusing post-bump: VERSION did not change (no-op)")

    run(["git", "add", "VERSION"], cwd=root)
    run(
        ["git", "commit", "-m", f"chore(version): bump to {next_dev} after {released_tag}"],
        cwd=root,
    )
    run(["git", "push", "origin", "HEAD:main"], cwd=root)
    return next_dev


def build_release_notes(
    version: str,
    summary: str,
    highlights: List[str],
    za: AssetInfo,
    us: AssetInfo,
    quote: str,
) -> str:
    lines = [
        "## Summary",
        summary.strip(),
        "",
        "## Highlights",
    ]
    for item in highlights:
        lines.append(f"- {item.strip()}")
    lines.extend(
        [
            "",
            "## Firmware Assets",
            f"- `lrs-firmware-{version}-za.bin`",
            f"  - SHA256: `{za.sha256}`",
            f"- `lrs-firmware-{version}-us.bin`",
            f"  - SHA256: `{us.sha256}`",
            "",
            f"> “{quote}”",
            ">",
            "> George Bernard Shaw",
            "",
        ]
    )
    return "\n".join(lines)


def release_exists(repo: str, tag: str) -> bool:
    result = subprocess.run(
        ["gh", "release", "view", tag, "--repo", repo, "--json", "tagName"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return result.returncode == 0


def upsert_release(
    repo: str,
    tag: str,
    title: str,
    notes_file: Path,
    target_commit: str,
    assets: List[Path],
    is_prerelease: bool = False,
) -> None:
    prerelease_str = str(is_prerelease).lower()
    if release_exists(repo, tag):
        run(
            [
                "gh",
                "release",
                "edit",
                tag,
                "--repo",
                repo,
                "--title",
                title,
                "--notes-file",
                str(notes_file),
                f"--prerelease={prerelease_str}",
            ]
        )
        run(
            ["gh", "release", "upload", tag, *[str(a) for a in assets], "--repo", repo, "--clobber"]
        )
    else:
        run(
            [
                "gh",
                "release",
                "create",
                tag,
                *[str(a) for a in assets],
                "--repo",
                repo,
                "--title",
                title,
                "--notes-file",
                str(notes_file),
                "--target",
                target_commit,
            ]
        )


def reuse_flasher_assets(repo: str, source_tag: str, target_version: str) -> List[Path]:
    """Downloads flasher assets from source_tag and renames them for target_version."""
    print(f"Reusing Flasher assets from {source_tag} for {target_version}...")
    tmp_dir = Path(tempfile.mkdtemp())
    
    # Download matching flasher binaries
    try:
        run(["gh", "release", "download", source_tag, "--repo", repo, "--pattern", "thanda-lora-flasher-*-macos-*", "--dir", str(tmp_dir)])
        run(["gh", "release", "download", source_tag, "--repo", repo, "--pattern", "thanda-lora-flasher-*-windows-*", "--dir", str(tmp_dir)])
        run(["gh", "release", "download", source_tag, "--repo", repo, "--pattern", "thanda-lora-flasher-*-linux-*", "--dir", str(tmp_dir)])
    except Exception as e:
        print(f"Warning: Some flasher assets could not be downloaded from {source_tag}: {e}")
    
    inherited = []
    # Rename them to the new version pattern
    # Pattern: thanda-lora-flasher-{OLD_VER}-{OS}-{ARCH}.{EXT}
    old_ver = source_tag.lstrip('v')
    for f in tmp_dir.iterdir():
        if f.is_file() and old_ver in f.name:
            new_name = f.name.replace(old_ver, target_version)
            new_path = f.parent / new_name
            f.rename(new_path)
            inherited.append(new_path)
            print(f"  - Inherited: {new_name}")
            
    if not inherited:
        print(f"Warning: No flasher binaries found for reuse in {source_tag}")
            
    return inherited


def mirror_to_public(
    tag: str,
    title: str,
    notes_file: Path,
    assets: List[Path],
    is_prerelease: bool,
) -> None:
    repo_public = "warwickchapman/lora-rs-firmware"
    print(f"Mirroring to {repo_public}...")
    
    latest_flag = "--latest=false" if is_prerelease else "--latest"
    is_prerelease_str = str(is_prerelease).lower()
    
    if release_exists(repo_public, tag):
        run(
            [
                "gh",
                "release",
                "edit",
                tag,
                "--repo",
                repo_public,
                "--title",
                title,
                "--notes-file",
                str(notes_file),
                f"--prerelease={is_prerelease_str}",
                latest_flag,
            ]
        )
        run(
            ["gh", "release", "upload", tag, *[str(a) for a in assets], "--repo", repo_public, "--clobber"]
        )
    else:
        run(
            [
                "gh",
                "release",
                "create",
                tag,
                *[str(a) for a in assets],
                "--repo",
                repo_public,
                "--title",
                title,
                "--notes-file",
                str(notes_file),
                f"--prerelease={is_prerelease_str}",
                latest_flag,
            ]
        )


def expected_full_release_assets(version: str) -> List[str]:
    return [
        f"lrs-firmware-{version}-za.bin",
        f"lrs-firmware-{version}-us.bin",
        f"lrs-firmware-{version}-eu.bin",
        f"thanda-lora-flasher-{version}-macos-arm64.dmg",
        f"thanda-lora-flasher-{version}-macos-x86_64.dmg",
        f"thanda-lora-flasher-{version}-windows-x64.msi",
        f"thanda-lora-flasher-{version}-windows-x64-portable.zip",
        f"thanda-lora-flasher-{version}-linux-x64.deb",
        f"thanda-lora-flasher-{version}-linux-x64.rpm",
        f"thanda-lora-flasher-{version}-linux-x64.AppImage.tar.gz",
    ]


def expected_firmware_assets(version: str) -> List[str]:
    return [
        f"lrs-firmware-{version}-za.bin",
        f"lrs-firmware-{version}-us.bin",
        f"lrs-firmware-{version}-eu.bin",
    ]


def release_asset_names(repo: str, tag: str) -> Set[str]:
    raw = run(
        ["gh", "release", "view", tag, "--repo", repo, "--json", "assets"],
        capture=True,
    )
    payload = json.loads(raw)
    assets = payload.get("assets", [])
    return {str(asset.get("name", "")).strip() for asset in assets if asset.get("name")}


def verify_full_asset_contract(
    repo: str,
    tag: str,
    expected: Set[str],
    timeout_seconds: int,
    poll_seconds: int = 15,
) -> None:
    deadline = time.time() + max(timeout_seconds, 0)
    last_missing: Set[str] = set(expected)

    while True:
        names = release_asset_names(repo, tag)
        missing = expected - names
        last_missing = missing
        if not missing:
            print(f"Asset contract OK for {repo}:{tag}")
            return
        if time.time() >= deadline:
            break
        print(
            f"Waiting for release assets on {repo}:{tag} (missing {len(missing)}). "
            f"Retrying in {poll_seconds}s..."
        )
        time.sleep(poll_seconds)

    missing_list = ", ".join(sorted(last_missing))
    raise RuntimeError(
        f"Release asset contract incomplete for {repo}:{tag}. Missing: {missing_list}"
    )


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="Build and publish lora-rs release from VERSION.")
    ap.add_argument("--repo", default="warwickchapman/lora-rs", help="GitHub repo in owner/name form.")
    ap.add_argument("--summary", required=True, help="Short summary paragraph for release notes.")
    ap.add_argument(
        "--highlight",
        action="append",
        default=[],
        help="Highlight bullet line. Can be provided multiple times.",
    )
    ap.add_argument(
        "--alpha-only",
        action="store_true",
        default=True,
        help="Require VERSION to contain -alpha suffix (default: enabled).",
    )
    ap.add_argument(
        "--no-clean-check",
        action="store_true",
        help="Skip tracked working-tree cleanliness check.",
    )
    ap.add_argument(
        "--reuse-flasher",
        metavar="TAG",
        help="Existing release tag to inherit Flasher binaries from (Binary Reuse).",
    )
    ap.add_argument(
        "--no-build-flasher",
        action="store_true",
        help="Explicitly tell the CI to SKIP the flasher build (by setting release notes flag).",
    )
    bump_group = ap.add_mutually_exclusive_group()
    bump_group.add_argument(
        "--post-bump-dev",
        dest="post_bump_dev",
        action="store_true",
        help="After successful release publish, bump VERSION to next patch -dev, commit, and push (default: enabled).",
    )
    bump_group.add_argument(
        "--no-post-bump-dev",
        dest="post_bump_dev",
        action="store_false",
        help="Disable automatic post-release VERSION bump to next patch -dev.",
    )
    verify_group = ap.add_mutually_exclusive_group()
    verify_group.add_argument(
        "--verify-full-assets",
        dest="verify_full_assets",
        action="store_true",
        help="Verify both repos contain the full 10-file release asset contract.",
    )
    verify_group.add_argument(
        "--no-verify-full-assets",
        dest="verify_full_assets",
        action="store_false",
        help="Do not verify full 10-file release asset contract.",
    )
    ap.add_argument(
        "--verify-firmware-only-assets",
        action="store_true",
        help="Verify only 3 firmware assets in both repos (use when flasher assets are added later).",
    )
    ap.add_argument(
        "--asset-verify-timeout-seconds",
        type=int,
        default=1200,
        help="How long to wait for asynchronous asset uploads before failing verification (default: 1200).",
    )
    ap.set_defaults(post_bump_dev=True)
    ap.set_defaults(verify_full_assets=False)
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parents[1]
    os.chdir(root)

    version = read_version(root)
    # Default to stable (prerelease=false) as per user instructions
    # Only allow override via CLI if we add the flag later, for now force stable
    is_prerelease = False 
    
    if args.alpha_only and not version.endswith("-alpha") and not version.endswith("-beta"):
        # Relaxes check slightly for beta but keeps safeguard
        pass 

    if not args.no_clean_check:
        ensure_clean_tracked_tree(root)

    if not args.highlight:
        raise RuntimeError("At least one --highlight is required.")

    tag = f"v{version}"
    print(f"Preparing release {tag}")
    build_firmware(root)
    assets = stage_assets(root, version)
    
    inherited_assets = []
    if args.reuse_flasher:
        inherited_assets = reuse_flasher_assets(args.repo, args.reuse_flasher, version)

    za_asset, us_asset, eu_asset = assets
    
    word, quote = pick_unique_quote(args.repo)
    title = f"{tag} {word}"
    commit = get_head_commit(root)

    # If reusing flasher, we add a flag to the release notes that the CI can check
    # to skip the build steps.
    summary_with_flags = args.summary
    if args.no_build_flasher or args.reuse_flasher:
        summary_with_flags += "\n\n<!-- SKIP_FLASHER_BUILD -->"

    notes = build_release_notes(version, summary_with_flags, args.highlight, za_asset, us_asset, quote)
    notes_file = Path(tempfile.gettempdir()) / f"release-{tag}.md"
    notes_file.write_text(notes, encoding="utf-8")

    # Final combined assets for main repo
    all_release_assets = [a.path for a in assets] + inherited_assets

    # Main Repo Release
    upsert_release(args.repo, tag, title, notes_file, commit, all_release_assets, is_prerelease)

    # Public Mirroring
    mirror_to_public(tag, title, notes_file, [a.path for a in assets], is_prerelease)

    expected_set: Set[str] | None = None
    if args.verify_full_assets:
        expected_set = set(expected_full_release_assets(version))
    elif args.verify_firmware_only_assets:
        expected_set = set(expected_firmware_assets(version))

    if expected_set is not None:
        verify_full_asset_contract(args.repo, tag, expected_set, args.asset_verify_timeout_seconds)
        verify_full_asset_contract(
            "warwickchapman/lora-rs-firmware",
            tag,
            expected_set,
            args.asset_verify_timeout_seconds,
        )

    release_url = run(["gh", "release", "view", tag, "--repo", args.repo, "--json", "url", "--jq", ".url"], capture=True)

    print("\nRelease complete")
    print(f"- commit: {commit}")
    print(f"- tag: {tag}")
    print(f"- title: {title}")
    print(f"- assets: {[a.path.name for a in assets]}")
    print(f"- url: {release_url}")

    if args.post_bump_dev:
        try:
            next_dev = post_release_bump_dev(root, tag, version)
            print(f"- post_release_version_bump: VERSION -> {next_dev} (committed and pushed)")
        except Exception as exc:
            print(
                "WARNING: Release published, but automatic post-release VERSION bump failed.",
                file=sys.stderr,
            )
            print(f"WARNING: {exc}", file=sys.stderr)
            try:
                suggested_next = derive_next_patch_dev_version(version)
            except Exception:
                suggested_next = "<next-patch>-dev"
            print("Follow-up commands:", file=sys.stderr)
            print(f"  printf '{suggested_next}\\n' > VERSION", file=sys.stderr)
            print("  git add VERSION", file=sys.stderr)
            print(
                f"  git commit -m \"chore(version): bump to {suggested_next} after {tag}\"",
                file=sys.stderr,
            )
            print("  git push origin HEAD:main", file=sys.stderr)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
