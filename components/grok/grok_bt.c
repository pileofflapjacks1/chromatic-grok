/* Chromatic Grok link. A modified work based on the ModRetro Chromatic MCU
 * firmware (GPL-3.0-or-later). See LICENSE and NOTICE.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Feasibility spike: the ESP32 as a Bluetooth Classic hands-free Audio
 * Gateway (HFP AG) for a headset. Built only with CONFIG_GROK_BT_HEADSET_SPIKE
 * (sdkconfig.bt_spike, Bluedroid instead of NimBLE). See
 * docs/bt-headset-spike.md.
 *
 * What it proves: pairing, an SCO link (CVSD 8 kHz, or mSBC 16 kHz when
 * CONFIG_BT_HFP_WBS_ENABLE), a few seconds of headset mic into RAM, playback
 * to the headset, and heap numbers at each stage with Wi-Fi and TLS running.
 *
 * Audio goes over HCI (no PCM pins: GPIO 25/26/27/33 are the FPGA I2S).
 * The mic is stored as 4-bit IMA ADPCM, so 5 s at 8 kHz is 20 KB of heap,
 * allocated by "bt rec" and freed by "bt off".
 *
 * Every command except "bt status" and "bt mem" runs on the grok radio task
 * (its 16 KB stack also carries the TLS probe), so it never blocks the
 * console and never races a grok ask.
 */
#include "grok_bt.h"

#include "sdkconfig.h"

#if CONFIG_GROK_BT_HEADSET_SPIKE

#include "grok_bt_util.h"
#include "grok_internal.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_ag_api.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BT_NAME "ChromaticGrok"
#define TLS_PROBE_URL "https://api.x.ai/v1/models"

enum {
    kEvDiscDone = BIT0,
    kEvSlcUp = BIT1,
    kEvSlcDown = BIT2,
    kEvAudioUp = BIT3,
    kEvAudioDown = BIT4,
    kEvAuthDone = BIT5,
    kEvRecDone = BIT6,
    kEvPlayDone = BIT7,
};

typedef enum {
    MODE_IDLE = 0,
    MODE_REC,
    MODE_PLAY,
    MODE_TONE,
} AudioMode_t;

static EventGroupHandle_t s_ev;
static bool s_on;
static bool s_mem_released;
static bool s_slc;
static volatile bool s_audio;
static volatile bool s_msbc;
static uint16_t s_sco_handle;
static esp_bd_addr_t s_peer;
static bool s_have_peer;
static esp_bd_addr_t s_found;
static bool s_have_found;

/* Audio state shared with the Bluedroid data callbacks (BTU task). */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile AudioMode_t s_mode;
static uint8_t *s_clip;
static size_t s_clip_cap;      /* bytes */
static volatile size_t s_clip_len; /* nibbles = samples */
static size_t s_play_pos;
static GrokAdpcm_t s_enc;
static GrokAdpcm_t s_dec;
static uint32_t s_credit;
static uint32_t s_tone_left;
static uint32_t s_tone_phase;
static volatile uint32_t s_rx_bytes;
static volatile uint32_t s_rx_pkts;
static volatile uint32_t s_tx_bytes;
static volatile int32_t s_peak;
static uint32_t s_last_in_len;

/* ------------------------------------------------------------------ mem */

static bool s_monitor;

/* Free/largest/min for internal 8-bit and DMA heap. "min" is the low point
 * since the previous mark, so each stage shows its own worst case. */
static void mark(const char *stage)
{
    const uint32_t icaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t dcaps = MALLOC_CAP_DMA;

    printf("btmem %-16s total=%u | int8 free=%u largest=%u min=%u | dma free=%u largest=%u min=%u\n",
           stage,
           (unsigned)esp_get_free_heap_size(),
           (unsigned)heap_caps_get_free_size(icaps),
           (unsigned)heap_caps_get_largest_free_block(icaps),
           (unsigned)heap_caps_get_minimum_free_size(icaps),
           (unsigned)heap_caps_get_free_size(dcaps),
           (unsigned)heap_caps_get_largest_free_block(dcaps),
           (unsigned)heap_caps_get_minimum_free_size(dcaps));
    fflush(stdout);
    if (s_monitor)
    {
        heap_caps_monitor_local_minimum_free_size_stop();
    }
    s_monitor = (heap_caps_monitor_local_minimum_free_size_start() == ESP_OK);
}

/* ---------------------------------------------------------------- audio */

