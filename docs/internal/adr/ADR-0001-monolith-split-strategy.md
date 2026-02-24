# ADR-0001: Monolith Split Strategy (Same-Class Multi-`.cpp` First)

## Status
Accepted (Milestone 1 baseline)

## Decision
Use behavior-preserving same-class multi-`.cpp` splits before any class extraction or framework abstraction.

## Rationale
- Lowest semantic risk on ESP8266 firmware with stability-sensitive runtime paths.
- Reduces file size/cognitive load for maintainers and AI tools.
- Preserves ownership/lifecycle and avoids abstraction overhead.

## Consequences
- Temporary duplication may remain until post-split cleanup phases.
- Header may remain large, but implementation risk is reduced significantly.
