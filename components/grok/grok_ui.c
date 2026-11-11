/* Chromatic Grok link. A modified work based on the ModRetro Chromatic MCU
 * firmware (GPL-3.0-or-later). See LICENSE and NOTICE.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_internal.h"

#include "grok_bots.h"
#include "grok_loader.h"
#include "grok_voice_util.h"

#include "button.h"
#include "color.h"
#include "osd.h"
#include "osd_default.h"
#include "osd_shared.h"

#include "esp_timer.h"
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
    kPageLines = GROK_TEXT_PAGE_LINES,
    kLinePitch = 9,     // 8 px glyph cell + 1 px line space
    kHeaderY = 1,
    kBodyY = 11,
    kFooterY = 135,
    kPaneX = 77,
    kPaneY = 46,
    kPaneStep = 12,
    kLoaderGiveUpMs = 12000,
};

/* Voice screen colors (original palette, RGB888). Not 0xFF00FF: that is the
 * LVGL chroma key in sdkconfig.defaults. Text on each fill is at least 7:1
 * (see tools/voice_preview.py, which prints the ratios). */
enum {
    kVoiceNavy = 0x140F2E,      /* screen background */
    kVoicePanelFill = 0x1F1747, /* caption panel */
    kVoiceFooterFill = 0x2A1F5C,
    kVoiceAmber = 0xF5A33A,     /* header bar */
    kVoiceBarText = 0x1A1238,   /* dark navy on amber */
    kVoiceCream = 0xFFF3D6,     /* reply text, ball */
    kVoiceCyan = 0x7FD6E8,      /* panel border, page number, listening dots */
    kVoiceLavender = 0xBDB4F0,  /* footer hint */
    kVoiceDim = 0x4B4385,       /* arrow with nowhere to go */
    kVoiceBallDim = 0x6A5FB0,   /* the dim loader dot */
};

typedef enum {
    UI_REPLY = 0,
    UI_PICKER,
    UI_BOTS,
    UI_NOTE,
    UI_LOADER,
    UI_VOICE,
} UiView_t;

typedef enum {
    VOICE_PHASE_LISTENING = 0,
    VOICE_PHASE_THINKING,
    VOICE_PHASE_CAPTION,
} VoicePhase_t;

typedef struct UiState {
    char body[GROK_REPLY_MAX + 1];
    int page;
    int pick;      /* question picker selection */
    int bot;       /* bot menu selection */
    UiView_t view;
    bool open;
    bool dirty;
    uint32_t loader_start_ms;
    int loader_frame;
    VoicePhase_t voice_phase;
    char voice_text[GROK_REPLY_MAX + 1];
    size_t voice_page;
} UiState_t;

static SemaphoreHandle_t s_lock;
static UiState_t s_ui;
static lv_obj_t *s_panel;
static lv_obj_t *s_header;
static lv_obj_t *s_body;
static lv_obj_t *s_footer;
static lv_obj_t *s_loader; /* full-screen custom-drawn art */
static lv_obj_t *s_deco;   /* voice bars, panel border and arrows, drawn in one callback */
static lv_obj_t *s_status; /* voice header: Listening / Thinking / Reply */
static lv_obj_t *s_page;   /* voice footer: 2/4 */
static lv_obj_t *s_pane[3];
static char s_header_shown[64];
static char s_body_shown[kPageLines * (GROK_TEXT_COLS + 2)];
static char s_pane_shown[96];
static const char *s_footer_shown;
static char s_page_shown[16];
static const char *s_status_shown;

typedef enum {
    SKIN_NONE = 0,
    SKIN_VOICE_BAR,     /* ball loader with the header bar */
    SKIN_VOICE_CAPTION, /* header bar, bordered panel, footer bar */
} Skin_t;

/* Written by render() and read by deco_draw_cb(), both on the LVGL task. */
static Skin_t s_skin;
static int s_arrow_up_x;
static bool s_arrow_up_on;
static bool s_arrow_down_on;