static const int16_t kTone[20] = {
    /* 400 Hz at 8 kHz, about -12 dBFS. */
    0, 2531, 4815, 6627, 7791, 8192, 7791, 6627, 4815, 2531,
    0, -2531, -4815, -6627, -7791, -8192, -7791, -6627, -4815, -2531,
};

static void hf_incoming(const uint8_t *buf, uint32_t len)
{
    const uint32_t n = len / 2;
    bool rec_done = false;

    s_rx_bytes += len;
    s_rx_pkts++;
    if (s_mode == MODE_REC)
    {
        size_t pos = s_clip_len;

        for (uint32_t i = 0; i < n; i++)
        {
            int16_t s;

            memcpy(&s, buf + (2 * i), sizeof(s));
            if (s < 0)
            {
                s_peak = ((int32_t)-s > s_peak) ? (int32_t)-s : s_peak;
            }
            else if (s > s_peak)
            {
                s_peak = s;
            }
            if (!grok_adpcm_put(&s_enc, s_clip, s_clip_cap, &pos, s))
            {
                rec_done = true;
                break;
            }
        }
        s_clip_len = pos;
        if (rec_done)
        {
            s_mode = MODE_IDLE;
            xEventGroupSetBits(s_ev, kEvRecDone);
        }
    }

    /* Send exactly as much as we receive: SCO is symmetric, so the headset's
     * packets pace ours and the stack's send loop always terminates. */
    portENTER_CRITICAL(&s_mux);
    s_last_in_len = len;
    s_credit += len;
    if (s_credit > (8 * len))
    {
        s_credit = 8 * len;
    }
    portEXIT_CRITICAL(&s_mux);
    esp_hf_ag_outgoing_data_ready();
}

static uint32_t hf_outgoing(uint8_t *buf, uint32_t len)
{
    const uint32_t n = len / 2;
    bool play_done = false;

    portENTER_CRITICAL(&s_mux);
    if (s_credit < len)
    {
        portEXIT_CRITICAL(&s_mux);
        return 0;
    }
    s_credit -= len;
    portEXIT_CRITICAL(&s_mux);

    for (uint32_t i = 0; i < n; i++)
    {
        int16_t s = 0;

        if (s_mode == MODE_PLAY)
        {
            if (!grok_adpcm_get(&s_dec, s_clip, s_clip_len, &s_play_pos, &s))
            {
                s = 0;
                play_done = true;
            }
        }
        else if (s_mode == MODE_TONE)
        {
            if (s_tone_left == 0)
            {
                play_done = true;
            }
            else
            {
                /* mSBC runs at 16 kHz: step the 8 kHz table at half speed. */
                s = kTone[(s_tone_phase >> (s_msbc ? 1 : 0)) % 20];
                s_tone_phase++;
                s_tone_left--;
            }
        }
        memcpy(buf + (2 * i), &s, sizeof(s));
    }
    if (play_done && ((s_mode == MODE_PLAY) || (s_mode == MODE_TONE)))
    {
        s_mode = MODE_IDLE;
        xEventGroupSetBits(s_ev, kEvPlayDone);
    }
    s_tx_bytes += len;
    return len;
}

static uint32_t sample_rate(void)
{
    return s_msbc ? 16000 : 8000;
}

/* ------------------------------------------------------------ callbacks */

