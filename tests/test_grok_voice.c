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

/* Same predicate grok_voice_handle_line uses before esp_console_run. */
static int may_dispatch(GrokVoiceEvt_t ev)
{
    return grok_voice_reaches_command(ev);
}

static void format_data(char *dst, size_t cap, const char *payload)
{
    snprintf(dst, cap, "VOICE_DATA %u %s", (unsigned)strlen(payload), payload);
}

static void test_parser_partial_bytes(void)
{
    GrokVoiceParser_t p;
    const char *line = NULL;
    const char *stream = "VOI" "CE_BEGIN\nVOICE_DATA 8 Hi there\nVOICE_END\n";
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
        expect_true("partial not dispatched", !may_dispatch(ev));
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
    char line[GROK_VOICE_LINE_MAX + 1];

    grok_voice_parser_init(&p);
    expect_true("line cmd", grok_voice_parser_feed_line(&p, "voice listening") ==
                GROK_VOICE_EVT_LINE);
    expect_true("is cmd", grok_voice_is_cmd_line("voice listening"));
    expect_true("is cmd bare", grok_voice_is_cmd_line("voice"));
    expect_true("not cmd", !grok_voice_is_cmd_line("grok ask hi"));
    expect_true("wifi outside is dispatch",
                may_dispatch(grok_voice_parser_feed_line(&p, "wifi clear")));
    expect_true("begin", grok_voice_parser_feed_line(&p, GROK_VOICE_BEGIN) ==
                GROK_VOICE_EVT_BEGIN);
    format_data(line, sizeof(line), "One.");
    expect_true("body1", grok_voice_parser_feed_line(&p, line) == GROK_VOICE_EVT_BODY_LINE);
    expect_true("nl", grok_voice_parser_feed_line(&p, GROK_VOICE_NL) == GROK_VOICE_EVT_BODY_LINE);
    format_data(line, sizeof(line), "Two!");
    expect_true("body2", grok_voice_parser_feed_line(&p, line) == GROK_VOICE_EVT_BODY_LINE);
    expect_true("joined", strcmp(p.body, "One.\nTwo!") == 0);
    expect_true("end", grok_voice_parser_feed_line(&p, GROK_VOICE_END) == GROK_VOICE_EVT_END);
    expect_true("quotes kept", 1);
    format_data(line, sizeof(line), "say \"hi\" C:\\path");
    grok_voice_parser_init(&p);
    grok_voice_parser_feed_line(&p, GROK_VOICE_BEGIN);
    grok_voice_parser_feed_line(&p, line);
    expect_true("literal", strcmp(p.body, "say \"hi\" C:\\path") == 0);
    expect_true("literal end", grok_voice_parser_feed_line(&p, GROK_VOICE_END) == GROK_VOICE_EVT_END);
}

