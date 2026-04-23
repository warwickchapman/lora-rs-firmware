# Deploy Report

- Commit: `<hash>` `<message>`
- Build env: `lrs_za`
- Build metrics:
  - RAM: `<used>/<total>`
  - Flash: `<used>/<total>`
  - Size: `text=<n> data=<n> bss=<n> dec=<n>`
- Delta vs previous:
  - RAM: `<delta>`
  - Flash: `<delta>`
  - text/data/bss/dec: `<delta tuple>`

## Targets
- `<target-id>` `<method>` `<result>`
- `<target-id>` `<method>` `<result>`

## Post-deploy verification
- `<target-id>` runtime hash/version: `<verified|unreachable from shell|pending user check>`
- `<target-id>` runtime hash/version: `<verified|unreachable from shell|pending user check>`

## Notes
- `<busy port resolved / network visibility limits / retry info>`