static void fmt(const uint8_t *mac, char *out)
{
    grok_bt_format_mac(mac, out, 18);
}

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    char mac[18];

    switch (event)
    {
    case ESP_BT_GAP_DISC_RES_EVT:
    {
        uint32_t cod = 0;
        int rssi = 0;
        char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1] = "";

        for (int i = 0; i < param->disc_res.num_prop; i++)
        {
            esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];

            if (p->type == ESP_BT_GAP_DEV_PROP_COD)
            {
                cod = *(uint32_t *)p->val;
            }
            else if (p->type == ESP_BT_GAP_DEV_PROP_RSSI)
            {
                rssi = *(int8_t *)p->val;
            }
            else if ((p->type == ESP_BT_GAP_DEV_PROP_BDNAME) && (name[0] == '\0'))
            {
                int l = (p->len < ESP_BT_GAP_MAX_BDNAME_LEN) ? p->len : ESP_BT_GAP_MAX_BDNAME_LEN;
                memcpy(name, p->val, l);
                name[l] = '\0';
            }
            else if ((p->type == ESP_BT_GAP_DEV_PROP_EIR) && (name[0] == '\0'))
            {
                uint8_t l = 0;
                uint8_t *n = esp_bt_gap_resolve_eir_data(p->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &l);

                if (n == NULL)
                {
                    n = esp_bt_gap_resolve_eir_data(p->val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &l);
                }
                if (n != NULL)
                {
                    l = (l < ESP_BT_GAP_MAX_BDNAME_LEN) ? l : ESP_BT_GAP_MAX_BDNAME_LEN;
                    memcpy(name, n, l);
                    name[l] = '\0';
                }
            }
        }
        grok_text_fold_ascii(name);
        fmt(param->disc_res.bda, mac);
        printf("bt: found %s cod=0x%06x rssi=%d %s%s\n", mac, (unsigned)cod, rssi,
               (name[0] != '\0') ? name : "(no name)",
               grok_bt_cod_is_audio(cod) ? " [audio]" : "");
        if (grok_bt_cod_is_audio(cod) && !s_have_found)
        {
            memcpy(s_found, param->disc_res.bda, ESP_BD_ADDR_LEN);
            s_have_found = true;
        }
        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED)
        {
            xEventGroupSetBits(s_ev, kEvDiscDone);
        }
        break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        fmt(param->auth_cmpl.bda, mac);
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS)
        {
            char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];

            snprintf(name, sizeof(name), "%s", (const char *)param->auth_cmpl.device_name);
            grok_text_fold_ascii(name);
            printf("bt: paired with %s %s\n", mac, name);
        }
        else
        {
            printf("bt: pairing with %s failed (status %d)\n", mac, param->auth_cmpl.stat);
        }
        xEventGroupSetBits(s_ev, kEvAuthDone);
        break;
    case ESP_BT_GAP_PIN_REQ_EVT:
    {
        /* Legacy (pre-2.1) headsets: almost all use 0000. */
        esp_bt_pin_code_t pin;
        uint8_t len = param->pin_req.min_16_digit ? 16 : 4;

        memset(pin, '0', sizeof(pin));
        printf("bt: legacy pin request, replying %u zeros\n", (unsigned)len);
        esp_bt_gap_pin_reply(param->pin_req.bda, true, len, pin);
        break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
        /* NoInputNoOutput on both sides: Just Works. */
        printf("bt: ssp confirm %06u, accepting\n", (unsigned)param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        printf("bt: passkey %06u\n", (unsigned)param->key_notif.passkey);
        break;
    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        fmt(param->acl_disconn_cmpl_stat.bda, mac);
        printf("bt: acl to %s closed (reason 0x%x)\n", mac, param->acl_disconn_cmpl_stat.reason);
        break;
    default:
        break;
    }
}

