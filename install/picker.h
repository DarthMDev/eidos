/*
 * install/picker.h -- the platform's own "open file" dialog, for choosing
 * the ROM.
 *
 * SDL3's SDL_ShowOpenFileDialog answers on a callback, after the call
 * returns, and a dialog drawn by eidos itself would know nothing of the
 * player's bookmarks, cloud folders or sandbox grants.  So each platform's
 * native picker is called here, behind one blocking function:
 *
 *   Windows  IFileOpenDialog (COM), falling back to GetOpenFileNameW
 *   macOS    NSOpenPanel (install/picker_mac.m, compiled only on Apple)
 *   Linux    the XDG desktop portal's FileChooser over D-Bus (libdbus-1,
 *            loaded at run time so the build does not need it), then
 *            zenity, then kdialog
 *   Android, iOS: not yet (BW_PICK_UNAVAILABLE); the plan is in
 *            docs/installer.md
 *
 * Every platform also has drag and drop onto the window (SDL_EVENT_DROP_FILE),
 * which the wizard offers beside the button, and the --rom option; so
 * BW_PICK_UNAVAILABLE is a message, never a dead end.
 *
 * The dialog is modal and the call blocks until the player has chosen.
 * It is filtered to *.nds but offers "All files", since dumps arrive with
 * other extensions.  Plain C on every platform but macOS; no SDL.
 */

#ifndef EIDOS_PICKER_H
#define EIDOS_PICKER_H

#include <stddef.h>

enum {
    BW_PICK_OK = 0,         /* `out` holds the chosen file's UTF-8 path */
    BW_PICK_CANCELLED,      /* the player closed the dialog */
    BW_PICK_UNAVAILABLE     /* no dialog on this system; `out` says why */
};

/*
 * Show the dialog.  `parent` is the native window to be modal over (an
 * HWND on Windows, an NSWindow * on macOS; ignored elsewhere), or NULL.
 * `title` is UTF-8.
 */
int bw_pick_rom(void *parent, const char *title, char *out, size_t n);

#endif /* EIDOS_PICKER_H */
