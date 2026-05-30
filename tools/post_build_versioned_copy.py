"""PlatformIO post-build script: copy firmware.bin to a versioned filename.

Produces  .pio/build/<env>/lrs-firmware-<version>.bin
alongside the default firmware.bin, so the Flasher can display the version
that was actually compiled into the binary.
"""

import shutil
from pathlib import Path

from version_metadata import version_parts, version_dev_build

Import("env")


def _read_repo_version(default: str) -> str:
    try:
        version_path = Path(env.subst("$PROJECT_DIR")) / "VERSION"
        raw = version_path.read_text(encoding="utf-8").strip()
        if not raw:
            return default
        return raw[1:] if raw.startswith("v") else raw
    except Exception:
        return default


def post_build_copy(source, target, env):
    fw_version = _read_repo_version("0.0.0-dev")
    build_dir = Path(env.subst("$BUILD_DIR"))
    src = build_dir / "firmware.bin"
    if not src.is_file():
        return

    versioned_name = f"lrs-firmware-{fw_version}.bin"
    dst = build_dir / versioned_name

    # Remove stale versioned binaries from previous builds
    for old in build_dir.glob("lrs-firmware-*.bin"):
        if old.name != versioned_name:
            old.unlink(missing_ok=True)

    shutil.copy2(str(src), str(dst))
    print(f"  Copied firmware to {dst}")


env.AddPostAction("$BUILD_DIR/firmware.bin", post_build_copy)