static void hf_cb(esp_hf_cb_event_t event, esp_hf_cb_param_t *param)
{
    char mac[18];

    switch (event)
    {
    case ESP_HF_CONNECTION_STATE_EVT:
        fmt(param->conn_stat.remote_bda, mac);
        printf("bt: hfp %s state %d (0 down, 1 connecting, 2 rfcomm, 3 slc)\n", mac, param->conn_stat.state);
        if (param->conn_stat.state == ESP_HF_CONNECTION_STATE_SLC_CONNECTED)
        {
            memcpy(s_peer, param->conn_stat.remote_bda, ESP_BD_ADDR_LEN);
            s_have_peer = true;
            s_slc = true;
            xEventGroupClearBits(s_ev, kEvSlcDown);
            xEventGroupSetBits(s_ev, kEvSlcUp);
        }
        else if (param->conn_stat.state == ESP_HF_CONNECTION_STATE_DISCONNECTED)
        {
            s_slc = false;
            s_audio = false;
            s_mode = MODE_IDLE;
            xEventGroupClearBits(s_ev, kEvSlcUp | kEvAudioUp);
            xEventGroupSetBits(s_ev, kEvSlcDown | kEvAudioDown);
        }
        break;
    case ESP_HF_AUDIO_STATE_EVT:
        if ((param->audio_stat.state == ESP_HF_AUDIO_STATE_CONNECTED) ||
            (param->audio_stat.state == ESP_HF_AUDIO_STATE_CONNECTED_MSBC))
        {
            s_msbc = (param->audio_stat.state == ESP_HF_AUDIO_STATE_CONNECTED_MSBC);
            portENTER_CRITICAL(&s_mux);
            s_credit = 0;
            portEXIT_CRITICAL(&s_mux);
            esp_hf_ag_register_data_callback(hf_incoming, hf_outgoing);
            s_sco_handle = param->audio_stat.sync_conn_handle;
            s_audio = true;
            printf("bt: sco open, %s %u Hz, handle 0x%x\n", s_msbc ? "msbc" : "cvsd",
                   (unsigned)sample_rate(), param->audio_stat.sync_conn_handle);
            xEventGroupClearBits(s_ev, kEvAudioDown);
            xEventGroupSetBits(s_ev, kEvAudioUp);
        }
        else if (param->audio_stat.state == ESP_HF_AUDIO_STATE_DISCONNECTED)
        {
            s_audio = false;
            s_mode = MODE_IDLE;
            printf("bt: sco closed\n");
            xEventGroupClearBits(s_ev, kEvAudioUp);
            xEventGroupSetBits(s_ev, kEvAudioDown | kEvRecDone | kEvPlayDone);
        }
        break;
    case ESP_HF_BVRA_RESPONSE_EVT:
        /* The headset's voice button (AT+BVRA=1). The real feature would
         * start listening here; the spike opens audio. */
        printf("bt: headset voice button: %s\n", (param->vra_rep.value == ESP_HF_VR_STATE_ENABLED) ? "on" : "off");
        if ((param->vra_rep.value == ESP_HF_VR_STATE_ENABLED) && !s_audio)
        {
            esp_hf_ag_audio_connect(param->vra_rep.remote_addr);
        }
        break;
    case ESP_HF_VOLUME_CONTROL_EVT:
        printf("bt: headset %s volume %d\n", (param->volume_control.type == ESP_HF_VOLUME_TYPE_SPK) ? "speaker" : "mic",
               param->volume_control.volume);
        break;
    case ESP_HF_UNAT_RESPONSE_EVT:
        esp_hf_ag_unknown_at_send(param->unat_rep.remote_addr, NULL);
        break;
    case ESP_HF_IND_UPDATE_EVT:
        esp_hf_ag_ciev_report(param->ind_upd.remote_addr, ESP_HF_IND_TYPE_SERVICE, 1);
        break;
    case ESP_HF_CIND_RESPONSE_EVT:
        /* No call, service up, signal 4, not roaming, battery 3, none held.
         * Headsets stall the SLC handshake without this answer. */
        esp_hf_ag_cind_response(param->cind_rep.remote_addr, 0, 0, 1, 4, 0, 3, 0);
        break;
    case ESP_HF_COPS_RESPONSE_EVT:
        esp_hf_ag_cops_response(param->cops_rep.remote_addr, "Grok");
        break;
    case ESP_HF_CLCC_RESPONSE_EVT:
        /* No current calls: index 0 is the final OK. */
        esp_hf_ag_clcc_response(param->clcc_rep.remote_addr, 0, 0, 0, 0, 0, NULL, 0);
        break;
    case ESP_HF_CNUM_RESPONSE_EVT:
        esp_hf_ag_cmee_send(param->cnum_rep.remote_addr, ESP_HF_AT_RESPONSE_CODE_OK, ESP_HF_CME_AG_FAILURE);
        break;
    case ESP_HF_WBS_RESPONSE_EVT:
    case ESP_HF_BCS_RESPONSE_EVT:
        printf("bt: codec %s\n", ((event == ESP_HF_WBS_RESPONSE_EVT ? param->wbs_rep.codec : param->bcs_rep.mode) == ESP_HF_WBS_YES) ? "msbc" : "cvsd");
        break;
    case ESP_HF_PKT_STAT_NUMS_GET_EVT:
        printf("bt: sco packets rx total=%u ok=%u err=%u none=%u lost=%u | tx total=%u discarded=%u\n",
               (unsigned)param->pkt_nums.rx_total, (unsigned)param->pkt_nums.rx_correct,
               (unsigned)param->pkt_nums.rx_err, (unsigned)param->pkt_nums.rx_none,
               (unsigned)param->pkt_nums.rx_lost, (unsigned)param->pkt_nums.tx_total,
               (unsigned)param->pkt_nums.tx_discarded);
        break;
    default:
        break;
    }
}

/* -------------------------------------------------------------- control */

