/*
 * The environment a game sees.
 *
 * These definitions replace newlib's at link time: object files are searched
 * before the C library archive, so a program's getenv() and setenv() calls
 * land here without needing the linker to be told anything.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "env.h"

#define ENV_MAX   12
#define ENV_VALUE 64

struct env_slot
{
    const char *name;
    char value[ENV_VALUE];
};

static struct env_slot slots[ENV_MAX];
static int slots_used;

/*
 * One `{"NAME", "value"},` line per entry the game declared in its
 * manifest. Generated into the build directory by the engine's CMake, so a
 * game changes its environment by editing its own manifest and nothing in
 * the engine.
 */
static const struct
{
    const char *name;
    const char *value;
} declared[] = {
#include "rp2040_console_env.inc"
    { NULL, NULL }
};

static struct env_slot *lookup(const char *name)
{
    for (int i = 0; i < slots_used; i++) {
        if (strcmp(slots[i].name, name) == 0)
            return &slots[i];
    }

    return NULL;
}

char *getenv(const char *name)
{
    if (name == NULL)
        return NULL;

    struct env_slot *s = lookup(name);

    return s ? s->value : NULL;
}

int setenv(const char *name, const char *value, int overwrite)
{
    if (name == NULL || value == NULL || *name == 0 || strchr(name, '='))
        return -1;

    struct env_slot *s = lookup(name);
    int added = 0;

    if (s != NULL) {
        if (!overwrite)
            return 0;
    } else {
        if (slots_used == ENV_MAX)
            return -1; /* declared the environment full rather than drop it */
        s = &slots[slots_used++];
        s->name = name; /* names are literals from the table or the caller */
        added = 1;
    }

    /*
     * Refuse rather than truncate: a silently shortened data directory or TERM
     * is a far more confusing failure than a variable that is simply absent.
     */
    if (strlen(value) >= ENV_VALUE) {
        if (added)
            slots_used--;
        return -1;
    }

    strcpy(s->value, value);
    return 0;
}

int unsetenv(const char *name)
{
    if (name == NULL)
        return -1;

    for (int i = 0; i < slots_used; i++) {
        if (strcmp(slots[i].name, name) == 0) {
            slots[i] = slots[slots_used - 1];
            slots_used--;
            break;
        }
    }

    return 0;
}

void env_init(void)
{
    for (int i = 0; declared[i].name != NULL; i++)
        setenv(declared[i].name, declared[i].value, 1);
}
