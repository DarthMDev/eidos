/*
 * install/picker_mac.m -- NSOpenPanel for install/picker.c, compiled only
 * on macOS (CMakeLists.txt enables Objective-C there and nowhere else).
 *
 * Why Objective-C: AppKit has no C interface, and calling it through the
 * Objective-C runtime from C (objc_msgSend casts) is longer and less safe
 * than these few lines.  Written without ARC, so it builds with the
 * compiler's defaults.
 *
 * When the app is sandboxed, the path of a file the player chose in the
 * panel stays readable for the life of the process (the
 * com.apple.security.files.user-selected.read-only entitlement), which is
 * all the installer needs: it reads the ROM once.  The panel is not
 * restricted to .nds (dumps arrive with other extensions).
 */

#import <Cocoa/Cocoa.h>

#include <string.h>

#include "picker.h"

int bw_pick_rom_mac(void *parent, const char *title, char *out, size_t n);

int bw_pick_rom_mac(void *parent, const char *title, char *out, size_t n)
{
    int rc = BW_PICK_CANCELLED;

    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        NSModalResponse r;

        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        [panel setMessage:[NSString stringWithUTF8String:title]];
        r = [panel runModal];
        if (r == NSModalResponseOK && [[panel URLs] count] > 0) {
            const char *p = [[[panel URLs] objectAtIndex:0] fileSystemRepresentation];
            size_t len = p != NULL ? strlen(p) : 0;
            if (len > 0 && len < n) {
                memcpy(out, p, len + 1);
                rc = BW_PICK_OK;
            } else {
                rc = BW_PICK_UNAVAILABLE;
            }
        }
        if (parent != NULL) {
            [(NSWindow *)parent makeKeyAndOrderFront:nil];
        }
    }
    return rc;
}
