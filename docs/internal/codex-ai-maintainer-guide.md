# Codex / AI Maintainer Guide

## Goal
Enable safe AI-assisted maintenance on an ESP8266 firmware codebase with tight heap/fragmentation constraints.

## What To Read First (by task type)
- Architecture/takeover: `architecture-map.md`, `file-ownership-map.md`
- Runtime-sensitive changes: `runtime-invariants.md`, `memory-budget-and-measurement.md`
- API/admin changes: `admin-api-contract-notes.md`
- Refactors/splits: `change-playbook.md`, ADRs

## High-Risk Areas (Read Before Editing)
- `NodeStateMachine` replay, RX dispatch, provisioning state machines
- `App::tick()` ordering and apply boundary behavior
- serial admin status/config behavior and settings apply path
- `RadioProtocol` crypto/MAC/default-key gating

## Validation Checklist (Minimum)
- Compile target(s) relevant to change
- Runtime memory metrics before/after (`max_free_block`, `free_heap`, `frag%`)
- Behavior checks for affected routes/protocol flows
- Confirm no route/payload/protocol semantic drift unless explicitly planned

## Anti-Patterns to Avoid
- Introducing generic frameworks/abstractions during split phases
- Adding heapful wrappers for route registration
- Reintroducing `Settings` deep copies in hot modules
- Using `String`/JSON convenience rewrites in hot or polled paths
- Combining refactor + behavior fix in a single commit without clear exception scope

## Safe Defaults
- same-class multi-`.cpp` split
- static/anonymous-namespace helpers in owning `.cpp`
- explicit lifecycle/ownership over abstraction
- document assumptions when uncertain
