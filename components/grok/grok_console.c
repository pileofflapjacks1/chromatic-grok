/* Chromatic Grok link. A modified work based on the ModRetro Chromatic MCU
 * firmware (GPL-3.0-or-later). See LICENSE and NOTICE.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Serial console line reader. Replaces the esp_console linenoise REPL so
 * secrets typed on the serial port are not echoed:
 *   grok key set <key>, grok token set <token>, wifi set <ssid> <password>
 * echo '*' for the secret part. linenoise has no mask hook and redraws the
 * whole line on every key (multiline + hints mode), so it cannot be masked
 * from outside.
 *
 * UART setup matches esp_console_new_repl_uart() (same pins, REF_TICK clock
 * so light sleep keeps the baud rate, CR -> LF on input, CRLF on output).
 * Supported editing: printable ASCII, Backspace/DEL, Ctrl-U (clear line),
 * Ctrl-C (cancel line), Up arrow (recall the last non-secret command).
 * Other escape sequences are ignored. No TAB completion.
 *
 * The line buffer is zeroed after every command. Secret lines are never
 * kept for recall. esp_console_run() copies the line into its own buffer;
 * grok zeroes the key in that copy (argv) after storing it.
 */
#include "grok.h"

#include "grok_internal.h"

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    kRecallCap = 256,
};

static const char *s_prompt = "> ";
static size_t s_line_cap;
static uint32_t s_stack;
static UBaseType_t s_prio;
static BaseType_t s_core;
static bool s_ready;

static void out_flush(void)
{
    fflush(stdout);
}

static void erase_chars(size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        fputs("\b \b", stdout);
    }
}

static int read_char(void)
{
    for (;;)
    {
        int c = fgetc(stdin);
        if (c != EOF)
        {
            return c;
        }
        clearerr(stdin);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* Returns true for Up arrow. Swallows the rest of any other sequence. */
static bool read_escape(void)
{
    int c = read_char();

    if ((c != '[') && (c != 'O'))
    {
        return false;
    }
    c = read_char();
    if (c == 'A')
    {
        return true;
    }
    /* ESC [ n ~ and ESC [ n ; m X: read to the final byte (bounded). */
    for (int i = 0; (i < 8) && (c >= '0') && (c <= '?'); i++)
    {
        c = read_char();
    }
    return false;
}

static void run_line(char *line)
{
    int ret = 0;
    esp_err_t err = esp_console_run(line, &ret);

    if (err == ESP_ERR_NOT_FOUND)
    {
        printf("Unrecognized command\n");
    }
    else if ((err == ESP_OK) && (ret != 0))
    {
        printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
    }
    else if ((err != ESP_OK) && (err != ESP_ERR_INVALID_ARG))
    {
        printf("Internal error: %s\n", esp_err_to_name(err));
    }
}

static void console_task(void *arg)
{
    char *line = (char *)arg;
    char *recall = line + s_line_cap;

    setvbuf(stdin, NULL, _IONBF, 0);
    printf("\nType 'help' to get the list of commands.\n"
           "Up arrow recalls the last command. Keys and passwords echo as '*'.\n");

    for (;;)
    {
        size_t len = 0;
        bool cancel = false;

        memset(line, 0, s_line_cap);
        fputs(s_prompt, stdout);
        out_flush();

        for (;;)
        {
            int c = read_char();

            if ((c == '\n') || (c == '\r'))
            {
                fputc('\n', stdout);
                break;
            }
            if ((c == 0x7f) || (c == 0x08))
            {
                if (len > 0)
                {
                    line[--len] = '\0';
                    erase_chars(1);
                }
            }
            else if (c == 0x03)
            {
                fputs("^C\n", stdout);
                cancel = true;
                break;
            }
            else if (c == 0x15)
            {
                erase_chars(len);
                memset(line, 0, s_line_cap);
                len = 0;
            }
            else if (c == 0x1b)
            {
                if (read_escape() && (recall[0] != '\0'))
                {
                    erase_chars(len);
                    memset(line, 0, s_line_cap);
                    len = strlen(recall);
                    memcpy(line, recall, len);
                    fputs(line, stdout);
                }
            }
            else if ((c >= 0x20) && (c <= 0x7e) && ((len + 1) < s_line_cap))
            {
                bool masked = grok_text_secret_at(line, len);

                line[len++] = (char)c;
                line[len] = '\0';
                fputc(masked ? '*' : c, stdout);
            }
            out_flush();
        }
        out_flush();

        if (!cancel && (len > 0))
        {
            if (!grok_text_secret_at(line, len) && (len < kRecallCap))
            {
                memcpy(recall, line, len + 1);
            }
            run_line(line);
        }
        memset(line, 0, s_line_cap);
    }
}

esp_err_t Grok_ConsoleInit(const esp_console_dev_uart_config_t *dev, const esp_console_repl_config_t *repl)
{
    esp_console_config_t console_config = ESP_CONSOLE_CONFIG_DEFAULT();
    esp_err_t err;

    if ((dev == NULL) || (repl == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    fflush(stdout);
    fsync(fileno(stdout));
    uart_vfs_dev_port_set_rx_line_endings(dev->channel, ESP_LINE_ENDINGS_CR);
    uart_vfs_dev_port_set_tx_line_endings(dev->channel, ESP_LINE_ENDINGS_CRLF);

    const uart_config_t uart_config = {
        .baud_rate = dev->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .source_clk = UART_SCLK_REF_TICK,
    };
    uart_param_config(dev->channel, &uart_config);
    uart_set_pin(dev->channel, dev->tx_gpio_num, dev->rx_gpio_num, -1, -1);
    err = uart_driver_install(dev->channel, 256, 0, 0, NULL, 0);
    if (err != ESP_OK)
    {
        return err;
    }
    uart_vfs_dev_use_driver(dev->channel);

    if (repl->max_cmdline_length != 0)
    {
        console_config.max_cmdline_length = repl->max_cmdline_length;
    }
    console_config.hint_color = -1;
    err = esp_console_init(&console_config);
    if (err != ESP_OK)
    {
        return err;
    }

    s_prompt = (repl->prompt != NULL) ? repl->prompt : "> ";
    s_line_cap = console_config.max_cmdline_length;
    s_stack = repl->task_stack_size;
    s_prio = repl->task_priority;
    s_core = repl->task_core_id;
    s_ready = true;
    return ESP_OK;
}

esp_err_t Grok_ConsoleStart(void)
{
    char *buf;

    if (!s_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }
    /* One allocation for the life of the console: line + recall. linenoise
     * held a calloc'd line of the same size while waiting for input. */
    buf = calloc(1, s_line_cap + kRecallCap);
    if (buf == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(console_task, "console_repl", s_stack, buf, s_prio, NULL, s_core) != pdPASS)
    {
        free(buf);
        return ESP_FAIL;
    }
    return ESP_OK;
}
