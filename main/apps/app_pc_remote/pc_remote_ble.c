#include "pc_remote_ble.h"
#include "pc_frame.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static pc_frame_rx_t menu_rx;
static QueueHandle_t menu_queue;
static const char *TAG = "PC_REMOTE_BLE";
static uint8_t own_addr_type;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t notify_handle;
static volatile int ping_count;
static volatile int last_error;
static bool started;
static volatile bool notify_enabled;
static unsigned int next_seq = 1;
static volatile unsigned int pending_seq;
static volatile int command_state;
// UI task owns the bounded transmit buffer and pacing. BLE callbacks only
// complete pending_seq/command_state. No automatic retry after unknown receipt.
static uint8_t text_body[513];
static size_t text_length, text_offset;
static uint8_t text_part, text_parts;
static unsigned int text_seq;
static TickType_t pending_since, last_chunk;
static volatile bool text_active;

static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    0x00,0xa1,0xc4,0x2b,0x8f,0xae,0x2e,0x9c,0x2d,0x4b,0x22,0x8c,0x00,0x10,0xbf,0x7c);
static const ble_uuid128_t write_uuid = BLE_UUID128_INIT(
    0x00,0xa1,0xc4,0x2b,0x8f,0xae,0x2e,0x9c,0x2d,0x4b,0x22,0x8c,0x01,0x10,0xbf,0x7c);
static const ble_uuid128_t notify_uuid = BLE_UUID128_INIT(
    0x00,0xa1,0xc4,0x2b,0x8f,0xae,0x2e,0x9c,0x2d,0x4b,0x22,0x8c,0x02,0x10,0xbf,0x7c);

static int access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        size_t size = OS_MBUF_PKTLEN(ctxt->om);
        if (size > PC_FRAME_HEADER_SIZE + PC_FRAME_MAX_CHUNK) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        char input[PC_FRAME_HEADER_SIZE + PC_FRAME_MAX_CHUNK + 1] = {0};
        if (ble_hs_mbuf_to_flat(ctxt->om, input, size, NULL)) return BLE_ATT_ERR_UNLIKELY;
        if (size && (uint8_t)input[0] == 2) {
            int result = pc_frame_feed(&menu_rx, (uint8_t *)input, size);
            if (result < 0) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            if (result == 1) {
                if (menu_rx.kind != 2) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
                if (xQueueSend(menu_queue, &menu_rx, 0) != pdTRUE)
                    return BLE_ATT_ERR_INSUFFICIENT_RES;
                pc_frame_reset(&menu_rx);
            }
            return 0;
        }
        if (memcmp(input, "PING", 4) != 0) {
            unsigned int seq = 0;
            int consumed = 0;
            char status[8] = {0};
            if (sscanf(input, "ACK:%u:%7s%n", &seq, status, &consumed) != 2 ||
                consumed != (int)size || seq == 0 || seq > 65535 ||
                (strcmp(status, "OK") != 0 && strcmp(status, "ERR") != 0 &&
                 strcmp(status, "UNKNOWN") != 0)) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
            if (pending_seq != 0 && seq == pending_seq) {
                command_state = strcmp(status, "OK") == 0 ? 2 :
                    strcmp(status, "UNKNOWN") == 0 ? 4 : 3;
                pending_seq = 0;
            }
            // A delayed/duplicate receipt must not overwrite a newer request,
            // or tear down BLE after the UI's timeout. UNKNOWN remains UNKNOWN.
            return 0;
        }
        if (OS_MBUF_PKTLEN(ctxt->om) != 4) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        ping_count++;
        if (conn_handle == conn && notify_handle) {
            struct os_mbuf *om = ble_hs_mbuf_from_flat("PONG", 4);
            if (om) {
                int rc = ble_gatts_notify_custom(conn, notify_handle, om);
                if (rc) last_error = rc;
            }
        }
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, "PONG", 4) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_chr_def characteristics[] = {
    {.uuid=&write_uuid.u, .access_cb=access_cb, .flags=BLE_GATT_CHR_F_WRITE},
    {.uuid=&notify_uuid.u, .access_cb=access_cb, .val_handle=&notify_handle,
     .flags=BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ},
    {0}
};
static const struct ble_gatt_svc_def services[] = {
    {.type=BLE_GATT_SVC_TYPE_PRIMARY, .uuid=&service_uuid.u, .characteristics=characteristics},
    {0}
};

