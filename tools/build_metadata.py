import hashlib
import json
import subprocess
from datetime import datetime
from pathlib import Path

from version_metadata import version_dev_build, version_parts

Import("env")


def _run_git(args, default):
    try:
        out = subprocess.check_output(["git"] + args, stderr=subprocess.DEVNULL, text=True).strip()
        return out if out else default
    except Exception:
        return default


def _git_dirty_flag():
    try:
        rc = subprocess.call(
            ["git", "diff", "--quiet", "--ignore-submodules", "HEAD", "--", ".", ":!tools"],
            stderr=subprocess.DEVNULL,
        )
        return 1 if rc != 0 else 0
    except Exception:
        return 0


def _deterministic_tree_id(git_sha, dirty):
    if dirty == 0:
        return git_sha

    # Hash the exact local patch set so a dirty working tree is still stable
    # across repeated builds until the tree content changes.
    # Exclude tools/ — Flasher and tooling changes must not affect firmware builds.
    staged = _run_git(["diff", "--cached", "--binary", "HEAD", "--", ".", ":!tools"], "")
    unstaged = _run_git(["diff", "--binary", "HEAD", "--", ".", ":!tools"], "")
    untracked_list = _run_git(["ls-files", "--others", "--exclude-standard"], "")
    untracked_entries = []
    if untracked_list:
        for path in untracked_list.splitlines():
            path = path.strip()
            if not path or path.startswith("tools/"):
                continue
            try:
                blob = subprocess.check_output(
                    ["git", "hash-object", "--", path], stderr=subprocess.DEVNULL, text=True
                ).strip()
            except Exception:
                blob = "missing"
            untracked_entries.append(f"{path}:{blob}")
    untracked = "|".join(untracked_entries)
    dirty_fingerprint = hashlib.sha1(
        f"{git_sha}|{staged}|{unstaged}|{untracked}".encode("utf-8")
    ).hexdigest()
    return f"{git_sha}-dirty-{dirty_fingerprint[:7]}"



def _read_repo_version(default):
    try:
        version_path = Path(env.subst("$PROJECT_DIR")) / "VERSION"
        raw = version_path.read_text(encoding="utf-8").strip()
        if not raw:
            return default
        return raw[1:] if raw.startswith("v") else raw
    except Exception:
        return default


def _read_release_compatibility(project_dir):
    try:
        data = json.loads((Path(project_dir) / "RELEASE_COMPATIBILITY.json").read_text(encoding="utf-8"))
        revision = int(data["revision"])
    except Exception as exc:
        raise RuntimeError(f"Invalid RELEASE_COMPATIBILITY.json: {exc}")
    if revision < 1 or revision > 255:
        raise RuntimeError("RELEASE_COMPATIBILITY.json revision must be 1..255")
    return revision


def _guard_unique_dev_build(project_dir, fw_version, build_id):
    if "~" not in fw_version:
        return
    stamp_dir = Path(project_dir) / ".pio"
    stamp_dir.mkdir(exist_ok=True)
    stamp_path = stamp_dir / "dev_build_versions.json"
    try:
        used = json.loads(stamp_path.read_text(encoding="utf-8")) if stamp_path.exists() else {}
    except Exception:
        used = {}

    previous = used.get(fw_version)
    if previous and previous != build_id:
        raise RuntimeError(
            f"VERSION {fw_version} was already used for a different build tree. "
            "Run `python3 tools/bump_dev_build.py` before building changed dev firmware."
        )

    if previous != build_id:
        used[fw_version] = build_id
        stamp_path.write_text(json.dumps(used, indent=2, sort_keys=True) + "\n", encoding="utf-8")


fw_version = _read_repo_version(env.GetProjectOption("custom_fw_version", "0.0.0-dev"))
compatibility_revision = _read_release_compatibility(env.subst("$PROJECT_DIR"))
release_compatibility_enabled = 0 if ("~" in fw_version or "-dev" in fw_version) else 1
fw_major, fw_minor, fw_patch = version_parts(fw_version)
fw_dev_build = version_dev_build(fw_version)
git_sha = _run_git(["rev-parse", "--short", "HEAD"], "nogit")
git_branch = _run_git(["rev-parse", "--abbrev-ref", "HEAD"], "unknown")
dirty = _git_dirty_flag()
now = datetime.utcnow()
build_date_short = now.strftime("%y%m%d")
build_id = _deterministic_tree_id(git_sha, dirty)
_guard_unique_dev_build(env.subst("$PROJECT_DIR"), fw_version, build_id)

env.Append(
    CPPDEFINES=[
        ("LRS_FW_VERSION", '\\"%s\\"' % fw_version),
        ("LRS_FW_MAJOR", fw_major),
        ("LRS_FW_MINOR", fw_minor),
        ("LRS_FW_PATCH", fw_patch),
        ("LRS_FW_DEV_BUILD", fw_dev_build),
        ("LRS_COMPATIBILITY_REVISION", compatibility_revision),
        ("LRS_RELEASE_COMPATIBILITY_ENABLED", release_compatibility_enabled),
        ("LRS_GIT_SHA", '\\"%s\\"' % git_sha),
        ("LRS_GIT_BRANCH", '\\"%s\\"' % git_branch),
        ("LRS_GIT_DIRTY", dirty),
        ("LRS_BUILD_ID", '\\"%s\\"' % build_id),
        ("LRS_BUILD_DATE_SHORT", '\\"%s\\"' % build_date_short),
    ]
)
