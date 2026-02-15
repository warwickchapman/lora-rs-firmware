#!/usr/bin/env python3
import argparse
import random

ADJ = [
    "amber", "brisk", "clear", "delta", "eager", "frost", "grand", "harbor",
    "ivory", "jolly", "keen", "lunar", "maple", "noble", "opal", "proud",
    "quiet", "rapid", "solar", "tidal", "urban", "vivid", "wild", "young",
]
NOUN = [
    "anchor", "bridge", "canyon", "diesel", "engine", "field", "geyser",
    "harvest", "island", "junction", "kernel", "lantern", "meadow",
    "network", "orchard", "pump", "quarry", "relay", "switch", "tank",
    "uplink", "valve", "water", "yard",
]
TAIL = [
    "alpha", "beacon", "cobalt", "drift", "ember", "forge", "grove", "horizon",
    "ion", "jet", "kiln", "leaf", "mesa", "nova", "orbit", "pulse",
    "quest", "ridge", "stone", "trail", "unity", "vista", "wave", "zen",
]


def make_key(rng: random.Random) -> str:
    return f"{rng.choice(ADJ)}-{rng.choice(NOUN)}-{rng.choice(TAIL)}"


def main():
    ap = argparse.ArgumentParser(description="Generate readable three-word deployment keys for factory batches")
    ap.add_argument("--count", type=int, default=1, help="Number of keys to output")
    ap.add_argument("--seed", type=int, default=None, help="Optional deterministic seed")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    seen = set()
    out = []
    while len(out) < max(1, args.count):
        key = make_key(rng)
        if key in seen:
            continue
        seen.add(key)
        out.append(key)

    for k in out:
        print(k)


if __name__ == "__main__":
    main()

