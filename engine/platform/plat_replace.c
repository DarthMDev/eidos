/*
 * engine/platform/plat_replace.c -- replace a file atomically.
 *
 * The one OS-specific piece of the settings file's save: write a temporary
 * sibling, then move it over the real file so a crash never leaves half a
 * settings.ini.  rename() does this on POSIX; Windows' rename() refuses an
 * existing target, so MoveFileEx with MOVEFILE_REPLACE_EXISTING is used.
 */

#include <stdio.h>

#include "settings.h"

#ifdef _WIN32
#include <windows.h>

int eidos_replace_file(const char *from, const char *to)
{
    wchar_t wf[1024];
    wchar_t wt[1024];

    if (MultiByteToWideChar(CP_UTF8, 0, from, -1, wf, 1024) <= 0 ||
        MultiByteToWideChar(CP_UTF8, 0, to, -1, wt, 1024) <= 0) {
        return -1;
    }
    return MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING |
                       MOVEFILE_WRITE_THROUGH) ? 0 : -1;
}
#else
int eidos_replace_file(const char *from, const char *to)
{
    return rename(from, to) == 0 ? 0 : -1;
}
#endif
