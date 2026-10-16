#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// Prototype peripheral; isolated from existing HID initialization.
// Must not be active simultaneously with BLE keyboard mode.
int pc_remote_ble_start(void);
int pc_remote_ble_connected(void);
int pc_remote_ble_ping_count(void);
int pc_remote_ble_last_error(void);
#ifdef __cplusplus
}
#endif
