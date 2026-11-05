/* Host checks for laptop-voice framing and caption chunking.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_voice_util.h"

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

static void test_parser_partial_bytes(void)
{
    GrokVoiceParser_t p;
    const char *line = NULL;
    const char *stream = "VOI" "CE_BEGIN\nHi there\nVOICE_END\n";
    GrokVoiceEvt_t ev;
    int saw_begin = 0;
    int saw_body = 0;
    int saw_end = 0;
    size_t i;

    grok_voice_parser_init(&p);
    for (i = 0; stream[i] != '\0'; i++)
    {
        ev = grok_voice_parser_feed(&p, stream[i], &line);
        if (ev == GROK_VOICE_EVT_NONE)
        {
            continue;
        }
        if (ev == GROK_VOICE_EVT_BEGIN)
        {
            saw_begin = 1;
            expect_true("in frame", p.in_frame);
        }
        else if (ev == GROK_VOICE_EVT_BODY_LINE)
        {
            saw_body = 1;
            expect_true("body text", strcmp(p.body, "Hi there") == 0);
        }
        else if (ev == GROK_VOICE_EVT_END)
        {
            saw_end = 1;
            expect_true("not in frame", !p.in_frame);
            expect_true("body kept", strcmp(p.body, "Hi there") == 0);
        }
        else
        {
            expect_true("unexpected evt", 0);
        }
    }
    expect_true("saw begin", saw_begin);
    expect_true("saw body", saw_body);
    expect_true("saw end", saw_end);
}

static void test_parser_feed_line(void)
{
    GrokVoiceParser_t p;

    grok_voice_parser_init(&p);
    expect_true("line cmd", grok_voice_parser_feed_line(&p, "voice listening") ==
                GROK_VOICE_EVT_LINE);
    expect_true("is cmd", grok_voice_is_cmd_line("voice listening"));
    expect_true("is cmd bare", grok_voice_is_cmd_line("voice"));
    expect_true("not cmd", !grok_voice_is_cmd_line("grok ask hi"));
    expect_true("begin", grok_voice_parser_feed_line(&p, GROK_VOICE_BEGIN) ==
                GROK_VOICE_EVT_BEGIN);
    expect_true("body1", grok_voice_parser_feed_line(&p, "One.") ==
                GROK_VOICE_EVT_BODY_LINE);
    expect_true("body2", grok_voice_parser_feed_line(&p, "Two!") ==
                GROK_VOICE_EVT_BODY_LINE);
    expect_true("joined", strcmp(p.body, "One.\nTwo!") == 0);
    expect_true("end", grok_voice_parser_feed_line(&p, GROK_VOICE_END) ==
                GROK_VOICE_EVT_END);
}

static void test_parser_overflow(void)
{
    GrokVoiceParser_t p;
    char long_line[GROK_VOICE_LINE_MAX + 8];
    size_t i;

    grok_voice_parser_init(&p);
    memset(long_line, 'A', sizeof(long_line) - 1);
    long_line[sizeof(long_line) - 1] = '\0';
    expect_true("line overflow",
                grok_voice_parser_feed_line(&p, long_line) == GROK_VOICE_EVT_OVERFLOW);

    grok_voice_parser_init(&p);
    expect_true("begin ok",
                grok_voice_parser_feed_line(&p, GROK_VOICE_BEGIN) == GROK_VOICE_EVT_BEGIN);
    for (i = 0; i < (GROK_VOICE_BODY_MAX / 8) + 4; i++)
    {
        GrokVoiceEvt_t ev = grok_voice_parser_feed_line(&p, "ABCDEFG");
        if (ev == GROK_VOICE_EVT_OVERFLOW)
        {
            expect_true("body overflow seen", 1);
            return;
        }
        expect_true("body accept", ev == GROK_VOICE_EVT_BODY_LINE);
    }
    expect_true("body overflow expected", 0);
}

static void test_chunk_captions(void)
{
    char chunks[8][96];
    size_t n;

    n = grok_voice_chunk_captions(
        "Hello world. More text here! And a question?",
        &chunks[0][0], 96, 8, 20);
    expect_true("got chunks", n >= 2);
    expect_true("first has Hello", strstr(chunks[0], "Hello") != NULL);
    expect_true("first ends punct or short",
                (chunks[0][strlen(chunks[0]) - 1] == '.') ||
                (strlen(chunks[0]) <= 80));

    n = grok_voice_chunk_captions(
        "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz0123456789",
        &chunks[0][0], 96, 8, 20);
    expect_true("hard wrap", n >= 2);
    expect_true("wrap width", strlen(chunks[0]) <= 80);

    n = grok_voice_chunk_captions("", &chunks[0][0], 96, 8, 20);
    expect_true("empty", n == 0);

    n = grok_voice_chunk_captions("Short.", &chunks[0][0], 96, 8, 20);
    expect_true("one phrase", n == 1);
    expect_true("short text", strcmp(chunks[0], "Short.") == 0);
}

static void test_bot_line(void)
{
    char out[64];
    size_t n = grok_voice_format_bot_line(out, sizeof(out), "Carty G");

    expect_true("bot len", n == strlen(out));
    expect_true("bot text", strcmp(out, "VOICE_BOT Carty G\n") == 0);
    grok_voice_format_bot_line(out, sizeof(out), NULL);
    expect_true("bot null", strcmp(out, "VOICE_BOT ?\n") == 0);
}

int main(void)
{
    test_parser_partial_bytes();
    test_parser_feed_line();
    test_parser_overflow();
    test_chunk_captions();
    test_bot_line();
    if (g_failed)
    {
        fprintf(stderr, "%d voice host checks failed\n", g_failed);
        return 1;
    }
    printf("test_grok_voice: ok\n");
    return 0;
}
