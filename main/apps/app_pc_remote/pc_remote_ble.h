#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Prototype peripheral; isolated from existing HID initialization.
// Must not be active simultaneously with BLE keyboard mode.
int pc_remote_ble_start(void);
int pc_remote_ble_connected(void);
int pc_remote_ble_ping_count(void);
int pc_remote_ble_last_error(void);
int pc_remote_ble_send_task(unsigned int index);
// Printable ASCII only; body is index, sequence, text; never a shell command.
int pc_remote_ble_send_text(unsigned int index, const char *text, size_t length);
void pc_remote_ble_poll(void);
int pc_remote_ble_take_menu(char *out, size_t capacity);
int pc_remote_ble_command_state(void);
#ifdef __cplusplus
}
#endif

