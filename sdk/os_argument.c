/*
 * sdk/os_argument.c -- the deliver argument (os_argument.o): what one DSi
 * title hands the next across an application jump.
 *
 * The boot reaches the family through OSi_InitPrevTitleId (func_020883D4,
 * matched, TWL only): initialise a buffer, decode the argument the previous
 * title left, remember its title id, then either re-encode an empty one or
 * mark the state invalid.  On hardware the argument is 0x300 bytes at the
 * start of main RAM, written by the launcher with a CRC, and
 * OS_IsValidDeliverArg checks the system flag that says one is there.
 *
 * Nothing launches this port from another title, so there is never an
 * argument to decode, and nothing reads one after it.  The module is
 * therefore one piece of port code with the SDK's state machine and
 * return values (transcribed from 0x02087F60..0x0208812C):
 *
 *   OS_InitDeliverArgInfo   as matched func_02087F60: clear the buffer,
 *                           state 3 (buffer set, writable).
 *   OS_DecodeDeliverArg     -1, the SDK's answer when no valid argument is
 *                           present (OS_IsValidDeliverArg false).
 *   OS_EncodeDeliverArg     -1 without a buffer (state bit 0 clear), else
 *                           state 0 and 0, as the SDK; the encoded block
 *                           would go to main RAM for a next title that does
 *                           not exist, so it is not written.
 *   OS_GetTitleIdFromDeliverArg, OS_SetDeliverArgStateInvalid  as matched.
 *
 * The module's .bss (g_02151384) is private here, so the matched files of
 * os_argument.o (func_02087F60.c, func_0208812C.c) stay out of the build.
 */

#include <string.h>

#include "os.h"
#include "types.h"

typedef struct OSDeliverArgHeader {
    u64 titleId;
    u8  reserved1;
    u8  flag;
    u16 makerCode;
    u16 argBufferSize;
    u16 binarySize;
    u16 crc;
    u16 sysParam;
} OSDeliverArgHeader;

typedef struct OSDeliverArgInfo {
    OSDeliverArgHeader header;
    u8 buf[748];
} OSDeliverArgInfo;

static u8               *g_ptr;          /* sPtr        */
static u32               g_state;        /* sBufferState */
static OSDeliverArgInfo *g_info;         /* sBufferPtr  */

void OS_InitDeliverArgInfo(OSDeliverArgInfo *info, int binSize)
{
    g_info = info;
    memset(info, 0, sizeof(*info));
    g_state = 3;
    g_info->header.argBufferSize = (u16)(748 - binSize);
    g_info->header.binarySize = 0;
    g_ptr = g_info->buf;
    g_info->buf[0] = 0;
    g_ptr[1] = 0;
}

s32 OS_DecodeDeliverArg(void)
{
    return -1;
}

s32 OS_EncodeDeliverArg(void)
{
    if (!(g_state & 1u)) {
        return -1;
    }
    g_state = 0;
    return 0;
}

void OS_SetDeliverArgStateInvalid(void)
{
    g_state = 0;
}

u64 OS_GetTitleIdFromDeliverArg(void)
{
    if (!(g_state & 1u)) {
        return 0;
    }
    return g_info->header.titleId;
}

void bw_os_argument_reset(void)
{
    g_ptr = NULL;
    g_state = 0;
    g_info = NULL;
}

/* Address spellings (sdk/aliases.c says why a host link needs them). */
void func_02087F60(OSDeliverArgInfo *info, int binSize) { OS_InitDeliverArgInfo(info, binSize); }
s32  func_02087FBC(void) { return OS_EncodeDeliverArg(); }
s32  func_0208806C(void) { return OS_DecodeDeliverArg(); }
void func_02088118(void) { OS_SetDeliverArgStateInvalid(); }
u64  func_0208812C(void) { return OS_GetTitleIdFromDeliverArg(); }
