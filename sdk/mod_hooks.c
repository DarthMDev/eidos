/*
 * sdk/mod_hooks.c -- see mod_hooks.h.
 */
#include "mod_hooks.h"

#include <stdlib.h>
#include <string.h>

#define HOOK_MAX 32
#define HOOK_NAME_MAX 48

static struct {
    char name[HOOK_NAME_MAX];
    int on;
} g_set[HOOK_MAX];
static int g_set_n;

static int env_lists(const char *name)
{
    const char *e = getenv("EIDOS_HOOKS_OFF");
    size_t n = strlen(name);
    while (e && *e) {
        size_t len = strcspn(e, ",");
        if ((len == n && memcmp(e, name, n) == 0) || (len == 3 && memcmp(e, "all", 3) == 0))
            return 1;
        e += len;
        if (*e == ',')
            e++;
    }
    return 0;
}

void bw_hook_set(const char *name, int on)
{
    int i;
    for (i = 0; i < g_set_n; i++) {
        if (strcmp(g_set[i].name, name) == 0) {
            g_set[i].on = on != 0;
            return;
        }
    }
    if (g_set_n < HOOK_MAX && strlen(name) < HOOK_NAME_MAX) {
        strcpy(g_set[g_set_n].name, name);
        g_set[g_set_n].on = on != 0;
        g_set_n++;
    }
}

int bw_hook_enabled(const char *name)
{
    int i;
    for (i = 0; i < g_set_n; i++)
        if (strcmp(g_set[i].name, name) == 0)
            return g_set[i].on;
    return !env_lists(name);
}
