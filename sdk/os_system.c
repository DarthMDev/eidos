/*
 * sdk/os_system.c -- the CPU- and boot-level OS calls the boot reaches that
 * are not threads, interrupts or alarms.
 *
 * OS_GetBootType (0x02087B04) and OSi_DetectDeviceType (0x02085D0C)
 *
 *   Neither is matched C.  The boot type is the u16 the firmware leaves at
 *   0x02FFFC40 (OS_GetBootInfo's address) before the game starts; this port
 *   is always started the way a cartridge is, so bw_os_reset stands in for
 *   the firmware and writes OS_BOOTTYPE_ROM (1) there, and OS_GetBootType
 *   reads it back through the DS address space like the SDK does.  The
 *   device type is the SDK's table at 0x020A5CD4 indexed by the boot type
 *   (`ldr r0, [table, bootType, lsl #2]`), read out of the player's own
 *   ARM9 image at run time; without one it is 0.
 *
 * OS_GetConsoleType (0x02085CBC)
 *
 *   EXACT C in aletheia (func_02085CBC.c), shimmed so that its cache, its
 *   TWL test and OSi_DetectDeviceType are one module: 0x01800004 on TWL,
 *   0x80000001 on NITRO, OR the device type, computed once.  "TWL" is the
 *   same test the matched OS_IsRunOnTwl makes, SCFG_A9ROM (0x04004000) bits
 *   0-1 equal to 1, read from the register file; the register file's
 *   power-on value therefore decides which console the game believes it is
 *   on, for both functions at once.
 *
 * OS_SetProtectionRegionEx (0x02086DB0) and OS_SetProtectionRegionParam
 *
 *   The ARM946's protection unit (CP15 c6) guards the DS's physical
 *   regions.  The DS address space here is ordinary host memory reached
 *   through bw_ds_ptr, which panics on an address outside every region, so
 *   there is nothing to program and both are no-ops.  The matched Ex
 *   (func_02086DB0.c) only computes the Param word; the Param call itself is
 *   asm (mcr p15).
 *
 * OSi_SyncWithOtherProc (0x02084AB0)
 *
 *   OS_InitLock's handshake with the ARM7 through a four-byte buffer in the
 *   shared area: each side writes a token, waits to see the other's, and
 *   sets a done flag.  The ARM7 stand-in answers synchronously, so the shim
 *   leaves the buffer in the state both sides leave it in once the
 *   handshake has completed (type 0: tokens equal and byte 3 set; type 1:
 *   bytes 2 and 3 set) and returns.
 *
 * OSi_WaitVCount0 (0x02086348)
 *
 *   asm: IME off, spin until VCOUNT reads 0, IME back.  The end of OS_Init
 *   uses it to start the game at the top of a frame.  On the host the game
 *   runs between frames by construction, and a spin on a register nothing
 *   is advancing would never end, so it returns at once.
 */

#include <string.h>

#include "dsmem.h"
#include "os.h"
#include "port.h"

#define DS_BOOT_INFO        0x02FFFC40u
#define DS_DEVICE_TYPE_TAB  0x020A5CD4u
#define OS_BOOTTYPE_ROM     1u
#define OS_BOOTTYPE_MAX     8u
#define SCFG_A9ROM          0x04004000u

static u32 g_console_type_cache = 0xFFFFFFFFu;

u16 OS_GetBootType(void)
{
    u16 v;
    memcpy(&v, bw_ds_ptr(DS_BOOT_INFO), sizeof(v));
    return v;
}

u32 OSi_DetectDeviceType(void)
{
    u32 bt = OS_GetBootType();
    const void *p;

    if (bt >= OS_BOOTTYPE_MAX) {
        return 0;
    }
    p = bw_arm9_rodata(DS_DEVICE_TYPE_TAB + 4u * bt, 4u);
    return p != NULL ? bw_rd32((const u8 *)p) : 0;
}

u32 OS_GetConsoleType(void)
{
    if (g_console_type_cache == 0xFFFFFFFFu) {
        int twl = (bw_io(SCFG_A9ROM)[0] & 3u) == 1u;

        g_console_type_cache = twl ? 0x01800004u : 0x80000001u;
        g_console_type_cache |= OSi_DetectDeviceType();
    }
    return g_console_type_cache;
}

void OS_SetProtectionRegionParam(int regionNo, u32 param)
{
    (void)regionNo;
    (void)param;
}

void OS_SetProtectionRegionEx(int regionNo, u32 address, u32 sizeDef)
{
    (void)regionNo;
    (void)address;
    (void)sizeDef;
}

void OSi_SyncWithOtherProc(int type, void *syncBuf)
{
    u8 *b = (u8 *)syncBuf;

    if (type == 0) {
        b[0] = 0x80;
        b[1] = 0x80;
        b[3] = 1;
    } else {
        b[2] = 1;
        b[3] = 1;
    }
}

void OSi_WaitVCount0(void)
{
}

void bw_os_system_reset(void)
{
    u16 rom = OS_BOOTTYPE_ROM;

    g_console_type_cache = 0xFFFFFFFFu;
    memcpy(bw_ds_ptr(DS_BOOT_INFO), &rom, sizeof(rom));
}

/* Address spellings (sdk/aliases.c says why a host link needs them). */
u16  func_02087B04(void)                         { return OS_GetBootType(); }
u32  func_02085D0C(void)                         { return OSi_DetectDeviceType(); }
u32  func_02085CBC(void)                         { return OS_GetConsoleType(); }
void func_02086DB0(int r, u32 a, u32 s)          { OS_SetProtectionRegionEx(r, a, s); }
void func_02084AB0(int type, void *buf)          { OSi_SyncWithOtherProc(type, buf); }
void func_02086348(void)                         { OSi_WaitVCount0(); }
