/* Host checks for the portable Grok text helpers. Not part of the ESP-IDF build.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
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

static void expect_line(const char *name, const char *storage, size_t stride, size_t index, const char *want)
{
    const char *got = storage + (index * stride);
    if (strcmp(got, want) != 0)
    {
        fprintf(stderr, "FAIL %s line %zu got \"%s\" want \"%s\"\n", name, index, got, want);
        g_failed++;
    }
}

static void test_wrap(void)
{
    char storage[8][8];
    size_t n;

    memset(storage, 0x5a, sizeof(storage));
    n = grok_text_wrap("hi", &storage[0][0], 8, 8, 5);
    expect_true("hi count", n == 1);
    expect_line("hi", &storage[0][0], 8, 0, "hi");

    memset(storage, 0, sizeof(storage));
    n = grok_text_wrap("hello world", &storage[0][0], 8, 8, 5);
    expect_true("hello count", n == 2);
    expect_line("hello", &storage[0][0], 8, 0, "hello");
    expect_line("world", &storage[0][0], 8, 1, "world");

    memset(storage, 0, sizeof(storage));
    n = grok_text_wrap("abcdef", &storage[0][0], 8, 8, 3);
    expect_true("abcdef count", n == 2);
    expect_line("abc", &storage[0][0], 8, 0, "abc");
    expect_line("def", &storage[0][0], 8, 1, "def");

    memset(storage, 0, sizeof(storage));
    n = grok_text_wrap("ab cd", &storage[0][0], 8, 8, 5);
    expect_true("ab cd count", n == 1);
    expect_line("ab cd", &storage[0][0], 8, 0, "ab cd");

    memset(storage, 0, sizeof(storage));
    n = grok_text_wrap("a  b", &storage[0][0], 8, 8, 5);
    expect_true("spaces count", n == 1);
    expect_line("spaces", &storage[0][0], 8, 0, "a b");

    n = grok_text_wrap("", &storage[0][0], 8, 8, 5);
    expect_true("empty", n == 0);

    memset(storage, 0, sizeof(storage));
    n = grok_text_wrap("a\nb", &storage[0][0], 8, 8, 5);
    expect_true("newline count", n == 2);
    expect_line("nl a", &storage[0][0], 8, 0, "a");
    expect_line("nl b", &storage[0][0], 8, 1, "b");

    memset(storage, 0, sizeof(storage));
    n = grok_text_wrap("hello world", &storage[0][0], 8, 1, 5);
    expect_true("ellipsis count", n == 1);
    expect_true("ellipsis tail", strcmp(storage[0] + 2, "...") == 0);
}

static void test_json(void)
{
    char dst[64];
    char out[64];
    size_t n = 0;

    expect_true("escape", grok_text_json_escape(dst, sizeof(dst), "say \"hi\"", &n));
    expect_true("escape text", strcmp(dst, "say \\\"hi\\\"") == 0);

    expect_true("reply", grok_text_json_get_string("{\"reply\":\"a\\nb\"}", "reply", out, sizeof(out)));
    expect_true("reply text", strcmp(out, "a\nb") == 0);

    expect_true("missing", !grok_text_json_get_string("{\"reply\":\"x\"}", "nope", out, sizeof(out)));
    expect_true("error", grok_text_json_get_string("{\"error\":\"nope\"}", "error", out, sizeof(out)));
    expect_true("error text", strcmp(out, "nope") == 0);
}

static void test_fold(void)
{
    char text[8];

    memcpy(text, "caf\xc3", 5);
    grok_text_fold_ascii(text);
    expect_true("fold", strcmp(text, "caf?") == 0);
}

int main(void)
{
    test_wrap();
    test_json();
    test_fold();
    if (g_failed != 0)
    {
        fprintf(stderr, "%d failed\n", g_failed);
        return 1;
    }
    printf("grok_text ok\n");
    return 0;
}
