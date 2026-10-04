/*
 * sdk/mod_title.c -- see mod_title.h.
 */
#include "mod_title.h"

#include "mod_hooks.h"

static int g_in_settings;

int bw_mod_title_settings_on(void)
{
    return bw_hook_enabled(BW_HOOK_TITLE_SETTINGS);
}

int bw_mod_title_in_settings(void)
{
    return g_in_settings;
}

int bw_mod_title_set_in_settings(int on)
{
    g_in_settings = on != 0;
    return g_in_settings;
}
