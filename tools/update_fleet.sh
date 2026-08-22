#!/usr/bin/env bash
# Bootstrap the home fleet using the current ZA firmware.bin.
#
# This deliberately uses direct espota.py, not Flasher Remote OTA Pull. Older
# remotes cannot acknowledge the new LoRa manifest protocol until bootstrapped.

set -e
set -o pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIRMWARE="$ROOT/.pio/build/lrs_za/firmware.bin"
ESPOTA="${HOME}/.platformio/packages/framework-arduinoespressif8266/tools/espota.py"
DERIVE_PASSWORD="$ROOT/tools/derive_passwords.py"
APPLY=false
ALL=false

usage() {
  cat <<'EOF'
Usage: tools/update_fleet.sh [--apply] [--all] [remote-number ... | g]

Default: print the direct-OTA bootstrap plan for the devices known to need a
bootstrap from the captured Fleet inventory.
--apply: perform the updates, one device at a time.
--all: include every known device, including those not selected by default.
remote-number: update one remote, for example 4 6 7.
g: update the gateway.

The script derives credentials locally from the chip ID. It never prints them.
It stops at the first failed or indeterminate espota.py result; inspect that
device before trying it again.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --apply) APPLY=true ;;
    --all) ALL=true ;;
    --help|-h)
      usage
      exit 0
      ;;
    *) break ;;
  esac
  shift
done

# Inventory from the 2026-08-22 Fleet view. Keep this as chip ID plus IP only:
# passwords are derived at execution time from the canonical source tool.
name=()
chip=()
ip=()
needs_bootstrap=()

name[1]="12"
chip[1]="lrs-00fc4f9f"
ip[1]="192.168.133.23"
needs_bootstrap[1]=true

name[2]="11"
chip[2]="lrs-00fc4f9e"
ip[2]="192.168.133.24"
needs_bootstrap[2]=true

name[3]="10"
chip[3]="lrs-00fc4f9d"
ip[3]="192.168.133.25"
needs_bootstrap[3]=true

name[4]="7"
chip[4]="lrs-0029ca55"
ip[4]="192.168.133.26"
needs_bootstrap[4]=true

name[5]="8"
chip[5]="lrs-004a9c27"
ip[5]="192.168.133.27"
needs_bootstrap[5]=true

name[6]="9"
chip[6]="lrs-00fc4f9c"
ip[6]="192.168.133.28"
needs_bootstrap[6]=true

name[7]="6"
chip[7]="lrs-0029ca51"
ip[7]="192.168.133.30"
needs_bootstrap[7]=true

name[8]="4"
chip[8]="lrs-0029ca6f"
ip[8]="192.168.133.22"
needs_bootstrap[8]=true

name[9]="g"
chip[9]="lrs-0048cb85"
ip[9]="192.168.133.20"
needs_bootstrap[9]=true

# Not selected by default because they were already current in the captured view.
name[10]="3"
chip[10]="lrs-000af8e6"
ip[10]="192.168.133.21"
needs_bootstrap[10]=false

name[11]="5"
chip[11]="lrs-0048d1bb"
ip[11]="192.168.133.29"
needs_bootstrap[11]=false

name[12]="2"
chip[12]="lrs-000af8d9"
ip[12]="192.168.133.31"
needs_bootstrap[12]=false

name[13]="1"
chip[13]="lrs-000af8ce"
ip[13]="192.168.133.32"
needs_bootstrap[13]=false

TOTAL_UNITS=13
targets=()

requested_names=("$@")
for requested in "${requested_names[@]}"; do
  found=false
  for ((i = 1; i <= TOTAL_UNITS; i++)); do
    if [[ "${name[$i]}" == "$requested" ]]; then
      found=true
      break
    fi
  done
  if [[ "$found" != true ]]; then
    echo "Unknown target '$requested'. Use remote numbers 1-12 or g for the gateway." >&2
    exit 2
  fi
done

add_target_by_name() {
  local candidate="$1"
  for ((i = 1; i <= TOTAL_UNITS; i++)); do
    if [[ "${name[$i]}" == "$candidate" ]]; then
      targets+=("$i")
      return
    fi
  done
}

if [[ ${#requested_names[@]} -gt 0 ]]; then
  for requested in "${requested_names[@]}"; do
    add_target_by_name "$requested"
  done
elif [[ "$ALL" == true ]]; then
  for requested in 1 2 3 4 5 6 7 8 9 10 11 12 g; do
    add_target_by_name "$requested"
  done
else
  for requested in 4 6 7 8 9 10 11 12 g; do
    add_target_by_name "$requested"
  done
fi

if [[ ${#targets[@]} -eq 0 ]]; then
  echo "No targets selected. Known names: ${name[*]}" >&2
  exit 2
fi

if [[ "$APPLY" == true ]]; then
  [[ -f "$FIRMWARE" ]] || { echo "Firmware not found: $FIRMWARE" >&2; exit 2; }
  [[ -f "$ESPOTA" ]] || { echo "espota.py not found: $ESPOTA" >&2; exit 2; }
  [[ -f "$DERIVE_PASSWORD" ]] || { echo "Password derivation tool not found: $DERIVE_PASSWORD" >&2; exit 2; }
fi

echo "Home fleet direct OTA bootstrap"
echo "Firmware: $FIRMWARE"
echo "Mode: $([[ "$APPLY" == true ]] && echo APPLY || echo DRY-RUN)"
echo

for i in "${targets[@]}"; do
  printf '%-10s %-14s %s' "${name[$i]}" "${chip[$i]}" "${ip[$i]}"
  if [[ "${needs_bootstrap[$i]}" == true ]]; then
    printf '  bootstrap\n'
  else
    printf '  not selected by default\n'
  fi
done

if [[ "$APPLY" != true ]]; then
  echo
  echo "Dry run only. Re-run with --apply after confirming this inventory."
  exit 0
fi

for i in "${targets[@]}"; do
  derived_line="$(python3 "$DERIVE_PASSWORD" "${chip[$i]}")" || {
    echo "Password derivation failed for ${name[$i]}" >&2
    exit 1
  }
  auth="${derived_line##* -> }"
  if [[ -z "$auth" || "$auth" == "$derived_line" ]]; then
    echo "Password derivation produced an unexpected result for ${name[$i]}" >&2
    exit 1
  fi

  echo
  echo "Updating ${name[$i]} (${chip[$i]}) at ${ip[$i]}..."
  if ! python3 "$ESPOTA" -i "${ip[$i]}" -p 8266 -a "$auth" -f "$FIRMWARE"; then
    echo "STOP: espota.py did not confirm ${name[$i]}. Do not retry automatically." >&2
    echo "Inspect the device and Flasher state before running this target again." >&2
    exit 1
  fi
  echo "OTA upload completed for ${name[$i]}."
done

echo
echo "Direct OTA bootstrap completed. Use Flasher to confirm every device reports the firmware version you built."