static bool bt_on(void)
{
    esp_err_t err;
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bluedroid_config_t bcfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;

    if (s_on)
    {
        return true;
    }
    if (s_ev == NULL)
    {
        s_ev = xEventGroupCreate();
        if (s_ev == NULL)
        {
            printf("bt: no memory for event group\n");
            return false;
        }
    }
    if (!s_mem_released)
    {
        /* BLE controller RAM is never needed: hand it to the heap once. */
        err = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
        printf("bt: ble controller ram release: %s\n", esp_err_to_name(err));
        s_mem_released = true;
        mark("ble released");
    }
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE)
    {
        err = esp_bt_controller_init(&cfg);
        if (err != ESP_OK)
        {
            printf("bt: controller init failed (%s)\n", esp_err_to_name(err));
            return false;
        }
    }
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED)
    {
        err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
        if (err != ESP_OK)
        {
            printf("bt: controller enable failed (%s)\n", esp_err_to_name(err));
            return false;
        }
    }
    mark("controller on");
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED)
    {
        err = esp_bluedroid_init_with_cfg(&bcfg);
        if (err != ESP_OK)
        {
            printf("bt: bluedroid init failed (%s)\n", esp_err_to_name(err));
            return false;
        }
    }
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED)
    {
        err = esp_bluedroid_enable();
        if (err != ESP_OK)
        {
            printf("bt: bluedroid enable failed (%s)\n", esp_err_to_name(err));
            return false;
        }
    }
    esp_bt_gap_register_callback(gap_cb);
    esp_bt_gap_set_device_name(BT_NAME);
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap));
    {
        esp_bt_pin_code_t pin;

        memset(pin, '0', sizeof(pin));
        esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 4, pin);
    }
    esp_hf_ag_register_callback(hf_cb);
    err = esp_hf_ag_init();
    if (err != ESP_OK)
    {
        printf("bt: hfp ag init failed (%s)\n", esp_err_to_name(err));
        return false;
    }
    /* Connectable so a bonded headset can reconnect; not discoverable. */
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    s_on = true;
    printf("bt: on as %s (hfp audio gateway)\n", BT_NAME);
    mark("bt enabled");
    return true;
}

static void clip_free(void)
{
    s_mode = MODE_IDLE;
    free(s_clip);
    s_clip = NULL;
    s_clip_cap = 0;
    s_clip_len = 0;
}

static void bt_off(void)
{
    if (!s_on)
    {
        clip_free();
        printf("bt: already off\n");
        return;
    }
    if (s_audio && s_have_peer)
    {
        esp_hf_ag_audio_disconnect(s_peer);
        xEventGroupWaitBits(s_ev, kEvAudioDown, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));
    }
    if (s_slc && s_have_peer)
    {
        esp_hf_ag_slc_disconnect(s_peer);
        xEventGroupWaitBits(s_ev, kEvSlcDown, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));
    }
    clip_free();
    esp_hf_ag_deinit();
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    s_on = false;
    s_slc = false;
    s_audio = false;
    /* The classic controller RAM stays reserved so "bt on" works again;
     * esp_bt_controller_mem_release(ESP_BT_MODE_BTDM) would free it until
     * reboot. */
    printf("bt: off\n");
    mark("bt off");
}

static bool scan(uint32_t secs)
{
    uint8_t len = (uint8_t)(((secs * 100) + 127) / 128); /* units of 1.28 s */

    if (len < 1)
    {
        len = 1;
    }
    if (len > 0x30)
    {
        len = 0x30;
    }
    s_have_found = false;
    xEventGroupClearBits(s_ev, kEvDiscDone);
    printf("bt: scanning about %u s. Put the headset in pairing mode.\n", (unsigned)secs);
    if (esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, len, 0) != ESP_OK)
    {
        printf("bt: discovery did not start\n");
        return false;
    }
    xEventGroupWaitBits(s_ev, kEvDiscDone, pdFALSE, pdFALSE, pdMS_TO_TICKS((secs + 5) * 1000));
    return s_have_found;
}

static bool hfp_connect(const uint8_t *mac)
{
    char text[18];

    fmt(mac, text);
    if (s_slc && s_have_peer && (memcmp(mac, s_peer, ESP_BD_ADDR_LEN) == 0))
    {
        return true;
    }
    printf("bt: connecting hfp to %s (pairs first if needed)\n", text);
    xEventGroupClearBits(s_ev, kEvSlcUp | kEvSlcDown);
    if (esp_hf_ag_slc_connect((uint8_t *)mac) != ESP_OK)
    {
        printf("bt: slc connect did not start\n");
        return false;
    }
    if ((xEventGroupWaitBits(s_ev, kEvSlcUp | kEvSlcDown, pdFALSE, pdFALSE, pdMS_TO_TICKS(30000)) & kEvSlcUp) == 0)
    {
        printf("bt: no service level connection to %s\n", text);
        return false;
    }
    return true;
}

