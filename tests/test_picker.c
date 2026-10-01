/*
 * tests/test_picker.c -- the native file dialog (install/picker.c), opened
 * for real and answered by a helper thread, on Windows.
 *
 * It puts a window on the desktop, so it runs only when asked:
 * EIDOS_TEST_DIALOGS=1.  Otherwise, and on every platform but Windows (the
 * other dialogs cannot be driven from inside the process), it reports
 * itself skipped.  Two runs:
 *
 *   1. the helper finds the dialog, types the full path of a file named
 *      outside ASCII into its file name box and presses Open: the picker
 *      returns BW_PICK_OK and that path, as UTF-8;
 *   2. the helper closes the dialog: BW_PICK_CANCELLED.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

#include "port_test.h"
#include "picker.h"
#include "setup.h"

static volatile LONG g_mode;            /* 1 type and open, 2 close */
static wchar_t g_wpath[1024];

static BOOL CALLBACK find_dialog(HWND h, LPARAM out)
{
    DWORD pid = 0;
    wchar_t cls[64];

    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h)
        && GetClassNameW(h, cls, 64) > 0 && wcscmp(cls, L"#32770") == 0) {
        *(HWND *)out = h;
        return FALSE;
    }
    return TRUE;
}

/* The file name box: IFileDialog's ComboBoxEx32 (id 1148) holds a ComboBox
 * holding the Edit; the old dialog's is the Edit 1152. */
static HWND name_box(HWND dlg)
{
    HWND cbx = GetDlgItem(dlg, 1148);
    HWND e;

    if (cbx != NULL) {
        HWND combo = FindWindowExW(cbx, NULL, L"ComboBox", NULL);
        e = FindWindowExW(combo != NULL ? combo : cbx, NULL, L"Edit", NULL);
        if (e != NULL) {
            return e;
        }
        if (combo == NULL) {
            return cbx;
        }
    }
    return GetDlgItem(dlg, 1152);
}

static DWORD WINAPI helper(LPVOID arg)
{
    HWND dlg = NULL;
    int tries;

    (void)arg;
    for (tries = 0; tries < 200 && dlg == NULL; tries++) {
        Sleep(100);
        EnumWindows(find_dialog, (LPARAM)&dlg);
    }
    if (dlg == NULL) {
        return 1;
    }
    Sleep(300);                         /* let it finish laying out */
    if (g_mode == 1) {
        HWND e = name_box(dlg);
        if (e == NULL) {
            PostMessageW(dlg, WM_CLOSE, 0, 0);
            return 1;
        }
        SendMessageW(e, WM_SETTEXT, 0, (LPARAM)g_wpath);
        PostMessageW(dlg, WM_COMMAND, IDOK, 0);
    } else {
        PostMessageW(dlg, WM_CLOSE, 0, 0);
    }
    return 0;
}

static void lower(char *s)
{
    for (; *s != '\0'; s++) {
        if (*s >= 'A' && *s <= 'Z') {
            *s = (char)(*s - 'A' + 'a');
        } else if (*s == '/') {
            *s = '\\';
        }
    }
}

int main(int argc, char **argv)
{
    char dir[600], file[700], full[1024], out[1024];
    HANDLE t;
    FILE *fp;
    int rc;

    if (getenv("EIDOS_TEST_DIALOGS") == NULL || argc < 2) {
        printf("SKIPPED: set EIDOS_TEST_DIALOGS=1 to open the file dialog on the desktop\n");
        return 0;
    }
    snprintf(dir, sizeof(dir), "%s", argv[1]);
    bw_remove_tree_utf8(dir);
    BW_REQUIRE("work dir", bw_mkdirs_utf8(dir) == 0);
    snprintf(file, sizeof(file), "%s/pick me \xc3\xa9.nds", dir);
    fp = bw_fopen_utf8(file, "wb");
    BW_REQUIRE("the file to pick", fp != NULL);
    fputs("not a ROM", fp);
    fclose(fp);
    BW_REQUIRE("its full path", bw_os_abspath(file, full, sizeof(full)) == 0);
    MultiByteToWideChar(CP_UTF8, 0, full, -1, g_wpath, 1024);
    {
        /* Back slashes for the dialog's name box. */
        wchar_t *c;
        for (c = g_wpath; *c != 0; c++) {
            if (*c == L'/') {
                *c = L'\\';
            }
        }
    }

    g_mode = 1;
    t = CreateThread(NULL, 0, helper, NULL, 0, NULL);
    rc = bw_pick_rom(NULL, "eidos test: choose the file", out, sizeof(out));
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    BW_CHECK_EQ("typed and opened", rc, BW_PICK_OK);
    if (rc == BW_PICK_OK) {
        lower(out);
        lower(full);
        printf("  picked %s\n", out);
        BW_CHECK_EQ("the path, as UTF-8", strcmp(out, full), 0);
    }

    g_mode = 2;
    t = CreateThread(NULL, 0, helper, NULL, 0, NULL);
    rc = bw_pick_rom(NULL, "eidos test: this one is closed", out, sizeof(out));
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    BW_CHECK_EQ("closed", rc, BW_PICK_CANCELLED);

    if (bw_failures == 0) {
        bw_remove_tree_utf8(dir);
    }
    BW_REPORT("test_picker");
}

#else

int main(void)
{
    printf("SKIPPED: the dialog can only be driven from inside the process on Windows\n");
    return 0;
}

#endif
