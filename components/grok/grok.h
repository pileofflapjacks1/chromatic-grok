/* Public entry points for the Chromatic Grok link.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>

#include "esp_console.h"

struct _lv_obj_t;

/* Call after OSD_Default_Init and before the console REPL starts. */
void Grok_Init(struct _lv_obj_t *screen);

/* Start Wi-Fi, BLE, and the HTTP worker. Call after the REPL is running. */
void Grok_Start(void);

/* True while light sleep must stay off so the radios can remain associated. */
bool Grok_WantsRadioAwake(void);

/* Serial console with masked echo for secrets. Same UART setup as
 * esp_console_new_repl_uart(); call Init before registering "help" and
 * Start where esp_console_start_repl() was called. */
esp_err_t Grok_ConsoleInit(const esp_console_dev_uart_config_t *dev, const esp_console_repl_config_t *repl);
esp_err_t Grok_ConsoleStart(void);
