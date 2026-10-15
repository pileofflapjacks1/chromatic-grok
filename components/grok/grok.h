/* Public entry points for the Chromatic Grok link.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>

struct _lv_obj_t;

/* Call after OSD_Default_Init and before the console REPL starts. */
void Grok_Init(struct _lv_obj_t *screen);

/* Start Wi-Fi, BLE, and the HTTP worker. Call after the REPL is running. */
void Grok_Start(void);

/* True while light sleep must stay off so the radios can remain associated. */
bool Grok_WantsRadioAwake(void);
