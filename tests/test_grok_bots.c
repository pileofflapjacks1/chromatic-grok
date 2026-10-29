/* Host checks for the static bot/team list and last-prompt packing.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_bots.h"
#include "grok_text.h"

#include <stdio.h>
#include <string.h>

static int g_failed;

static void expect_true(const char *name, int cond)
{
    if (!cond)
    {
        fprintf(stderr, "FAIL %s\n", name);
        g_failed++;
    }
}

static void test_bots(void)
{
    char row[32];
    char note[256];
    char packed[GROK_BOTS_LAST_PACK_MAX + 1];
    char prompt[GROK_BOTS_LAST_PROMPT_MAX + 1];
    char reply[GROK_BOTS_LAST_REPLY_MAX + 1];
    char long_prompt[200];
    char long_reply[120];
    const char *want_names[GROK_BOTS_COUNT] = {
        "Carty G",
        "The Chief",
        "Awesome Coder",
        "Chromatic Add-ons",
    };

    expect_true("count", GROK_BOTS_COUNT == 4);
    expect_true("menu count", GROK_BOTS_MENU_COUNT == 6);
    expect_true("ask idx", GROK_BOTS_ASK_IDX == 4);
    expect_true("phone idx", GROK_BOTS_PHONE_IDX == 5);
    expect_true("name max fits row",
                (GROK_BOTS_ROW_PREFIX + GROK_BOTS_NAME_MAX) == GROK_TEXT_COLS);
    expect_true("pack fits", GROK_BOTS_LAST_PACK_MAX <= 121);

    for (int i = 0; i < GROK_BOTS_COUNT; i++)
    {
        const char *name = grok_bots_name(i);

        expect_true("name set", (name != NULL) && (strcmp(name, want_names[i]) == 0));
        expect_true("name fits", (name != NULL) && (strlen(name) <= GROK_BOTS_NAME_MAX));
        grok_bots_format_row(row, sizeof(row), i, i == 0, GROK_TEXT_COLS);
        expect_true("row width", strlen(row) <= GROK_TEXT_COLS);
        expect_true("row has name", (name != NULL) && (strstr(row, name) != NULL));
        grok_bots_format_note(note, sizeof(note), i, "", "");
        expect_true("note header", strstr(note, "Where we left off:") != NULL);
        expect_true("note empty body", strstr(note, "(nothing yet)") != NULL);
        expect_true("note has name", (name != NULL) && (strstr(note, name) != NULL));
    }

    grok_bots_format_row(row, sizeof(row), GROK_BOTS_PHONE_IDX, true, GROK_TEXT_COLS);
    grok_bots_format_row(row, sizeof(row), GROK_BOTS_ASK_IDX, false, GROK_TEXT_COLS);
    expect_true("ask row", strstr(row, "Ask Grok") != NULL);
    grok_bots_format_row(row, sizeof(row), GROK_BOTS_PHONE_IDX, true, GROK_TEXT_COLS);
    expect_true("phone row", strcmp(row, ">6 Phone typing") == 0);

    expect_true("name range lo", grok_bots_name(-1) == NULL);
    expect_true("name range hi", grok_bots_name(GROK_BOTS_COUNT) == NULL);
    expect_true("step down", grok_bots_step(0, 1) == 1);
    expect_true("step wrap down", grok_bots_step(GROK_BOTS_COUNT - 1, 1) == 0);
    expect_true("step wrap up", grok_bots_step(0, -1) == GROK_BOTS_COUNT - 1);
    expect_true("menu wrap phone",
                grok_bots_menu_step(GROK_BOTS_PHONE_IDX, 1, GROK_BOTS_MENU_COUNT) == 0);
    expect_true("menu to ask",
                grok_bots_menu_step(GROK_BOTS_COUNT - 1, 1, GROK_BOTS_MENU_COUNT) ==
                    GROK_BOTS_ASK_IDX);
    expect_true("menu to phone",
                grok_bots_menu_step(GROK_BOTS_ASK_IDX, 1, GROK_BOTS_MENU_COUNT) ==
                    GROK_BOTS_PHONE_IDX);

    grok_bots_format_row(row, sizeof(row), 0, true, GROK_TEXT_COLS);
    expect_true("row sel", strcmp(row, ">1 Carty G") == 0);
    grok_bots_format_row(row, sizeof(row), 3, false, GROK_TEXT_COLS);
    expect_true("row last bot", strcmp(row, " 4 Chromatic Add-ons") == 0);

    grok_bots_select(2);
    expect_true("selected", grok_bots_selected() == 2);
    grok_bots_select(99);
    expect_true("selected clamp", grok_bots_selected() == 2);

    /* Clip / pack / unpack / note with last prompt. */
    memset(long_prompt, 'A', sizeof(long_prompt) - 1);
    long_prompt[sizeof(long_prompt) - 1] = '\0';
    memset(long_reply, 'B', sizeof(long_reply) - 1);
    long_reply[sizeof(long_reply) - 1] = '\0';
    grok_bots_clip(prompt, sizeof(prompt), long_prompt, GROK_BOTS_LAST_PROMPT_MAX);
    expect_true("clip prompt len", strlen(prompt) == GROK_BOTS_LAST_PROMPT_MAX);
    grok_bots_clip(reply, sizeof(reply), "hi\nthere", GROK_BOTS_LAST_REPLY_MAX);
    expect_true("clip nl", strcmp(reply, "hi there") == 0);

    expect_true("pack", grok_bots_pack_last(packed, sizeof(packed),
                                            "What is a Game Boy?",
                                            "A handheld console.") > 0);
    grok_bots_unpack_last(packed, prompt, sizeof(prompt), reply, sizeof(reply));
    expect_true("unpack prompt", strcmp(prompt, "What is a Game Boy?") == 0);
    expect_true("unpack reply", strcmp(reply, "A handheld console.") == 0);

    expect_true("pack trunc",
                grok_bots_pack_last(packed, sizeof(packed), long_prompt, long_reply) > 0);
    grok_bots_unpack_last(packed, prompt, sizeof(prompt), reply, sizeof(reply));
    expect_true("trunc prompt", strlen(prompt) == GROK_BOTS_LAST_PROMPT_MAX);
    expect_true("trunc reply", strlen(reply) == GROK_BOTS_LAST_REPLY_MAX);

    grok_bots_format_note(note, sizeof(note), 0, "Hello?", "Hi.");
    expect_true("note prompt", strstr(note, "Hello?") != NULL);
    expect_true("note reply", strstr(note, "Hi.") != NULL);
    expect_true("note no empty", strstr(note, "(nothing yet)") == NULL);

    grok_bots_format_note(note, sizeof(note), 1, "Only prompt", "");
    expect_true("note prompt only", strstr(note, "Only prompt") != NULL);
    expect_true("note no reply blank", strstr(note, "\n\n\n") == NULL);

    grok_bots_set_name(1, "Chief X");
    expect_true("override", strcmp(grok_bots_name(1), "Chief X") == 0);
    grok_bots_set_name(1, NULL);
    expect_true("restore", strcmp(grok_bots_name(1), "The Chief") == 0);
    grok_bots_set_fast(true);
    expect_true("fast on", grok_bots_fast());
    grok_bots_select(0);
    expect_true("fast off on select", !grok_bots_fast());
}

int main(void)
{
    test_bots();
    if (g_failed != 0)
    {
        fprintf(stderr, "%d failed\n", g_failed);
        return 1;
    }
    printf("grok_bots ok\n");
    return 0;
}
