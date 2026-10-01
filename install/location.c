/*
 * install/location.c -- the operating-system half of install/setup.c: the
 * folder each platform keeps local application data in, the folder the
 * executable runs from, and the environment and command line as UTF-8.
 *
 * Why it exists: none of the four has a standard C spelling.  The known
 * folder on Windows is a shell call (SHGetKnownFolderPath) and the wide
 * environment and command line are the only ones that carry a non-ASCII
 * user name; macOS and iOS put application data under the home folder the
 * sandbox already points at; Linux follows the XDG base directory rules;
 * Android has no answer a C library can reach (only the Java side knows
 * the app's internal storage), so the front end passes it in through
 * bw_setup_set_home.  Everything else in setup.c is plain C99.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L   /* readlink under -std=c99 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "setup.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <stdint.h>
#include <mach-o/dyld.h>
#endif

/* The folder part of a file path, in place: "a/b/c.exe" -> "a/b". */
static int strip_file(char *path)
{
    char *slash = strrchr(path, '/');
#ifdef _WIN32
    char *back = strrchr(path, '\\');
    if (back != NULL && (slash == NULL || back > slash)) {
        slash = back;
    }
#endif
    if (slash == NULL) {
        return -1;
    }
    *slash = '\0';
    return 0;
}

#ifdef _WIN32

/* ------------------------------------------------------------------ */
/* Win32                                                               */
/* ------------------------------------------------------------------ */

/* FOLDERID_LocalAppData, {F1B32785-6FBA-4FCF-9D55-7B8E7F157091}, spelled
 * here so the build needs neither uuid.lib nor INITGUID. */
static const GUID local_appdata_id = {
    0xF1B32785u, 0x6FBA, 0x4FCF, { 0x9D, 0x55, 0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91 }
};

static int narrow(const wchar_t *w, char *out, size_t n)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL);
    return len > 0 ? 0 : -1;
}

int bw_os_app_home(char *out, size_t n)
{
    PWSTR w = NULL;
    char base[BW_SETUP_PATH_MAX];
    int rc = -1;

    /* Local, not Roaming: 210 MB re-derivable from the ROM must not be
     * copied between machines at every logon. */
    if (SHGetKnownFolderPath(&local_appdata_id, 0, NULL, &w) == S_OK
        && narrow(w, base, sizeof(base)) == 0) {
        rc = snprintf(out, n, "%s\\eidos", base) < (int)n ? 0 : -1;
    }
    if (w != NULL) {
        CoTaskMemFree(w);
    }
    return rc;
}

int bw_os_exe_dir(char *out, size_t n)
{
    wchar_t w[4096];
    DWORD len = GetModuleFileNameW(NULL, w, 4096);

    if (len == 0 || len >= 4096 || narrow(w, out, n) != 0) {
        return -1;
    }
    return strip_file(out);
}

const char *bw_os_getenv(const char *name, char *buf, size_t n)
{
    wchar_t wname[128];
    wchar_t wval[4096];
    DWORD len;

    if (MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 128) == 0) {
        return NULL;
    }
    len = GetEnvironmentVariableW(wname, wval, 4096);
    if (len == 0 || len >= 4096 || narrow(wval, buf, n) != 0 || buf[0] == '\0') {
        return NULL;
    }
    return buf;
}

int bw_os_abspath(const char *path, char *out, size_t n)
{
    wchar_t w[4096];
    wchar_t full[4096];
    DWORD len;

    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 4096) == 0) {
        return -1;
    }
    len = GetFullPathNameW(w, 4096, full, NULL);
    if (len == 0 || len >= 4096) {
        return -1;
    }
    return narrow(full, out, n);
}

char **bw_os_utf8_argv(int *argc, char **argv)
{
    int wargc = 0;
    LPWSTR *w = CommandLineToArgvW(GetCommandLineW(), &wargc);
    char **out;
    int i;

    if (w == NULL) {
        return argv;
    }
    out = (char **)calloc((size_t)wargc + 1, sizeof(char *));
    if (out == NULL) {
        LocalFree(w);
        return argv;
    }
    for (i = 0; i < wargc; i++) {
        int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, NULL, 0, NULL, NULL);
        out[i] = (char *)malloc((size_t)(len > 0 ? len : 1));
        if (out[i] == NULL) {
            LocalFree(w);
            return argv;                /* the ANSI argv is better than none */
        }
        out[i][0] = '\0';
        WideCharToMultiByte(CP_UTF8, 0, w[i], -1, out[i], len, NULL, NULL);
    }
    LocalFree(w);
    *argc = wargc;
    return out;
}

#else

/* ------------------------------------------------------------------ */
/* POSIX: macOS, iOS, Linux, Android                                   */
/* ------------------------------------------------------------------ */

static int put(char *out, size_t n, const char *s)
{
    size_t len = strlen(s);

    if (len + 1 > n) {
        return -1;
    }
    memcpy(out, s, len + 1);
    return 0;
}

int bw_os_app_home(char *out, size_t n)
{
    const char *home = getenv("HOME");

#if defined(__ANDROID__)
    /* Context.getFilesDir() is a Java call: the front end passes it to
     * bw_setup_set_home (SDL_AndroidGetInternalStoragePath). */
    (void)home;
    (void)out;
    (void)n;
    return -1;
#elif defined(__APPLE__)
    /* NSApplicationSupportDirectory in the user domain.  Sandboxed (and on
     * iOS) HOME is the app container, so the same path is the container's
     * own Library/Application Support, which is what the Foundation call
     * would return. */
    if (home == NULL || home[0] == '\0') {
        return -1;
    }
    return snprintf(out, n, "%s/Library/Application Support/eidos", home) < (int)n ? 0 : -1;
#else
    /* XDG base directories: $XDG_DATA_HOME if set and absolute (the spec
     * says a relative one is to be ignored), else ~/.local/share.  Flatpak
     * and Snap set XDG_DATA_HOME to their own per-app folder. */
    const char *xdg = getenv("XDG_DATA_HOME");

    if (xdg != NULL && xdg[0] == '/') {
        return snprintf(out, n, "%s/eidos", xdg) < (int)n ? 0 : -1;
    }
    if (home == NULL || home[0] == '\0') {
        return -1;
    }
    return snprintf(out, n, "%s/.local/share/eidos", home) < (int)n ? 0 : -1;
#endif
}

int bw_os_exe_dir(char *out, size_t n)
{
#if defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);

    if (_NSGetExecutablePath(buf, &size) != 0 || put(out, n, buf) != 0) {
        return -1;
    }
    return strip_file(out);
#elif defined(__linux__)
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);

    if (len <= 0) {
        return -1;
    }
    buf[len] = '\0';
    if (put(out, n, buf) != 0) {
        return -1;
    }
    return strip_file(out);
#else
    (void)out;
    (void)n;
    (void)strip_file;
    return -1;                          /* no portable mode here */
#endif
}

const char *bw_os_getenv(const char *name, char *buf, size_t n)
{
    const char *v = getenv(name);

    if (v == NULL || v[0] == '\0' || put(buf, n, v) != 0) {
        return NULL;
    }
    return buf;
}

int bw_os_abspath(const char *path, char *out, size_t n)
{
    char cwd[4096];

    if (path[0] == '/') {
        return put(out, n, path);
    }
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return -1;
    }
    return snprintf(out, n, "%s/%s", cwd, path) < (int)n ? 0 : -1;
}

char **bw_os_utf8_argv(int *argc, char **argv)
{
    (void)argc;
    return argv;                        /* already the bytes of the names */
}

#endif
