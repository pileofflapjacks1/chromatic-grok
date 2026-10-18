/* Host tests for components/grok/grok_web_util.c (phone page helpers).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "grok_web_util.h"

#include <stdio.h>
#include <string.h>

static int s_fail;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            s_fail++;                                                  \
        }                                                              \
    } while (0)

static void test_pin(void)
{
    char pin[GROK_WEB_PIN_LEN + 1];

    grok_web_pin_format(0, pin);
    CHECK(strcmp(pin, "000000") == 0);
    grok_web_pin_format(42, pin);
    CHECK(strcmp(pin, "000042") == 0);
    grok_web_pin_format(4294967295u, pin);
    CHECK(strcmp(pin, "967295") == 0);

    CHECK(grok_web_pin_equal("123456", "123456"));
    CHECK(!grok_web_pin_equal("123456", "123457"));
    CHECK(!grok_web_pin_equal("123456", "12345"));
    CHECK(!grok_web_pin_equal("123456", "1234567"));
    CHECK(!grok_web_pin_equal("123456", ""));
    CHECK(!grok_web_pin_equal("123456", NULL));
    CHECK(!grok_web_pin_equal(NULL, "123456"));
    CHECK(!grok_web_pin_equal("", ""));
}

static void test_lockout(void)
{
    GrokWebGuard_t g;
    int64_t t = 1000;

    memset(&g, 0, sizeof(g));
    CHECK(grok_web_guard_check(&g, t, "111111", "111111") == GROK_WEB_PIN_OK);
    for (int i = 0; i < 4; i++)
    {
        CHECK(grok_web_guard_check(&g, t, "111111", "222222") == GROK_WEB_PIN_BAD);
    }
    /* A right PIN resets the count. */
    CHECK(grok_web_guard_check(&g, t, "111111", "111111") == GROK_WEB_PIN_OK);
    for (int i = 0; i < 4; i++)
    {
        CHECK(grok_web_guard_check(&g, t, "111111", "") == GROK_WEB_PIN_BAD);
    }
    CHECK(grok_web_guard_check(&g, t, "111111", "000000") == GROK_WEB_PIN_LOCKED);
    CHECK(grok_web_guard_wait_s(&g, t) == 60);
    /* Locked: even the right PIN is refused. */
    CHECK(grok_web_guard_check(&g, t + 59999, "111111", "111111") == GROK_WEB_PIN_LOCKED);
    CHECK(grok_web_guard_wait_s(&g, t + 59001) == 1);
    CHECK(grok_web_guard_wait_s(&g, t + 60000) == 0);
    CHECK(grok_web_guard_check(&g, t + 60000, "111111", "111111") == GROK_WEB_PIN_OK);
    CHECK(grok_web_guard_check(NULL, t, "111111", "111111") == GROK_WEB_PIN_BAD);
}

static void test_form(void)
{
    char out[16];
    const char *f = "pin=012345&q=hi+there%21&x=";

    CHECK(grok_web_form_get(f, strlen(f), "pin", out, sizeof(out)) && (strcmp(out, "012345") == 0));
    CHECK(grok_web_form_get(f, strlen(f), "q", out, sizeof(out)) && (strcmp(out, "hi there!") == 0));
    CHECK(grok_web_form_get(f, strlen(f), "x", out, sizeof(out)) && (out[0] == '\0'));
    CHECK(!grok_web_form_get(f, strlen(f), "p", out, sizeof(out)));
    CHECK(!grok_web_form_get(f, strlen(f), "missing", out, sizeof(out)));
    /* Length limit: 15 chars fit in 16, 16 do not. */
    CHECK(grok_web_form_get("q=123456789012345", 17, "q", out, sizeof(out)));
    CHECK(!grok_web_form_get("q=1234567890123456", 18, "q", out, sizeof(out)) && (out[0] == '\0'));
    /* Bad escapes. */
    CHECK(!grok_web_form_get("q=a%2", 5, "q", out, sizeof(out)));
    CHECK(!grok_web_form_get("q=a%zz", 6, "q", out, sizeof(out)));
    CHECK(grok_web_form_get("q=%41%62", 8, "q", out, sizeof(out)) && (strcmp(out, "Ab") == 0));
    /* form_len bounds the scan; no NUL needed. */
    CHECK(grok_web_form_get("q=abcdef", 4, "q", out, sizeof(out)) && (strcmp(out, "ab") == 0));
    CHECK(!grok_web_form_get(NULL, 0, "q", out, sizeof(out)));
}

static void test_sanitize(void)
{
    char a[] = "  hi\r\n\tthere  ";
    char b[] = "caf\xc3\xa9 \x01ok\x7f";
    char c[] = " \n\t ";

    CHECK(grok_web_sanitize(a) == 8);
    CHECK(strcmp(a, "hi there") == 0);
    CHECK(grok_web_sanitize(b) == 6);
    CHECK(strcmp(b, "caf ok") == 0);
    CHECK(grok_web_sanitize(c) == 0);
    CHECK(c[0] == '\0');
    CHECK(grok_web_sanitize(NULL) == 0);
}

int main(void)
{
    test_pin();
    test_lockout();
    test_form();
    test_sanitize();
    if (s_fail != 0)
    {
        printf("grok_web: %d failed\n", s_fail);
        return 1;
    }
    printf("grok_web: ok\n");
    return 0;
}