static bool sco_open(void)
{
    if (s_audio)
    {
        return true;
    }
    if (!s_slc)
    {
        printf("bt: no headset connected. bt pair [mac]\n");
        return false;
    }
    xEventGroupClearBits(s_ev, kEvAudioUp | kEvAudioDown);
    esp_hf_ag_audio_connect(s_peer);
    if ((xEventGroupWaitBits(s_ev, kEvAudioUp | kEvAudioDown, pdFALSE, pdFALSE, pdMS_TO_TICKS(8000)) & kEvAudioUp) == 0)
    {
        /* Some headsets open audio only after voice recognition starts. */
        printf("bt: sco did not open; trying voice recognition\n");
        xEventGroupClearBits(s_ev, kEvAudioDown);
        esp_hf_ag_vra_control(s_peer, ESP_HF_VR_STATE_ENABLED);
        esp_hf_ag_audio_connect(s_peer);
        if ((xEventGroupWaitBits(s_ev, kEvAudioUp, pdFALSE, pdFALSE, pdMS_TO_TICKS(8000)) & kEvAudioUp) == 0)
        {
            printf("bt: sco did not open\n");
            return false;
        }
    }
    return true;
}

static void sco_close(void)
{
    if (s_audio && s_have_peer)
    {
        xEventGroupClearBits(s_ev, kEvAudioDown);
        esp_hf_ag_audio_disconnect(s_peer);
        xEventGroupWaitBits(s_ev, kEvAudioDown, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));
    }
}

