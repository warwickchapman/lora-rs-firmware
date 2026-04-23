---
name: lora-provisioning-deploy-and-verify
description: use this when firmware changes must be committed, built, deployed (ota or usb) to specific lora devices, and verified with build deltas and per-target results.
---

# lora-provisioning-deploy-and-verify

## Purpose
Run the validated provisioning/stabilization deployment workflow from this project thread:
- commit first
- build from clean tree
- report memory/flash deltas
- deploy only to intended targets
- verify what actually landed

## Use when
- User asks to deploy/upload/push firmware to coordinator and/or targets.
- User asks for USB flashing with changing `/dev/tty.usbserial-*` ports.
- User asks for reproducible build/deploy/verify reporting.

## Do not use when
- No deployment is requested.
- User explicitly wants uncommitted local-only testing.

## Required inputs
- Target scope: `coordinator-only`, `targets-only`, or `all`.
- Deployment path: `ota` or `usb`.
- If USB: available serial ports.
- If OTA: IP/passwords or approved batch script inputs.
- Previous build metrics (if available) to compute deltas.

## Preconditions
- Build env: `python3 -m platformio run -e lrs_za` works.
- Deployment candidate commit is created before upload.
- Working tree is clean before build/deploy.

## Workflow

### 1) Lock scope
- Restate target devices and deployment method.
- Do not flash unspecified devices.

### 2) Commit-first gate
1. Stage only intended files.
2. Commit with clear message.
3. Confirm clean tree:
   - `git status --short` must be empty before build/deploy.

### 3) Build + metrics
1. Build:
   - `python3 -m platformio run -e lrs_za`
2. Size report:
   - `python3 -m platformio run -e lrs_za -t size`
3. Capture and report:
   - RAM used / total
   - Flash used / total
   - `text`, `data`, `bss`, `dec`
4. Report deltas vs previous build in-thread when available.

### 4) Deploy branch

#### A) OTA path (primary when reachable)
- Use established project OTA flow for requested scope (for example `tools/ota_batch.py` with requested location/flags).
- Keep allocation exactly as requested (for example coordinator no reset, targets with `--factory-reset`).
- Record per-target success/failure.

#### B) USB path (primary for offline targets)
1. Identify each connected port by chip ID before flashing:
   - `python3 /Users/warwick/.platformio/packages/tool-esptoolpy/esptool.py --port <port> chip_id`
2. Map chip IDs to intended targets.
3. Flash only mapped target ports:
   - `python3 -m platformio run -e lrs_za -t upload --upload-port <port>`
4. If upload fails with `Resource busy`:
   - Find holder process (`lsof ...usbserial...`), stop it, retry upload.

#### C) OTA fallback sub-path (when normal PlatformIO OTA path is unreliable)
- Use direct ESP OTA tool with already built binary:
  - `python3 /Users/warwick/.platformio/packages/framework-arduinoespressif8266/tools/espota.py -i <ip> -p 8266 -a <ota_pass> -f .pio/build/lrs_za/firmware.bin`
- Apply only to explicitly requested targets.
- Record per-target result.

### 5) Post-deploy verification
1. Confirm upload success per target from tool output.
2. Verify runtime identity/hash/version where reachable (UI footer or status endpoint).
3. If shell cannot reach HTTP but upload succeeded, state that explicitly and mark runtime verification as user-side pending.

### 6) Final deployment report
- Use concise per-target reporting with:
  - commit hash used
  - build metrics + deltas
  - deploy method
  - per-target result (`ok`/`failed`)
  - verification status (`verified`/`unreachable from shell`)

## Output contract
Always provide:
1. Commit hash and message used for deployment.
2. Build metrics and deltas.
3. Per-target deployment results.
4. Verification status and any blocker.

## Boundaries
- This skill does not define provisioning protocol behavior.
- This skill does not change reset policy unless requested.
- This skill does not override target scope requested by user.
- This skill does not resolve unrelated repo changes; if unexpected changes appear, stop and ask.

## Quick command snippets

```bash
# clean-tree check
git status --short
```

```bash
# build + size
python3 -m platformio run -e lrs_za
python3 -m platformio run -e lrs_za -t size
```

```bash
# usb chip-id probe
python3 /Users/warwick/.platformio/packages/tool-esptoolpy/esptool.py --port /dev/tty.usbserial-2 chip_id
```

```bash
# usb flash single target
python3 -m platformio run -e lrs_za -t upload --upload-port /dev/tty.usbserial-2
```

```bash
# direct ota fallback
python3 /Users/warwick/.platformio/packages/framework-arduinoespressif8266/tools/espota.py \
  -i 192.168.0.170 -p 8266 -a <ota_pass> -f .pio/build/lrs_za/firmware.bin
```
