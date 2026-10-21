/*
 * sdk/mod_input.c -- the input injection service (docs/mod-services.md,
 * "Input injection").
 *
 * What this file is
 * -----------------
 *
 * The services table's call (bw_mod_input_press) and the rules it keeps:
 * the button is one of the game's twelve, a press of zero frames does
 * nothing, and a press longer than the port allows is refused with a
 * report.
 *
 * What is the port's and what is this file's
 * ------------------------------------------
 *
 * The injected press is the port's own, not the game's.  The game reads the
 * buttons straight from the hardware (KEYINPUT and the ARM7's halfword,
 * sdk/input.h), so the only way to press a button for a mod is to put it in
 * the port's own input layer: the layer holds it for the number of frames
 * the mod asked for, and the game sees it as if the player had pressed it.
 * Nothing here touches the game's memory, and nothing here changes what the
 * player does: the injected buttons are ORed into what the game reads, so
 * the player's own keys are seen as they are.
 *
 * The port holds the button and lets it go on its own: the input layer ages
 * every held button out one frame at a time (bw_input_frame, sdk/input.c),
 * so a press ends even when the mod that asked for it never asks again.
 *
 * The trust model
 * ---------------
 *
 * A mod is native code loaded into the port's process with the port's full
 * privilege (docs/mod-interface.md, section 5), so a mod may press a button
 * by itself.  The service is the port's way of doing it for the mod: it
 * names the buttons, it caps how long a press may be, and it reports what
 * it refuses.
 *
 * Who is calling
 * --------------
 *
 * The service is bound into every mod's table (sdk/mod_loader.c), so the
 * port says which mod is calling by putting that mod's namespace in force
 * around every call it makes into a mod, the way it does everywhere else
 * (sdk/mod_frame.c).  The namespace names the mod in the report, so the
 * player can tell which mod asked for a press the port refused.
 *
 * The service keeps no state of its own: what it holds is the game's
 * input, which is one thing for the whole port, and the presses of two mods
 * that hold the same button are the same press as far as the game is
 * concerned.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "input.h"
#include "mod_input.h"

/* ------------------------------------------------------------------ */
/* What the other layers provide                                       */
/* ------------------------------------------------------------------ */

/*
 * The saved-data layer (sdk/mod_savedata.c): the namespace of the mod that
 * is calling.  The layer has no header of its own yet, so the prototype is
 * here.  It is weak, so a link without the layer still succeeds: the report
 * then names no mod (sdk/mod_frame.c does the same).
 */
#if defined(__GNUC__) || defined(__clang__)
#define BW_PORT_WEAK __attribute__((weak))
#else
#define BW_PORT_WEAK
#endif

extern const char *bw_mod_savedata_namespace(void) BW_PORT_WEAK;

/* The namespace of the mod that is calling, or "" when there is none. */
static const char *current_namespace(void)
{
    const char *ns;

    if (bw_mod_savedata_namespace == NULL) {
        return "";
    }
    ns = bw_mod_savedata_namespace();
    return (ns != NULL) ? ns : "";
}

/* ------------------------------------------------------------------ */
/* The report                                                          */
/* ------------------------------------------------------------------ */

/*
 * The port's reports, the shape the other services give:
 *
 *   eidos: mod <ns>: the input press was refused: <why>
 *   eidos: input: the input press was refused: <why>
 *
 * The call answers nothing (the services table's call for the injection is
 * frozen as a void one), so a refusal is reported here.
 */
static void report(const char *fmt, ...)
{
    const char *ns = current_namespace();
    va_list ap;

    if (ns[0] != '\0') {
        fprintf(stderr, "eidos: mod %s: the input press was refused: ", ns);
    } else {
        fprintf(stderr, "eidos: input: the input press was refused: ");
    }
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/*
 * The bits of a mask the game has no button for, as text: the name the port
 * knows for them ("DEBUG"), the hex for the ones it does not.
 */
static void unknown_bits(u32 bits, char *buf, size_t n)
{
    size_t used = 0;
    int i;

    if (n == 0) {
        return;
    }
    buf[0] = '\0';
    for (i = 0; i < 16; i++) {
        u32 bit = 1u << i;
        const char *name;
        char one[16];
        size_t len;

        if ((bits & bit) == 0) {
            continue;
        }
        name = bw_input_button_name(bit);
        if (name == NULL) {
            snprintf(one, sizeof one, "0x%04lX", (unsigned long)bit);
        } else {
            snprintf(one, sizeof one, "%s", name);
        }
        len = strlen(one);
        if (used != 0) {
            if (used + 1 >= n) {
                break;
            }
            buf[used++] = '+';
        }
        if (used + len >= n) {
            len = n - used - 1;
        }
        memcpy(buf + used, one, len);
        used += len;
        buf[used] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* The service                                                         */
/* ------------------------------------------------------------------ */

void bw_mod_input_press(u32 buttons, u32 frames)
{
    switch (bw_input_inject(buttons, frames)) {
    case 0:
        /* The press was taken.  A press of zero frames does nothing, and
         * that is not worth a report. */
        break;
    case -1:
        if (buttons == 0) {
            report("there is no button to press");
        } else {
            char buf[64];

            unknown_bits(buttons & ~BW_KEY_GAME, buf, sizeof buf);
            report("%s is not one of the game's twelve buttons", buf);
        }
        break;
    case -2:
        if (bw_input_max_press() == 0) {
            report("the input injection is off; the port allows no press");
        } else {
            report("a press of %lu frames is longer than the port allows "
                   "(%lu frames); the port holds the button for the whole "
                   "count, so a long press would hold the key down long "
                   "after the mod stopped asking",
                   (unsigned long)frames,
                   (unsigned long)bw_input_max_press());
        }
        break;
    default:
        break;
    }
}