static bool record(uint32_t secs)
{
    size_t need;
    int64_t t0;

    if (!s_audio)
    {
        printf("bt: sco is not open. bt sco\n");
        return false;
    }
    need = ((size_t)secs * sample_rate() + 1) / 2;
    if ((s_clip != NULL) && (s_clip_cap != need))
    {
        clip_free();
    }
    if (s_clip == NULL)
    {
        s_clip = heap_caps_malloc(need, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (s_clip == NULL)
        {
            printf("bt: no %u bytes for the clip (largest block %u)\n", (unsigned)need,
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            return false;
        }
        s_clip_cap = need;
    }
    grok_adpcm_reset(&s_enc);
    s_clip_len = 0;
    s_peak = 0;
    s_rx_bytes = 0;
    s_rx_pkts = 0;
    xEventGroupClearBits(s_ev, kEvRecDone);
    printf("bt: recording %u s (%u bytes adpcm). Speak into the headset.\n", (unsigned)secs, (unsigned)need);
    t0 = esp_timer_get_time();
    s_mode = MODE_REC;
    vTaskDelay(pdMS_TO_TICKS(secs * 500));
    mark("recording");
    xEventGroupWaitBits(s_ev, kEvRecDone, pdFALSE, pdFALSE, pdMS_TO_TICKS(secs * 1000 + 3000));
    s_mode = MODE_IDLE;
    {
        uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        uint32_t want = s_clip_cap * 2;

        printf("bt: recorded %u of %u samples in %u ms, %u packets, peak %d (%d dBFS-ish)\n",
               (unsigned)s_clip_len, (unsigned)want, (unsigned)ms, (unsigned)s_rx_pkts, (int)s_peak,
               (s_peak > 0) ? (int)(20.0f * log10f((float)s_peak / 32767.0f)) : -99);
        if (ms > (secs * 1000 + 500))
        {
            printf("bt: slower than real time: sco packets were lost (coexistence?)\n");
        }
    }
    esp_hf_ag_pkt_stat_nums_get(s_sco_handle);
    return s_clip_len > 0;
}

static bool play(bool tone)
{
    uint32_t secs;

    if (!s_audio)
    {
        printf("bt: sco is not open. bt sco\n");
        return false;
    }
    if (!tone && ((s_clip == NULL) || (s_clip_len == 0)))
    {
        printf("bt: nothing recorded. bt rec\n");
        return false;
    }
    xEventGroupClearBits(s_ev, kEvPlayDone);
    if (tone)
    {
        s_tone_phase = 0;
        s_tone_left = 2 * sample_rate();
        secs = 2;
        printf("bt: 400 Hz tone for 2 s\n");
        s_mode = MODE_TONE;
    }
    else
    {
        grok_adpcm_reset(&s_dec);
        s_play_pos = 0;
        secs = (uint32_t)(s_clip_len / sample_rate()) + 1;
        printf("bt: playing %u samples\n", (unsigned)s_clip_len);
        s_mode = MODE_PLAY;
    }
    xEventGroupWaitBits(s_ev, kEvPlayDone, pdFALSE, pdFALSE, pdMS_TO_TICKS(secs * 1000 + 3000));
    s_mode = MODE_IDLE;
    printf("bt: playback done, %u bytes sent\n", (unsigned)s_tx_bytes);
    return true;
}

/* A TLS handshake and a small GET with Bluetooth up. No key is sent;
 * api.x.ai answers 401, which is fine: the handshake and the record buffers
 * are the memory cost. */
static void tls_probe(const char *url)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    esp_http_client_handle_t client;
    esp_err_t err;
    int64_t t0;
    char buf[128];
    int total = 0;

    if (!grok_wifi_is_connected())
    {
        printf("bt: tls probe skipped, no wifi\n");
        return;
    }
    mark("before tls");
    client = esp_http_client_init(&cfg);
    if (client == NULL)
    {
        printf("bt: tls client init failed\n");
        return;
    }
    t0 = esp_timer_get_time();
    err = esp_http_client_open(client, 0);
    printf("bt: tls %s %s in %u ms\n", url, (err == ESP_OK) ? "connected" : esp_err_to_name(err),
           (unsigned)((esp_timer_get_time() - t0) / 1000));
    mark("tls handshake");
    if (err == ESP_OK)
    {
        esp_http_client_fetch_headers(client);
        for (;;)
        {
            int n = esp_http_client_read(client, buf, sizeof(buf));
            if (n <= 0)
            {
                break;
            }
            total += n;
        }
        printf("bt: https status %d, %d body bytes\n", esp_http_client_get_status_code(client), total);
    }
    esp_http_client_cleanup(client);
    mark("after tls");
}

static void proxy_probe(void)
{
    char url[GROK_URL_MAX + 1];
    char token[GROK_TOKEN_MAX + 1];
    char *reply;

    grok_store_get_url(url, sizeof(url));
    if ((url[0] == '\0') || !grok_wifi_is_connected())
    {
        printf("bt: proxy ask skipped (%s)\n", (url[0] == '\0') ? "no proxy url" : "no wifi");
        return;
    }
    reply = malloc(GROK_REPLY_MAX + 1);
    if (reply == NULL)
    {
        printf("bt: proxy ask skipped, no memory for the reply\n");
        return;
    }
    grok_store_get_token(token, sizeof(token));
    mark("before proxy");
    grok_http_ask(url, token, "Reply with the single word OK.", reply, GROK_REPLY_MAX + 1);
    memset(token, 0, sizeof(token));
    printf("bt: proxy reply: %.60s\n", reply);
    free(reply);
    mark("after proxy");
}

static bool pick_and_connect(const char *mac_text)
{
    uint8_t mac[6];

    if (mac_text != NULL)
    {
        if (!grok_bt_parse_mac(mac_text, mac))
        {
            printf("bt: mac must look like aa:bb:cc:dd:ee:ff\n");
            return false;
        }
    }
    else if (s_have_peer)
    {
        memcpy(mac, s_peer, sizeof(mac));
    }
    else
    {
        if (!scan(10))
        {
            printf("bt: no audio device found. Is the headset in pairing mode?\n");
            return false;
        }
        memcpy(mac, s_found, sizeof(mac));
    }
    return hfp_connect(mac);
}

static uint32_t parse_secs(const char *text, uint32_t dflt)
{
    int v = (text != NULL) ? atoi(text) : 0;

    return ((v >= 1) && (v <= 10)) ? (uint32_t)v : dflt;
}

static void full_test(const char *mac_text)
{
    printf("bt: === headset spike test ===\n");
    mark("start");
    if (grok_wifi_has_ssid())
    {
        for (int i = 0; (i < 75) && !grok_wifi_is_connected(); i++)
        {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    mark(grok_wifi_is_connected() ? "wifi connected" : "no wifi");
    if (!bt_on())
    {
        return;
    }
    if (!pick_and_connect(mac_text))
    {
        return;
    }
    mark("headset slc");
    if (!sco_open())
    {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(300));
    mark("sco open");
    play(true);
    if (record(CONFIG_GROK_BT_REC_SECONDS))
    {
        mark("recorded");
        play(false);
        mark("played");
    }
    /* Network with SCO still running: the real voice path's worst case. */
    proxy_probe();
    tls_probe(TLS_PROBE_URL);
    esp_hf_ag_pkt_stat_nums_get(s_sco_handle);
    vTaskDelay(pdMS_TO_TICKS(200));
    sco_close();
    mark("sco closed");
    printf("bt: === done. Look for the lowest int8 min= above. bt off frees it all. ===\n");
}

static void status(void)
{
    char mac[18] = "none";

    if (s_have_peer)
    {
        fmt(s_peer, mac);
    }
    printf("bt: %s, headset %s%s, sco %s%s, clip %u samples, wifi %s\n",
           s_on ? "on" : "off", mac, s_slc ? " connected" : "",
           s_audio ? (s_msbc ? "msbc" : "cvsd") : "closed",
           (s_mode == MODE_REC) ? " recording" : ((s_mode != MODE_IDLE) ? " playing" : ""),
           (unsigned)s_clip_len, grok_wifi_is_connected() ? "up" : "down");
    if (s_on)
    {
        int n = esp_bt_gap_get_bond_device_num();
        printf("bt: %d bonded device(s)\n", n);
    }
}

static void unpair(void)
{
    esp_bd_addr_t list[4];
    int n = 4;

    if (!s_on)
    {
        printf("bt: bt on first\n");
        return;
    }
    if (esp_bt_gap_get_bond_device_list(&n, list) != ESP_OK)
    {
        n = 0;
    }
    for (int i = 0; i < n; i++)
    {
        esp_bt_gap_remove_bond_device(list[i]);
    }
    s_have_peer = false;
    printf("bt: removed %d bond(s)\n", n);
}

void grok_bt_usage(void)
{
    grok_say("bt: test [mac] | on | off | scan | pair [mac] | sco | sco off | tone | rec [secs] | play | tls [url] | proxy | status | mem | unpair\n");
}

bool grok_bt_is_quick(int argc, char **argv)
{
    return (argc < 2) || (strcmp(argv[1], "status") == 0) || (strcmp(argv[1], "mem") == 0);
}

bool grok_bt_active(void)
{
    return s_on;
}

void grok_bt_boot_mark(void)
{
    mark("boot");
}

void grok_bt_exec(int argc, char **argv)
{
    const char *sub = (argc >= 2) ? argv[1] : "";
    const char *arg = (argc >= 3) ? argv[2] : NULL;

    if (strcmp(sub, "status") == 0)
    {
        status();
        return;
    }
    if (strcmp(sub, "mem") == 0)
    {
        mark("now");
        return;
    }
    if (strcmp(sub, "test") == 0)
    {
        full_test(arg);
        return;
    }
    if (strcmp(sub, "on") == 0)
    {
        bt_on();
        return;
    }
    if (strcmp(sub, "off") == 0)
    {
        bt_off();
        return;
    }
    if (strcmp(sub, "tls") == 0)
    {
        tls_probe((arg != NULL) ? arg : TLS_PROBE_URL);
        return;
    }
    if (strcmp(sub, "proxy") == 0)
    {
        proxy_probe();
        return;
    }
    if (!s_on && !bt_on())
    {
        return;
    }
    if (strcmp(sub, "scan") == 0)
    {
        scan(10);
        return;
    }
    if (strcmp(sub, "pair") == 0)
    {
        if (pick_and_connect(arg))
        {
            mark("headset slc");
        }
        return;
    }
    if (strcmp(sub, "sco") == 0)
    {
        if ((arg != NULL) && (strcmp(arg, "off") == 0))
        {
            sco_close();
            mark("sco closed");
        }
        else if (sco_open())
        {
            mark("sco open");
        }
        return;
    }
    if (strcmp(sub, "tone") == 0)
    {
        play(true);
        return;
    }
    if (strcmp(sub, "rec") == 0)
    {
        if (record(parse_secs(arg, CONFIG_GROK_BT_REC_SECONDS)))
        {
            mark("recorded");
        }
        return;
    }
    if (strcmp(sub, "play") == 0)
    {
        play(false);
        return;
    }
    if (strcmp(sub, "unpair") == 0)
    {
        unpair();
        return;
    }
    grok_bt_usage();
}

#else /* !CONFIG_GROK_BT_HEADSET_SPIKE */

bool grok_bt_active(void)
{
    return false;
}

#endif
