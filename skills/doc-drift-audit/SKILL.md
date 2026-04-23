---
name: doc-drift-audit
description: use this when documentation drift must be audited by comparing docs to code before making fixes, especially for provisioning, automations, APIs, defaults, setting names, and role terminology.
---

# doc-drift-audit

## Purpose
Audit documentation drift by checking code and docs together, reporting findings first, then patching only confirmed drift.

## Use when
- User asks whether docs were updated correctly.
- User asks to review or fix docs for provisioning, automations, APIs, defaults, setting names, or roles.
- A change likely left stale names, defaults, endpoints, examples, or compatibility notes in docs.

## Do not use when
- The task is pure writing with no need to verify against code.
- The user wants product redesign or new semantics.
- The task is unrelated to docs, settings, APIs, provisioning, automations, or role terminology.

## Required inputs
- Repo/workspace path.
- Audit scope.
- Any known target docs or files from the user.

## Preconditions
- Relevant code and docs are available locally.
- You can inspect:
  - route registration
  - settings/config defaults and names
  - mode/role mapping logic
  - provisioning behavior when in scope
  - automation guardrails when in scope

## Workflow

### 1) Define scope
- Keep the audit limited to the requested area.
- Default validated scope from this project:
  - API route registration
  - settings names
  - defaults
  - provisioning behavior
  - automation guardrails when relevant
  - mode/role terminology

### 2) Inspect code first
- Check route registration in the web/API route table.
- Check config/settings load, save, and defaults.
- Check mode/role mapping logic.
- Check automation runtime guardrails if automations are in scope.
- Check provisioning behavior only where implemented and confirmed.

### 3) Extract validated facts
- Record only facts confirmed in code:
  - registered endpoints
  - current setting names
  - current defaults
  - canonical mode/role mappings
  - current guardrails/constraints
  - current provisioning behavior

### 4) Compare code vs docs
- Search likely documentation targets first:
  - `docs/USER_GUIDE.md`
  - `docs/DEVELOPER_GUIDE.md`
  - `docs/PROVISIONING.md`
  - `docs/PRODUCT_MANUAL.md`
  - `CHANGELOG.md`
- Include canonical spec/contract docs only when the scope touches them.
- Look for:
  - stale setting names
  - stale defaults
  - stale endpoint lists
  - stale examples
  - stale compatibility notes
  - old terminology replacing current external terms

### 5) Use canonical external terminology
- Prefer validated external terms:
  - `standalone`
  - `paired`
  - `mesh`
- Use mode-aware role terms in docs and user-facing explanations.
- Mention internal `role_tx` only when explaining implementation/runtime behavior.

### 6) Report findings before patching
- Findings come first.
- Order by severity.
- Use file references and line references when possible.
- If no findings are discovered, say so explicitly before any summary.

### 7) Patch confirmed drift only
- Update docs to match verified code.
- Do not redesign product behavior.
- Do not invent new semantics.
- If behavior is nuanced, document only what is currently implemented.

### 8) Update changelog when docs change
- If the audit results in meaningful repo doc edits, add a concise changelog note for the alignment pass.

### 9) Final verification sweep
- Re-search for stale names/defaults/old terms in the audited scope.
- Verify updated docs now match:
  - registered routes
  - current setting names
  - current defaults
  - current mode/role mapping
- Confirm no unrelated files were modified.

## Validation
- Route list matches route registration.
- Settings names in docs match current code.
- Defaults in docs match current code.
- Provisioning behavior is documented only if confirmed in code.
- Automation guardrails are documented only if confirmed in code.
- Canonical external role terms are used where validated.

## Output contract
Always provide:
1. Findings first.
2. A brief change summary after findings.
3. The docs updated.
4. Whether `CHANGELOG.md` was updated.
5. Any remaining uncertainty or unverified area.

## Boundaries
- Do not patch code unless explicitly asked.
- Do not broaden the audit beyond the requested scope without a concrete reason.
- Do not turn implementation quirks into product promises unless they are intentionally documented and confirmed.
- Do not replace confirmed current behavior with preferred behavior.

## Example triggers
- "Review the docs for provisioning and automation drift."
- "Make sure the documentation matches the current commissioning and roles behavior."
- "Check whether the setting rename was reflected everywhere."
- "Audit API/docs drift for provisioning."
- "Verify docs were updated for standalone, paired, and mesh roles."
