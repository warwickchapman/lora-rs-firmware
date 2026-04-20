---
name: lora-rs-release-orchestrator
description: use this when preparing or publishing a lora-rs release so firmware, flasher assets, release notes, and dual-repo publishing stay aligned with the established release contract.
---

# lora-rs-release-orchestrator

## Purpose
Run the validated end-to-end LRS release workflow:
- prepare version + changelog from a clean tracked tree
- build/publish firmware
- rebuild flasher when `tools/flasher/**` changed since the source release
- publish to both `warwickchapman/lora-rs` and `warwickchapman/lora-rs-firmware`
- verify that the final release matches the current asset contract

## Use when
- User asks to prepare, publish, retry, or verify a repo release.
- User wants firmware + flasher assets aligned to one release version.
- User wants release notes and GitHub assets checked against the documented contract.
- User wants a one-off local flasher build for testing without any release publication.

## Do not use when
- No release is being prepared.
- User only wants code changes, docs edits, or local testing without release publication.
- Working tree still contains unrelated tracked changes that are not yet sorted into or out of the release.

## Required inputs
- Release version from repo root `VERSION` (plain form, for example `0.6.0-alpha`).
- Release summary paragraph.
- At least one feature-focused highlight bullet.
- Decision on whether current `tools/flasher/**` changes belong in the release.
- For ad-hoc local flasher builds: target platform/arch and whether the working tree version label should include `.dirty`.

## Preconditions
- `VERSION` is the single source of truth.
- Tracked working tree is clean before build/publish steps.
- `CHANGELOG.md` is updated in human-readable form before release prep is committed.
- If flasher assets are being built, run `python3 tools/flasher/sync_version.py` first.
- Rebuild flasher if `tools/flasher/**` changed since the source release.
- macOS flasher builds are local; Windows/Linux flasher builds come from CI.
- For local ad-hoc flasher builds, do not mutate releases/tags/assets; build locally only.

## Workflow

### 1) Release prep
1. Review changes since the last release:
   - `git diff --name-only <last-tag>..HEAD`
   - `git log --oneline <last-tag>..HEAD`
2. Rewrite `CHANGELOG.md` so the unreleased entry reads like release notes, not a raw patch dump.
3. Confirm release docs are still consistent:
   - `README.md`
   - `docs/DEVELOPER_GUIDE.md`
4. Bump `VERSION` if needed.
5. Commit release prep intentionally.

### 2) Firmware release
1. Run:
   - `python3 tools/release_manager.py --summary "<summary>" --highlight "<highlight>" ...`
2. This is the standard firmware release path:
   - reads `VERSION`
   - builds fresh firmware binaries
   - creates/updates the GitHub release
   - mirrors firmware release assets to `warwickchapman/lora-rs-firmware`
3. Firmware asset names must be:
   - `lrs-firmware-<version>-za.bin`
   - `lrs-firmware-<version>-us.bin`
   - `lrs-firmware-<version>-eu.bin`

### 3) Flasher release path
1. Decide whether flasher must be rebuilt:
   - if `tools/flasher/**` changed since the source release, rebuild
   - otherwise existing flasher assets may be reused
2. Before any flasher build:
   - `python3 tools/flasher/sync_version.py`
3. Build macOS flasher locally:
   - arm64 DMG
   - x64 DMG
4. Build Windows/Linux flasher assets via CI.
5. Upload flasher assets to both repos with `gh release upload --clobber`.

### 3a) Local ad-hoc flasher build path
Use this when the user wants a test artifact only, not a release.

1. Decide the local build version from current repo state:
   - release builds use exact `VERSION`
   - local ad-hoc builds use:
     - clean tree: `<next-patch>-dev.<shortsha>`
     - dirty tree: `<next-patch>-dev.<shortsha>.dirty`
2. Sync flasher metadata before building:
   - `python3 tools/flasher/sync_version.py`
3. Build the requested local artifact only:
   - for macOS testing, build the requested `.app`/`.dmg` locally
4. Report the exact output path, architecture, and checksum.
5. Do not create or update tags, releases, or release assets.

### 4) Release notes
Release notes must be:
- human-readable
- feature-focused
- not just a compare link

Include:
- short summary
- highlights
- firmware assets + SHA256 checksums

When used in this repo’s established release flow, the final notes may also include:
- one-word release name
- George Bernard Shaw quote

### 5) Asset contract enforcement
Current release contract:
- Firmware: 3
  - `lrs-firmware-<version>-za.bin`
  - `lrs-firmware-<version>-us.bin`
  - `lrs-firmware-<version>-eu.bin`
- Flasher: 7
  - `thanda-lora-flasher-<version>-windows-x64.msi`
  - `thanda-lora-flasher-<version>-windows-x64-portable.zip`
  - `thanda-lora-flasher-<version>-linux-x64.deb`
  - `thanda-lora-flasher-<version>-linux-x64.rpm`
  - `thanda-lora-flasher-<version>-linux-x64.AppImage.tar.gz`
  - `thanda-lora-flasher-<version>-macos-arm64.dmg`
  - `thanda-lora-flasher-<version>-macos-x64.dmg`
- Total binaries: 10

Enforce these rules:
- Windows ships `MSI + portable ZIP` only.
- Linux ships `.AppImage.tar.gz` only, not raw `.AppImage`.

## Validation

### Pre-publish
- `git status --short --untracked-files=no` is empty.
- `README.md` and `docs/DEVELOPER_GUIDE.md` still describe the same release process.
- `tools/flasher/sync_version.py` has been run before local flasher builds.

### Ad-hoc local build verification
- Confirm the local artifact version label is not reusing the last shipped release string.
- Confirm the reported output path and target architecture match the user request.
- If a DMG/app was produced, report a checksum for the final file.

### Post-publish
Check both repos:
- `warwickchapman/lora-rs`
- `warwickchapman/lora-rs-firmware`

Verify:
1. Tag, title, and asset names match `VERSION`.
2. Firmware assets are present for `za`, `us`, and `eu`.
3. Flasher assets match the 7-file contract above.
4. No raw `.AppImage` or Windows setup EXE slipped in.
5. Release notes contain summary, highlights, and firmware checksums.
6. Release docs remain consistent with the actual workflow used.

## Output contract
Always report:
1. Commit/tag used.
2. Whether flasher was rebuilt or reused, and why.
3. Final asset names.
4. Firmware SHA256 checksums.
5. Release URLs for both repos.
6. Any mismatch between docs, workflow, or published assets.

For local ad-hoc builds, report instead:
1. Git base used (`VERSION`, short SHA, dirty/clean state).
2. Derived local build version string.
3. Output artifact path.
4. Architecture/target built.
5. Final checksum.

## Boundaries
- This skill does not decide product scope; it only packages what belongs in the release.
- This skill does not silently include unrelated local changes.
- This skill does not invent new asset types or release policy.
- This skill does not replace project docs; it follows them and flags drift.
- This skill does not publish ad-hoc local builds unless the user later explicitly asks for release/publish work.

## Quick commands

```bash
# review release scope
git diff --name-only <last-tag>..HEAD
git log --oneline <last-tag>..HEAD
```

```bash
# clean tracked tree check
git status --short --untracked-files=no
```

```bash
# sync flasher versions before any flasher build
python3 tools/flasher/sync_version.py
```

```bash
# standard firmware release path
python3 tools/release_manager.py --summary "<summary>" --highlight "<highlight>"
```

```bash
# local flasher metadata sync before ad-hoc build
python3 tools/flasher/sync_version.py
```
