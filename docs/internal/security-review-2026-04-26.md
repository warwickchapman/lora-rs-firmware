# Security Review Report - 2026-04-26

## Scope
This review covered the LoRa RS ESP8266 firmware, the Tauri desktop flasher, release/flashing tooling, and the current operator/developer documentation.

The review focused on:
- Device access control and credential lifecycle.
- LoRa packet authentication/encryption and provisioning flows.
- Web console, OTA, MQTT, and SoftAP exposure.
- Flasher supply-chain trust and local execution permissions.
- Documentation and operational risks outside the code.

This was a source review. No firmware or flasher patch was applied as part of this report.

## Executive Summary
The project already has several good security foundations: the web console has an authenticated session model, normal LoRa traffic is blocked while the default fleet key is configured, LoRa packets use keyed authentication plus encryption, and many API reads redact secrets.

The highest-priority risks are not exotic protocol issues. They are credential lifecycle and release trust:

1. Factory/admin passwords are deterministic from public device identity and a shared product secret embedded in shipped software.
2. Real-looking OTA/admin credentials are present in `platformio.ini`.
3. The flasher computes downloaded firmware hashes but does not verify them against a trusted signature or manifest before flashing.

Those three items should be treated as the first remediation wave.

## Findings

### P1: Deterministic factory/admin password is recoverable
Evidence:
- `src/config_store.cpp` embeds the product secret and derives an 8-character password from chip ID.
- `tools/flasher/src-tauri/src/commands/device.rs` contains the same derivation rule.
- The SoftAP SSID exposes the chip ID as `lrs-<chipid>`.

Impact:
Anyone who can see the SoftAP SSID and knows or extracts the derivation rule can compute the AP/admin password offline. Because the flasher must be distributed to installers, the embedded derivation rule should be assumed public.

Recommendation:
- Move to per-device random factory credentials generated during manufacturing/provisioning.
- Store the random credential on the device and print it on the sticker/report.
- Force admin password rotation on first successful login or first commissioning.
- Stop shipping the shared product-secret derivation rule in the flasher.
- As an interim improvement only, lengthen generated credentials and remove real deployment secrets from publicly distributed tooling.

Notes:
The current deterministic flow is operationally convenient, but it makes every device credential recoverable from a visible identifier once the shared rule is known. The safer alternative is a factory CSV/profile workflow where recovery is handled by controlled records, not by a global derivation secret.

### P1: Real OTA/admin credentials are committed
Evidence:
- `platformio.ini` contains per-device OTA environments with LAN IPs and `--auth` values.
- Some entries use weak placeholder-like passwords.

Impact:
If the repository or build machine is shared, backed up, copied, or exposed, those devices should be considered compromised for OTA/admin access. The IP/SSID-style comments also leak deployment topology.

Recommendation:
- Rotate credentials for all devices referenced by committed OTA auth values.
- Remove per-device secrets from tracked files.
- Replace tracked OTA environments with templates using placeholders.
- Move real OTA targets/auth to an ignored local file, environment variables, or a local operator profile.
- Update docs to discourage putting admin passwords in shell history, release notes, screenshots, or support tickets.

### P1: Flasher does not authenticate firmware artifacts
Evidence:
- `tools/flasher/src-tauri/src/services/firmware.rs` downloads release assets from GitHub.
- `tools/flasher/src-tauri/src/commands/flash.rs` calculates and logs SHA256, but does not compare it to a trusted value.

Impact:
The hash proves only what was downloaded, not whether it is the intended firmware. A compromised GitHub account/release, mistaken asset upload, or malicious release asset would still be flashed.

Recommendation:
- Publish a signed manifest per release containing asset names, SHA256 hashes, version, region, and build metadata.
- Verify the manifest signature inside the flasher using a pinned public key.
- Fail closed when the asset name, version, region, hash, or signature does not match.
- Keep local override flashing available, but label it as unverified/manual and require deliberate operator confirmation.

### P2: Crypto/session randomness depends on Arduino `random()`
Evidence:
- `src/radio_protocol.cpp` uses `random()` for LoRa packet nonces and boot nonce.
- `src/web_console.cpp` uses `random()` for session tokens.
- No central seeding path was found in the reviewed sources.

Impact:
If the PRNG state is predictable across boots, web session tokens may be guessable and AES-CTR nonce reuse becomes more plausible after counter reset/reboot patterns.

Recommendation:
- Add a central secure-random helper for firmware.
- Seed it early from ESP8266 hardware/system entropy where available, mixed with timing jitter and device identity.
- Use that helper for web sessions, LoRa packet nonces, boot nonces, provisioning session nonces, and transfer IDs.
- Add a short developer note explaining which randomness is security-critical.

### P2: Plaintext management/control channels need stronger operational boundaries
Evidence:
- The first-access flow uses HTTP on the SoftAP.
- ArduinoOTA uses the admin password.
- MQTT uses `WiFiClient`/PubSubClient without TLS.
- Docs currently describe setup and OTA, but do not clearly state a network isolation requirement.

