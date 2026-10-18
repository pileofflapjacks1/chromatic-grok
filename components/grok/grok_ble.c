/* Chromatic Grok link. A modified work based on the ModRetro Chromatic MCU
 * firmware (GPL-3.0-or-later). See LICENSE and NOTICE.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * BLE peripheral. The same newline commands as the serial console are written
 * to the RX characteristic. Replies are notified on TX, framed with
 * GROK_BEGIN / GROK_END. Notify flushes run on the NimBLE host queue.
 *
 * Security: the link must be paired, bonded and encrypted with MITM
 * protection before RX accepts a write or TX sends a notify. The Chromatic
 * has a screen and no keypad, so it pairs as DisplayOnly: a random 6-digit
 * passkey is shown on the handheld and typed on the phone or computer
 * (LE Secure Connections, passkey entry). Unauthenticated peers get
 * "insufficient authentication" and no data.
 */
#include "grok_internal.h"

#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

#if CONFIG_BT_NIMBLE_ENABLED

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nimble/nimble_npl.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "host/ble_att.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include "host/ble_store.h"

/* Not exported by a public NimBLE header in ESP-IDF v5.3; the IDF examples
 * declare it the same way. */
void ble_store_config_init(void);

#include "esp_bt.h"
#include "esp_err.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "os/os_mbuf.h"

enum {
    kLineMax = 240,
    kTxCap = 768,
    kChunkMax = 180,
    kFailLimit = 8,
};

static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(
    0x5f, 0x4e, 0x3d, 0x2c, 0x1b, 0x0a, 0x11, 0x9c,
    0x5e, 0x4d, 0x2a, 0x6b, 0x01, 0x00, 0x3c, 0x8f);

static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(
    0x5f, 0x4e, 0x3d, 0x2c, 0x1b, 0x0a, 0x11, 0x9c,
    0x5e, 0x4d, 0x2a, 0x6b, 0x02, 0x00, 0x3c, 0x8f);

static const ble_uuid128_t s_tx_uuid = BLE_UUID128_INIT(
    0x5f, 0x4e, 0x3d, 0x2c, 0x1b, 0x0a, 0x11, 0x9c,
    0x5e, 0x4d, 0x2a, 0x6b, 0x03, 0x00, 0x3c, 0x8f);

static uint16_t s_tx_handle;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_own_addr_type;
static bool s_started;
static bool s_advertising;
static bool s_callout_ready;
static bool s_released;
static SemaphoreHandle_t s_lock;
static struct ble_npl_callout s_tx_co;

static char s_line[kLineMax];
static size_t s_line_len;
static char s_tx[kTxCap];
static size_t s_tx_len;
static size_t s_tx_off;
static int s_tx_fail;

static int gap_event(struct ble_gap_event *event, void *arg);
static int gatt_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static void start_adv(void);
static void tx_flush(struct ble_npl_event *ev);

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_rx_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN,
            },
            {
                .uuid = &s_tx_uuid.u,
                .access_cb = gatt_access,
                .val_handle = &s_tx_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN,
            },
            {
                0,
            },
        },
    },
    {
        0,
    },
};

/* True only for an encrypted, MITM-authenticated (passkey) link. */
static bool conn_secure(uint16_t conn)
{
    struct ble_gap_conn_desc desc;

    if (conn == BLE_HS_CONN_HANDLE_NONE)
    {
        return false;
    }
    if (ble_gap_conn_find(conn, &desc) != 0)
    {
        return false;
    }
    return desc.sec_state.encrypted && desc.sec_state.authenticated;
}

static void lock(void)
{
    if (s_lock != NULL)
    {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_lock != NULL)
    {
        xSemaphoreGive(s_lock);
    }
}

static void tx_schedule(uint32_t delay_ms)
{
    if (!s_callout_ready)
    {
        return;
    }
    ble_npl_callout_reset(&s_tx_co, ble_npl_time_ms_to_ticks32(delay_ms));
}

static void tx_flush(struct ble_npl_event *ev)
{
    char chunk[kChunkMax];
    size_t left;
    size_t n;
    uint16_t conn;
    uint16_t handle;
    uint16_t mtu;
    struct os_mbuf *om;
    int rc;

    (void)ev;

    lock();
    conn = s_conn;
    handle = s_tx_handle;
    left = (s_tx_off < s_tx_len) ? (s_tx_len - s_tx_off) : 0;
    if ((conn == BLE_HS_CONN_HANDLE_NONE) || (handle == 0) || (left == 0))
    {
        unlock();
        return;
    }
    if (!conn_secure(conn))
    {
        // Never notify replies over an unpaired or unencrypted link.
        unlock();
        return;
    }

    mtu = ble_att_mtu(conn);
    n = (mtu > 3) ? (size_t)(mtu - 3) : 1;
    if (n > kChunkMax)
    {
        n = kChunkMax;
    }
    if (n > left)
    {
        n = left;
    }
    memcpy(chunk, s_tx + s_tx_off, n);
    unlock();

    om = ble_hs_mbuf_from_flat(chunk, n);
    if (om == NULL)
    {
        tx_schedule(50);
        return;
    }

    rc = ble_gatts_notify_custom(conn, handle, om);
    if (rc != 0)
    {
        os_mbuf_free_chain(om);
        lock();
        s_tx_fail++;
        if (s_tx_fail >= kFailLimit)
        {
            s_tx_off = s_tx_len;
            s_tx_fail = 0;
            unlock();
            printf("ble: notify dropped\n");
            return;
        }
        unlock();
        tx_schedule(50);
        return;
    }

    lock();
    s_tx_off += n;
    s_tx_fail = 0;
    left = (s_tx_off < s_tx_len);
    unlock();
    if (left)
    {
        tx_schedule(40);
    }
}

