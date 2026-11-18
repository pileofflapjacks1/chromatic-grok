/* Bluetooth headset (HFP Audio Gateway) feasibility spike. Only built with
 * CONFIG_GROK_BT_HEADSET_SPIKE; see docs/bt-headset-spike.md.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "sdkconfig.h"

#include <stdbool.h>

/* True while the Bluetooth controller is on (keeps light sleep off). */
bool grok_bt_active(void);

#if CONFIG_GROK_BT_HEADSET_SPIKE
/* "bt ..." console command. Long subcommands run on the grok radio task. */
void grok_bt_exec(int argc, char **argv);
/* status / mem: safe to run on the console task. */
bool grok_bt_is_quick(int argc, char **argv);
void grok_bt_usage(void);
/* Heap line at radio task start, before Wi-Fi. */
void grok_bt_boot_mark(void);
#endif
