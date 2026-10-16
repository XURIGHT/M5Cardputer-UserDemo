#include "pc_remote_ble.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

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
        if (OS_MBUF_PKTLEN(ctxt->om) > 32) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        char input[33] = {0};
        if (ble_hs_mbuf_to_flat(ctxt->om, input, sizeof(input), NULL)) return BLE_ATT_ERR_UNLIKELY;
        if (memcmp(input, "PING", 4) != 0) {
            unsigned int seq = 0;
            char status[8] = {0};
            if (sscanf(input, "ACK:%u:%7s", &seq, status) == 2 &&
                pending_seq != 0 && seq == pending_seq) {
                command_state = strcmp(status, "OK") == 0 ? 2 : 3;
                pending_seq = 0;
                return 0;
            }
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
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
        if (event->connect.status == 0) { conn_handle = event->connect.conn_handle; notify_enabled = false; pending_seq = 0; command_state = 0; }
        else advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        notify_enabled = false;
        pending_seq = 0;
        command_state = 0;
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

int pc_remote_ble_send_notepad(void)
{
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE || !notify_enabled) return -1;
    if (pending_seq != 0) return -2;
    unsigned int seq = next_seq++;
    if (next_seq > 999999) next_seq = 1;
    char frame[32];
    int len = snprintf(frame, sizeof(frame), "RUN:NOTEPAD:%u", seq);
    if (len <= 0 || len >= (int)sizeof(frame)) return -3;
    pending_seq = seq;
    command_state = 1;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(frame, len);
    if (!om) { pending_seq = 0; command_state = 3; return -4; }
    int rc = ble_gatts_notify_custom(conn_handle, notify_handle, om);
    if (rc) { last_error = rc; pending_seq = 0; command_state = 3; }
    return rc;
}
int pc_remote_ble_command_state(void) { return command_state; }
