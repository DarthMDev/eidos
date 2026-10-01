/*
 * sdk/asm/NHTTPi_GetConnection.c -- 0x0204960C (layout name
 * NHTTPi_GetConnection, a weak-evidence row; it is the game's archive seek).
 *
 * Replaces src/matched/arm9_sdk/NHTTPi_GetConnection.c (an asm tail call):
 *
 *     ldr ip, =func_02078B44; mov r2, #0; bx ip
 *
 * FS_SeekFile(file, pos, FS_SEEK_SET) with FS_SeekFile's result.  The
 * callers use the spellings func_0204960C and sub_0204960C.
 *
 * func_0204960C is not defined here: stubs/reimpl_0204960C.c already
 * defines it, from the two call sites, as exactly this seek.  The match
 * confirms that reading; when the boot links this file the stub can become
 * a forwarder to NHTTPi_GetConnection (or be deleted with func_0204960C
 * added below).
 */

#include "asmsdk.h"

extern BOOL func_02078B44(void *file, s32 offset, u32 origin);

BOOL NHTTPi_GetConnection(void *file, u32 pos)
{
    return func_02078B44(file, (s32)pos, 0);
}

BOOL sub_0204960C(void *file, u32 pos)
{
    return NHTTPi_GetConnection(file, pos);
}
