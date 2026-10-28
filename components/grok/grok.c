/* Chromatic Grok link. A modified work based on the ModRetro Chromatic MCU
 * firmware (GPL-3.0-or-later). See LICENSE and NOTICE.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok.h"

#include "grok_internal.h"
#include "grok_ota_util.h"

#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

typedef enum {
    GROK_JOB_ASK = 0,
    GROK_JOB_UPDATE,   /* prompt holds the https URL */
    GROK_JOB_ROLLBACK,
} GrokJobKind_t;

typedef struct GrokJob {
    uint8_t kind;
    uint8_t web;       /* asked from the phone page */
    char prompt[GROK_PROMPT_MAX + 1];
} GrokJob_t;

static volatile bool s_keep_awake = true;
static SemaphoreHandle_t s_mu;
static SemaphoreHandle_t s_pub;
static QueueHandle_t s_jobs;
/* Set by the radio task while it runs a job the phone page asked. */
static volatile bool s_job_web;
static char s_last[GROK_PROMPT_MAX + 1];
static char s_show[GROK_REPLY_MAX + 1];
static char s_folded[GROK_REPLY_MAX + 1];

static void lock(void)
{
    if (s_mu != NULL)
    {
        xSemaphoreTake(s_mu, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_mu != NULL)
    {
        xSemaphoreGive(s_mu);
    }
}

static int cmd_entry(int argc, char **argv);
static void radio_task(void *arg);

bool Grok_WantsRadioAwake(void)
{
    return s_keep_awake;
}

void grok_say(const char *text)
{
    if (text == NULL)
    {
        return;
    }
    fputs(text, stdout);
    fflush(stdout);
}

void grok_publish(const char *text)
{
    if (text == NULL)
    {
        text = "";
    }
    if (s_pub != NULL)
    {
        xSemaphoreTake(s_pub, portMAX_DELAY);
    }
    snprintf(s_folded, sizeof(s_folded), "%s", text);
    grok_text_fold_ascii(s_folded);

    grok_say("GROK_BEGIN\n");
    grok_say(s_folded);
    if ((s_folded[0] != '\0') && (s_folded[strlen(s_folded) - 1] != '\n'))
    {
        grok_say("\n");
    }
    grok_say("GROK_END\n");
    grok_ui_set_body(s_folded);
    grok_ble_queue_text(s_folded);
    if (s_job_web)
    {
        grok_web_answer(s_folded);
    }
    if (s_pub != NULL)
    {
        xSemaphoreGive(s_pub);
    }
}

bool grok_submit_prompt(const char *prompt)
{
    GrokJob_t job;

    if ((prompt == NULL) || (prompt[0] == '\0'))
    {
        grok_publish("empty prompt");
        return false;
    }
    if (s_jobs == NULL)
    {
        grok_publish("grok: radio is not up");
        return false;
    }

    memset(&job, 0, sizeof(job));
    lock();
    snprintf(s_last, sizeof(s_last), "%s", prompt);
    snprintf(job.prompt, sizeof(job.prompt), "%s", s_last);
    unlock();

    grok_ui_set_body("...");
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE)
    {
        grok_publish("busy");
        return false;
    }
    return true;
}

bool grok_submit_web(const char *prompt)
{
    GrokJob_t job;

    if ((prompt == NULL) || (prompt[0] == '\0') || (s_jobs == NULL))
    {
        return false;
    }
    memset(&job, 0, sizeof(job));
    job.kind = GROK_JOB_ASK;
    job.web = 1;
    lock();
    snprintf(s_last, sizeof(s_last), "%s", prompt);
    snprintf(job.prompt, sizeof(job.prompt), "%s", s_last);
    unlock();

    if (xQueueSend(s_jobs, &job, 0) != pdTRUE)
    {
        return false;
    }
    grok_ui_set_body("...");
    return true;
}

void grok_submit_show(const char *text)
{
    grok_publish((text != NULL) ? text : "");
}

