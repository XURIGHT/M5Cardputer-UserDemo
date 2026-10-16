# PC Remote BLE firmware integration audit (2026-10-16)

## Verified in current CardputerADV source

- `Hal::bleKeyboardInit()` calls `ble_hid_device_helper_init()` (see `main/hal/hal.cpp`).
- `ble_hid_device_helper.c` starts the NimBLE host via `esp_nimble_enable(ble_hid_device_host_task)` inside its HID initialization.
- HID initialization configures GAP advertising and HID services; its callbacks restart advertising after disconnect.
- `AppKeyboard::onClose()` calls `esp_restart()` when the keyboard mode was activated.
- The app registry is in `main/main.cpp`; sources are globbed by `main/CMakeLists.txt`.
- The Windows BLE diagnostic client exists, but no peripheral-side GATT service exists yet.

## Implementation constraints

Do **not** call `esp_nimble_enable` a second time or assume an HID keyboard connection is the same as a PC Remote GATT connection.

Proposed first implementation:
1. Extract/centralize NimBLE host ownership into a shared BLE manager, with mutually exclusive advertising modes for `Keyboard` and `PC Remote`. Preserve the keyboard's existing HID report map, name and behavior.
2. Register the custom PC Remote GATT service before starting the NimBLE host. The peripheral must expose a write-with-response characteristic and a notify characteristic, using the UUIDs in `pc_remote/BLE_PROTOCOL.md`.
3. Register the app in the existing Mooncake launcher, reusing existing icons or providing a compatible AppIcon_t; G0 must close the app.
4. Process BLE callbacks without touching the UI canvas from the NimBLE thread; queue received frames and render from the app update loop.
5. Test `PING` -> `PONG` on Windows using `ble_client.py`. No task execution in this phase.
6. Verify BLE keyboard mode still works after leaving PC Remote (and after reboot); verify Wi-Fi, Q quiet mode, and G0.
7. Add authenticated, allowlisted task requests only after GATT connectivity is confirmed. Do not expose an arbitrary shell command channel.

## Stop conditions

Do not flash if the cloud build fails, if GATT registration conflicts with HID initialization, or if the firmware loses the launcher/keyboard functionality. Keep the previous successful silent firmware artifact for recovery.

## Next engineering gate

Implement shared host lifecycle + peripheral GATT service in a separate branch, compile with the existing GitHub Actions workflow, inspect logs, then manually test BLE discovery and PING/PONG before merging into `CardputerADV`.
