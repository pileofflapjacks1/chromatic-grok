/* Chromatic Grok link. A modified work based on the ModRetro Chromatic MCU
 * firmware (GPL-3.0-or-later). See LICENSE and NOTICE.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_internal.h"
#include "grok_keys.h"

#include "button.h"
#include "color.h"
#include "osd.h"
#include "osd_default.h"
#include "osd_shared.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
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
    kGlyphW = 8,
    /* Keyboard view, in body lines: the typed text, a gap, the key rows,
     * a gap, the wide keys. */
    kTypedLines = 4,
    kKeyLine0 = kTypedLines + 1,
    kWideLine = kKeyLine0 + GROK_KEYS_CHAR_ROWS + 1,
};

/* The app's screens. Reply is 0 so a zeroed state shows the reply pages. */
typedef enum {
    VIEW_REPLY = 0, /* answer pages */
    VIEW_PICK,      /* preset question list */
    VIEW_HOME,      /* app home: what to do */
    VIEW_KEYS,      /* on-screen keyboard */
} View_t;

enum {
    kHomeType = 0,
    kHomeQuick,
    kHomeLast,
    kHomeMenu,
    kHomeCount,
};

static const char *const kHomeItem[kHomeCount] = {
    "Type a question",
    "Quick questions",
    "Last answer",
    "Chromatic menu",
};

typedef struct UiState {
    char body[GROK_REPLY_MAX + 1];
    int page;
    int pick;      /* picker selection, 0..GROK_PICK_COUNT-1 */
    /* Text typed on the keyboard: GROK_PROMPT_MAX + 1 bytes of heap from the
     * first use of the keyboard until the question is sent or dropped. */
    char *typed;
    GrokKeys_t keys;
    uint8_t view;  /* View_t */
    uint8_t home;  /* home selection, 0..kHomeCount-1 */
    bool open;
    bool dirty;
} UiState_t;

static SemaphoreHandle_t s_lock;
static UiState_t s_ui;
static lv_obj_t *s_panel;
static lv_obj_t *s_header;
static lv_obj_t *s_body;
static lv_obj_t *s_footer;
static lv_obj_t *s_keycur;
/* Menu button opens the app first (grok app on|off). */
static volatile bool s_auto;
static bool s_was_visible;
static lv_obj_t *s_pane[3];
static char s_header_shown[64];
static char s_body_shown[kPageLines * (GROK_TEXT_COLS + 2)];
static char s_pane_shown[96];
static const char *s_footer_shown;

static const char kFooterReply[] = "A retry B back U/D";
static const char kFooterPick[] = "A ask B back U/D";
static const char kFooterHome[] = "A select B menu";
static const char kFooterKeys[] = "A key B del STA send";

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
    /* A label line is the font's line height plus the line space. */
    lv_obj_set_style_text_line_space(s_body, kLinePitch - (int)lv_font_unscii_8.line_height, 0);
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

    /* Keyboard cursor: the selected key again, black on white, on top of
     * the body text. */
    s_keycur = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_keycur, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(s_keycur, lv_color_hex(kColor_Black), 0);
    lv_obj_set_style_bg_color(s_keycur, lv_color_hex(kColor_White), 0);
    lv_obj_set_style_bg_opa(s_keycur, LV_OPA_COVER, 0);
    lv_label_set_text(s_keycur, "");
    lv_obj_add_flag(s_keycur, LV_OBJ_FLAG_HIDDEN);
}

static void set_body(const char *text)
{
    if (strcmp(text, s_body_shown) != 0)
    {
        lv_label_set_text(s_body, text);
        snprintf(s_body_shown, sizeof(s_body_shown), "%s", text);
    }
}

static void set_header_text(const char *text)
{
    if (strcmp(text, s_header_shown) != 0)
    {
        lv_label_set_text(s_header, text);
        snprintf(s_header_shown, sizeof(s_header_shown), "%s", text);
    }
}