static void test_wifi_clear_never_dispatched(void)
{
    GrokVoiceParser_t p;
    char over[GROK_VOICE_LINE_MAX + 8];
    char line[GROK_VOICE_LINE_MAX + 1];
    GrokVoiceEvt_t ev;
    unsigned i;

    memset(over, 'B', sizeof(over) - 1);
    over[sizeof(over) - 1] = '\0';
    expect_true("over is past line max", strlen(over) == GROK_VOICE_LINE_MAX + 7);

    grok_voice_parser_init(&p);
    expect_true("begin", grok_voice_parser_feed_line(&p, GROK_VOICE_BEGIN) == GROK_VOICE_EVT_BEGIN);
    ev = grok_voice_parser_feed_line(&p, over);
    expect_true("overflow held", ev == GROK_VOICE_EVT_HOLD);
    expect_true("still framed", p.in_frame);
    ev = grok_voice_parser_feed_line(&p, "wifi clear");
    expect_true("wifi after overflow consumed", !may_dispatch(ev));
    expect_true("wifi after overflow held", ev == GROK_VOICE_EVT_HOLD);
    ev = grok_voice_parser_feed_line(&p, GROK_VOICE_END);
    expect_true("bad frame dropped", ev == GROK_VOICE_EVT_DROPPED);
    expect_true("body empty", p.body[0] == '\0');
    expect_true("frame closed", !p.in_frame);
    expect_true("console works after drop",
                may_dispatch(grok_voice_parser_feed_line(&p, "wifi clear")));

    grok_voice_parser_init(&p);
    grok_voice_parser_feed_line(&p, GROK_VOICE_BEGIN);
    format_data(line, sizeof(line), "wifi clear");
    ev = grok_voice_parser_feed_line(&p, line);
    expect_true("marker payload not dispatched", !may_dispatch(ev));
    expect_true("payload stored", strcmp(p.body, "wifi clear") == 0);
    format_data(line, sizeof(line), "VOICE_END");
    ev = grok_voice_parser_feed_line(&p, line);
    expect_true("embedded end not dispatched", !may_dispatch(ev));
    expect_true("embedded end kept", strcmp(p.body, "wifi clearVOICE_END") == 0);
    format_data(line, sizeof(line), "VOICE_BEGIN");
    grok_voice_parser_feed_line(&p, line);
    ev = grok_voice_parser_feed_line(&p, GROK_VOICE_END);
    expect_true("real end", ev == GROK_VOICE_EVT_END);
    expect_true("markers are data", strcmp(p.body, "wifi clearVOICE_ENDVOICE_BEGIN") == 0);

    grok_voice_parser_init(&p);
    grok_voice_parser_feed_line(&p, GROK_VOICE_BEGIN);
    format_data(line, sizeof(line), "He said \"it's 'fine'\" \\");
    grok_voice_parser_feed_line(&p, line);
    expect_true("mixed literal",
                strcmp(p.body, "He said \"it's 'fine'\" \\") == 0);
    grok_voice_parser_feed_line(&p, GROK_VOICE_END);

    /* Past any old 700-line cap the frame stays open. Line 701 and later
     * reply lines must not reach esp_console. Only VOICE_END closes it. */
    grok_voice_parser_init(&p);
    grok_voice_parser_feed_line(&p, GROK_VOICE_BEGIN);
    for (i = 1; i <= 700; i++)
    {
        ev = grok_voice_parser_feed_line(&p, "nope");
        expect_true("700 consumed", !may_dispatch(ev));
        expect_true("700 still framed", p.in_frame);
    }
    ev = grok_voice_parser_feed_line(&p, "wifi clear");
    expect_true("line 701 not command", ev == GROK_VOICE_EVT_HOLD);
    expect_true("line 701 not dispatch", !may_dispatch(ev));
    expect_true("line 701 still framed", p.in_frame);
    for (i = 0; i < 400; i++)
    {
        ev = grok_voice_parser_feed_line(&p, "wifi clear");
        expect_true("beyond cap consumed", !may_dispatch(ev));
    }
    expect_true("beyond cap still framed", p.in_frame);
    expect_true("beyond cap body bounded", p.body_len == 0);
    ev = grok_voice_parser_feed_line(&p, "voice close");
    expect_true("verb inside frame not dispatch", !may_dispatch(ev));
    ev = grok_voice_parser_feed_line(&p, GROK_VOICE_END);
    expect_true("end after cap drops", ev == GROK_VOICE_EVT_DROPPED);
    expect_true("end after cap not dispatch", !may_dispatch(ev));
    expect_true("frame closed only at end", !p.in_frame);
    expect_true("command after real end",
                may_dispatch(grok_voice_parser_feed_line(&p, "help")));
}

