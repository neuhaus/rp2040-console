/*
 * Ask for one environment variable before the program starts.
 *
 * Some programs take the player's name from the environment and have no
 * prompt of their own: Rogue uses the login name and quits without one.
 * With ASK_ENV in the manifest, the engine asks for that variable on the
 * console - at every boot, and at the start of every soft-reboot session -
 * and sets it for the program.
 *
 * The default is the last answer, kept in a dot-file on the volume, or else
 * the manifest's own ENV value. Enter takes it, so a returning player only
 * presses a key; that matters because programs such as Rogue only restore a
 * save for the name that made it. Without a default the question repeats
 * until something is typed.
 *
 * Programs with a name prompt of their own (Hack, NetHack) do not need this,
 * and their manifests leave USER and LOGNAME unset so that they ask.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ask_env.h"
#include "console.h"
#include "storage.h"

#ifdef RP2040_CONSOLE_ASK_ENV

#ifndef RP2040_CONSOLE_ASK_PROMPT
#define RP2040_CONSOLE_ASK_PROMPT "Who are you?"
#endif

/* Same patience as the date prompt: an unattended board takes the default. */
#define ask_first_ms 15000u
#define ask_char_ms  10000u

static const char ask_file[] = "/.askenv";

/* The remembered answer: one line, "NAME=value". Empty if none. */
static void recall(char *value, size_t cap)
{
    char line[64];
    const size_t name_len = sizeof(RP2040_CONSOLE_ASK_ENV) - 1;
    int fd, n;

    value[0] = 0;
    if (!storage_is_available())
        return;
    fd = open(ask_file, O_RDONLY);
    if (fd < 0)
        return;
    n = read(fd, line, sizeof(line) - 1);
    close(fd);
    if (n <= 0)
        return;
    line[n] = 0;
    line[strcspn(line, "\r\n")] = 0;
    if (strncmp(line, RP2040_CONSOLE_ASK_ENV "=", name_len + 1) == 0) {
        strncpy(value, line + name_len + 1, cap - 1);
        value[cap - 1] = 0;
    }
}

static void remember(const char *value)
{
    char line[64];
    int fd, len;

    if (!storage_is_available())
        return;
    len = snprintf(line, sizeof(line), "%s=%s\n", RP2040_CONSOLE_ASK_ENV, value);
    if (len <= 0 || (size_t)len >= sizeof(line))
        return;
    fd = open(ask_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;
    (void)write(fd, line, (size_t)len);
    close(fd);
}

/* Leading and trailing blanks are not part of a name. */
static char *trim(char *s)
{
    size_t n;

    while (*s == ' ')
        s++;
    n = strlen(s);
    while (n > 0 && s[n - 1] == ' ')
        s[--n] = 0;
    return s;
}

void console_ask_env(void)
{
    char remembered[ASK_ENV_MAX + 1], line[ASK_ENV_MAX + 1];
    const char *deflt, *answer;

    recall(remembered, sizeof(remembered));
    deflt = remembered[0] ? remembered : getenv(RP2040_CONSOLE_ASK_ENV);
    if (deflt != NULL && *deflt == 0)
        deflt = NULL;

    for (;;) {
        if (deflt != NULL)
            printf("\n%s [%s] ", RP2040_CONSOLE_ASK_PROMPT, deflt);
        else
            printf("\n%s ", RP2040_CONSOLE_ASK_PROMPT);
        fflush(stdout);
        console_read_line(line, sizeof(line), ask_first_ms, ask_char_ms);
        answer = trim(line);
        if (*answer == 0)
            answer = deflt;
        if (answer != NULL)
            break;
    }
    setenv(RP2040_CONSOLE_ASK_ENV, answer, 1);
    if (strcmp(answer, remembered) != 0)
        remember(answer);
}

#else

void console_ask_env(void)
{
}

#endif /* RP2040_CONSOLE_ASK_ENV */