static int gap_event(struct ble_gap_event *event, void *arg);
static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc) { last_error = rc; return; }

    struct ble_hs_adv_fields scan = {0};
    const char *name = "Cardputer-PCRemote";
    scan.name = (uint8_t *)name;
    scan.name_len = strlen(name);
    scan.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&scan);
    if (rc) { last_error = rc; return; }

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc) last_error = rc;
}
static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) { conn_handle = event->connect.conn_handle; notify_enabled = false; text_active = false; next_seq = 1; pending_seq = 0; command_state = command_state == 1 ? 4 : command_state; }
        else advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        text_active = false;
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        notify_enabled = false;
        pc_frame_reset(&menu_rx);
        xQueueReset(menu_queue);
        pending_seq = 0;
        if (command_state == 1) command_state = 4;
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == notify_handle)
            notify_enabled = event->subscribe.cur_notify != 0;
        break;
    default: break;
    }
    return 0;
}
static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc) { last_error = rc; return; }
    advertise();
}
static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}
int pc_remote_ble_start(void)
{
    if (started) return 0;
    menu_queue = xQueueCreate(16, sizeof(pc_frame_rx_t));
    if (!menu_queue) return -1;
    int rc = nimble_port_init();
    if (rc) { last_error = rc; return rc; }
    ble_svc_gap_init();
    ble_svc_gatt_init();
    rc = ble_gatts_count_cfg(services);
    if (!rc) rc = ble_gatts_add_svcs(services);
    if (rc) { last_error = rc; return rc; }
    rc = ble_svc_gap_device_name_set("Cardputer-PCRemote");
    if (rc) { last_error = rc; return rc; }
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    started = true;
    ESP_LOGI(TAG, "PC Remote BLE started");
    return 0;
}
int pc_remote_ble_connected(void) { return conn_handle != BLE_HS_CONN_HANDLE_NONE; }
int pc_remote_ble_ping_count(void) { return ping_count; }
int pc_remote_ble_last_error(void) { return last_error; }

int pc_remote_ble_send_task(unsigned int index)
{
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE || !notify_enabled) { command_state = 3; return -1; }
    if (pending_seq != 0) return -2;
    if (next_seq > 65535) { command_state = 5; return -5; }
    unsigned int seq = next_seq++;
    char frame[32];
    int len = snprintf(frame, sizeof(frame), "RUN:%u:%u", index, seq);
    if (len <= 0 || len >= (int)sizeof(frame)) return -3;
    pending_since = xTaskGetTickCount();
    pending_seq = seq;
    command_state = 1;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(frame, len);
    if (!om) { pending_seq = 0; command_state = 3; return -4; }
    int rc = ble_gatts_notify_custom(conn_handle, notify_handle, om);
    if (rc) { last_error = rc; pending_seq = 0; command_state = 3; }
    return rc;
}
int pc_remote_ble_send_text(unsigned int index, const char *text, size_t length)
{
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE || !notify_enabled) {
        command_state = 3; return -1;
    }
    if (pending_seq || text_active) return -2;
    if (index >= 64 || !text || length == 0 || length > 512) {
        command_state = 3; return -3;
    }
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)text[i] < 0x20 || (unsigned char)text[i] > 0x7e) {
            command_state = 3; return -3;
        }
    if (next_seq > 65535) { command_state = 5; return -5; }
    text_seq = next_seq++;
    text_body[0] = (uint8_t)index;
    memcpy(text_body + 1, text, length);
    text_length = length + 1;
    text_offset = 0; text_part = 0;
    text_parts = (uint8_t)((text_length + 11) / 12);
    pending_since = last_chunk = xTaskGetTickCount();
    pending_seq = text_seq;
    command_state = 1; text_active = true;
    return 0;
}

void pc_remote_ble_poll(void)
{
    TickType_t now = xTaskGetTickCount();
    if (!pending_seq) { text_active = false; return; }
    if (now - pending_since >= pdMS_TO_TICKS(15000)) {
        // The sink may already have received the text. Never claim failure or retry.
        pending_seq = 0; text_active = false; command_state = 4; return;
    }
    if (!text_active || now - last_chunk < pdMS_TO_TICKS(30)) return;
    if (!pc_remote_ble_connected() || !notify_enabled) {
        pending_seq = 0; text_active = false; command_state = 4; return;
    }
    size_t chunk = text_length - text_offset;
    if (chunk > 12) chunk = 12; // MTU 23 minus ATT 3 minus header 8.
    uint8_t frame[20] = {2, 3, (uint8_t)(text_seq >> 8), (uint8_t)text_seq,
        text_part, text_parts, (uint8_t)(text_length >> 8), (uint8_t)text_length};
    memcpy(frame + 8, text_body + text_offset, chunk);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(frame, chunk + 8);
    if (!om) { pending_seq = 0; text_active = false; command_state = 3; return; }
    int rc = ble_gatts_notify_custom(conn_handle, notify_handle, om);
    if (rc) {
        last_error = rc; pending_seq = 0; text_active = false;
        command_state = 4; return;
    }
    last_chunk = now; text_offset += chunk; ++text_part;
    if (text_offset == text_length) text_active = false;
}
int pc_remote_ble_command_state(void) { return command_state; }


int pc_remote_ble_take_menu(char *out, size_t capacity)
{
    pc_frame_rx_t message;
    if (!menu_queue || xQueueReceive(menu_queue, &message, 0) != pdTRUE) return 0;
    if (capacity <= message.received) return -1;
    memcpy(out, message.buffer, message.received);
    out[message.received] = 0;
    return message.received;
}
