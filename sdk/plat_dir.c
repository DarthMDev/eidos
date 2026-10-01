/*
 * sdk/plat_dir.c -- path kind and directory listing, for Win32 and for
 * POSIX (sdk/plat_dir.h says why it exists).
 *
 * Win32 uses GetFileAttributesA and FindFirstFileA/FindNextFileA, the ANSI
 * forms, because the rest of the file layer opens files with narrow fopen
 * and both must agree on what a path names.  Everything else uses stat and
 * opendir/readdir.
 */

/* stat and opendir are POSIX, not C99: ask for them under -std=c99. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <string.h>

#include "plat_dir.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

int bw_plat_path_kind(const char *path)
{
    DWORD a;

    if (path == NULL || *path == '\0') {
        return BW_PATH_NONE;
    }
    a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES) {
        return BW_PATH_NONE;
    }
    if ((a & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return BW_PATH_DIR;
    }
    if ((a & FILE_ATTRIBUTE_DEVICE) != 0) {
        return BW_PATH_OTHER;
    }
    return BW_PATH_FILE;
}

int bw_plat_list_dir(const char *path, int (*fn)(void *ctx, const char *name),
                     void *ctx)
{
    char pattern[1200];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int rc = 0;

    if (path == NULL || strlen(path) + 3 > sizeof(pattern)) {
        return -1;
    }
    sprintf(pattern, "%s/*", path);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }
        rc = fn(ctx, fd.cFileName);
    } while (rc == 0 && FindNextFileA(h, &fd));
    FindClose(h);
    return rc;
}

#else /* POSIX */

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

int bw_plat_path_kind(const char *path)
{
    struct stat st;

    if (path == NULL || *path == '\0' || stat(path, &st) != 0) {
        return BW_PATH_NONE;
    }
    if (S_ISDIR(st.st_mode)) {
        return BW_PATH_DIR;
    }
    if (S_ISREG(st.st_mode)) {
        return BW_PATH_FILE;
    }
    return BW_PATH_OTHER;
}

int bw_plat_list_dir(const char *path, int (*fn)(void *ctx, const char *name),
                     void *ctx)
{
    DIR *d;
    struct dirent *e;
    int rc = 0;

    if (path == NULL || (d = opendir(path)) == NULL) {
        return -1;
    }
    while (rc == 0 && (e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        rc = fn(ctx, e->d_name);
    }
    closedir(d);
    return rc;
}

#endif
