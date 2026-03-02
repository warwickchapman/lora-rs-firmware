import hashlib
import subprocess
from datetime import datetime
from pathlib import Path

Import("env")


def _run_git(args, default):
    try:
        out = subprocess.check_output(["git"] + args, stderr=subprocess.DEVNULL, text=True).strip()
        return out if out else default
    except Exception:
        return default


def _git_dirty_flag():
    try:
        rc = subprocess.call(["git", "diff", "--quiet", "--ignore-submodules", "HEAD"], stderr=subprocess.DEVNULL)
        return 1 if rc != 0 else 0
    except Exception:
        return 0


def _deterministic_tree_id(git_sha, dirty):
    if dirty == 0:
        return git_sha

    # Hash the exact local patch set so a dirty working tree is still stable
    # across repeated builds until the tree content changes.
    staged = _run_git(["diff", "--cached", "--binary", "HEAD"], "")
    unstaged = _run_git(["diff", "--binary", "HEAD"], "")
    untracked_list = _run_git(["ls-files", "--others", "--exclude-standard"], "")
    untracked_entries = []
    if untracked_list:
        for path in untracked_list.splitlines():
            path = path.strip()
            if not path:
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


fw_version = _read_repo_version(env.GetProjectOption("custom_fw_version", "0.0.0-dev"))
git_sha = _run_git(["rev-parse", "--short", "HEAD"], "nogit")
git_branch = _run_git(["rev-parse", "--abbrev-ref", "HEAD"], "unknown")
dirty = _git_dirty_flag()
now = datetime.utcnow()
build_date_short = now.strftime("%y%m%d")
build_id = _deterministic_tree_id(git_sha, dirty)

env.Append(
    CPPDEFINES=[
        ("LRS_FW_VERSION", '\\"%s\\"' % fw_version),
        ("LRS_GIT_SHA", '\\"%s\\"' % git_sha),
        ("LRS_GIT_BRANCH", '\\"%s\\"' % git_branch),
        ("LRS_GIT_DIRTY", dirty),
        ("LRS_BUILD_ID", '\\"%s\\"' % build_id),
        ("LRS_BUILD_DATE_SHORT", '\\"%s\\"' % build_date_short),
    ]
)
