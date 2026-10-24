/* Chromatic Grok link. A modified work based on the ModRetro Chromatic MCU
 * firmware (GPL-3.0-or-later). See LICENSE and NOTICE.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_internal.h"

#include "button.h"
#include "color.h"
#include "osd.h"
#include "osd_default.h"
#include "osd_shared.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

#if !LV_FONT_UNSCII_8
#error "Grok overlay needs CONFIG_LV_FONT_UNSCII_8=y (see sdkconfig.defaults). Enable it in menuconfig or delete sdkconfig and rebuild."
#endif

/* The overlay uses LVGL's unscii_8: 1 bpp, monospace 8x8, so GROK_TEXT_COLS (20)
 * characters fill the 160 px width exactly and the column-based wrap never clips.
 * The OSD font (fingfai) draws lowercase in 3x3 px, where a, e and o look the same. */
enum {
    kPageLines = 13,
    kLinePitch = 9,     // 8 px glyph cell + 1 px line space
    kHeaderY = 1,
    kBodyY = 11,
    kFooterY = 135,
    kPaneX = 77,
    kPaneY = 46,
    kPaneStep = 12,
};

typedef struct UiState {
    char body[GROK_REPLY_MAX + 1];
    int page;
    int pick;      /* picker selection, 0..GROK_PICK_COUNT-1 */
    bool picker;   /* true: question picker, false: reply pages */
    bool open;
    bool dirty;
} UiState_t;

static SemaphoreHandle_t s_lock;
static UiState_t s_ui;
static lv_obj_t *s_panel;
static lv_obj_t *s_header;
static lv_obj_t *s_body;
static lv_obj_t *s_footer;
static lv_obj_t *s_pane[3];
static char s_header_shown[64];
static char s_body_shown[kPageLines * (GROK_TEXT_COLS + 2)];
static char s_pane_shown[96];
static const char *s_footer_shown;

static const char kFooterReply[] = "A retry B back U/D";
static const char kFooterPick[] = "A ask B back U/D";

static OSD_Widget_t s_widget = {
    .Name = "GrokOverlay",
};

static OSD_Result_t item_draw(void *arg);
static OSD_Result_t item_button(const Button_t Button, const ButtonState_t State, void *arg);
static OSD_Result_t item_transition(void *arg);
static OSD_Result_t widget_draw(void *arg);
static bool on_inputs(void);

static TabItem_t s_item = {
    .Widget = {
        .Name = "GROK",
        .fnDraw = item_draw,
        .fnOnButton = item_button,
        .fnOnTransition = item_transition,
    },
};

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

static void ensure_overlay(lv_obj_t *screen)
{
    if ((s_panel != NULL) || (screen == NULL))
    {
        return;
    }

    s_panel = lv_obj_create(screen);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_pos(s_panel, 0, 0);
    lv_obj_set_size(s_panel, kOSD_Width_px, kOSD_Height_px);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(kColor_Black), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

    s_header = lv_label_create(s_panel);
    lv_obj_add_style(s_header, OSD_GetStyleTextGrey(), 0);
    lv_obj_set_style_text_font(s_header, &lv_font_unscii_8, 0);
    lv_obj_set_pos(s_header, 0, kHeaderY);
    lv_obj_set_width(s_header, kOSD_Width_px);
    lv_label_set_long_mode(s_header, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_header, "GROK");

    s_body = lv_label_create(s_panel);
    lv_obj_add_style(s_body, OSD_GetStyleTextWhite(), 0);
    lv_obj_set_style_text_font(s_body, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_line_space(s_body, kLinePitch - 8, 0);
    lv_obj_set_pos(s_body, 0, kBodyY);
    lv_obj_set_size(s_body, kOSD_Width_px, kPageLines * kLinePitch);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_body, "");

    s_footer = lv_label_create(s_panel);
    lv_obj_add_style(s_footer, OSD_GetStyleTextGrey(), 0);
    lv_obj_set_style_text_font(s_footer, &lv_font_unscii_8, 0);
    lv_obj_set_pos(s_footer, 0, kFooterY);
    lv_obj_set_width(s_footer, kOSD_Width_px);
    lv_label_set_long_mode(s_footer, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_footer, kFooterReply);
    s_footer_shown = kFooterReply;
}