bool grok_retry(void)
{
    char copy[GROK_PROMPT_MAX + 1];
    bool empty;

    lock();
    snprintf(copy, sizeof(copy), "%s", s_last);
    empty = (copy[0] == '\0');
    unlock();

    if (empty)
    {
        grok_publish("No prompt yet.");
        return false;
    }
    return grok_submit_prompt(copy);
}

/* False when the joined text does not fit. An empty join still returns true. */
static bool join_from(int argc, char **argv, int start, char *dst, size_t cap)
{
    size_t used = 0;

    if ((dst == NULL) || (cap == 0))
    {
        return false;
    }
    dst[0] = '\0';
    for (int i = start; i < argc; i++)
    {
        size_t len = strlen(argv[i]);
        size_t need = len + ((used > 0) ? 1 : 0);

        if ((used + need) >= cap)
        {
            dst[0] = '\0';
            return false;
        }
        if (used > 0)
        {
            dst[used++] = ' ';
        }
        memcpy(dst + used, argv[i], len);
        used += len;
        dst[used] = '\0';
    }
    return true;
}

static bool is_number(const char *text)
{
    if ((text == NULL) || (text[0] == '\0'))
    {
        return false;
    }
    for (const char *p = text; *p != '\0'; p++)
    {
        if ((*p < '0') || (*p > '9'))
        {
            return false;
        }
    }
    return true;
}

static int split_args(char *line, char **argv, int max)
{
    int argc = 0;
    char *p = line;

    while ((*p != '\0') && (argc < max))
    {
        while ((*p == ' ') || (*p == '\t'))
        {
            p++;
        }
        if (*p == '\0')
        {
            break;
        }
        if (argc == (max - 1))
        {
            if (*p == '"')
            {
                p++;
                argv[argc++] = p;
                while ((*p != '\0') && (*p != '"'))
                {
                    p++;
                }
                if (*p == '"')
                {
                    *p = '\0';
                }
            }
            else
            {
                argv[argc++] = p;
            }
            break;
        }
        if (*p == '"')
        {
            p++;
            argv[argc++] = p;
            while ((*p != '\0') && (*p != '"'))
            {
                p++;
            }
            if (*p == '"')
            {
                *p++ = '\0';
            }
        }
        else
        {
            argv[argc++] = p;
            while ((*p != '\0') && (*p != ' ') && (*p != '\t'))
            {
                p++;
            }
            if (*p != '\0')
            {
                *p++ = '\0';
            }
        }
    }
    return argc;
}

bool grok_question_get(int idx, char *out, size_t out_cap, bool *custom)
{
    const char *builtin = grok_pick_builtin(idx);

    if (custom != NULL)
    {
        *custom = false;
    }
    if ((out == NULL) || (out_cap == 0) || (builtin == NULL))
    {
        return false;
    }
    if (grok_store_get_question(idx, out, out_cap))
    {
        if (custom != NULL)
        {
            *custom = true;
        }
        return true;
    }
    snprintf(out, out_cap, "%s", builtin);
    return true;
}

static void cmd_q(int argc, char **argv)
{
    char text[GROK_PROMPT_MAX + 1];
    int idx = 0;

    if ((argc == 3) && (strcmp(argv[2], "list") == 0))
    {
        for (int i = 0; i < GROK_PICK_COUNT; i++)
        {
            bool custom = false;

            grok_question_get(i, text, sizeof(text), &custom);
            printf("q%d%s %s\n", i + 1, custom ? "*" : " ", text);
        }
        grok_say("(* = custom, saved on the device)\n");
        return;
    }
    if ((argc >= 5) && (strcmp(argv[2], "set") == 0) && grok_pick_parse_slot(argv[3], &idx))
    {
        if (!join_from(argc, argv, 4, text, sizeof(text)) || (text[0] == '\0'))
        {
            grok_say("grok: question must be 1 to 200 characters\n");
            return;
        }
        grok_say(grok_store_set_question(idx, text) ? "grok: question saved\n" : "grok: could not save question\n");
        return;
    }
    if ((argc == 4) && (strcmp(argv[2], "clear") == 0) && grok_pick_parse_slot(argv[3], &idx))
    {
        grok_store_clear_question(idx);
        grok_say("grok: question reset to built-in\n");
        return;
    }
    if ((argc == 4) && (strcmp(argv[2], "ask") == 0) && grok_pick_parse_slot(argv[3], &idx))
    {
        grok_question_get(idx, text, sizeof(text), NULL);
        grok_submit_prompt(text);
        return;
    }
    grok_say("grok: q list | q set <1-8> <text> | q clear <1-8> | q ask <1-8>\n");
}