static void show_panel(bool keys)
{
    if (keys != !lv_obj_has_flag(s_keycur, LV_OBJ_FLAG_HIDDEN))
    {
        if (keys)
        {
            lv_obj_clear_flag(s_keycur, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(s_keycur, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_panel);
}

/* Home: title, the four choices, then the radio status. */
static void render_home(int sel, char *page_text, size_t page_cap)
{
    char wifi[24];
    char ble[16];
    size_t used;

    grok_wifi_format(wifi, sizeof(wifi));
    grok_ble_format(ble, sizeof(ble));
    used = (size_t)snprintf(page_text, page_cap, "\n   CHROMATIC GROK\n\n");
    for (int i = 0; (i < kHomeCount) && (used < page_cap); i++)
    {
        used += (size_t)snprintf(page_text + used, page_cap - used, "%c %s\n", (i == sel) ? '>' : ' ', kHomeItem[i]);
    }
    if (used < page_cap)
    {
        snprintf(page_text + used, page_cap - used, "\n%.*s\n%.*s", GROK_TEXT_COLS, wifi, GROK_TEXT_COLS, ble);
    }
}

/* Keyboard: the end of the typed text, then the keys. */
static void render_keys(const GrokKeys_t *keys, const char *typed, char *page_text, size_t page_cap)
{
    char row[GROK_KEYS_ROW_CHARS + 1];
    char one[2];
    const char *label;
    size_t used;
    int col = 0;
    int line;

    grok_keys_tail(typed, page_text, page_cap, GROK_TEXT_COLS, kTypedLines);
    used = strlen(page_text);
    for (int r = 0; (r < GROK_KEYS_ROWS) && (used < page_cap); r++)
    {
        grok_keys_row(keys, r, row);
        used += (size_t)snprintf(page_text + used, page_cap - used, "%s%s",
                                 ((r == 0) || (r == GROK_KEYS_CHAR_ROWS)) ? "\n\n" : "\n", row);
    }
    set_body(page_text);

    label = grok_keys_label(keys, one, &col);
    line = (keys->row < GROK_KEYS_CHAR_ROWS) ? (kKeyLine0 + keys->row) : kWideLine;
    if (strcmp(lv_label_get_text(s_keycur), label) != 0)
    {
        lv_label_set_text(s_keycur, label);
    }
    lv_obj_set_pos(s_keycur, col * kGlyphW, kBodyY + (line * kLinePitch));
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
    View_t which;
    GrokKeys_t keys;
    int home;
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
    which = (View_t)s_ui.view;
    picker = (which == VIEW_PICK);
    keys = s_ui.keys;
    home = s_ui.home;
    pick = s_ui.pick;
    dirty = s_ui.dirty;
    if (which == VIEW_REPLY)
    {
        snprintf(view, sizeof(view), "%s", s_ui.body);
    }
    else if (which == VIEW_KEYS)
    {
        snprintf(view, sizeof(view), "%s", (s_ui.typed != NULL) ? s_ui.typed : "");
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

    if (which == VIEW_HOME)
    {
        set_header_text("HOME");
        set_footer(kFooterHome);
        /* Rebuilt every frame: the radio lines change by themselves. */
        render_home(home, page_text, sizeof(page_text));
        set_body(page_text);
        show_panel(false);
        return;
    }
    if (which == VIEW_KEYS)
    {
        char header[GROK_TEXT_COLS + 1];

        snprintf(header, sizeof(header), "TYPE %u/%u%s", (unsigned)strlen(view), (unsigned)GROK_PROMPT_MAX,
                 keys.shift ? " CAP" : "");
        set_header_text(header);
        set_footer(kFooterKeys);
        if (dirty || lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN) || lv_obj_has_flag(s_keycur, LV_OBJ_FLAG_HIDDEN))
        {
            render_keys(&keys, view, page_text, sizeof(page_text));
        }
        show_panel(true);
        return;
    }
    if (picker)
    {
        set_header("ASK ", pick + 1, GROK_PICK_COUNT);
        set_footer(kFooterPick);
        if (dirty || lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN))
        {
            render_picker(pick, view, sizeof(view), &lines[0][0], page_text, sizeof(page_text));
            set_body(page_text);
        }
        show_panel(false);
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
    set_body(page_text);
    show_panel(false);
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
        grok_ui_open_home();
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

/* Keyboard: the typed text lives on the heap from here until it is sent
 * or dropped. */
static void open_keys(void)
{
    bool ok;

    lock();
    if (s_ui.typed == NULL)
    {
        s_ui.typed = calloc(1, GROK_PROMPT_MAX + 1);
        s_ui.keys.row = 1;
        s_ui.keys.col = 0;
        s_ui.keys.shift = false;
    }
    ok = (s_ui.typed != NULL);
    if (ok)
    {
        s_ui.view = VIEW_KEYS;
        s_ui.open = true;
        s_ui.dirty = true;
    }
    unlock();
    if (!ok)
    {
        grok_ui_set_body("Out of memory for the keyboard.");
    }
}

static void drop_typed(void)
{
    lock();
    free(s_ui.typed);
    s_ui.typed = NULL;
    unlock();
}

/* Send what was typed. Nothing happens while the text is empty. */
static void keys_send(void)
{
    char text[GROK_PROMPT_MAX + 1];
    size_t len;

    lock();
    snprintf(text, sizeof(text), "%s", (s_ui.typed != NULL) ? s_ui.typed : "");
    unlock();
    len = strlen(text);
    while ((len > 0) && (text[len - 1] == ' '))
    {
        text[--len] = '\0';
    }
    if (len == 0)
    {
        return;
    }
    drop_typed();
    grok_submit_prompt(text); /* shows "..." on the reply view */
}

static void keys_button(Button_t button)
{
    bool empty_back = false;
    bool send = false;

    lock();
    if (s_ui.typed == NULL)
    {
        unlock();
        grok_ui_open_home();
        return;
    }
    if (button == kButton_Up)
    {
        grok_keys_move(&s_ui.keys, 0, -1);
    }
    else if (button == kButton_Down)
    {
        grok_keys_move(&s_ui.keys, 0, 1);
    }
    else if (button == kButton_Left)
    {
        grok_keys_move(&s_ui.keys, -1, 0);
    }
    else if (button == kButton_Right)
    {
        grok_keys_move(&s_ui.keys, 1, 0);
    }
    else if (button == kButton_B)
    {
        empty_back = !grok_keys_backspace(s_ui.typed);
    }
    else if (button == kButton_Start)
    {
        send = true;
    }
    else if (button == kButton_A)
    {
        char ch = '\0';

        switch (grok_keys_selected(&s_ui.keys, &ch))
        {
        case GROK_KEY_CHAR:
            grok_keys_type(s_ui.typed, GROK_PROMPT_MAX + 1, ch);
            break;
        case GROK_KEY_SPACE:
            grok_keys_type(s_ui.typed, GROK_PROMPT_MAX + 1, ' ');
            break;
        case GROK_KEY_DEL:
            grok_keys_backspace(s_ui.typed);
            break;
        case GROK_KEY_SHIFT:
            s_ui.keys.shift = !s_ui.keys.shift;
            break;
        case GROK_KEY_SEND:
            send = true;
            break;
        default:
            break;
        }
    }
    s_ui.dirty = true;
    unlock();

    if (send)
    {
        keys_send();
    }
    else if (empty_back)
    {
        /* B with nothing typed leaves the keyboard. */
        drop_typed();
        grok_ui_open_home();
    }
}

static void home_button(Button_t button)
{
    int sel;

    if ((button == kButton_Up) || (button == kButton_Down))
    {
        lock();
        s_ui.home = (uint8_t)(((int)s_ui.home + ((button == kButton_Up) ? (kHomeCount - 1) : 1)) % kHomeCount);
        s_ui.dirty = true;
        unlock();
        return;
    }
    if ((button == kButton_B) || (button == kButton_Start))
    {
        grok_ui_close(); /* the Chromatic menu is underneath */
        return;
    }
    if (button != kButton_A)
    {
        return;
    }
    lock();
    sel = s_ui.home;
    unlock();
    switch (sel)
    {
    case kHomeType:
        open_keys();
        break;
    case kHomeQuick:
        grok_ui_open_picker();
        break;
    case kHomeLast:
        grok_ui_open();
        break;
    default:
        grok_ui_close();
        break;
    }
}

static bool on_inputs(void)
{
    bool rising;
    View_t which;

    if (!OSD_IsVisible())
    {
        s_was_visible = false;
        return false;
    }
    rising = !s_was_visible;
    s_was_visible = true;

    if (!grok_ui_is_open())
    {
        if (rising && s_auto)
        {
            /* The menu button opens the app; B from home shows the menu. */
            grok_ui_open_home();
            return true;
        }
        if (Button_GetState(kButton_Start) == kButtonState_Pressed)
        {
            grok_ui_open_home();
        }
        return false;
    }

    for (int i = 0; i < kNumButtons; i++)
    {
        Button_t button = (Button_t)i;
        ButtonState_t state = Button_GetState(button);

        if (state != kButtonState_Pressed)
        {
            continue;
        }
        /* Read per button: a press can change the view. */
        lock();
        which = (View_t)s_ui.view;
        unlock();
        if (which == VIEW_HOME)
        {
            home_button(button);
            return true;
        }
        if (which == VIEW_KEYS)
        {
            keys_button(button);
            return true;
        }
        if (which == VIEW_PICK)
        {
            /* Up/Down pick, A asks, B goes home, Start closes the app. */
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
            else if (button == kButton_B)
            {
                grok_ui_open_home();
                return true;
            }
            else if (button == kButton_Start)
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
            grok_ui_open_home();
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
    s_ui.view = VIEW_REPLY;
    s_ui.open = true;
    s_ui.dirty = true;
    unlock();
}

void grok_ui_open_picker(void)
{
    lock();
    s_ui.view = VIEW_PICK;
    s_ui.open = true;
    s_ui.dirty = true;
    unlock();
}

void grok_ui_open_home(void)
{
    lock();
    s_ui.view = VIEW_HOME;
    s_ui.open = true;
    s_ui.dirty = true;
    unlock();
}

void grok_ui_set_auto(bool on)
{
    s_auto = on;
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
    s_ui.view = VIEW_REPLY;
    s_ui.open = true;
    s_ui.dirty = true;
    unlock();
}

void grok_ui_page(int delta)
{
    lock();
    s_ui.page += delta;
    s_ui.view = VIEW_REPLY;
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
    s_ui.view = VIEW_REPLY;
    s_ui.open = true;
    s_ui.dirty = true;
    unlock();
}
