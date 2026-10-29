/* Host checks for Grok Bot webhook JSON + poll state machine.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_bot_proto.h"
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

static void test_json(void)
{
    char body[256];
    GrokBotReply_t reply;
    char row[32];

    expect_true("build", grok_bot_build_post_json(body, sizeof(body), "Hello?",
                                                  "aabbccddeeff", "1-2"));
    expect_true("has text", strstr(body, "\"text\":\"Hello?\"") != NULL);
    expect_true("has device", strstr(body, "\"device_id\":\"aabbccddeeff\"") != NULL);
    expect_true("has msg", strstr(body, "\"msg_id\":\"1-2\"") != NULL);

    expect_true("parse", grok_bot_parse_reply_json(
                             "{\"reply\":\"Ship it.\",\"suggestions\":[\"Open PR\","
                             "\"Add tests\",\"Wait\"]}",
                             &reply));
    expect_true("reply", strcmp(reply.reply, "Ship it.") == 0);
    expect_true("sug n", reply.suggest_n == 3);
    expect_true("sug0", strcmp(reply.suggest[0], "Open PR") == 0);
    expect_true("sug1", strcmp(reply.suggest[1], "Add tests") == 0);
    expect_true("sug2", strcmp(reply.suggest[2], "Wait") == 0);

    grok_bot_format_option(row, sizeof(row), 0, true, &reply, GROK_TEXT_COLS);
    expect_true("opt0", strstr(row, ">1 Open PR") != NULL);
    grok_bot_format_option(row, sizeof(row), 3, false, &reply, GROK_TEXT_COLS);
    expect_true("wait", strstr(row, "Wait / do nothing") != NULL);

    expect_true("no reply", !grok_bot_parse_reply_json("{\"suggestions\":[]}", &reply));
    expect_true("escape build",
                grok_bot_build_post_json(body, sizeof(body), "say \"hi\"", "d", "m"));
    expect_true("escaped", strstr(body, "say \\\"hi\\\"") != NULL);
}

static void test_poll(void)
{
    GrokBotPoll_t p;
    char path[80];
    char masked[24];

    grok_bot_poll_begin(&p, 1000, 5000, 20000);
    expect_true("first", grok_bot_poll_tick(&p, 1000) == GROK_BOT_POLL_DO_GET);
    expect_true("idle", grok_bot_poll_tick(&p, 2000) == GROK_BOT_POLL_IDLE);
    expect_true("again", grok_bot_poll_tick(&p, 6000) == GROK_BOT_POLL_DO_GET);
    expect_true("timeout", grok_bot_poll_tick(&p, 25000) == GROK_BOT_POLL_TIMEOUT);
    expect_true("done", grok_bot_poll_tick(&p, 26000) == GROK_BOT_POLL_IDLE);

    expect_true("path", grok_bot_build_poll_path(path, sizeof(path), "abc", "9"));
    expect_true("path val", strcmp(path, "/v1/reply?device_id=abc&msg_id=9") == 0);

    grok_bot_mask_secret(masked, sizeof(masked), NULL);
    expect_true("mask unset", strcmp(masked, "(unset)") == 0);
    grok_bot_mask_secret(masked, sizeof(masked), "short");
    expect_true("mask short", strcmp(masked, "****") == 0);
    grok_bot_mask_secret(masked, sizeof(masked), "abcdefghijklmnop");
    expect_true("mask long", strncmp(masked, "abcd", 4) == 0);
}

int main(void)
{
    test_json();
    test_poll();
    if (g_failed != 0)
    {
        fprintf(stderr, "%d failed\n", g_failed);
        return 1;
    }
    printf("grok_bot_proto ok\n");
    return 0;
}