static const char kFooterReply[] = "A retry B back U/D";
static const char kFooterPick[] = "A ask B back U/D";
static const char kFooterBots[] = "A open B back U/D";
static const char kFooterNote[] = "A ask B bots";
static const char kFooterVoiceCap[] = "A next B exit";
static const char kFooterVoiceCapShort[] = "A next";

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

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

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

/* Paint one RLE loader frame with horizontal rects — no RGB565 scratch buffer. */
static void loader_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj;
    lv_draw_ctx_t *draw_ctx;
    lv_area_t coords;
    const GrokLoaderFrame *fr;
    int frame;
    size_t i;
    size_t pix;
    lv_draw_rect_dsc_t rect_dsc;
    bool voice;
    uint32_t dot_accent;

    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN)
    {
        return;
    }
    obj = lv_event_get_target(e);
    draw_ctx = lv_event_get_draw_ctx(e);
    if ((obj == NULL) || (draw_ctx == NULL))
    {
        return;
    }
    lock();
    frame = s_ui.loader_frame;
    voice = (s_ui.view == UI_VOICE);
    dot_accent = (s_ui.voice_phase == VOICE_PHASE_THINKING) ? kVoiceAmber : kVoiceCyan;
    unlock();
    if ((frame < 0) || (frame >= GROK_LOADER_FRAME_COUNT))
    {
        frame = 0;
    }
    fr = &grok_loader_frames[frame];
    lv_obj_get_coords(obj, &coords);
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.border_width = 0;

    pix = 0;
    for (i = 0; (i + 1) < fr->rle_len; i += 2)
    {
        uint8_t count = fr->rle[i];
        uint8_t index = fr->rle[i + 1];
        uint8_t left = count;

        if (index >= GROK_LOADER_PALETTE_COUNT)
        {
            break;
        }
        rect_dsc.bg_color = lv_color_hex(grok_loader_palette_rgb[index]);
        while (left > 0)
        {
            size_t x = pix % GROK_LOADER_WIDTH;
            size_t y = pix / GROK_LOADER_WIDTH;
            size_t run = GROK_LOADER_WIDTH - x;
            lv_area_t area;

            if (y >= GROK_LOADER_HEIGHT)
            {
                return;
            }
            if (run > left)
            {
                run = left;
            }
            if (voice)
            {
                /* Same art, voice colors: navy ground, cream ball, and the
                 * three dots below the ball take the phase accent. */
                uint32_t rgb = grok_loader_palette_rgb[index];

                if (index == 0)
                {
                    rgb = kVoiceNavy;
                }
                else if (index == 1)
                {
                    rgb = kVoiceBallDim;
                }
                else
                {
                    rgb = (y >= GROK_VOICE_DOT_Y) ? dot_accent : kVoiceCream;
                }
                rect_dsc.bg_color = lv_color_hex(rgb);
            }
            area.x1 = coords.x1 + (lv_coord_t)x;
            area.y1 = coords.y1 + (lv_coord_t)y;
            area.x2 = area.x1 + (lv_coord_t)run - 1;
            area.y2 = area.y1;
            lv_draw_rect(draw_ctx, &rect_dsc, &area);
            pix += run;
            left = (uint8_t)(left - run);
        }
    }
}

static void deco_fill(lv_draw_ctx_t *ctx, const lv_draw_rect_dsc_t *dsc, const lv_area_t *origin,
                      int x, int y, int w, int h)
{
    lv_area_t a;

    a.x1 = origin->x1 + (lv_coord_t)x;
    a.y1 = origin->y1 + (lv_coord_t)y;
    a.x2 = a.x1 + (lv_coord_t)w - 1;
    a.y2 = a.y1 + (lv_coord_t)h - 1;
    lv_draw_rect(ctx, dsc, &a);
}

