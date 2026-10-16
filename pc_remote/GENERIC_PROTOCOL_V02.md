# PC Remote Generic BLE Protocol v0.2 (design contract)

## Objective
Cardputer ADV is a generic display/input terminal. Windows owns the task catalog and execution policy. Adding a Windows/WSL/Codex task must not require a Cardputer firmware update.

## BLE roles
- Windows: central, runs persistent client/bridge.
- Cardputer: peripheral, advertises `Cardputer-PCRemote`.
- Retain current GATT UUIDs (service `7cbf1000-8c22-4b2d-9c2e-ae8f2bc4a100`, write `...1001`, notify `...1002`).
- Preserve `PING/PONG` compatibility.

## Application protocol
Versioned, bounded UTF-8 messages, carried over BLE characteristic write/notify. BLE notifications can be small: use framing, fragmentation and reassembly with explicit maximum message size (e.g. 1024 bytes) and timeout. No assumption that a whole JSON message fits one ATT packet.

Logical message types:
- `HELLO`: client and terminal advertise protocol versions, session nonce and limits.
- `MENU_REQUEST`: Cardputer asks for current task catalog.
- `MENU_RESPONSE`: Windows sends array of {id,label,category}, with pagination.
- `RUN_REQUEST`: Cardputer selects task id; includes session-scoped request id.
- `RUN_ACK`: Windows reports accepted/rejected; does not imply task completed.
- `TASK_STATUS`: running/completed/failed plus bounded output excerpt.
- `ERROR`: explicit protocol or authorization failure.

The firmware never accepts raw shell commands, executable code, or arbitrary URLs as task definitions. Task identifiers are opaque strings; Windows maps them to an allowlist.

## Authentication and trust
BLE pairing alone is not an application authorization decision. Require an explicit pairing/trust setup before RUN_REQUEST is enabled, and prevent unauthenticated neighbors from issuing tasks. Avoid sending reusable static secrets over an unencrypted connection. Limit Windows execution to an allowlist, do not bind an unrestricted command shell. Include replay protection, deduplication, input bounds, and rate limits.

## Device UI
- On entering PC Remote: advertise, connect, negotiate protocol, fetch menu.
- Display paginated task list and current connection status.
- Arrow keys navigate, Enter selects, G0 returns (current BLE host lifecycle may require reboot).
- Display task accepted, running, finished/failed. No busy-wait in the UI event loop.
- Disconnection clears pending operations and supports reconnect.
- Q quiet mode and other stock apps must remain intact.

## Verification gates
1. Pure protocol unit tests: framing, pagination, duplicate requests, malformed input, disconnect.
2. Windows fake BLE transport test: menu changes without reflashing device.
3. Cloud firmware build.
4. Device BLE HELLO/menu rendering, then authenticated RUN and status.
5. Stock keyboard, G0, Q, Wi-Fi regression checks.

## Non-goals for v0.2
Firmware OTA over BLE, arbitrary remote terminal, unrestricted Codex shell access, desktop video streaming.