static void set_footer(const char *text)
{
    if (s_footer_shown != text)
    {
        lv_label_set_text(s_footer, text);
        s_footer_shown = text;
    }
}

static void set_header(const char *prefix, int cur, int total)
{
    char wifi[24];
    char header[64];

    grok_wifi_format(wifi, sizeof(wifi));
    snprintf(header, sizeof(header), "%s%d/%d %s", prefix, cur, total, wifi);
    if (strlen(header) > GROK_TEXT_COLS)
    {
        // A long IP does not fit after the prefix. Keep the count and the IP.
        snprintf(header, sizeof(header), "%d/%d %s", cur, total, wifi);
        header[GROK_TEXT_COLS] = '\0';
    }
    if (strcmp(header, s_header_shown) != 0)
    {
        lv_label_set_text(s_header, header);
        snprintf(s_header_shown, sizeof(s_header_shown), "%s", header);
    }
}

/* Picker page: 8 rows (">1*question"), a blank line, then the selected
 * question wrapped on up to 4 lines. Built only when the state changed,
 * because custom questions are read from NVS. Reuses render()'s scratch. */
static void render_picker(int pick, char *view, size_t view_cap, char *lines, char *page_text, size_t page_cap)
{
    enum { kPreviewLines = kPageLines - GROK_PICK_COUNT - 1 };
    size_t used = 0;
    size_t n;

    page_text[0] = '\0';
    for (int i = 0; i < GROK_PICK_COUNT; i++)
    {
        bool custom = false;
        char row[GROK_TEXT_COLS + 1];
        size_t len;

        grok_question_get(i, view, view_cap, &custom);
        /* Built-in rows show the short label; the preview below and the
         * question that is asked keep the full text. */
        len = grok_pick_format_row(row, sizeof(row), i, custom ? view : grok_pick_label(i),
                                   i == pick, custom, GROK_TEXT_COLS);
        if ((used + len + 2) >= page_cap)
        {
            break;
        }
        memcpy(page_text + used, row, len);
        used += len;
        page_text[used++] = '\n';
        page_text[used] = '\0';
    }
    if ((used + 1) < page_cap)
    {
        page_text[used++] = '\n';
        page_text[used] = '\0';
    }

    grok_question_get(pick, view, view_cap, NULL);
    n = grok_text_wrap(view, lines, GROK_TEXT_COLS + 1, kPreviewLines, GROK_TEXT_COLS);
    for (size_t i = 0; i < n; i++)
    {
        const char *line = lines + (i * (GROK_TEXT_COLS + 1));
        size_t len = strlen(line);

        if ((used + len + 2) >= page_cap)
        {
            break;
        }
        if (i > 0)
        {
            page_text[used++] = '\n';
        }
        memcpy(page_text + used, line, len);
        used += len;
        page_text[used] = '\0';
    }
}

