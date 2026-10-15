#include "grok_text.h"

#include <string.h>

void grok_text_fold_ascii(char *text)
{
    char *dst;
    const char *src;

    if (text == NULL)
    {
        return;
    }

    dst = text;
    for (src = text; *src != '\0'; src++)
    {
        unsigned char c = (unsigned char)*src;

        if (c == '\n' || (c >= 0x20 && c <= 0x7e))
        {
            *dst++ = (char)c;
        }
        else if (c == '\t')
        {
            *dst++ = ' ';
        }
        else if (c == '\r')
        {
            if (dst == text || dst[-1] != '\n')
            {
                *dst++ = '\n';
            }
        }
        else
        {
            *dst++ = '?';
        }
    }
    *dst = '\0';
}

bool grok_text_json_escape(char *dst, size_t dst_cap, const char *src, size_t *written)
{
    size_t n = 0;

    if (written != NULL)
    {
        *written = 0;
    }
    if ((dst == NULL) || (dst_cap == 0))
    {
        return false;
    }
    if (src == NULL)
    {
        src = "";
    }

    for (const unsigned char *p = (const unsigned char *)src; *p != 0; p++)
    {
        char tmp[2];
        size_t add = 1;
        unsigned char c = *p;

        if ((c == '"') || (c == '\\'))
        {
            tmp[0] = '\\';
            tmp[1] = (char)c;
            add = 2;
        }
        else if ((c == '\n') || (c == '\r'))
        {
            tmp[0] = '\\';
            tmp[1] = 'n';
            add = 2;
        }
        else if (c == '\t')
        {
            tmp[0] = ' ';
        }
        else if ((c < 0x20) || (c > 0x7e))
        {
            tmp[0] = '?';
        }
        else
        {
            tmp[0] = (char)c;
        }

        if ((n + add + 1) > dst_cap)
        {
            dst[n] = '\0';
            if (written != NULL)
            {
                *written = n;
            }
            return false;
        }

        memcpy(dst + n, tmp, add);
        n += add;
    }

    dst[n] = '\0';
    if (written != NULL)
    {
        *written = n;
    }
    return true;
}

static int hex_nibble(char c)
{
    if ((c >= '0') && (c <= '9'))
    {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f'))
    {
        return c - 'a' + 10;
    }
    if ((c >= 'A') && (c <= 'F'))
    {
        return c - 'A' + 10;
    }
    return -1;
}

static bool decode_json_string(const char **cursor, char *out, size_t out_cap)
{
    const char *p = *cursor;
    size_t n = 0;

    if ((out == NULL) || (out_cap == 0))
    {
        return false;
    }
    out[0] = '\0';

    while (*p != '\0')
    {
        unsigned char produced;
        char ch = *p++;

        if (ch == '"')
        {
            out[n] = '\0';
            *cursor = p;
            return true;
        }

        if (ch == '\\')
        {
            char esc = *p++;
            if (esc == '\0')
            {
                break;
            }
            if (esc == 'n')
            {
                produced = '\n';
            }
            else if (esc == 'r')
            {
                produced = '\n';
            }
            else if (esc == 't')
            {
                produced = ' ';
            }
            else if ((esc == '"') || (esc == '\\') || (esc == '/'))
            {
                produced = (unsigned char)esc;
            }
            else if (esc == 'u')
            {
                int code = 0;
                for (int i = 0; i < 4; i++)
                {
                    int nib = hex_nibble(*p++);
                    if (nib < 0)
                    {
                        out[n] = '\0';
                        return false;
                    }
                    code = (code << 4) | nib;
                }
                produced = ((code >= 0x20) && (code <= 0x7e)) ? (unsigned char)code : '?';
            }
            else
            {
                produced = '?';
            }
        }
        else
        {
            produced = ((unsigned char)ch >= 0x20 && (unsigned char)ch <= 0x7e) ? (unsigned char)ch : '?';
        }

        if ((n + 1) >= out_cap)
        {
            continue;
        }
        out[n++] = (char)produced;
    }

    out[n] = '\0';
    return false;
}

bool grok_text_json_get_string(const char *json, const char *key, char *out, size_t out_cap)
{
    size_t key_len;

    if ((json == NULL) || (key == NULL) || (out == NULL) || (out_cap == 0))
    {
        return false;
    }
    out[0] = '\0';
    key_len = strlen(key);

    for (const char *p = json; (p = strchr(p, '"')) != NULL; )
    {
        const char *after;

        p++;
        if ((strncmp(p, key, key_len) != 0) || (p[key_len] != '"'))
        {
            continue;
        }

        after = p + key_len + 1;
        while ((*after == ' ') || (*after == '\t') || (*after == '\n') || (*after == '\r'))
        {
            after++;
        }
        if (*after != ':')
        {
            continue;
        }
        after++;
        while ((*after == ' ') || (*after == '\t') || (*after == '\n') || (*after == '\r'))
        {
            after++;
        }
        if (*after != '"')
        {
            return false;
        }
        after++;
        return decode_json_string(&after, out, out_cap);
    }

    return false;
}

static void end_line(char *line, size_t col)
{
    line[col] = '\0';
}

static void mark_ellipsis(char *line, size_t cols)
{
    if (cols >= 3)
    {
        memcpy(line + cols - 3, "...", 3);
        line[cols] = '\0';
    }
    else
    {
        line[0] = '\0';
    }
}

size_t grok_text_wrap(const char *text, char *storage, size_t line_stride,
                      size_t max_lines, size_t cols)
{
    size_t line = 0;
    size_t col = 0;
    const char *p;

    if ((storage == NULL) || (line_stride == 0) || (max_lines == 0) || (cols == 0) || (cols >= line_stride))
    {
        return 0;
    }
    if (text == NULL)
    {
        text = "";
    }

    p = text;
    storage[0] = '\0';

    while (*p != '\0')
    {
        char *cur;
        size_t word;

        if (line >= max_lines)
        {
            break;
        }

        cur = storage + (line * line_stride);

        if (*p == '\n')
        {
            end_line(cur, col);
            line++;
            col = 0;
            p++;
            if ((line < max_lines) && (*p == '\0'))
            {
                break;
            }
            continue;
        }

        if (*p == ' ')
        {
            p++;
            continue;
        }

        word = 0;
        while ((p[word] != '\0') && (p[word] != ' ') && (p[word] != '\n'))
        {
            word++;
        }

        if (word > cols)
        {
            size_t take = cols - col;
            if (take == 0)
            {
                end_line(cur, col);
                line++;
                col = 0;
                continue;
            }
            memcpy(cur + col, p, take);
            col += take;
            p += take;
            end_line(cur, col);
            line++;
            col = 0;
            continue;
        }

        if ((col > 0) && ((col + 1 + word) > cols))
        {
            end_line(cur, col);
            line++;
            col = 0;
            continue;
        }

        if (line >= max_lines)
        {
            break;
        }
        cur = storage + (line * line_stride);
        if (col > 0)
        {
            cur[col++] = ' ';
        }
        memcpy(cur + col, p, word);
        col += word;
        p += word;
    }

    if ((line < max_lines) && (col > 0))
    {
        end_line(storage + (line * line_stride), col);
        line++;
        col = 0;
    }

    if ((*p != '\0') && (line > 0))
    {
        size_t last = line - 1;
        char *cur = storage + (last * line_stride);
        size_t len = strlen(cur);
        if (len < cols)
        {
            memset(cur + len, ' ', cols - len);
        }
        mark_ellipsis(cur, cols);
    }

    (void)col;
    return line;
}
