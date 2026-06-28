# Project Instructions

- Guide poor design, UX, or architecture decisions directly, with the reason and a simpler alternative.
- Favor simple, reliable firmware and operator workflows over cleverness.
- Keep UX clean, intuitive, dense where useful, and neatly ordered.
- Maintain documentation and human-readable changelog entries when behavior changes.
- Do not discard uncommitted work. Inspect `git status --short` before editing.

## Plan Review Discipline

When reviewing an implementation plan against a defined boundary or constraint:

- Do not approve from a checklist. Test each item against the boundary rule.
- For every injected dependency, method, or option: state whether it is in-scope model/state or out-of-scope orchestration. Flag violations explicitly.
- Before approving, argue the rejection case. List what should be removed and why.
- Treat "not a blocker" as a decision that requires justification, not a default.

## Implementation Review Discipline

When reviewing an implementation against an approved plan:

- Verify: (1) the API matches what was approved, (2) no operator-facing behaviour changed unless explicitly planned, (3) every boundary rule from the plan is respected in the code, (4) tests cover the behaviours listed in the plan, not just happy paths.
- For every adapter/computed mapping, verify that every referenced variable/function exists in the current source and has the correct name and type.
- For every field, computed, template binding, and constant in the child, verify it matches the original behaviour in the source. Compare adapter mappings against the original expressions line by line.
- Flag anything added beyond the plan scope and anything from the plan that was skipped or simplified.

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
