#!/usr/bin/env bash
# Update the fixed home test fleet using the current ZA firmware.bin.

set -e
set -o pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIRMWARE="$ROOT/.pio/build/lrs_za/firmware.bin"
ESPOTA="${HOME}/.platformio/packages/framework-arduinoespressif8266/tools/espota.py"
DERIVE_PASSWORD="$ROOT/tools/derive_passwords.py"
APPLY=false

usage() {
  cat <<'EOF'
Usage: tools/update_fleet.sh [--apply] [remote-number ... | g]

Default: print the direct-OTA plan for all 12 remotes and the gateway.
--apply: perform the updates, one device at a time.
remote-number: select specific remotes, for example 4 6 7.
g: update the gateway.

The script derives credentials locally from the chip ID. It never prints them.
It stops at the first failed or indeterminate espota.py result; inspect that
device before trying it again.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --apply) APPLY=true ;;
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
name[1]="12"
chip[1]="lrs-00fc4f9f"
ip[1]="192.168.133.23"

name[2]="11"
chip[2]="lrs-00fc4f9e"
ip[2]="192.168.133.24"

name[3]="10"
chip[3]="lrs-00fc4f9d"
ip[3]="192.168.133.25"

name[4]="7"
chip[4]="lrs-0029ca55"
ip[4]="192.168.133.26"

name[5]="8"
chip[5]="lrs-004a9c27"
ip[5]="192.168.133.27"

name[6]="9"
chip[6]="lrs-00fc4f9c"
ip[6]="192.168.133.28"

name[7]="6"
chip[7]="lrs-0029ca51"
ip[7]="192.168.133.30"

name[8]="4"
chip[8]="lrs-0029ca6f"
ip[8]="192.168.133.22"

name[9]="g"
chip[9]="lrs-0048cb85"
ip[9]="192.168.133.20"
name[10]="3"
chip[10]="lrs-000af8e6"
ip[10]="192.168.133.21"

name[11]="5"
chip[11]="lrs-0048d1bb"
ip[11]="192.168.133.29"

name[12]="2"
chip[12]="lrs-000af8d9"
ip[12]="192.168.133.31"

name[13]="1"
chip[13]="lrs-000af8ce"
ip[13]="192.168.133.32"

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
else
  for requested in 1 2 3 4 5 6 7 8 9 10 11 12 g; do
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

echo "Home test fleet direct OTA update"
echo "Firmware: $FIRMWARE"
echo "Mode: $([[ "$APPLY" == true ]] && echo APPLY || echo DRY-RUN)"
echo

for i in "${targets[@]}"; do
  printf '%-10s %-14s %s\n' "${name[$i]}" "${chip[$i]}" "${ip[$i]}"
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
echo "Direct OTA update completed. Use Flasher to confirm every device reports the firmware version you built."
