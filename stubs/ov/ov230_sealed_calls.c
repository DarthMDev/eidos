/*
 * stubs/ov/ov230_sealed_calls.c -- overlay 230's six sealed-body calls, with
 * the results a genuine cartridge gives.
 *
 * Overlay 230 is the game's copy protection.  Each OV230_CallSealedBodyN
 * (func_021882A0 + 0x3C * N in the pinned aletheia) decrypts body N in
 * place, calls it, and seals it again; body N for N = 0..5 is an integrity
 * chain, OV230_IntegrityChainN(hook, hookArg), that sums the tags of its
 * checks (card image, stub signatures, console identity, return tags) onto
 * 0x30EB87 and, when the sum says "tampered", returns hook(hookArg).  The
 * dispatcher and its cipher need the ciphertext at its load address, and the
 * pinned aletheia has no C for the bodies, so this file gives what the
 * chains return on an untampered console, from aletheia's
 * docs/OV230_CHECKS.md (derived from the matched C and the listings):
 *
 *   bodies 0, 2, 4: the sum passes, nothing is called; they return
 *                   0x325507, 0x324415 and 0x31A138;
 *   bodies 1, 3, 5: the odd chains take the hook path when genuine: they
 *                   call hook(hookArg) and return its result, ~hookArg for
 *                   every caller's "call if set, return ~fn" helper.
 *
 * The hook calls matter beyond the check: in a battle, bodies 1 and 3 run
 * EVENT_BATTLE_IntegrityProbeA and B, which draw four GF_STANDARD_Rand
 * values, so leaving them out would put the random sequence off a
 * console's.  The third argument is never read.  src/game has the bodies as
 * C (region ov230d), but their checks read the cartridge and the console,
 * which the port does not have, so the stand-ins stay.
 */

typedef unsigned int u32;

typedef u32 (*Ov230Hook)(void *arg);

u32 func_021882A0(void (*hook)(void), void (*arg)(void), u32 unused)
{
    (void)hook; (void)arg; (void)unused;
    return 0x325507u;
}

u32 func_021882DC(void (*hook)(void), void (*arg)(void), u32 unused)
{
    (void)unused;
    return ((Ov230Hook)hook)((void *)arg);
}

u32 func_02188318(void (*hook)(void), void (*arg)(void), u32 unused)
{
    (void)hook; (void)arg; (void)unused;
    return 0x324415u;
}

u32 func_02188354(void (*hook)(void), void (*arg)(void), u32 unused)
{
    (void)unused;
    return ((Ov230Hook)hook)((void *)arg);
}

u32 func_02188390(void (*hook)(void), void (*arg)(void), u32 unused)
{
    (void)hook; (void)arg; (void)unused;
    return 0x31A138u;
}

u32 func_021883CC(void (*hook)(void), void (*arg)(void), u32 unused)
{
    (void)unused;
    return ((Ov230Hook)hook)((void *)arg);
}

/*
 * The overlay's static initialisers, which FS_StartOverlay runs when the
 * new game's first map loads it: LOADER_ENTRY_Start (0x021889B0) and the
 * four passes it reaches, LOADER_ENTRY_DecryptCallStubs0 (0x02188408),
 * LOADER_ENTRY_DecryptCallDispatch (0x02188CB0),
 * LOADER_ENTRY_DecryptCallStubs2 (0x02189448) and LOADER_ENTRY_DecryptCipher
 * (0x02189908).  On the DS they decrypt the sealed code in place.  The
 * port's code is native and was never encrypted, so they do nothing; without
 * them the start of a new game stopped on "function not in this build".
 */
void func_021889B0(void) {}
void func_02188408(void) {}
void func_02188CB0(void) {}
void func_02189448(void) {}
void func_02189908(void) {}
