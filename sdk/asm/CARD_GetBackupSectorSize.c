/*
 * sdk/asm/CARD_GetBackupSectorSize.c -- CARD_GetBackupSectorSize at 0x02076B6C.
 *
 * Replaces src/matched/arm9_sdk/CARD_GetBackupSectorSize.c (TwlSDK
 * card_backup.o, an asm function):
 *
 *     ldr r0, =g_02150200     cardi_common
 *     ldr r0, [r0]            cardi_common.cmd (CARDiCommandArg *)
 *     ldr r0, [r0, #0x1c]     cmd->spec.sect_size
 *     bx  lr
 *
 * u32 CARD_GetBackupSectorSize(void).  g_02150200 is cardi_common (the
 * matched CARD files spell its first field `CARDiCommandArg *cmd`), so its
 * first word is a pointer the matched C stored: a host pointer on the host.
 * The sector size is the u32 at +0x1C of the command block, which is shared
 * with the ARM7 and made of 32-bit fields only, so +0x1C holds on every host.
 */

#include "asmsdk.h"

extern const u8 *g_02150200;   /* cardi_common.cmd, the struct's first field */

u32 CARD_GetBackupSectorSize(void)
{
    return *(const u32 *)(const void *)(g_02150200 + 0x1C);
}
