/*
 * sdk/asm/func_02049238.c -- 0x02049238.
 *
 * Replaces src/matched/arm9_sdk/func_02049238.c (an asm function):
 *
 *     stmfd sp!, {r4, lr}
 *     mov r4, r0
 *     bl func_02078A80          ; FS_CloseFile(handle)
 *     mov r0, r4
 *     bl func_020307B0          ; sub_020307B0(handle), the heap release
 *     ldmfd sp!, {r4, pc}
 *
 * Close an archive handle's file, then free the handle.  The C calls the
 * canonical names (FS_CloseFile is the port's sdk/fs.c, sub_020307B0 is
 * matched C).
 */

#include "asmsdk.h"

extern BOOL FS_CloseFile(void *file);
extern void sub_020307B0(void *p);

void func_02049238(void *handle)
{
    FS_CloseFile(handle);
    sub_020307B0(handle);
}