static void render(lv_obj_t *screen)
{
    static char view[GROK_REPLY_MAX + 1];
    static char lines[GROK_TEXT_LINES][GROK_TEXT_COLS + 1];
    static char page_text[kPageLines * (GROK_TEXT_COLS + 2)];
    bool open;
    bool dirty;
    bool picker;
    int pick;
    int page;
    size_t nlines;
    int pages;
    int start;
    size_t used;
    ensure_overlay(screen);
    if (s_panel == NULL)
    {
        return;
    }

    lock();
    picker = s_ui.picker;
    pick = s_ui.pick;
    dirty = s_ui.dirty;
    if (!picker)
    {
        snprintf(view, sizeof(view), "%s", s_ui.body);
    }
    page = s_ui.page;
    open = s_ui.open;
    s_ui.dirty = false;
    unlock();

    if (!open)
    {
        if (!lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN))
        {
            lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (picker)
    {
        set_header("ASK ", pick + 1, GROK_PICK_COUNT);
        set_footer(kFooterPick);
        if (dirty || lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN))
        {
            render_picker(pick, view, sizeof(view), &lines[0][0], page_text, sizeof(page_text));
            if (strcmp(page_text, s_body_shown) != 0)
            {
                lv_label_set_text(s_body, page_text);
                snprintf(s_body_shown, sizeof(s_body_shown), "%s", page_text);
            }
        }
        lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_panel);
        return;
    }
    set_footer(kFooterReply);

    nlines = grok_text_wrap(view, &lines[0][0], GROK_TEXT_COLS + 1, GROK_TEXT_LINES, GROK_TEXT_COLS);
    pages = (int)((nlines + (kPageLines - 1)) / kPageLines);
    if (pages < 1)
    {
        pages = 1;
    }
    if (page >= 0)
    {
        page = page % pages;
    }
    else
    {
        page = page % pages;
        if (page < 0)
        {
            page += pages;
        }
    }

    lock();
    if (!s_ui.dirty)
    {
        s_ui.page = page;
    }
    unlock();

    set_header("GROK ", page + 1, pages);

    used = 0;
    page_text[0] = '\0';
    start = page * kPageLines;
    for (int i = 0; i < kPageLines; i++)
    {
        size_t len;
        int idx = start + i;

        if ((nlines == 0) || ((size_t)idx >= nlines))
        {
            break;
        }
        len = strlen(lines[idx]);
        if ((used + len + 2) >= sizeof(page_text))
        {
            break;
        }
        if (i > 0)
        {
            page_text[used++] = '\n';
        }
        memcpy(page_text + used, lines[idx], len);
        used += len;
        page_text[used] = '\0';
    }
    if (strcmp(page_text, s_body_shown) != 0)
    {
        lv_label_set_text(s_body, page_text);
        snprintf(s_body_shown, sizeof(s_body_shown), "%s", page_text);
    }

    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_panel);
}

static OSD_Result_t widget_draw(void *arg)
{
    if (arg == NULL)
    {
        return kOSD_Result_Err_NullDataPtr;
    }
    render((lv_obj_t *)arg);
    return kOSD_Result_Ok;
}

static void pane_set(lv_obj_t *screen)
{
    char wifi[24];
    char ble[16];
    char next[96];
    const char *lines[3];

    if (screen == NULL)
    {
        return;
    }

    grok_wifi_format(wifi, sizeof(wifi));
    grok_ble_format(ble, sizeof(ble));
    lines[0] = wifi;
    lines[1] = ble;
    lines[2] = "A or Start";
    snprintf(next, sizeof(next), "%s\n%s\n%s", lines[0], lines[1], lines[2]);
    if (strcmp(next, s_pane_shown) == 0 && (s_pane[0] != NULL))
    {
        return;
    }
    snprintf(s_pane_shown, sizeof(s_pane_shown), "%s", next);

    for (int i = 0; i < 3; i++)
    {
        if (s_pane[i] == NULL)
        {
            s_pane[i] = lv_label_create(screen);
            lv_obj_add_style(s_pane[i], (i == 2) ? OSD_GetStyleTextGrey() : OSD_GetStyleTextWhite(), 0);
            lv_obj_align(s_pane[i], LV_ALIGN_TOP_LEFT, kPaneX, kPaneY + (i * kPaneStep));
            lv_label_set_long_mode(s_pane[i], LV_LABEL_LONG_CLIP);
            lv_obj_set_width(s_pane[i], 72);
        }
        lv_label_set_text(s_pane[i], lines[i]);
    }
}

static OSD_Result_t item_draw(void *arg)
{
    if (arg == NULL)
    {
        return kOSD_Result_Err_NullDataPtr;
    }
    pane_set((lv_obj_t *)arg);
    return kOSD_Result_Ok;
}

static OSD_Result_t item_button(const Button_t Button, const ButtonState_t State, void *arg)
{
    (void)arg;

    if ((Button == kButton_A) && (State == kButtonState_Pressed))
    {
        grok_ui_open_picker();
    }
    return kOSD_Result_Ok;
}

static OSD_Result_t item_transition(void *arg)
{
    (void)arg;

    for (int i = 0; i < 3; i++)
    {
        if ((s_pane[i] != NULL) && lv_obj_is_valid(s_pane[i]))
        {
            lv_obj_del(s_pane[i]);
        }
        s_pane[i] = NULL;
    }
    s_pane_shown[0] = '\0';
    return kOSD_Result_Ok;
}

