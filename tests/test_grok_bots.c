/* Host checks for the static bot/team list. Not part of the ESP-IDF build.
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
    char note[GROK_BOTS_NOTE_MAX + 64];
    const char *want_names[GROK_BOTS_COUNT] = {
        "Carty G",
        "The Chief",
        "Awesome Coder",
        "Chromatic Add-ons",
    };

    expect_true("count", GROK_BOTS_COUNT == 4);
    expect_true("name max fits row",
                (GROK_BOTS_ROW_PREFIX + GROK_BOTS_NAME_MAX) == GROK_TEXT_COLS);

    for (int i = 0; i < GROK_BOTS_COUNT; i++)
    {
        const char *name = grok_bots_name(i);
        const char *n = grok_bots_note(i);

        expect_true("name set", (name != NULL) && (strcmp(name, want_names[i]) == 0));
        expect_true("name fits", (name != NULL) && (strlen(name) <= GROK_BOTS_NAME_MAX));
        expect_true("note empty default", (n != NULL) && (n[0] == '\0'));
        grok_bots_format_row(row, sizeof(row), i, i == 0, GROK_TEXT_COLS);
        expect_true("row width", strlen(row) <= GROK_TEXT_COLS);
        expect_true("row has name", (name != NULL) && (strstr(row, name) != NULL));
        grok_bots_format_note(note, sizeof(note), i);
        expect_true("note header", strstr(note, "Where we left off:") != NULL);
        expect_true("note empty body", strstr(note, "(nothing yet)") != NULL);
        expect_true("note has name", (name != NULL) && (strstr(note, name) != NULL));
    }

    expect_true("name range lo", grok_bots_name(-1) == NULL);
    expect_true("name range hi", grok_bots_name(GROK_BOTS_COUNT) == NULL);
    expect_true("note range lo", grok_bots_note(-1) == NULL);
    expect_true("step down", grok_bots_step(0, 1) == 1);
    expect_true("step wrap down", grok_bots_step(GROK_BOTS_COUNT - 1, 1) == 0);
    expect_true("step wrap up", grok_bots_step(0, -1) == GROK_BOTS_COUNT - 1);

    grok_bots_format_row(row, sizeof(row), 0, true, GROK_TEXT_COLS);
    expect_true("row sel", strcmp(row, ">1 Carty G") == 0);
    grok_bots_format_row(row, sizeof(row), 3, false, GROK_TEXT_COLS);
    expect_true("row last", strcmp(row, " 4 Chromatic Add-ons") == 0);
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
