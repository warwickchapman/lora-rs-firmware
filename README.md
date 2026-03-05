# LoRa-RS Firmware Hub

Public release repository for **LoRa-RS** firmware and flasher binaries.

## What You Get Per Release

Each release tag (for example `v0.5.8-alpha`) is expected to include:

- Firmware binaries
  - `lrs-firmware-<version>-za.bin`
  - `lrs-firmware-<version>-us.bin`
  - `lrs-firmware-<version>-eu.bin`
- Flasher desktop binaries
  - macOS: arm64 + x64 DMG
  - Windows: setup EXE + MSI + portable ZIP
  - Linux: `.deb` + `.rpm` + `.AppImage.tar.gz`
- `SHA256SUMS.txt` for asset integrity verification

## Region Notes

- `US` build uses LoRa **915 MHz** defaults.
- `ZA` and `EU` builds currently use LoRa **433 MHz** defaults.

## Verify Downloads

From a terminal in your downloads folder:

```bash
sha256sum -c SHA256SUMS.txt
```

On macOS:

```bash
shasum -a 256 -c SHA256SUMS.txt
```

## Install / Flash

- End users should use the released **Thanda LoRa Flasher** desktop app.
- Advanced users can flash firmware directly using `esptool` if needed.

## Source Repository

Main development repository:

- https://github.com/warwickchapman/lora-rs

Release notes for each tag contain feature-focused highlights and known changes.