static bool submit_job(uint8_t kind, const char *text)
{
    GrokJob_t job;

    if (s_jobs == NULL)
    {
        grok_publish("grok: radio is not up");
        return false;
    }
    memset(&job, 0, sizeof(job));
    job.kind = kind;
    snprintf(job.prompt, sizeof(job.prompt), "%s", (text != NULL) ? text : "");
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE)
    {
        grok_publish("busy");
        return false;
    }
    return true;
}

static void cmd_update(int argc, char **argv)
{
    const char *url = GROK_UPDATE_DEFAULT_URL;

    if ((argc == 3) && (strcmp(argv[2], "status") == 0))
    {
        grok_update_status();
        return;
    }
    if ((argc == 3) && (strcmp(argv[2], "rollback") == 0))
    {
        submit_job(GROK_JOB_ROLLBACK, NULL);
        return;
    }
    if ((argc == 3) && (strcmp(argv[2], "hold") == 0))
    {
        grok_update_arm_hold();
        return;
    }
    if (argc == 3)
    {
        url = argv[2];
    }
    else if (argc != 2)
    {
        grok_say("grok update [https-url] | status | rollback | hold\n");
        return;
    }
    if (!grok_ota_url_ok(url, GROK_PROMPT_MAX + 1))
    {
        grok_say("update: url must be https:// and at most 200 characters\n");
        return;
    }
    printf("update: queued. BLE turns off until the next reboot.\n");
    submit_job(GROK_JOB_UPDATE, url);
}

static void cmd_web(int argc, char **argv)
{
    if ((argc == 2) || ((argc == 3) && (strcmp(argv[2], "status") == 0)))
    {
        grok_web_status();
        return;
    }
    if ((argc == 3) && (strcmp(argv[2], "on") == 0))
    {
        if (!grok_store_set_web(true))
        {
            grok_say("grok: could not save web setting\n");
            return;
        }
        grok_say("web: on. plain http on the lan: the pin and questions are not encrypted\n");
        grok_web_kick();
        grok_web_status();
        return;
    }
    if ((argc == 3) && (strcmp(argv[2], "off") == 0))
    {
        if (!grok_store_set_web(false))
        {
            grok_say("grok: could not save web setting\n");
        }
        grok_web_down();
        grok_say("web: off\n");
        return;
    }
    grok_say("grok: web on | web off | web status\n");
}

static void wifi_usage(void)
{
    grok_say("wifi: status | connect | disconnect | clear | set <ssid> <password>\n");
}

static void grok_usage(void)
{
    grok_say("grok: status | open | close | retry | sleep on|off | url [set <url>] | token set <token>|clear | mode [direct|proxy] | key set <key>|clear | mem | bots | q list|set <n> <text>|clear <n>|ask <n> | web on|off|status | ble unpair | update [url]|status|rollback|hold | page next|prev|N | ask <prompt> | show <text>\n");
}