/* A 7x4 triangle drawn as four 1 px rows. up=true points up. */
static void deco_arrow(lv_draw_ctx_t *ctx, lv_draw_rect_dsc_t *dsc, const lv_area_t *origin,
                       int x, int y, bool up, bool on)
{
    dsc->bg_color = lv_color_hex(on ? kVoiceCyan : kVoiceDim);
    for (int row = 0; row < 4; row++)
    {
        int width = up ? (1 + (2 * row)) : (7 - (2 * row));

        deco_fill(ctx, dsc, origin, x + ((7 - width) / 2), y + row, width, 1);
    }
}

static void deco_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj;
    lv_draw_ctx_t *ctx;
    lv_area_t origin;
    lv_draw_rect_dsc_t dsc;

    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN)
    {
        return;
    }
    obj = lv_event_get_target(e);
    ctx = lv_event_get_draw_ctx(e);
    if ((obj == NULL) || (ctx == NULL) || (s_skin == SKIN_NONE))
    {
        return;
    }
    lv_obj_get_coords(obj, &origin);

    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;
    dsc.bg_color = lv_color_hex(kVoiceAmber);
    deco_fill(ctx, &dsc, &origin, 0, 0, GROK_VOICE_SCREEN_W, GROK_VOICE_BAR_H);
    if (s_skin != SKIN_VOICE_CAPTION)
    {
        return;
    }

    dsc.bg_color = lv_color_hex(kVoicePanelFill);
    dsc.radius = GROK_VOICE_PANEL_R;
    dsc.border_width = 1;
    dsc.border_opa = LV_OPA_COVER;
    dsc.border_color = lv_color_hex(kVoiceCyan);
    deco_fill(ctx, &dsc, &origin, GROK_VOICE_PANEL_X, GROK_VOICE_PANEL_Y,
              GROK_VOICE_PANEL_W, GROK_VOICE_PANEL_H);

    dsc.radius = 0;
    dsc.border_width = 0;
    dsc.bg_color = lv_color_hex(kVoiceFooterFill);
    deco_fill(ctx, &dsc, &origin, 0, GROK_VOICE_FOOTER_Y, GROK_VOICE_SCREEN_W, GROK_VOICE_FOOTER_H);

    {
        int y = GROK_VOICE_FOOTER_Y + ((GROK_VOICE_FOOTER_H - 4) / 2);

        deco_arrow(ctx, &dsc, &origin, s_arrow_up_x, y, true, s_arrow_up_on);
        deco_arrow(ctx, &dsc, &origin, GROK_VOICE_SCREEN_W - 4 - 7, y, false, s_arrow_down_on);
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

    s_loader = lv_obj_create(s_panel);
    lv_obj_remove_style_all(s_loader);
    lv_obj_set_pos(s_loader, 0, 0);
    lv_obj_set_size(s_loader, GROK_LOADER_WIDTH, GROK_LOADER_HEIGHT);
    lv_obj_set_style_bg_opa(s_loader, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_loader, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_loader, loader_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_flag(s_loader, LV_OBJ_FLAG_HIDDEN);

    /* Above the loader art, below every label. */
    s_deco = lv_obj_create(s_panel);
    lv_obj_remove_style_all(s_deco);
    lv_obj_set_pos(s_deco, 0, 0);
    lv_obj_set_size(s_deco, kOSD_Width_px, kOSD_Height_px);
    lv_obj_set_style_bg_opa(s_deco, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_deco, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_deco, deco_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_flag(s_deco, LV_OBJ_FLAG_HIDDEN);

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

    s_status = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_status, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(kVoiceBarText), 0);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_status, GROK_VOICE_HEAD_X,
                   (GROK_VOICE_BAR_H - GROK_VOICE_PITCH) / 2);
    lv_obj_set_width(s_status, GROK_VOICE_SCREEN_W - (2 * GROK_VOICE_HEAD_X));
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_status, "");
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);

    s_page = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_page, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(s_page, lv_color_hex(kVoiceCyan), 0);
    lv_label_set_long_mode(s_page, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_page, "");
    lv_obj_add_flag(s_page, LV_OBJ_FLAG_HIDDEN);
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

