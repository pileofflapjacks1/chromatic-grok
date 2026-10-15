/* Chromatic Grok link. A modified work based on the ModRetro Chromatic MCU
 * firmware (GPL-3.0-or-later). See LICENSE and NOTICE.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok.h"

#include "grok_internal.h"

#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

typedef struct GrokJob {
    char prompt[GROK_PROMPT_MAX + 1];
} GrokJob_t;

static volatile bool s_keep_awake = true;
static SemaphoreHandle_t s_mu;
static SemaphoreHandle_t s_pub;
static QueueHandle_t s_jobs;
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

static void wifi_usage(void)
{
    grok_say("wifi: status | connect | disconnect | clear | set <ssid> <password>\n");
}

static void grok_usage(void)
{
    grok_say("grok: status | open | close | retry | sleep on|off | url [set <url>] | page next|prev|N | ask <prompt> | show <text>\n");
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
        char line[GROK_URL_MAX + 96];

        grok_wifi_format(wifi, sizeof(wifi));
        grok_ble_format(ble, sizeof(ble));
        grok_store_get_url(url, sizeof(url));
        snprintf(line, sizeof(line), "grok: %s %s %s url=%s\n",
                 wifi, ble,
                 s_keep_awake ? "awake" : "light-sleep allowed",
                 (url[0] != '\0') ? url : "(unset)");
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
    if (!grok_ble_start())
    {
        printf("ble: continuing with serial and wifi\n");
    }

    for (;;)
    {
        GrokJob_t job;
        char url[GROK_URL_MAX + 1];
        char reply[GROK_REPLY_MAX + 1];

        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE)
        {
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

        grok_store_get_url(url, sizeof(url));
        if (url[0] == '\0')
        {
            grok_publish("no proxy url. grok url set http://...");
            continue;
        }

        grok_http_ask(url, job.prompt, reply, sizeof(reply));
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
        .help = "Grok link: status, ask, show, url, page, open, close, retry, sleep",
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
