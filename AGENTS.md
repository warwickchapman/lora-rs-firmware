# Project Instructions

- Guide poor design, UX, or architecture decisions directly, with the reason and a simpler alternative.
- Favor simple, reliable firmware and operator workflows over cleverness.
- Keep UX clean, intuitive, dense where useful, and neatly ordered.
- Maintain documentation and human-readable changelog entries when behavior changes.
- Do not discard uncommitted work. Inspect `git status --short` before editing.

## Dev Build Versioning

- The repo-root `VERSION` file is the operator-visible firmware and Flasher version source of truth.
- Every development build that will be flashed, shared, or used for field testing must get a unique dev build revision before building.
- Use `MAJOR.MINOR.PATCH~DEVBUILD`, for example `0.9.2~1`, `0.9.2~2`, and `0.9.2~3`.
- The `~DEVBUILD` suffix implies a development build; do not add `-dev`.
- If `VERSION` is `0.9.2-dev`, the next development build is `0.9.2~1`.
- Increment only the dev build revision while staying on the same base patch.
- Do not reuse a dev build version for a different firmware or Flasher artifact.
- Use `python3 tools/bump_dev_build.py` before a new development build.
- Firmware builds record the source tree used for each `~DEVBUILD` value and reject reusing the same dev build number after code changes.
