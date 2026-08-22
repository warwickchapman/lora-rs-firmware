# Remote OTA Pull Reliability and Observability Plan

Status: proposed. This document is plan-only; no firmware, Flasher, version,
build, device, git, or release action accompanies it.

## Scope and decision

This is a post-point-release correction to Flasher-triggered **remote** OTA
Pull. It concerns the handoff from a gateway to a remote over LoRa and the
operator-visible lifecycle that follows it.

It does not change gateway MQTT OTA, replace the Fleet workflow with
`espota.py`, weaken SHA-256 validation, add a whole-fleet firmware payload, or
make routine scans/diagnostics compete with relay and input control.

The recommended v1 design is deliberately conservative:

- one remote OTA operation in Flasher at a time;
- one acknowledged LoRa manifest handoff in the gateway at a time;
- one bounded re-send of the exact manifest only when its acknowledgement is
  absent;
- no automatic re-trigger after manifest acceptance, HTTP failure, or missing
  reboot/version confirmation;
- a compact transfer ID and destination on every OTA handoff/status event;
- explicit operator stages rather than generic `Retrying` or `OTA Failed`.

Parallel remote HTTP downloads are **not** approved in this change. The host
server can serve concurrent sockets, but this has not been demonstrated as a
reliable end-to-end product workflow and the current LoRa lifecycle cannot
correlate it truthfully.

This is a pre-1.0 protocol break. New firmware requires the new acknowledgement
frame; there is no fallback to the old fire-and-forget lifecycle. A remote on
older firmware must be updated through a direct recovery path before it can
participate in the new Flasher remote-OTA workflow.

## Current paths

### Direct `espota.py`

1. The host sends a UDP invitation directly to the remote's ArduinoOTA port
   (`8266` by default), then hosts a direct TCP upload server. `espota.py`
   calculates an MD5 for the upload protocol and sends the binary in 1460-byte
   TCP chunks. It has no gateway, LoRa manifest, Flasher firmware HTTP server,
   or remote Fleet row in the path. See the framework tool
   `tools/espota.py:72-215`.