static void cmd_wifi(int argc, char **argv)
{
    if (argc < 2)
    {
        wifi_usage();
        return;
    }
    if (strcmp(argv[1], "status") == 0)
    {
        grok_wifi_print_status();
        return;
    }
    if (strcmp(argv[1], "connect") == 0)
    {
        grok_wifi_connect();
        return;
    }
    if (strcmp(argv[1], "disconnect") == 0)
    {
        grok_wifi_disconnect();
        return;
    }
    if (strcmp(argv[1], "clear") == 0)
    {
        grok_wifi_clear();
        return;
    }
    if (strcmp(argv[1], "set") == 0)
    {
        char pass[GROK_PASS_MAX + 1];

        if (argc < 3)
        {
            wifi_usage();
            return;
        }
        pass[0] = '\0';
        if ((argc >= 4) && !join_from(argc, argv, 3, pass, sizeof(pass)))
        {
            grok_say("wifi: password must be 0 to 64 characters\n");
            return;
        }
        grok_wifi_set(argv[2], pass);
        return;
    }
    wifi_usage();
}

static void cmd_grok(int argc, char **argv)
{
    if (argc < 2)
    {
        grok_usage();
        return;
    }
    if (strcmp(argv[1], "status") == 0)
    {
        char wifi[32];
        char ble[16];
        char url[GROK_URL_MAX + 1];
        char line[GROK_URL_MAX + 128];

        grok_wifi_format(wifi, sizeof(wifi));
        grok_ble_format(ble, sizeof(ble));
        grok_store_get_url(url, sizeof(url));
        snprintf(line, sizeof(line), "grok: %s %s %s mode=%s url=%s token=%s xai_key=%s\n",
                 wifi, ble,
                 s_keep_awake ? "awake" : "light-sleep allowed",
                 (grok_store_get_mode() == GROK_MODE_DIRECT) ? "direct" : "proxy",
                 (url[0] != '\0') ? url : "(unset)",
                 grok_store_has_token() ? "set" : "unset",
                 grok_store_has_xai_key() ? "set" : "unset");
        grok_say(line);
        return;
    }
    if (strcmp(argv[1], "open") == 0)
    {
        grok_ui_open();
        grok_say("grok: open\n");
        return;
    }
    if (strcmp(argv[1], "close") == 0)
    {
        grok_ui_close();
        grok_say("grok: closed\n");
        return;
    }
    if (strcmp(argv[1], "retry") == 0)
    {
        grok_retry();
        return;
    }
    if (strcmp(argv[1], "sleep") == 0)
    {
        if ((argc >= 3) && (strcmp(argv[2], "on") == 0))
        {
            s_keep_awake = false;
            grok_say("grok: light sleep allowed; wifi and ble will drop\n");
            return;
        }
        if ((argc >= 3) && (strcmp(argv[2], "off") == 0))
        {
            s_keep_awake = true;
            grok_say("grok: staying awake for wifi and ble\n");
            return;
        }
        grok_usage();
        return;
    }
    if (strcmp(argv[1], "token") == 0)
    {
        if ((argc == 4) && (strcmp(argv[2], "set") == 0))
        {
            if (strlen(argv[3]) > GROK_TOKEN_MAX)
            {
                grok_say("grok: token is too long\n");
                return;
            }
            {
                bool saved = grok_store_set_token(argv[3]);

                memset(argv[3], 0, strlen(argv[3]));
                grok_say(saved ? "grok: token saved\n" : "grok: could not save token\n");
            }
            return;
        }
        if ((argc == 3) && (strcmp(argv[2], "clear") == 0))
        {
            grok_store_clear_token();
            grok_say("grok: token cleared\n");
            return;
        }
        grok_usage();
        return;
    }
    if (strcmp(argv[1], "key") == 0)
    {
        /* xAI API key for direct mode. Never echoed, logged or put in an
         * error message. The serial console echoes it as '*' and keeps no
         * history of it (grok_console.c); argv is zeroed here. */
        if ((argc == 4) && (strcmp(argv[2], "set") == 0))
        {
            size_t len = strlen(argv[3]);
            bool saved = false;

            if ((len >= GROK_XAI_KEY_MIN) && (len <= GROK_XAI_KEY_MAX))
            {
                saved = grok_store_set_xai_key(argv[3]);
            }
            memset(argv[3], 0, len);
            if ((len < GROK_XAI_KEY_MIN) || (len > GROK_XAI_KEY_MAX))
            {
                grok_say("grok: key must be 16 to 192 printable characters; not saved\n");
                return;
            }
            grok_say(saved ? "grok: xai key saved\n" : "grok: could not save xai key\n");
            return;
        }
        if ((argc == 3) && (strcmp(argv[2], "clear") == 0))
        {
            grok_store_clear_xai_key();
            grok_say("grok: xai key cleared\n");
            return;
        }
        for (int i = 3; i < argc; i++)
        {
            memset(argv[i], 0, strlen(argv[i]));
        }
        grok_say("grok: key set <key> | key clear\n");
        return;
    }
    if (strcmp(argv[1], "mode") == 0)
    {
        if (argc == 2)
        {
            grok_say((grok_store_get_mode() == GROK_MODE_DIRECT) ? "grok: mode direct\n" : "grok: mode proxy\n");
            return;
        }
        if ((argc == 3) && (strcmp(argv[2], "direct") == 0))
        {
            if (!grok_store_set_mode(GROK_MODE_DIRECT))
            {
                grok_say("grok: could not save mode\n");
                return;
            }
            grok_say("grok: mode direct. ble stops before the next ask and stays off while mode is direct\n");
            return;
        }
        if ((argc == 3) && (strcmp(argv[2], "proxy") == 0))
        {
            if (!grok_store_set_mode(GROK_MODE_PROXY))
            {
                grok_say("grok: could not save mode\n");
                return;
            }
            grok_say(grok_ble_released() ? "grok: mode proxy. reboot to bring ble back\n" : "grok: mode proxy\n");
            return;
        }
        grok_usage();
        return;
    }
    if (strcmp(argv[1], "q") == 0)
    {
        cmd_q(argc, argv);
        return;
    }
    if (strcmp(argv[1], "update") == 0)
    {
        cmd_update(argc, argv);
        return;
    }
    if (strcmp(argv[1], "mem") == 0)
    {
        grok_mem_print("now");
        grok_web_mem_print();
        return;
    }
    if (strcmp(argv[1], "bots") == 0)
    {
        /* Static on-device list. No Grok Bot API. */
        for (int i = 0; i < GROK_BOTS_COUNT; i++)
        {
            const char *name = grok_bots_name(i);
            const char *note = grok_bots_note(i);
            char line[96];

            snprintf(line, sizeof(line), "grok: bot %d %s%s\n",
                     i + 1,
                     (name != NULL) ? name : "?",
                     ((note != NULL) && (note[0] != '\0')) ? " (note)" : "");
            grok_say(line);
        }
        grok_ui_open_bots();
        return;
    }
    if (strcmp(argv[1], "web") == 0)
    {
        cmd_web(argc, argv);
        return;
    }
    if ((strcmp(argv[1], "ble") == 0) && (argc == 3) && (strcmp(argv[2], "unpair") == 0))
    {
        grok_ble_unpair();
        return;
    }
    if (strcmp(argv[1], "url") == 0)
    {
        if ((argc >= 3) && (strcmp(argv[2], "set") == 0))
        {
            char url[GROK_URL_MAX + 1];

            if (argc < 4)
            {
                grok_usage();
                return;
            }
            if (!join_from(argc, argv, 3, url, sizeof(url)))
            {
                grok_say("grok: url is too long\n");
                return;
            }
            if ((url[0] == '\0') || (strncmp(url, "http://", 7) != 0))
            {
                grok_say("proxy url must start with http://\n");
                return;
            }
            if (!grok_store_set_url(url))
            {
                grok_say("grok: could not save url\n");
                return;
            }
            grok_say("grok: url saved\n");
            return;
        }
        if (argc == 2)
        {
            char url[GROK_URL_MAX + 1];
            char line[GROK_URL_MAX + 16];

            grok_store_get_url(url, sizeof(url));
            snprintf(line, sizeof(line), "grok: url=%s\n", (url[0] != '\0') ? url : "(unset)");
            grok_say(line);
            return;
        }
        grok_usage();
        return;
    }
    if (strcmp(argv[1], "page") == 0)
    {
        if (argc < 3)
        {
            grok_usage();
            return;
        }
        if (strcmp(argv[2], "next") == 0)
        {
            grok_ui_page(1);
        }
        else if (strcmp(argv[2], "prev") == 0)
        {
            grok_ui_page(-1);
        }
        else if (is_number(argv[2]))
        {
            int page = 0;
            for (const char *p = argv[2]; *p != '\0'; p++)
            {
                page = (page * 10) + (*p - '0');
                if (page > 999)
                {
                    break;
                }
            }
            if (page < 1)
            {
                page = 1;
            }
            grok_ui_set_page(page - 1);
        }
        else
        {
            grok_usage();
            return;
        }
        grok_ui_open();
        return;
    }
    if (strcmp(argv[1], "ask") == 0)
    {
        char prompt[GROK_PROMPT_MAX + 1];

        if (argc < 3)
        {
            grok_usage();
            return;
        }
        if (!join_from(argc, argv, 2, prompt, sizeof(prompt)) || (prompt[0] == '\0'))
        {
            grok_say("grok: prompt must be 1 to 200 characters\n");
            return;
        }
        grok_submit_prompt(prompt);
        return;
    }
    if (strcmp(argv[1], "show") == 0)
    {
        if (argc < 3)
        {
            grok_usage();
            return;
        }
        lock();
        if (!join_from(argc, argv, 2, s_show, sizeof(s_show)) || (s_show[0] == '\0'))
        {
            unlock();
            grok_say("grok: show text is empty or too long\n");
            return;
        }
        grok_submit_show(s_show);
        unlock();
        return;
    }
    grok_usage();
}

