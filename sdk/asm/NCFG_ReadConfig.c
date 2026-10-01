/*
 * sdk/asm/NCFG_ReadConfig.c -- NCFG_ReadConfig at 0x0205FAA8.
 *
 * Replaces src/matched/arm9_sdk/NCFG_ReadConfig.c (TwlWiFi libncfg info.o,
 * an asm function):
 *
 *     stmfd sp!, {r3, lr}
 *     bl    func_0205FFC4          readAllSlots(config); r1 (work) unused
 *     cmp   r0, #0
 *     movne r0, #0                 nonzero -> 0
 *     mvneq r0, #0                 zero    -> -1
 *     ldmfd sp!, {r3, pc}
 *
 * int NCFG_ReadConfig(NCFGConfig *config, void *work).  func_0205FFC4 is
 * matched C, int (NCFGConfig *config); the work pointer rides along in r1
 * and nothing reads it.  The game caller (func_020107CC.c) declares the
 * address spelling as void (u8 *p, u32 v) and passes 0 for the work
 * argument; that forwarder drops the result as its caller does.
 */

#include <stddef.h>

#include "asmsdk.h"

typedef struct NCFGConfig NCFGConfig;

extern int func_0205FFC4(NCFGConfig *config);

int NCFG_ReadConfig(NCFGConfig *config, void *work)
{
    (void)work;
    return func_0205FFC4(config) != 0 ? 0 : -1;
}

void func_0205FAA8(u8 *config, u32 work)
{
    (void)work;
    (void)NCFG_ReadConfig((NCFGConfig *)(void *)config, NULL);
}
