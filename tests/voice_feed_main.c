/* Feed stdin lines to the device voice parser. One event per line.
 * UNTESTED ON HARDWARE. Host regression driver only.
 */
#include "grok_voice_util.h"

#include <stdio.h>
#include <string.h>

static void print_body(const char *s)
{
    const unsigned char *p = (const unsigned char *)((s != NULL) ? s : "");

    for (; *p != '\0'; p++)
    {
        if (*p == '\\')
        {
            fputs("\\\\", stdout);
        }
        else if (*p == '\n')
        {
            fputs("\\n", stdout);
        }
        else if (*p == '\t')
        {
            fputs("\\t", stdout);
        }
        else
        {
            fputc((int)*p, stdout);
        }
    }
}

static const char *ev_name(GrokVoiceEvt_t ev)
{
    switch (ev)
    {
    case GROK_VOICE_EVT_NONE: return "NONE";
    case GROK_VOICE_EVT_LINE: return "LINE";
    case GROK_VOICE_EVT_BEGIN: return "BEGIN";
    case GROK_VOICE_EVT_BODY_LINE: return "BODY";
    case GROK_VOICE_EVT_END: return "END";
    case GROK_VOICE_EVT_HOLD: return "HOLD";
    case GROK_VOICE_EVT_DROPPED: return "DROPPED";
    case GROK_VOICE_EVT_OVERFLOW: return "OVERFLOW";
    default: return "OTHER";
    }
}

int main(void)
{
    GrokVoiceParser_t p;
    char buf[1200];

    grok_voice_parser_init(&p);
    while (fgets(buf, (int)sizeof(buf), stdin) != NULL)
    {
        size_t n = strlen(buf);
        GrokVoiceEvt_t ev;

        while ((n > 0) && ((buf[n - 1] == '\n') || (buf[n - 1] == '\r')))
        {
            buf[--n] = '\0';
        }
        ev = grok_voice_parser_feed_line(&p, buf);
        printf("%s\t%u\t", ev_name(ev), p.in_frame ? 1u : 0u);
        print_body(p.body);
        fputc('\n', stdout);
    }
    return 0;
}