static void line_push(char ch)
{
    if (ch == '\r')
    {
        return;
    }
    if (ch == '\n')
    {
        s_line[s_line_len] = '\0';
        grok_exec_line(s_line);
        s_line_len = 0;
        return;
    }
    if ((s_line_len + 1) >= sizeof(s_line))
    {
        s_line[s_line_len] = '\0';
        grok_exec_line(s_line);
        s_line_len = 0;
    }
    s_line[s_line_len++] = ch;
}

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t tmp[244];
    uint16_t len;
    uint16_t got = 0;

    (void)attr_handle;
    (void)arg;

    // The GATT flags already require encryption and authentication. Check
    // again so a stack or config change cannot open the command path.
    if (!conn_secure(conn_handle))
    {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > sizeof(tmp))
        {
            len = sizeof(tmp);
        }
        if (ble_hs_mbuf_to_flat(ctxt->om, tmp, len, &got) != 0)
        {
            return BLE_ATT_ERR_UNLIKELY;
        }
        for (uint16_t i = 0; i < got; i++)
        {
            line_push((char)tmp[i]);
        }
        return 0;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        int rc = os_mbuf_append(ctxt->om, "ok", 2);
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static void start_adv(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp;
    struct ble_gap_adv_params adv;
    const char *name;
    int rc;

    if (ble_gap_adv_active())
    {
        s_advertising = true;
        return;
    }

    name = ble_svc_gap_device_name();
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        printf("ble: adv fields failed (%d)\n", rc);
        s_advertising = false;
        return;
    }

    memset(&rsp, 0, sizeof(rsp));
    rsp.uuids128 = (ble_uuid128_t *)&s_svc_uuid;
    rsp.num_uuids128 = 1;
    rsp.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0)
    {
        printf("ble: scan response failed (%d)\n", rc);
    }

    memset(&adv, 0, sizeof(adv));
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv, gap_event, NULL);
    if (rc != 0)
    {
        printf("ble: advertise failed (%d)\n", rc);
        s_advertising = false;
        return;
    }
    s_advertising = true;
    printf("ble: advertising as ChromaticGrok\n");
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type)
    {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0)
            {
                s_conn = event->connect.conn_handle;
                s_advertising = false;
                printf("ble: connected; pairing required\n");
                ble_gap_security_initiate(s_conn);
            }
            else
            {
                s_conn = BLE_HS_CONN_HANDLE_NONE;
                start_adv();
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            s_conn = BLE_HS_CONN_HANDLE_NONE;
            printf("ble: disconnected\n");
            start_adv();
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            start_adv();
            return 0;
        case BLE_GAP_EVENT_MTU:
            return 0;
        case BLE_GAP_EVENT_ENC_CHANGE:
            if ((event->enc_change.status == 0) && conn_secure(event->enc_change.conn_handle))
            {
                printf("ble: paired and encrypted\n");
                grok_ui_close();
                tx_schedule(20);
            }
            else
            {
                printf("ble: pairing failed (%d)\n", event->enc_change.status);
                ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
            return 0;
        case BLE_GAP_EVENT_PASSKEY_ACTION:
            if (event->passkey.params.action == BLE_SM_IOACT_DISP)
            {
                struct ble_sm_io pk;
                char msg[96];

                memset(&pk, 0, sizeof(pk));
                pk.action = BLE_SM_IOACT_DISP;
                pk.passkey = esp_random() % 1000000u;
                // Show the passkey on the handheld screen only. It is not
                // sent over BLE.
                snprintf(msg, sizeof(msg), "BLE pairing\n\nPasskey: %06lu\n\nEnter it on the other device.",
                         (unsigned long)pk.passkey);
                grok_ui_set_body(msg);
                grok_ui_open();
                ble_sm_inject_io(event->passkey.conn_handle, &pk);
            }
            return 0;
        case BLE_GAP_EVENT_REPEAT_PAIRING:
        {
            struct ble_gap_conn_desc desc;

            // A known peer lost its bond. Drop the old bond so it can pair
            // again with a fresh passkey.
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0)
            {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.cur_notify)
            {
                tx_schedule(20);
            }
            return 0;
        default:
            return 0;
    }
}