Impact:
On a shared or hostile WiFi/LAN, local attackers may observe or manipulate management traffic and MQTT control messages. ESP8266 constraints make full TLS everywhere difficult, so operational isolation matters.

Recommendation:
- Document a hard requirement for isolated installer/OT networks.
- Default `ap_always_on` to off after commissioning where operationally acceptable.
- Prefer MQTT only on a trusted broker/VLAN/VPN, with broker ACLs per device/topic root.
- Treat OTA as a maintenance-network operation, not a general LAN feature.
- Add user-facing warnings around MQTT control and fleet WiFi provisioning.

### P3: Tauri app runs with CSP disabled
Evidence:
- `tools/flasher/src-tauri/tauri.conf.json` sets `"csp": null`.
- The app also allows spawning the esptool sidecar.

Impact:
The app currently appears bundled/local, so immediate exploitability is limited. But disabling CSP weakens the blast-radius controls if future UI changes introduce remote content, unsafe HTML, or dependency-level injection.

Recommendation:
- Add a restrictive CSP for the bundled frontend.
- Keep shell permissions constrained to the esptool sidecar.
- Avoid loading remote UI assets.
- Re-check the final app with the production bundle, not only Vite dev mode.

## Operational and Social Risks

### Stickers, CSVs, and support screenshots
Factory stickers, flasher logs, provisioning CSVs, and screenshots may contain enough data to access devices or derive credentials under the current model.

Actions:
- Treat factory CSVs and deployment reports as secret material.
- Redact passwords and fleet keys from screenshots before support sharing.
- Avoid sending admin passwords over chat/email where possible.
- Add a support checklist for what fields are safe to share.

### LoRa jamming and proximity attacks
LoRa encryption/authentication helps with spoofing but does not prevent RF denial of service.

Actions:
- Document expected behavior during RF loss.
- Set RX fail-safe mode intentionally per installation.
- Add commissioning checks for link margin, antenna placement, and interference.
- Avoid relying on LoRa for safety-critical emergency stop behavior unless an independent fail-safe exists.

### Fleet key quality
The fleet key is the real LoRa security boundary after commissioning.

Actions:
- Prefer generated random or multi-word high-entropy keys.
- Avoid customer/site names alone.
- Add a staged fleet-key rotation workflow.
- Record key ownership and recovery process in deployment handover notes.

### Installer workstation trust
The flasher reads chip identity, displays credentials, downloads firmware, and invokes a bundled esptool.

Actions:
- Distribute signed/notarized flasher binaries.
- Verify flasher app signatures in release QA.
- Train installers to download from the official release location only.
- Avoid running the flasher from untrusted USB drives or copied archives of unknown origin.

## Recommended Remediation Plan

### Phase 0: Immediate containment
- Rotate credentials for devices whose OTA/admin secrets are present in tracked files.
- Remove real OTA secrets from `platformio.ini`.
- Add a local-only OTA environment template and update docs.
- Communicate that current deterministic factory credentials are not suitable as long-term admin credentials.

### Phase 1: Release trust
- Add signed firmware manifest generation to the release pipeline.
- Teach the flasher to verify manifest signature and asset hash before flashing.
- Add a clear warning path for unverified local `.bin` files.
- Include verification status in flasher logs.

### Phase 2: Credential redesign
- Design per-device random factory credential storage and sticker/report output.
- Update factory tooling and flasher UI to display generated credentials without embedding a shared derivation secret.
- Force admin password rotation during first commissioning.
- Add a migration path for existing deterministic-password devices.

### Phase 3: Runtime hardening
- Add a central firmware secure-random helper and migrate session/protocol nonce generation to it.
- Review session cookie flags and CSRF exposure after auth changes.
- Review config export/import so default export redacts secrets.
- Decide whether SoftAP should default off after successful STA commissioning.

### Phase 4: Operational documentation
- Add an operator security section to user docs.
- Add release/verifier instructions to developer docs.
- Add support-handling guidance for secrets in screenshots, logs, stickers, CSVs, and provisioning profiles.
- Document network segmentation expectations for HTTP, OTA, and MQTT control.

## Open Decisions for the Team
- Should first-login password rotation be mandatory for every device, or only recommended?
- Should the product keep deterministic password recovery for lab/dev builds only?
- Where should per-device random credentials be stored in the factory flow: CSV only, local encrypted store, customer handover record, or all three?
- Which signing mechanism should be used for release manifests: minisign, cosign, GPG, or a small custom Ed25519 verifier?
- Should MQTT TLS be deferred because of ESP8266 memory constraints, or should MQTT control be documented as requiring broker/VPN/VLAN isolation?
- Should config export default to redacted with a separate "include secrets" option?

## Review Outcome
The core architecture is workable, but the security posture depends too heavily on shared secrets, trusted local networks, and trusted release hosting. The recommended direction is to keep the existing simple operational model, but move trust anchors into places that are easier to reason about:

- Per-device random credentials instead of global derivation.
- Signed firmware manifests instead of bare downloaded assets.
- Explicit network isolation guidance instead of implicit LAN trust.
- Redacted support artifacts by default.