static bool on_inputs(void)
{
    if (!OSD_IsVisible())
    {
        return false;
    }

    if (!grok_ui_is_open())
    {
        if (Button_GetState(kButton_Start) == kButtonState_Pressed)
        {
            grok_ui_open_picker();
        }
        return false;
    }

    lock();
    bool picker = s_ui.picker;
    unlock();

    for (int i = 0; i < kNumButtons; i++)
    {
        Button_t button = (Button_t)i;
        ButtonState_t state = Button_GetState(button);

        if (state != kButtonState_Pressed)
        {
            continue;
        }
        if (picker)
        {
            /* Up/Down pick, A asks, B (or Start) goes back to the menu. */
            if ((button == kButton_Up) || (button == kButton_Down))
            {
                lock();
                s_ui.pick = grok_pick_step(s_ui.pick, (button == kButton_Up) ? -1 : 1);
                s_ui.dirty = true;
                unlock();
            }
            else if (button == kButton_A)
            {
                char text[GROK_PROMPT_MAX + 1];
                int pick;

                lock();
                pick = s_ui.pick;
                unlock();
                grok_question_get(pick, text, sizeof(text), NULL);
                grok_submit_prompt(text); /* shows "..." on the reply view */
                return true;
            }
            else if ((button == kButton_B) || (button == kButton_Start))
            {
                grok_ui_close();
                return true;
            }
            continue;
        }
        if (button == kButton_Up)
        {
            grok_ui_page(-1);
        }
        else if (button == kButton_Down)
        {
            grok_ui_page(1);
        }
        else if (button == kButton_A)
        {
            grok_retry();
        }
        else if (button == kButton_B)
        {
            grok_ui_open_picker(); /* back to the questions */
            return true;
        }
        else if (button == kButton_Start)
        {
            grok_ui_close();
        }
    }
    return true;
}

void grok_ui_init(struct _lv_obj_t *screen)
{
    (void)screen;
    if (s_lock == NULL)
    {
        s_lock = xSemaphoreCreateMutex();
    }

    s_widget.fnDraw = widget_draw;
    if (OSD_AddWidget(&s_widget) != kOSD_Result_Ok)
    {
        printf("grok: overlay widget was not added\n");
    }
    OSD_SetInputHook(on_inputs);
    if (OSD_Default_AddSystemItem(&s_item) != kOSD_Result_Ok)
    {
        printf("grok: system row was not added\n");
    }
}

void grok_ui_open(void)
{
    lock();
    if (s_ui.body[0] == '\0')
    {
        snprintf(s_ui.body, sizeof(s_ui.body), "No reply yet.");
        s_ui.page = 0;
    }
    s_ui.picker = false;
    s_ui.open = true;
    s_ui.dirty = true;
    unlock();
}

void grok_ui_open_picker(void)
{
    lock();
    s_ui.picker = true;
    s_ui.open = true;
    s_ui.dirty = true;
    unlock();
}

void grok_ui_close(void)
{
    lock();
    s_ui.open = false;
    s_ui.dirty = true;
    unlock();
}

bool grok_ui_is_open(void)
{
    bool open;
    lock();
    open = s_ui.open;
    unlock();
    return open;
}

void grok_ui_set_body(const char *text)
{
    lock();
    snprintf(s_ui.body, sizeof(s_ui.body), "%s", (text != NULL) ? text : "");
    grok_text_fold_ascii(s_ui.body);
    s_ui.page = 0;
    s_ui.picker = false;
    s_ui.open = true;
    s_ui.dirty = true;
    unlock();
}

void grok_ui_page(int delta)
{
    lock();
    s_ui.page += delta;
    s_ui.picker = false;
    s_ui.open = true;
    s_ui.dirty = true;
    unlock();
}

void grok_ui_set_page(int page)
{
    lock();
    if (page < 0)
    {
        page = 0;
    }
    s_ui.page = page;
    s_ui.picker = false;
    s_ui.open = true;
    s_ui.dirty = true;
    unlock();
}