static void on_reset(int reason)
{
    s_conn = BLE_HS_CONN_HANDLE_NONE;
    s_advertising = false;
    printf("ble: host reset (%d)\n", reason);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        printf("ble: address failed (%d)\n", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0)
    {
        printf("ble: address type failed (%d)\n", rc);
        return;
    }
    start_adv();
    tx_schedule(20);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool grok_ble_start(void)
{
    esp_err_t err;
    int rc;

    if (s_lock == NULL)
    {
        s_lock = xSemaphoreCreateMutex();
    }

    err = nimble_port_init();
    if (err != ESP_OK)
    {
        printf("ble: init failed (%s)\n", esp_err_to_name(err));
        return false;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // DisplayOnly + MITM + Secure Connections = passkey entry pairing.
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    rc = ble_gatts_count_cfg(s_svcs);
    if (rc != 0)
    {
        printf("ble: service count failed (%d)\n", rc);
        return false;
    }
    rc = ble_gatts_add_svcs(s_svcs);
    if (rc != 0)
    {
        printf("ble: service add failed (%d)\n", rc);
        return false;
    }
    rc = ble_svc_gap_device_name_set("ChromaticGrok");
    if (rc != 0)
    {
        printf("ble: name failed (%d)\n", rc);
        return false;
    }

    ble_store_config_init();
    ble_npl_callout_init(&s_tx_co, nimble_port_get_dflt_eventq(), tx_flush, NULL);
    s_callout_ready = true;
    nimble_port_freertos_init(host_task);
    s_started = true;
    return true;
}

bool grok_ble_released(void)
{
    return s_released;
}

/* Direct HTTPS mode. Stop the host (this ends any connection and advertising),
 * deinit NimBLE and the controller, then return the controller's static RAM to
 * the heap. Runs on the grok radio task, never on the NimBLE host task. */
bool grok_ble_stop(void)
{
    esp_err_t err;
    int rc;

    if (s_released)
    {
        return true;
    }
    if (s_started)
    {
        lock();
        s_callout_ready = false;
        unlock();
        ble_npl_callout_stop(&s_tx_co);

        rc = nimble_port_stop();
        if (rc != 0)
        {
            printf("ble: stop failed (%d)\n", rc);
            return false;
        }
        ble_npl_callout_deinit(&s_tx_co);
        err = nimble_port_deinit();
        if (err != ESP_OK)
        {
            printf("ble: deinit failed (%s)\n", esp_err_to_name(err));
            return false;
        }
        s_started = false;
        s_advertising = false;
        s_conn = BLE_HS_CONN_HANDLE_NONE;
    }

    err = esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
    if (err != ESP_OK)
    {
        printf("ble: stopped; controller ram not released (%s)\n", esp_err_to_name(err));
        return true;
    }
    s_released = true;
    printf("ble: stopped and controller ram released until reboot\n");
    return true;
}

void grok_ble_unpair(void)
{
    int rc;

    if (!s_started)
    {
        grok_say("ble: not started\n");
        return;
    }
    if (s_conn != BLE_HS_CONN_HANDLE_NONE)
    {
        ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    rc = ble_store_clear();
    grok_say((rc == 0) ? "ble: all bonds cleared\n" : "ble: could not clear bonds\n");
}

void grok_ble_queue_text(const char *text)
{
    int n;

    if (s_lock == NULL)
    {
        return;
    }
    if (text == NULL)
    {
        text = "";
    }

    lock();
    n = snprintf(s_tx, sizeof(s_tx), "GROK_BEGIN\n%s", text);
    if ((n > 0) && ((size_t)n < sizeof(s_tx)))
    {
        size_t len = (size_t)n;
        if ((len == 0) || (s_tx[len - 1] != '\n'))
        {
            if ((len + 1) < sizeof(s_tx))
            {
                s_tx[len++] = '\n';
                s_tx[len] = '\0';
            }
        }
        if ((len + 9) < sizeof(s_tx))
        {
            memcpy(s_tx + len, "GROK_END\n", 9);
            len += 9;
            s_tx[len] = '\0';
        }
        s_tx_len = len;
    }
    else
    {
        snprintf(s_tx, sizeof(s_tx), "GROK_BEGIN\n?\nGROK_END\n");
        s_tx_len = strlen(s_tx);
    }
    s_tx_off = 0;
    s_tx_fail = 0;
    unlock();
    tx_schedule(1);
}

void grok_ble_format(char *out, size_t out_cap)
{
    const char *text = "ble off";

    if ((out == NULL) || (out_cap == 0))
    {
        return;
    }
    if (s_started && (s_conn != BLE_HS_CONN_HANDLE_NONE))
    {
        text = conn_secure(s_conn) ? "ble paired" : "ble unpaired";
    }
    else if (s_started && s_advertising)
    {
        text = "ble adv";
    }
    snprintf(out, out_cap, "%s", text);
}

#else

bool grok_ble_stop(void)
{
    return true;
}

bool grok_ble_released(void)
{
    return true;
}

bool grok_ble_start(void)
{
    printf("ble: disabled in sdkconfig\n");
    return false;
}

void grok_ble_queue_text(const char *text)
{
    (void)text;
}

void grok_ble_unpair(void)
{
    grok_say("ble: disabled in sdkconfig\n");
}

void grok_ble_format(char *out, size_t out_cap)
{
    if ((out == NULL) || (out_cap == 0))
    {
        return;
    }
    snprintf(out, out_cap, "ble off");
}

#endif
