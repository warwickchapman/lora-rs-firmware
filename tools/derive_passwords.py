#!/usr/bin/env python3
import argparse
import hashlib
import re
import sys

PRODUCT_SECRET = "LRS-v1-rotate-this-secret"


from chip_id_helper import parse_canonical_chip_id


def _normalize_chip(value: str) -> str:
    return parse_canonical_chip_id(value)


def _derive_password(chip_hex: str) -> str:
    # Keep aligned with src/config_store.cpp deriveShortPassword().
    digest = hashlib.sha256(f"{PRODUCT_SECRET}:{chip_hex}".encode()).hexdigest().lower()
    return digest[:8]


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Derive factory AP/admin passwords from LRS chip IDs or lrs-<chipid> SSIDs."
    )
    ap.add_argument(
        "values",
        nargs="+",
        help="Chip IDs or SSIDs, for example: 0048cb85 lrs-00fc4f9d",
    )
    args = ap.parse_args()

    had_error = False
    for value in args.values:
        try:
            chip = _normalize_chip(value)
            password = _derive_password(chip)
            print(f"lrs-{chip} -> {password}")
        except ValueError as exc:
            had_error = True
            print(f"{value} -> error: {exc}", file=sys.stderr)

    return 1 if had_error else 0


if __name__ == "__main__":
    raise SystemExit(main())
