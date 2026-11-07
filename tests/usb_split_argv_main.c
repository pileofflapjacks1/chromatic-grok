/* Host driver around the pinned ESP-IDF v5.3 splitter.
 * argv: usb_split_argv <usb|ble> <console-line>
 * Prints a JSON array of arguments. "usb" calls esp_console_split_argv.
 * "ble" calls a verbatim copy of split_args from components/grok/grok.c
 * (the BLE grok_exec_line parser). That copy is only a transport check.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t esp_console_split_argv(char *line, char **argv, size_t argv_size);

/* components/grok/grok.c split_args, max 16 in grok_exec_line. */
static int split_args(char *line, char **argv, int max)
{
    int argc = 0;
    char *p = line;

    while ((*p != '\0') && (argc < max))
    {
        while ((*p == ' ') || (*p == '\t'))
        {
            p++;
        }
        if (*p == '\0')
        {
            break;
        }
        if (argc == (max - 1))
        {
            if (*p == '"')
            {
                p++;
                argv[argc++] = p;
                while ((*p != '\0') && (*p != '"'))
                {
                    p++;
                }
                if (*p == '"')
                {
                    *p = '\0';
                }
            }
            else
            {
                argv[argc++] = p;
            }
            break;
        }
        if (*p == '"')
        {
            p++;
            argv[argc++] = p;
            while ((*p != '\0') && (*p != '"'))
            {
                p++;
            }
            if (*p == '"')
            {
                *p++ = '\0';
            }
        }
        else
        {
            argv[argc++] = p;
            while ((*p != '\0') && (*p != ' ') && (*p != '\t'))
            {
                p++;
            }
            if (*p != '\0')
            {
                *p++ = '\0';
            }
        }
    }
    return argc;
}

static void json_string(const char *s)
{
    const unsigned char *p;

    putchar('"');
    for (p = (const unsigned char *)((s != NULL) ? s : ""); *p != '\0'; p++)
    {
        if ((*p == '"') || (*p == '\\'))
        {
            putchar('\\');
            putchar((int)*p);
        }
        else if (*p < 0x20)
        {
            printf("\\u%04x", (unsigned)*p);
        }
        else
        {
            putchar((int)*p);
        }
    }
    putchar('"');
}

int main(int argc, char **argv)
{
    char *line;
    char *parsed[32];
    size_t n = 0;
    size_t i;

    if (argc != 3)
    {
        fprintf(stderr, "usage: usb_split_argv <usb|ble> <line>\n");
        return 2;
    }
    line = strdup(argv[2]);
    if (line == NULL)
    {
        return 1;
    }
    if (strcmp(argv[1], "usb") == 0)
    {
        n = esp_console_split_argv(line, parsed, 32);
    }
    else if (strcmp(argv[1], "ble") == 0)
    {
        n = (size_t)split_args(line, parsed, 16);
    }
    else
    {
        fprintf(stderr, "parser must be usb or ble\n");
        free(line);
        return 2;
    }
    putchar('[');
    for (i = 0; i < n; i++)
    {
        if (i > 0)
        {
            putchar(',');
        }
        json_string(parsed[i]);
    }
    putchar(']');
    putchar('\n');
    free(line);
    return 0;
}
