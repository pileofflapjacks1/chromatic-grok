#pragma once

#include "lvgl.h"
#include "tab_shared.h"

void OSD_Default_Init(lv_obj_t *const pScreen);

// Append a row to the stock System tab. Call after OSD_Default_Init.
OSD_Result_t OSD_Default_AddSystemItem(TabItem_t *const pItem);
