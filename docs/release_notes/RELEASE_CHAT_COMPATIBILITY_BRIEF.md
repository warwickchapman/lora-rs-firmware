# Release-chat compatibility brief

Copy this completed brief into the dedicated release chat before publishing.

- Release: `vX.Y.Z`
- Compatibility revision: `N` from `RELEASE_COMPATIBILITY.json`
- Minimum Flasher version: `X.Y.Z`
- Firmware scope: gateway / remote / both
- Flasher assets: rebuild required / reuse permitted, with reason
- Device impact: which devices lock until Flasher is updated
- Upgrade path: direct flashing remains available; normal device control is blocked until compatible
- Validation evidence: native tests, both firmware builds, Flasher tests/build when changed, and bench result
- Decision: `PROCEED` / `HOLD`, with owner