void grok_exec(int argc, char **argv)
{
    if ((argc < 1) || (argv == NULL) || (argv[0] == NULL))
    {
        grok_say("commands: wifi, grok\n");
        return;
    }
    if (strcmp(argv[0], "wifi") == 0)
    {
        cmd_wifi(argc, argv);
        return;
    }
    if (strcmp(argv[0], "grok") == 0)
    {
        cmd_grok(argc, argv);
        return;
    }
    grok_say("commands: wifi, grok\n");
}

void grok_exec_line(char *line)
{
    char *argv[16];
    int argc;

    if (line == NULL)
    {
        return;
    }
    while ((*line == ' ') || (*line == '\t'))
    {
        line++;
    }
    if (*line == '\0')
    {
        return;
    }
    argc = split_args(line, argv, 16);
    if (argc > 0)
    {
        grok_exec(argc, argv);
    }
}

static void radio_task(void *arg)
{
    (void)arg;

    grok_wifi_start();
    if (grok_store_get_mode() == GROK_MODE_DIRECT)
    {
        /* Direct HTTPS mode: never start BLE; give its RAM to the heap. */
        printf("grok: mode direct, ble not started\n");
        grok_ble_stop();
        grok_mem_print("boot");
        grok_web_kick();
    }
    else if (!grok_ble_start())
    {
        printf("ble: continuing with serial and wifi\n");
    }

    for (;;)
    {
        GrokJob_t job;
        char url[GROK_URL_MAX + 1];
        char token[GROK_TOKEN_MAX + 1];
        char reply[GROK_REPLY_MAX + 1];

        s_job_web = false;
        if (xQueueReceive(s_jobs, &job, grok_update_trial() ? pdMS_TO_TICKS(1000) : portMAX_DELAY) != pdTRUE)
        {
            grok_update_tick();
            continue;
        }
        grok_update_tick();
        s_job_web = (job.web != 0);

        if (job.kind == GROK_JOB_ROLLBACK)
        {
            grok_update_rollback_now();
            continue;
        }

        if (grok_wifi_has_ssid() && !grok_wifi_is_connected())
        {
            for (int i = 0; (i < 40) && !grok_wifi_is_connected(); i++)
            {
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }
        if (!grok_wifi_is_connected())
        {
            grok_publish("no wifi");
            continue;
        }

        if (job.kind == GROK_JOB_UPDATE)
        {
            /* Same as direct mode: TLS needs the BLE controller's RAM. */
            if (!grok_ble_released() && !grok_ble_stop())
            {
                grok_publish("ble did not stop; reboot and retry");
                continue;
            }
            grok_web_pause();
            grok_update_run(job.prompt);
            grok_web_resume();
            continue;
        }

        if (grok_store_get_mode() == GROK_MODE_DIRECT)
        {
            char key[GROK_XAI_KEY_MAX + 1];

            if (!grok_ble_released())
            {
                grok_mem_print("before ble stop");
                if (!grok_ble_stop())
                {
                    grok_publish("ble did not stop; reboot and retry");
                    continue;
                }
                grok_mem_print("after ble stop");
            }
            if (!grok_store_get_xai_key(key, sizeof(key)))
            {
                memset(key, 0, sizeof(key));
                grok_publish("no xai key. grok key set <key>");
                continue;
            }
            /* The phone page's server and sockets give their RAM to TLS
             * for the call; PIN and answer are kept. */
            grok_web_pause();
            grok_https_ask(key, job.prompt, reply, sizeof(reply));
            memset(key, 0, sizeof(key));
            grok_publish(reply);
            grok_web_resume();
            continue;
        }

        grok_store_get_url(url, sizeof(url));
        if (url[0] == '\0')
        {
            grok_publish("no proxy url. grok url set http://...");
            continue;
        }

        grok_store_get_token(token, sizeof(token));
        grok_http_ask(url, token, job.prompt, reply, sizeof(reply));
        memset(token, 0, sizeof(token));
        grok_publish(reply);
    }
}

static int cmd_entry(int argc, char **argv)
{
    grok_exec(argc, argv);
    return 0;
}

static void register_commands(void)
{
    const esp_console_cmd_t wifi_cmd = {
        .command = "wifi",
        .help = "Wi-Fi station: status, connect, disconnect, clear, set <ssid> <password>",
        .hint = NULL,
        .func = &cmd_entry,
    };
    const esp_console_cmd_t grok_cmd = {
        .command = "grok",
        .help = "Grok link: status, ask, bots, q, show, url, token, mode, key, mem, web, update, page, open, close, retry, sleep",
        .hint = NULL,
        .func = &cmd_entry,
    };

    if (esp_console_cmd_register(&wifi_cmd) != ESP_OK)
    {
        printf("grok: wifi command was not registered\n");
    }
    if (esp_console_cmd_register(&grok_cmd) != ESP_OK)
    {
        printf("grok: grok command was not registered\n");
    }
}

void Grok_Init(lv_obj_t *screen)
{
    s_keep_awake = true;
    if (s_mu == NULL)
    {
        s_mu = xSemaphoreCreateMutex();
    }
    if (s_pub == NULL)
    {
        s_pub = xSemaphoreCreateMutex();
    }
    if (s_jobs == NULL)
    {
        s_jobs = xQueueCreate(2, sizeof(GrokJob_t));
    }

    grok_store_init();
    grok_web_init();
    grok_ui_init(screen);
    register_commands();
    printf("grok: link ready. `help` lists wifi and grok.\n");
}

void Grok_Start(void)
{
    if (s_jobs == NULL)
    {
        printf("grok: radio task failed\n");
        return;
    }
    if (xTaskCreate(radio_task, "grok_radio", 16384, NULL, 5, NULL) != pdPASS)
    {
        printf("grok: radio task failed\n");
    }
}