static void test_length_and_legacy_markers(void)
{
    GrokVoiceParser_t p;
    GrokVoiceEvt_t ev;
    char huge[1000];

    grok_voice_parser_init(&p);
    grok_voice_parser_feed_line(&p, GROK_VOICE_BEGIN);
    ev = grok_voice_parser_feed_line(&p, "VOICE_DATA 3 wifi clear");
    expect_true("short length held", ev == GROK_VOICE_EVT_HOLD);
    expect_true("short length not dispatch", !may_dispatch(ev));
    ev = grok_voice_parser_feed_line(&p, "VOICE_DATA 9999 wifi clear");
    expect_true("wide length held", !may_dispatch(ev));
    ev = grok_voice_parser_feed_line(&p, "VOICE_DATA +10 wifi clear");
    expect_true("plus length held", !may_dispatch(ev));
    ev = grok_voice_parser_feed_line(&p, "VOICE_DATA");
    expect_true("bare data word held", !may_dispatch(ev));
    ev = grok_voice_parser_feed_line(&p, "VOICE_DATA 0 wifi clear");
    expect_true("zero length extra held", !may_dispatch(ev));
    ev = grok_voice_parser_feed_line(&p, "wifi clear");
    expect_true("after bad lengths not dispatch", !may_dispatch(ev));
    expect_true("bad lengths still framed", p.in_frame);
    expect_true("bad lengths body empty", p.body_len == 0);
    ev = grok_voice_parser_feed_line(&p, GROK_VOICE_END);
    expect_true("bad lengths end dropped", ev == GROK_VOICE_EVT_DROPPED);

    /* Legacy raw body. A raw VOICE_END is the terminator. Text before it,
     * including wifi clear and a lookalike marker, is not a command. */
    grok_voice_parser_init(&p);
    grok_voice_parser_feed_line(&p, GROK_VOICE_BEGIN);
    expect_true("legacy text",
                !may_dispatch(grok_voice_parser_feed_line(&p, "hello VOICE_END")));
    expect_true("legacy space marker",
                !may_dispatch(grok_voice_parser_feed_line(&p, "VOICE_END ")));
    expect_true("legacy still framed", p.in_frame);
    expect_true("legacy wifi",
                !may_dispatch(grok_voice_parser_feed_line(&p, "wifi clear")));
    ev = grok_voice_parser_feed_line(&p, GROK_VOICE_END);
    expect_true("legacy exact end", ev == GROK_VOICE_EVT_DROPPED);
    expect_true("legacy closed", !p.in_frame);

    memset(huge, 'Z', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';
    grok_voice_parser_init(&p);
    grok_voice_parser_feed_line(&p, GROK_VOICE_BEGIN);
    ev = grok_voice_parser_feed_line(&p, huge);
    expect_true("1000-char held", ev == GROK_VOICE_EVT_HOLD);
    expect_true("1000-char framed", p.in_frame);
    expect_true("1000 then wifi",
                !may_dispatch(grok_voice_parser_feed_line(&p, "wifi clear")));
    expect_true("1000 then end",
                grok_voice_parser_feed_line(&p, GROK_VOICE_END) == GROK_VOICE_EVT_DROPPED);
}

static void test_limits(void)
{
    GrokVoiceParser_t p;
    char payload[GROK_VOICE_CHUNK_MAX + 1];
    char line[GROK_VOICE_LINE_MAX + 8];
    char body[GROK_REPLY_MAX + 1];
    size_t filled = 0;

    memset(payload, 'A', GROK_VOICE_CHUNK_MAX);
    payload[GROK_VOICE_CHUNK_MAX] = '\0';
    format_data(line, sizeof(line), payload);
    expect_true("chunk line is 240", strlen(line) == GROK_VOICE_LINE_MAX);

    grok_voice_parser_init(&p);
    grok_voice_parser_feed_line(&p, GROK_VOICE_BEGIN);
    expect_true("240 accepted",
                grok_voice_parser_feed_line(&p, line) == GROK_VOICE_EVT_BODY_LINE);
    expect_true("225 stored", p.body_len == GROK_VOICE_CHUNK_MAX);

    memset(line, 'C', GROK_VOICE_LINE_MAX + 1);
    line[GROK_VOICE_LINE_MAX + 1] = '\0';
    expect_true("241 held", grok_voice_parser_feed_line(&p, line) == GROK_VOICE_EVT_HOLD);
    expect_true("241 not a command",
                !may_dispatch(grok_voice_parser_feed_line(&p, "wifi clear")));
    grok_voice_parser_feed_line(&p, GROK_VOICE_END);

    grok_voice_parser_init(&p);
    grok_voice_parser_feed_line(&p, GROK_VOICE_BEGIN);
    memset(body, 'D', GROK_REPLY_MAX);
    body[GROK_REPLY_MAX] = '\0';
    while (filled < GROK_REPLY_MAX)
    {
        size_t take = GROK_REPLY_MAX - filled;

        if (take > GROK_VOICE_CHUNK_MAX)
        {
            take = GROK_VOICE_CHUNK_MAX;
        }
        memcpy(payload, body + filled, take);
        payload[take] = '\0';
        format_data(line, sizeof(line), payload);
        expect_true("640 chunk",
                    grok_voice_parser_feed_line(&p, line) == GROK_VOICE_EVT_BODY_LINE);
        filled += take;
    }
    expect_true("640 stored", p.body_len == GROK_REPLY_MAX);
    expect_true("640 matches", memcmp(p.body, body, GROK_REPLY_MAX) == 0);
    format_data(line, sizeof(line), "X");
    expect_true("641 fails closed",
                grok_voice_parser_feed_line(&p, line) == GROK_VOICE_EVT_HOLD);
    expect_true("641 wifi consumed",
                !may_dispatch(grok_voice_parser_feed_line(&p, "wifi clear")));
    expect_true("641 dropped",
                grok_voice_parser_feed_line(&p, GROK_VOICE_END) == GROK_VOICE_EVT_DROPPED);

    grok_voice_parser_init(&p);
    grok_voice_parser_feed_line(&p, GROK_VOICE_BEGIN);
    expect_true("bad length held",
                grok_voice_parser_feed_line(&p, "VOICE_DATA 4 abc") == GROK_VOICE_EVT_HOLD);
    expect_true("bad length wifi consumed",
                !may_dispatch(grok_voice_parser_feed_line(&p, "wifi clear")));
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
    test_wifi_clear_never_dispatched();
    test_length_and_legacy_markers();
    test_limits();
    test_chunk_captions();
    test_bot_line();
    expect_true("parser bss delta is small", sizeof(GrokVoiceParser_t) < 1200);
    if (g_failed)
    {
        fprintf(stderr, "%d voice host checks failed\n", g_failed);
        return 1;
    }
    printf("test_grok_voice: ok\n");
    return 0;
}