2. The remote starts the ArduinoOTA UDP listener in `App::startOta()` and calls
   `ArduinoOTA.handle()` in the normal app tick when the heap gate allows it:
   [src/app.cpp](../src/app.cpp#L1136) and
   [src/app.cpp](../src/app.cpp#L262). `ArduinoOTA.begin(false)` suppresses
   mDNS advertisement, not the direct-IP UDP listener.
3. The Arduino framework validates the invitation/authentication and writes the
   TCP stream through `Update`; its listener is independent of the remote HTTP
   Pull code.

This direct path is an important control case. Field evidence that it is
reliable means the investigation must begin before remote WiFi/flash hardware,
not by blaming them.

### Flasher remote OTA Pull

1. Fleet calls `useFleetOta().flashLoraRemote()`, which places a row in a local
   queue. [useFleetOta.ts](../tools/flasher/src/composables/useFleetOta.ts#L168)
2. `processOtaQueue()` starts Flasher's local HTTP file server, obtains a LAN
   IP/port and SHA-256, then sends password-gated `remote_ota_pull` over serial
   or MQTT admin. [Flasher.vue](../tools/flasher/src/components/Flasher.vue#L911)
   and [useFleetOta.ts](../tools/flasher/src/composables/useFleetOta.ts#L308).
3. `AdminExecutor::handleRemoteOtaPull()` validates the destination, host,
   port, and 64-character SHA-256. It returns success when the gateway has
   *queued* the control transfer, not when the remote has it.
   [admin_executor.cpp](../src/admin_executor.cpp#L1580)
4. `NodeStateMachine::sendPeerOtaPullControl()` creates a one-byte transfer ID,
   then emits one start frame, four eight-byte hash frames, and one commit frame
   at 250 ms spacing. [state_machine.cpp](../src/state_machine.cpp#L1247).
5. The remote collects the fragments. On a complete commit it stores the host,
   port, SHA-256, and gateway source in `PendingCommandManager`; the next app
   tick consumes that request. [state_machine.cpp](../src/state_machine.cpp#L3790)
   and [app.cpp](../src/app.cpp#L581).
6. `otaPullFromUrl()` fetches `http://<host>:<port>/firmware.bin`, requires an
   HTTP `200` with content length, writes with a 512-byte stack buffer,
   enforces 15-second HTTP/read-progress limits, verifies SHA-256, ends the
   ESP updater, then the remote reboots. [ota_pull.cpp](../src/ota_pull.cpp#L33).
   The host server does supply `/firmware.bin` and `Content-Length` as expected.
   [network.rs](../tools/flasher/src-tauri/src/commands/network.rs#L74)
7. Flasher currently infers completion from scan/inventory version and uptime
   changes. Its follow-up starts LoRa inventory scans every four seconds during
   the operation. [useFleetOta.ts](../tools/flasher/src/composables/useFleetOta.ts#L118)
   and [useFleetInventory.ts](../tools/flasher/src/composables/useFleetInventory.ts#L99).

## Current evidence and failure-stage coverage

| Stage | Current evidence | Defect |
| --- | --- | --- |
| Admin request accepted | `remote_ota_pull` response | Means gateway queued work only. It is presented as a triggered download. |
| Gateway frames sent | `ota_pull_control_queued` / `_tx` logs | No transfer ID in the useful host-facing event fields; no remote receipt proof. |
| Remote start/hash/commit received | Remote-local `start_rx`, `incomplete`, `bad_hash`, `rx` logs | Not returned to the gateway. Flasher only tries to associate UDP/log text by chip ID or IP. |
| Remote accepts full manifest | `PendingCommandManager::requestOtaPull()` | No acknowledgement exists. A lost start, hash fragment, or commit leaves the gateway believing the handoff finished. |
| HTTP connect/download | Local errors such as `http_begin_failed`, `http_<code>`, `download_timeout` | The error remains on the remote; no compact gateway/Flasher status is sent. |
| SHA / flash write / updater end | `sha256_mismatch`, `update_write_failed`, `update_end_failed` | Same missing status path. SHA itself is correctly retained. |
| Reboot / new version | Flasher infers from uptime/version after scans | Not correlated to a transfer ID and can be delayed by low-priority observability. |

### Existing acknowledgement, retry, and correlation

- `OtaPullControl` has encrypted/authenticated LoRa frames and a transfer ID,
  but no acknowledgement, no sender wait state, and no retransmission. The
  transmitter clears `ota_pull_tx_` immediately after its sixth successful
  local `sendRaw()` call. [state_machine.cpp](../src/state_machine.cpp#L1272)
- `sendRaw()` only proves that the local LoRa driver accepted the frame; it
  cannot prove RF delivery. [radio_protocol.cpp](../src/radio_protocol.cpp#L127).
- The remote checks source and transfer ID while assembling the manifest, but
  discards the transfer ID once it commits to `PendingCommandManager`.
  [state_machine.cpp](../src/state_machine.cpp#L3827) and
  [pending_command_manager.cpp](../src/pending_command_manager.cpp#L85).
- Flasher currently accepts up to six active pulls, spaces triggers by 500 ms, and treats
  `gateway_busy` as a short reschedule. The gateway itself needs about 1.25
  seconds between the first and final manifest frame, and in reality, accepts one manifest
  transfer while `ota_pull_tx_` is active (meaning Flasher repeatedly encounters/reschedules
  `gateway_busy`). This is a real concurrency mismatch, not remote HTTP parallelism. See
  [useFleetOta.ts](../tools/flasher/src/composables/useFleetOta.ts#L22) and
  [admin_executor.cpp](../src/admin_executor.cpp#L1614). The proposed implementation deletes
  this parallel-active abstraction in Flasher.
- Flasher automatically retries three times after an eight-second apparent
  activity stall or a 45-second no-log timeout. Those timers can run while the
  original remote is downloading, because `ota_downloading` begins on admin
  acceptance rather than manifest receipt. [useFleetOta.ts](../tools/flasher/src/composables/useFleetOta.ts#L198)
  and [useFleetOta.ts](../tools/flasher/src/composables/useFleetOta.ts#L283).
- `handleOtaLogLine()` changes a Fleet row only after a best-effort log-to-row
  match. The listener finds a row only if a UDP line contains `[lrs-<chip>]` or
  begins with the row's IP. The current OTA events usually expose only generic
  `counter` and `state` fields. [Flasher.vue](../tools/flasher/src/components/Flasher.vue#L5898)
  and [logger.cpp](../src/logger.cpp#L238). This is not destination-correct
  correlation.

## Root-cause ranking

1. **High confidence: unacknowledged six-frame LoRa manifest.** A single lost
   control frame can prevent the remote from obtaining a complete manifest;
   the gateway reports success after local transmission anyway. This exactly
   matches the stated field contrast with direct `espota.py`.
2. **High confidence: misleading host concurrency and retries.** Flasher calls
   the state `downloading` before the remote has acknowledged anything, allows
   six apparent active downloads despite one control handoff transmitter, then
   reissues operations from log silence. This can overlap a valid first HTTP
   download and make the result worse.
3. **High confidence: insufficient identity-bearing status.** Existing log
   parsing cannot safely attribute a receipt/failure to the correct Fleet row
   or transfer.
4. **Medium confidence: the HTTP leg can still fail independently.** The pull
   path differs materially from `espota.py`: Flasher serves HTTP from an
   ephemeral port; remote uses HTTPClient and a 512-byte read/write loop with
   15-second limits and SHA-256; `espota.py` uses invitation UDP plus direct
   TCP with MD5 protocol validation. The current code provides no evidence that
   rules HTTP out. The proposed status path will establish this stage before
   changing its timeout/buffer behavior.
5. **Low confidence until reproduced: host-network/server selection.** The
   server permits concurrent sockets and supplies the expected path and length.
   It remains a bench test item, not a rationale for arbitrary timeout growth.

## Proposed protocol and state model

### Firmware protocol

Add one new encrypted, addressed `MessageType::OtaPullStatus` frame. It uses
the existing fixed 12-byte LoRa payload; no JSON, heap allocation, dynamic
strings, or fleet-sized state is introduced.

The payload contains only:

- status opcode;
- transfer ID;
- compact failure code when applicable; and
- reserved zero bytes.

The packet source and destination provide identity. The gateway accepts an OTA
status only when all of source address, destination address, transfer ID, and
the current single handoff session match.

The remote sends:

- `manifest_accepted` synchronously from inside `handleOtaPullControlFrame()` immediately
  after the commit validation is successful, using `radioTxBudgetAvailable()` and `markRadioTxSentThisTick()`
  to send the frame before the blocking HTTP pull begins;
- `download_failed:<code>` after a local HTTP/SHA/updater failure, using a
  fixed enum derived from existing errors; and
- no invented success packet after a successful updater end, because reboot
  follows. Success is a new-version confirmation after reboot.

The gateway sends the original six-frame manifest, then waits for
`manifest_accepted`. On an acknowledgement timeout it re-sends the **same
transfer ID and exact manifest once**. A second missing acknowledgement becomes
`handoff_unconfirmed`, releases the handoff slot, and does not start a remote
HTTP download claim. A received `manifest_accepted` releases the gateway's
control-handoff slot.

The gateway retains one compact latest-handoff record: destination, transfer
ID, stage, compact reason, and timestamp. A new password-gated read-only
`remote_ota_status` admin command returns that record. This is the Flasher
status source; it is not an MQTT retained tree, an event ring, or a queue.

All gateway and remote OTA log lines must include `target=<address>` and
`transfer=<id>` in addition to their stage/reason. Logs are bench evidence,
not the control plane used by Flasher.

### Flasher state

Delete the parallel-active abstraction entirely, and model exactly one `activeRemoteOta` operation
(with no "max active" configuration or setting). The `RemoteOtaOperation` tracks:

`address`, `chipId`, `transferId`, `stage`, `reason`, `targetVersion`, and
stage deadline timestamps.

The existing UI selection queue may remain host-only, but it dispatches the
next remote only after the current operation has a terminal, honest outcome.
It never becomes a gateway queue and never causes the ESP8266 to retain a
fleet-wide OTA payload.

| Flasher label | Evidence required | Outcome |
| --- | --- | --- |
| Queued | Operator selected row | Waiting locally. |
| Sending manifest | `remote_ota_pull` admin acceptance with address/transfer ID | Gateway has begun the bounded handoff. |
| Awaiting remote acknowledgement | Matching status record | One bounded manifest re-send may still occur in gateway. |
| Downloading | Matching `manifest_accepted` | Remote owns a complete endpoint and SHA-256 manifest. |
| Download failed: `<code>` | Matching `download_failed` | Terminal; operator may explicitly retry. |
| Awaiting reboot/version | Manifest accepted; normal confirmation window | No re-trigger. |
| Updated | Same remote identity reports selected/newer version after reboot | Terminal success. |
| No version confirmation | Deadline expires without a matching version | Terminal unknown/unconfirmed, not “failed”. |
| Handoff unconfirmed | Gateway exhausted its one manifest re-send | Terminal; no HTTP claim was made. |

While an operation is active, Flasher polls only `remote_ota_status` at a
small fixed interval over the already-selected admin transport. It must match
both address and transfer ID before changing a row. It must not parse UDP or
serial log text to mutate OTA state.

After `manifest_accepted`, Flasher performs no Fleet scan. For reboot/version
confirmation it may issue one explicit, low-priority `refresh_lora_peer` and
then read that one peer detail after the device has had time to reboot. The
existing scheduler defers it behind relay/input group control. If confirmation
does not arrive before the established operation deadline, show `No version
confirmation`; do not send another OTA manifest automatically.

## Implementation steps

1. **Firmware: bounded acknowledged handoff.**
    Update [radio_protocol.h](../src/radio_protocol.h),
    [state_machine.h](../src/state_machine.h), and
    [state_machine.cpp](../src/state_machine.cpp) with `OtaPullStatus`. The remote
    sends `manifest_accepted` synchronously inside `handleOtaPullControlFrame()` immediately
    after successful commit validation, using `radioTxBudgetAvailable()` and `markRadioTxSentThisTick()`.
    Implement a single sender wait-for-ack phase on the gateway, exact one-time manifest
    retransmission, destination/transfer validation, and compact status record. Preserve
    `isGroupActive()` and one-radio-send-per-tick control priority; OTA control
    remains lower priority than active relay/input group control.
2. **Firmware: retain identity through the remote lifecycle.**
   Extend only the pending OTA command data in
   [pending_command_manager.h](../src/pending_command_manager.h) and
   [pending_command_manager.cpp](../src/pending_command_manager.cpp) with the
   transfer ID and gateway destination needed for an exact failure status.
   Update [app.cpp](../src/app.cpp) to queue/send the fixed failure status from
   existing `otaPullFromUrl()` error codes. Do not alter HTTP buffer sizes,
   timeouts, SHA-256 checks, or updater behavior in this phase.
3. **Firmware: expose one compact status read.**
   Add password-gated `remote_ota_status` in
   [admin_executor.cpp](../src/admin_executor.cpp) and document its tiny
   response. `remote_ota_pull` responds with the allocated transfer ID and
   destination. It must not report `downloading` until the matching remote
   acknowledgement is observed.
4. **Flasher: remove false parallelism and blind retry.**
    Refactor [useFleetOta.ts](../tools/flasher/src/composables/useFleetOta.ts)
    to model exactly one `activeRemoteOta` operation, polling the status command through existing
    `sendEasyPairCommandOnPort`. Delete the parallel-active abstraction, "max active" cap, 500 ms launch
    cadence, three automatic retries, eight-second log-silence retry, 45-second
    no-log retry, and log-to-row state mutation. Keep an operator-visible Retry
    command only after a terminal, evidenced failure/unconfirmed result.
5. **Flasher: targeted confirmation only.**
   Update the Fleet integration in
   [Flasher.vue](../tools/flasher/src/components/Flasher.vue) so no scan is
   started during download. Use the one-peer refresh/detail path only during
   version confirmation, preserve remote chip-ID identity matching, and render
   the exact stage labels above. Remove the best-effort OTA use of
   `network-monitor-log` identity parsing.
6. **Protocol break and documentation.**
   Document `OtaPullStatus`, transfer-ID rules, one retry, and no fallback in
   [PROTOCOL.md](PROTOCOL.md), [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md), and
   [tools/flasher/README.md](../tools/flasher/README.md). Add a human-readable
   Unreleased entry in [CHANGELOG.md](../CHANGELOG.md), explicitly calling out
   that all nodes participating in remote Flasher OTA must run the new protocol
   and that old remotes require a direct recovery update first.

## Required automated coverage

Firmware native tests must cover:

- control manifest encoding (start, four digest chunks, commit) and the same
  transfer ID on the one re-send;
- no gateway success until a matching status has the correct source,
  destination, and transfer ID;
- ignored wrong destination, wrong source, stale transfer, and replayed status;
- one acknowledgement timeout re-sends, second timeout reaches
  `handoff_unconfirmed`, and no third send occurs;
- complete remote manifest emits `manifest_accepted` before the pending HTTP
  pull is consumed;
- missing fragment emits no acceptance and cannot start HTTP;
- each existing OTA pull failure maps to a bounded compact failure code;
- group-control activity defers OTA transmission/status without losing the
  session; relay/input scheduling continues to win;
- the status record remains fixed-size and only one handoff exists.

Flasher unit tests must cover:

- second selected remote remains queued while one operation is active;
- `remote_ota_pull` response must provide address and transfer ID;
- only matching address plus transfer ID progresses the row;
- a stale, wrong-address, or wrong-transfer status is ignored;
- manifest acceptance becomes `Downloading`, never admin acceptance alone;
- exact remote failure becomes terminal without reissue;
- handoff timeout, version confirmation, and no-confirmation labels are
  distinct;
- no Fleet scan occurs during the HTTP stage; only one targeted confirmation
  request is attempted after the reboot stage;
- explicit operator retry starts a new transfer only after the old operation is
  terminal;
- command poll cleanup on navigation/unmount.

Use the existing unit-test entry points plus `pio test -e native` and
`npm run test:unit --prefix tools/flasher`; build/version work is outside this
plan-only review and is required only after implementation approval.

## Physical bench matrix: 12 remotes

All devices in this matrix must first be on the new protocol firmware. Record
remote address/chip ID, gateway build, target SHA/version, Flasher transport,
and every `target`/`transfer` event. Confirm that relay/input control remains
responsive throughout each case.

| Case | Devices | Transport / fault | Expected result |
| --- | --- | --- | --- |
| Direct control baseline | 1-12, individually | Direct `espota.py` | Establish each remote's direct OTA baseline; not a product substitution. |
| Clean Flasher handoff | 1-12, sequentially | USB admin | One manifest acknowledgement, one HTTP pull, reboot, exact version confirmation. |
| MQTT admin path | 1-12, sequentially | MQTT admin | Same stages/transfer identity as USB; no transport-specific lifecycle difference. |
| One lost manifest frame | 1-12, rotate dropped start/hash/commit through a test radio fault hook or controlled RF loss | USB admin | One exact manifest re-send; remote acknowledges; no duplicate HTTP pull. |
| Lost acknowledgement | 1-12, rotate | USB admin | One re-send; then `handoff_unconfirmed` after second missing acknowledgement; no blind host retry. |
| HTTP connect failure | 1-6 | Server unreachable after manifest acceptance | Matching `download_failed:http_begin_failed` (or compact equivalent), no reissue. |
| HTTP content/SHA failure | 7-12 | Wrong digest or controlled truncated/bad server response | Exact compact failure stage; no SHA weakening and no reissue. |
| Reboot/version confirmation | 1-12 | Normal server | One targeted post-reboot confirmation; correct `Updated` or `No version confirmation`, never a false failure. |
| Control-priority stress | 1-12, repeated group input changes during handoff | USB and MQTT split | Group ACK/control wins; OTA handoff is deferred but completes or times out truthfully. |
| Operator sequencing | 1-12 selected together | USB and MQTT split | UI shows eleven queued, exactly one active; next begins only after prior terminal outcome. |

For every successful Flasher run, compare the server's connection count with
the expected one remote pull. For every failed handoff, verify no remote HTTP
connection occurred. That is the decisive proof that the LoRa handoff, rather
than the HTTP leg, was the failed stage.

## Approval gates

Do not implement until this plan is approved. Do not commit or push an
implementation until both the code review and the bench matrix are approved.
The field gate is especially important: compilation can prove frame handling
and state transitions, but it cannot prove LoRa delivery, radio scheduling, or
the remote's post-reboot network recovery.
