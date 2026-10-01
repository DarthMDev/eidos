/*
 * sdk/gx.c -- 0x02030524, the heap manager's error halfword.
 *
 * The file keeps its old name; what it holds is not GX.  aletheia's
 * layout/symbols_sdk.json offers GX_GetBankForBGExtPltt for 0x02030524, and
 * src/matched/sub_02030734.c, which calls the address on its out-of-memory
 * arm, already recorded that the offer is a signature-matcher guess that
 * does not fit.  aletheia has since matched the function itself
 * (src/matched/arm9_sdk/func_02030524.c): it returns the halfword at +0xE of
 * the heap manager at 0x02146954, the index of the heap the constructor
 * could not fit (sub_0202FC48 stores it there).
 *
 * The port defines it here rather than compiling the matched file because
 * the heap manager is a port object (sdk/game_globals.c, g_EnvModule, whose
 * layout follows the pointer width), and the matched file reads it at a
 * fixed byte offset.  The name is the one sub_02030734 calls,
 * `sub_02030524`; the game's boot reaches the same address as func_02030524,
 * which tools/bootlink.py renames to this definition (cmake/boot.cmake lists
 * the matched file as replaced).  sub_02030734 declares it void and ignores
 * the value, which is harmless.
 */

#include "port.h"

u16 sub_02030524(void)
{
    return (u16)bw_heap_error();
}