static void set_text_visible(bool text_on)
{
    if (text_on)
    {
        lv_obj_clear_flag(s_header, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_loader, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_header, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_loader, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Switch the shared labels between the plain overlay look and the voice look.
 * The labels are reused, so each style is set once on the way in and removed on
 * the way out. Plain views never see the voice colors or positions. */
static void set_skin(Skin_t skin)
{
    bool was_voice = (s_skin != SKIN_NONE);
    bool is_voice = (skin != SKIN_NONE);

    if (skin == s_skin)
    {
        return;
    }
    s_skin = skin;
    if (was_voice != is_voice)
    {
        if (is_voice)
        {
            lv_obj_set_style_bg_color(s_panel, lv_color_hex(kVoiceNavy), 0);
            lv_obj_set_pos(s_header, GROK_VOICE_HEAD_X, (GROK_VOICE_BAR_H - GROK_VOICE_PITCH) / 2);
            lv_obj_set_width(s_header, GROK_VOICE_HEAD_COLS * GROK_VOICE_GLYPH);
            lv_obj_set_style_text_color(s_header, lv_color_hex(kVoiceBarText), 0);
            lv_obj_set_pos(s_body, GROK_VOICE_TEXT_X, GROK_VOICE_TEXT_Y);
            lv_obj_set_size(s_body, GROK_VOICE_COLS * GROK_VOICE_GLYPH,
                            (GROK_VOICE_ROWS * GROK_VOICE_PITCH));
            lv_obj_set_style_text_color(s_body, lv_color_hex(kVoiceCream), 0);
            /* unscii_8 reports a 9 px line height. The plain overlay adds 1 px of
             * line space on top of that (10 px rows). Voice rows are 9 px. */
            lv_obj_set_style_text_line_space(s_body,
                                             GROK_VOICE_PITCH - lv_font_get_line_height(&lv_font_unscii_8), 0);
            lv_obj_set_pos(s_footer, GROK_VOICE_HEAD_X,
                           GROK_VOICE_FOOTER_Y + ((GROK_VOICE_FOOTER_H - GROK_VOICE_PITCH) / 2));
            lv_obj_set_style_text_color(s_footer, lv_color_hex(kVoiceLavender), 0);
        }
        else
        {
            lv_obj_set_style_bg_color(s_panel, lv_color_hex(kColor_Black), 0);
            lv_obj_set_pos(s_header, 0, kHeaderY);
            lv_obj_set_width(s_header, kOSD_Width_px);
            lv_obj_remove_local_style_prop(s_header, LV_STYLE_TEXT_COLOR, 0);
            lv_obj_set_pos(s_body, 0, kBodyY);
            lv_obj_set_size(s_body, kOSD_Width_px, kPageLines * kLinePitch);
            lv_obj_remove_local_style_prop(s_body, LV_STYLE_TEXT_COLOR, 0);
            lv_obj_set_style_text_line_space(s_body, kLinePitch - 8, 0);
            lv_obj_set_pos(s_footer, 0, kFooterY);
            lv_obj_set_width(s_footer, kOSD_Width_px);
            lv_obj_remove_local_style_prop(s_footer, LV_STYLE_TEXT_COLOR, 0);
            s_header_shown[0] = '\0';
        }
        s_body_shown[0] = '\0';
    }
    if (is_voice)
    {
        lv_obj_clear_flag(s_deco, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_deco, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_page, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_invalidate(s_deco);
}

static void voice_set_header(const char *name, GrokVoiceStatus_t status)
{
    char title[GROK_VOICE_HEAD_COLS + 1];
    const char *word = grok_voice_status_text(status);

    grok_voice_format_title(title, sizeof(title), name, grok_voice_title_cols(status));
    if (strcmp(title, s_header_shown) != 0)
    {
        lv_label_set_text(s_header, title);
        snprintf(s_header_shown, sizeof(s_header_shown), "%s", title);
    }
    if (s_status_shown != word)
    {
        lv_label_set_text(s_status, word);
        s_status_shown = word;
    }
    lv_obj_clear_flag(s_header, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_status, LV_OBJ_FLAG_HIDDEN);
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

static void render_bots(int bot, char *page_text, size_t page_cap)
{
    size_t used = 0;

    page_text[0] = '\0';
    for (int i = 0; i < GROK_BOTS_COUNT; i++)
    {
        char row[GROK_TEXT_COLS + 1];
        size_t len = grok_bots_format_row(row, sizeof(row), i, i == bot, GROK_TEXT_COLS);

        if ((used + len + 2) >= page_cap)
        {
            break;
        }
        memcpy(page_text + used, row, len);
        used += len;
        page_text[used++] = '\n';
        page_text[used] = '\0';
    }
}

static void apply_body(const char *page_text)
{
    if (strcmp(page_text, s_body_shown) != 0)
    {
        lv_label_set_text(s_body, page_text);
        snprintf(s_body_shown, sizeof(s_body_shown), "%s", page_text);
    }
}


static void voice_report_bot_locked(void)
{
    char line[64];
    const char *name = grok_bots_name(s_ui.bot);

    grok_voice_format_bot_line(line, sizeof(line), name);
    grok_say(line);
}

static void voice_show_caption_locked(void)
{
    s_ui.voice_phase = VOICE_PHASE_CAPTION;
    s_ui.view = UI_VOICE;
    s_ui.open = true;
    s_ui.dirty = true;
}

static void open_bots_locked(void)
{
    s_ui.view = UI_BOTS;
    s_ui.open = true;
    s_ui.dirty = true;
}

static void render(lv_obj_t *screen)
{
    static char view[GROK_REPLY_MAX + 1];
    static char lines[GROK_TEXT_LINES][GROK_TEXT_COLS + 1];
    static char page_text[kPageLines * (GROK_TEXT_COLS + 2)];
    bool open;
    bool dirty;
    UiView_t uiview;
    int pick;
    int bot;
    int page;
    int loader_frame;
    uint32_t loader_start;
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
    uiview = s_ui.view;
    pick = s_ui.pick;
    bot = s_ui.bot;
    dirty = s_ui.dirty;
    loader_frame = s_ui.loader_frame;
    loader_start = s_ui.loader_start_ms;
    if (uiview == UI_REPLY)
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

    /* Loader: always play at least one full cycle, then bots once Wi-Fi is
     * up / there is no SSID, or after the give-up timeout. */
    if (uiview == UI_LOADER)
    {
        uint32_t ms = now_ms();
        uint32_t elapsed = ms - loader_start;
        uint32_t cycle = grok_loader_cycle_ms();
        int next = grok_loader_frame_at(ms, loader_start);
        bool min_played = (elapsed >= cycle);
        bool wifi_ready = grok_wifi_is_connected() || (!grok_wifi_has_ssid());
        bool give_up = (elapsed >= (uint32_t)kLoaderGiveUpMs);
        bool done = min_played && (wifi_ready || give_up);

        if (done)
        {
            lock();
            open_bots_locked();
            unlock();
            uiview = UI_BOTS;
            dirty = true;
        }
        else
        {
            if (next != loader_frame)
            {
                lock();
                s_ui.loader_frame = next;
                unlock();
                loader_frame = next;
                lv_obj_invalidate(s_loader);
            }
            set_skin(SKIN_NONE);
            set_text_visible(false);
            lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_panel);
            return;
        }
    }


    if (uiview == UI_VOICE)
    {
        VoicePhase_t phase;
        size_t cap_idx;
        size_t cap_count;
        const char *bot_name;

        lock();
        phase = s_ui.voice_phase;
        cap_idx = s_ui.voice_page;
        bot_name = grok_bots_name(s_ui.bot);
        if (phase != VOICE_PHASE_CAPTION)
        {
            uint32_t ms = now_ms();
            int next = grok_loader_frame_at(ms, s_ui.loader_start_ms);
            if (next != s_ui.loader_frame)
            {
                s_ui.loader_frame = next;
                loader_frame = next;
            }
            else
            {
                loader_frame = s_ui.loader_frame;
            }
            cap_count = 0;
        }
        else
        {
            cap_count = grok_text_caption_page(s_ui.voice_text, GROK_VOICE_COLS,
                                               s_ui.voice_page, (size_t)GROK_VOICE_ROWS,
                                               view, sizeof(view));
            if ((cap_count > 0) &&
                (s_ui.voice_page >= ((cap_count + (size_t)GROK_VOICE_ROWS - 1u) / (size_t)GROK_VOICE_ROWS)))
            {
                s_ui.voice_page = 0;
                cap_idx = 0;
                cap_count = grok_text_caption_page(s_ui.voice_text, GROK_VOICE_COLS,
                                                   0, (size_t)GROK_VOICE_ROWS,
                                                   view, sizeof(view));
            }
        }
        unlock();

        if (phase != VOICE_PHASE_CAPTION)
        {
            /* Ball loader (voice colors) under the header bar. */
            set_text_visible(false);
            set_skin(SKIN_VOICE_BAR);
            voice_set_header(bot_name, (phase == VOICE_PHASE_THINKING)
                                           ? GROK_VOICE_STATUS_THINKING
                                           : GROK_VOICE_STATUS_LISTENING);
            lv_obj_add_flag(s_page, LV_OBJ_FLAG_HIDDEN);
            lv_obj_invalidate(s_loader);
            lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_panel);
            return;
        }

        set_text_visible(true);
        set_skin(SKIN_VOICE_CAPTION);
        voice_set_header(bot_name, GROK_VOICE_STATUS_REPLY);
        {
            char pg[sizeof(s_page_shown)];
            size_t pages = (cap_count == 0) ? 1u
                           : ((cap_count + (size_t)GROK_VOICE_ROWS - 1u) / (size_t)GROK_VOICE_ROWS);
            size_t pg_len;
            int pg_x;
            int up_x;
            bool up_on;
            bool down_on;

            if (cap_idx >= pages)
            {
                cap_idx = pages - 1u;
            }
            pg_len = grok_voice_format_page(pg, sizeof(pg), cap_idx, pages);
            /* Right to left: 4 px pad, down arrow (7 px), 3 px, "2/4", 3 px, up arrow. */
            pg_x = GROK_VOICE_SCREEN_W - 4 - 7 - 3 - ((int)pg_len * GROK_VOICE_GLYPH);
            up_x = pg_x - 3 - 7;
            up_on = (cap_idx > 0);
            down_on = ((cap_idx + 1u) < pages);
            if (strcmp(pg, s_page_shown) != 0)
            {
                lv_label_set_text(s_page, pg);
                snprintf(s_page_shown, sizeof(s_page_shown), "%s", pg);
            }
            lv_obj_set_pos(s_page, pg_x,
                           GROK_VOICE_FOOTER_Y + ((GROK_VOICE_FOOTER_H - GROK_VOICE_PITCH) / 2));
            lv_obj_clear_flag(s_page, LV_OBJ_FLAG_HIDDEN);
            if ((up_x != s_arrow_up_x) || (up_on != s_arrow_up_on) || (down_on != s_arrow_down_on))
            {
                s_arrow_up_x = up_x;
                s_arrow_up_on = up_on;
                s_arrow_down_on = down_on;
                lv_obj_invalidate(s_deco);
            }
            /* The hint gives way when a long page count needs the room. */
            set_footer(((up_x - 3) >= (GROK_VOICE_HEAD_X + ((int)(sizeof(kFooterVoiceCap) - 1) * GROK_VOICE_GLYPH)))
                           ? kFooterVoiceCap
                           : kFooterVoiceCapShort);
            if (cap_count == 0)
            {
                snprintf(view, sizeof(view), "(no caption)");
            }
        }
        if (dirty || lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN))
        {
            apply_body(view);
        }
        lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_panel);
        return;
    }

    set_skin(SKIN_NONE);
    set_text_visible(true);

    if (uiview == UI_BOTS)
    {
        set_header("BOT ", bot + 1, GROK_BOTS_COUNT);
        set_footer(kFooterBots);
        if (dirty || lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN))
        {
            render_bots(bot, page_text, sizeof(page_text));
            apply_body(page_text);
        }
        lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_panel);
        return;
    }

    if (uiview == UI_NOTE)
    {
        set_header("BOT ", bot + 1, GROK_BOTS_COUNT);
        set_footer(kFooterNote);
        if (dirty || lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN))
        {
            grok_bots_format_note(page_text, sizeof(page_text), bot);
            apply_body(page_text);
        }
        lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_panel);
        return;
    }

    if (uiview == UI_PICKER)
    {
        set_header("ASK ", pick + 1, GROK_PICK_COUNT);
        set_footer(kFooterPick);
        if (dirty || lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN))
        {
            render_picker(pick, view, sizeof(view), &lines[0][0], page_text, sizeof(page_text));
            apply_body(page_text);
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
    apply_body(page_text);

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

static void enter_menu(void)
{
    /* Always show the loader for at least one full animation cycle. */
    grok_ui_open_loader();
}

static OSD_Result_t item_button(const Button_t Button, const ButtonState_t State, void *arg)
{
    (void)arg;

    if ((Button == kButton_A) && (State == kButtonState_Pressed))
    {
        enter_menu();
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
    UiView_t uiview;

    if (!OSD_IsVisible())
    {
        return false;
    }

    if (!grok_ui_is_open())
    {
        if (Button_GetState(kButton_Start) == kButtonState_Pressed)
        {
            enter_menu();
        }
        return false;
    }

    lock();
    uiview = s_ui.view;
    unlock();

    for (int i = 0; i < kNumButtons; i++)
    {
        Button_t button = (Button_t)i;
        ButtonState_t state = Button_GetState(button);

        if (state != kButtonState_Pressed)
        {
            continue;
        }

        if (uiview == UI_VOICE)
        {
            if ((button == kButton_Left) || (button == kButton_Right))
            {
                lock();
                s_ui.bot = grok_bots_step(s_ui.bot,
                                          (button == kButton_Right) ? 1 : -1);
                s_ui.dirty = true;
                unlock();
                grok_ui_voice_report_bot();
                return true;
            }
            if ((button == kButton_A) || (button == kButton_Down))
            {
                grok_ui_voice_caption_step(1);
                return true;
            }
            if (button == kButton_Up)
            {
                grok_ui_voice_caption_step(-1);
                return true;
            }
            if ((button == kButton_B) || (button == kButton_Start))
            {
                grok_ui_close();
                return true;
            }
            continue;
        }
        if (uiview == UI_LOADER)
        {
            if ((button == kButton_B) || (button == kButton_Start))
            {
                grok_ui_close();
                return true;
            }
            if (button == kButton_A)
            {
                grok_ui_open_bots();
                return true;
            }
            continue;
        }
        if (uiview == UI_BOTS)
        {
            if ((button == kButton_Up) || (button == kButton_Down))
            {
                lock();
                s_ui.bot = grok_bots_step(s_ui.bot, (button == kButton_Up) ? -1 : 1);
                s_ui.dirty = true;
                unlock();
            }
            else if (button == kButton_A)
            {
                lock();
                s_ui.view = UI_NOTE;
                s_ui.dirty = true;
                unlock();
                return true;
            }
            else if ((button == kButton_B) || (button == kButton_Start))
            {
                grok_ui_close();
                return true;
            }
            continue;
        }
        if (uiview == UI_NOTE)
        {
            if (button == kButton_A)
            {
                grok_ui_open_picker();
                return true;
            }
            if ((button == kButton_B) || (button == kButton_Start))
            {
                grok_ui_open_bots();
                return true;
            }
            continue;
        }
        if (uiview == UI_PICKER)
        {
            /* Up/Down pick, A asks, B goes back to the bot note. */
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
                lock();
                s_ui.view = UI_NOTE;
                s_ui.open = true;
                s_ui.dirty = true;
                unlock();
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
            lock();
            s_ui.view = UI_NOTE;
            s_ui.open = true;
            s_ui.dirty = true;
            unlock();
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
    s_ui.view = UI_REPLY;
    s_ui.open = true;
    s_ui.dirty = true;
    unlock();
}

void grok_ui_open_picker(void)
{
    lock();
    s_ui.view = UI_PICKER;
    s_ui.open = true;
    s_ui.dirty = true;
    unlock();
}

void grok_ui_open_bots(void)
{
    lock();
    open_bots_locked();
    unlock();
}

void grok_ui_open_loader(void)
{
    lock();
    s_ui.view = UI_LOADER;
    s_ui.open = true;
    s_ui.dirty = true;
    s_ui.loader_start_ms = now_ms();
    s_ui.loader_frame = 0;
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
    s_ui.view = UI_REPLY;
    s_ui.open = true;
    s_ui.dirty = true;
    unlock();
}

void grok_ui_page(int delta)
{
    lock();
    s_ui.page += delta;
    s_ui.view = UI_REPLY;
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
    s_ui.view = UI_REPLY;
    s_ui.open = true;
    s_ui.dirty = true;
    unlock();
}

void grok_ui_open_voice(void)
{
    lock();
    s_ui.view = UI_VOICE;
    s_ui.voice_phase = VOICE_PHASE_LISTENING;
    s_ui.open = true;
    s_ui.dirty = true;
    s_ui.loader_start_ms = now_ms();
    s_ui.loader_frame = 0;
    s_ui.voice_text[0] = '\0';
    s_ui.voice_page = 0;
    unlock();
}

void grok_ui_voice_listening(void)
{
    lock();
    s_ui.view = UI_VOICE;
    s_ui.voice_phase = VOICE_PHASE_LISTENING;
    s_ui.open = true;
    s_ui.dirty = true;
    s_ui.loader_start_ms = now_ms();
    s_ui.loader_frame = 0;
    unlock();
}

void grok_ui_voice_thinking(void)
{
    lock();
    s_ui.view = UI_VOICE;
    s_ui.voice_phase = VOICE_PHASE_THINKING;
    s_ui.open = true;
    s_ui.dirty = true;
    s_ui.loader_start_ms = now_ms();
    s_ui.loader_frame = 0;
    unlock();
}

void grok_ui_voice_set_reply(const char *text)
{
    lock();
    snprintf(s_ui.voice_text, sizeof(s_ui.voice_text), "%s", (text != NULL) ? text : "");
    grok_text_fold_ascii(s_ui.voice_text);
    s_ui.voice_page = 0;
    voice_show_caption_locked();
    unlock();
}

void grok_ui_voice_caption_step(int delta)
{
    size_t lines;

    lock();
    if (s_ui.view != UI_VOICE || s_ui.voice_phase != VOICE_PHASE_CAPTION)
    {
        unlock();
        return;
    }
    lines = grok_text_caption_page(s_ui.voice_text, GROK_VOICE_COLS, 0,
                                   (size_t)GROK_VOICE_ROWS, NULL, 0);
    s_ui.voice_page = grok_text_caption_step(s_ui.voice_page, lines,
                                             (size_t)GROK_VOICE_ROWS, delta);
    s_ui.dirty = true;
    unlock();
}

void grok_ui_voice_report_bot(void)
{
    lock();
    voice_report_bot_locked();
    unlock();
}

int grok_ui_voice_bot_index(void)
{
    int bot;
    lock();
    bot = s_ui.bot;
    unlock();
    return bot;
}

bool grok_ui_voice_is_active(void)
{
    bool active;
    lock();
    active = s_ui.open && (s_ui.view == UI_VOICE);
    unlock();
    return active;
}
