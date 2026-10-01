/*
 * sdk/arm7/nvram.c -- the ARM7's SPI firmware flash ("NVRAM"), PXI tag 4,
 * and the flash behind it, kept in a host file.
 *
 * Why a stand-in: sdk/arm7/arm7.h.  The firmware flash holds the console's
 * header (MAC address, allowed channels), its user settings and the Nintendo
 * Wi-Fi Connection settings; on the DS only the ARM7 can reach it.  The
 * game's libnvram (aletheia src/matched/arm9_sdk/, nvram.o) sends SPI
 * commands to the ARM7 and libncfg reads the Wi-Fi settings through it.
 * NVRAMi_Init (func_02083644.c) spins until this tag is registered.
 *
 * The protocol is the SPI library's, from the matched senders:
 *
 *   - A command is one or more words.  Bit 25 of the data starts one
 *     (its command byte in bits 8-15, a first argument byte in bits 0-7),
 *     bit 24 ends it, bits 16-23 of every word after the first are its
 *     index and bits 0-15 its argument.
 *   - 0x20 write enable, 0x21 write disable, 0x2D software reset: one word
 *     (NVRAMi_WriteEnable, _WriteDisable, _SoftwareReset).
 *   - 0x22 read status register into a byte at a 32-bit address: the
 *     address's top byte in the first word, its middle 16 bits at index 1,
 *     its low byte in bits 8-15 at index 2 (NVRAMi_ReadStatusRegister).
 *   - 0x23 read (0x24 fast read): flash address bits 16-23 in the first
 *     word, bits 0-15 at index 1; size high and low at 2 and 3; the buffer
 *     address high and low at 4 and 5 (NVRAMi_ReadDataBytes).
 *   - 0x25 page write (0x26 page program): flash address as for read, size
 *     at 2, buffer high and low at 3 and 4 (NVRAMi_PageWrite).  The page is
 *     256 bytes and a write wraps inside it, as the flash does.
 *   - 0x27 page erase, 0x28 sector erase (64 KiB): the flash address only.
 *   - Every command is answered with one word: start and end bits, the
 *     command byte with bit 7 set in bits 8-15, the result in bits 0-7
 *     (0 success, 1 invalid command, 2 invalid parameter; the ARM9's
 *     callbacks read only the low byte, NVRAMi_ReadCallback and friends).
 *
 * Writes follow the flash's rules: a page write or program, an erase, is
 * ignored unless write enable came first, and clears the write-enable latch
 * when it has run.  The status register always reads ready (WIP = 0), so
 * NVRAMi_WriteCallback's polling ends at the first read.
 *
 * Buffers arrive as 32-bit numbers.  On a 32-bit host they are host
 * pointers; on a 64-bit host only the ARM9's own work buffer nvramTemp
 * (0x02150EC0, 32 bytes) can be found, through bw_arm7_resolve, and any
 * other buffer is answered with "invalid parameter".  The matched callers
 * themselves keep a buffer address in a u32 (nvramw.arg[2]), so the
 * firmware's write and verify paths are 32-bit-host paths in any case.
 *
 * The flash.  256 KiB, the size of a DS's (the SDK addresses it with 24
 * bits).  It lives in memory, loaded at the first command from
 * <save dir>/firmware.bin (sdk/save.h) when that exists (a dump of the
 * player's own console works), else built: zeroed header with the user
 * settings offset (0x3FE00 / 8) at 0x20, the MAC address at 0x36, the
 * allowed channels at 0x3C, Wi-Fi settings format 0 at 0x1FE (a NITRO
 * console: the three original connection slots only), everything else
 * erased (0xFF), so the Wi-Fi slots read as empty.  Either way the two
 * user-settings blocks at 0x3FE00 and 0x3FF00 are written from
 * sdk/firmware.h's settings, which are the authority.  A changed image goes
 * back to the file whole and atomically at write disable (the end of every
 * NVRAMi_Write), at bw_arm7_reset and on bw_arm7_nvram_flush.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm7.h"
#include "dsmem.h"
#include "firmware.h"
#include "pxi.h"
#include "save.h"

#define NVRAM_SIZE          0x40000u
#define NVRAM_PAGE          0x100u
#define NVRAM_SECTOR        0x10000u
#define NVRAM_USER_BLOCK    0x3FE00u
#define NVRAM_TEMP_DS       0x02150EC0u
#define NVRAM_TEMP_SIZE     32u

#define SPI_START   0x02000000u
#define SPI_END     0x01000000u

enum {
    CMD_WREN = 0x20, CMD_WRDI, CMD_RDSR, CMD_READ, CMD_FAST_READ,
    CMD_PW, CMD_PP, CMD_PE, CMD_SE, CMD_DP, CMD_RDP, CMD_CE, CMD_RSI,
    CMD_SR
};

enum { RES_SUCCESS = 0, RES_INVALID_COMMAND = 1, RES_INVALID_PARAMETER = 2 };

#define STATUS_WEL 0x02u
#define STATUS_WEL_CLEAR 0xFDu    /* every bit but the write-enable latch */

static struct {
    /* The command being received.                                          */
    int in_command;
    u32 command;
    u32 first;                      /* bits 0-7 of the start word           */
    u32 arg[8];                     /* bits 0-15 by index                   */
    /* The flash.                                                           */
    u8 *image;
    int loaded;
    int dirty;
    u8  status;
    char path[1100];
    int path_set;
    u8 *temp;                       /* bound nvramTemp, NULL = by DS address */
    u32 commands;
    u32 writes;
} g_nv;

/* ------------------------------------------------------------------ */
/* The image                                                           */
/* ------------------------------------------------------------------ */

static const char *nvram_path(void)
{
    if (!g_nv.path_set
        && bw_save_path(g_nv.path, sizeof g_nv.path, "firmware.bin") != 0)
        g_nv.path[0] = '\0';
    return g_nv.path;
}

static void default_header(u8 *img, const BwFirmwareSettings *s)
{
    memset(img, 0, 0x200);
    img[0x20] = (u8)((NVRAM_USER_BLOCK / 8) & 0xFF);
    img[0x21] = (u8)((NVRAM_USER_BLOCK / 8) >> 8);
    memcpy(img + 0x36, s->mac, 6);
    img[0x3C] = (u8)(s->channels & 0xFF);
    img[0x3D] = (u8)(s->channels >> 8);
    img[0x1FE] = 0x00;
}

/* The user settings blocks from the settings in force.                     */
static void write_user_blocks(void)
{
    const BwFirmwareSettings *s = bw_firmware_settings();

    /* Two copies; the one with the higher update counter is current.      */
    bw_firmware_user_block(s, 1, g_nv.image + NVRAM_USER_BLOCK);
    bw_firmware_user_block(s, 0, g_nv.image + NVRAM_USER_BLOCK + 0x100);
}

static int ensure_image(void)
{
    long n;

    if (g_nv.loaded)
        return 0;
    if (g_nv.image == NULL) {
        g_nv.image = (u8 *)malloc(NVRAM_SIZE);
        if (g_nv.image == NULL)
            return -1;
    }
    memset(g_nv.image, 0xFF, NVRAM_SIZE);
    n = bw_save_read(nvram_path(), g_nv.image, NVRAM_SIZE);
    if (n < 0) {
        default_header(g_nv.image, bw_firmware_settings());
    } else if ((u32)n < NVRAM_SIZE) {
        memset(g_nv.image + n, 0xFF, NVRAM_SIZE - (u32)n);
    }
    write_user_blocks();
    g_nv.loaded = 1;
    g_nv.dirty = 0;
    return 0;
}

int bw_arm7_nvram_flush(void)
{
    const char *path;

    if (!g_nv.dirty)
        return 0;
    path = nvram_path();
    if (path[0] == '\0' ||
        bw_save_write_atomic(path, g_nv.image, NVRAM_SIZE) != 0) {
        fprintf(stderr, "eidos: nvram: cannot write the firmware flash "
                        "(%s); it stays in memory\n",
                path[0] != '\0' ? path : "no save folder");
        return -1;
    }
    g_nv.dirty = 0;
    g_nv.writes++;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

static u8 *buffer(u32 value, u32 len)
{
    u8 *temp = g_nv.temp;

    if (temp == NULL)
        temp = (u8 *)bw_ds_try_ptr(NVRAM_TEMP_DS);
    return (u8 *)bw_arm7_resolve(value, len, temp, NVRAM_TEMP_SIZE);
}

static u32 flash_addr(void)
{
    return ((g_nv.first << 16) | (g_nv.arg[1] & 0xFFFFu)) & (NVRAM_SIZE - 1u);
}

static void erase(u32 addr, u32 unit)
{
    if (!(g_nv.status & STATUS_WEL))
        return;
    memset(g_nv.image + (addr & ~(unit - 1u)), 0xFF, unit);
    g_nv.status &= STATUS_WEL_CLEAR;
    g_nv.dirty = 1;
}

static int run(void)
{
    u32 addr, size, ptr, i;
    u8 *mem;

    if (ensure_image() != 0)
        return RES_INVALID_COMMAND;
    switch (g_nv.command) {
    case CMD_WREN:
        g_nv.status |= STATUS_WEL;
        return RES_SUCCESS;
    case CMD_WRDI:
        g_nv.status &= STATUS_WEL_CLEAR;
        (void)bw_arm7_nvram_flush();    /* the end of an NVRAMi_Write       */
        return RES_SUCCESS;
    case CMD_SR:
        g_nv.status = 0;
        return RES_SUCCESS;
    case CMD_DP:
    case CMD_RDP:
        return RES_SUCCESS;
    case CMD_RDSR:
        ptr = (g_nv.first << 24) | ((g_nv.arg[1] & 0xFFFFu) << 8)
              | ((g_nv.arg[2] >> 8) & 0xFFu);
        if ((mem = buffer(ptr, 1)) == NULL)
            return RES_INVALID_PARAMETER;
        mem[0] = g_nv.status;           /* WIP clear: never busy            */
        return RES_SUCCESS;
    case CMD_READ:
    case CMD_FAST_READ:
        addr = flash_addr();
        size = (g_nv.arg[2] << 16) | (g_nv.arg[3] & 0xFFFFu);
        ptr = (g_nv.arg[4] << 16) | (g_nv.arg[5] & 0xFFFFu);
        if ((mem = buffer(ptr, size)) == NULL)
            return RES_INVALID_PARAMETER;
        for (i = 0; i < size; i++)
            mem[i] = g_nv.image[(addr + i) & (NVRAM_SIZE - 1u)];
        return RES_SUCCESS;
    case CMD_PW:
    case CMD_PP:
        addr = flash_addr();
        size = g_nv.arg[2] & 0xFFFFu;
        ptr = (g_nv.arg[3] << 16) | (g_nv.arg[4] & 0xFFFFu);
        if (size > NVRAM_PAGE || (mem = buffer(ptr, size)) == NULL)
            return RES_INVALID_PARAMETER;
        if (!(g_nv.status & STATUS_WEL))
            return RES_SUCCESS;         /* the flash ignores it             */
        for (i = 0; i < size; i++) {
            /* Inside one page: the column wraps, the page does not move.  */
            u32 a = (addr & ~(NVRAM_PAGE - 1u)) | ((addr + i) & (NVRAM_PAGE - 1u));

            if (g_nv.command == CMD_PW)
                g_nv.image[a] = mem[i];
            else
                g_nv.image[a] &= mem[i];
        }
        g_nv.status &= STATUS_WEL_CLEAR;
        g_nv.dirty = 1;
        return RES_SUCCESS;
    case CMD_PE:
        erase(flash_addr(), NVRAM_PAGE);
        return RES_SUCCESS;
    case CMD_SE:
        erase(flash_addr(), NVRAM_SECTOR);
        return RES_SUCCESS;
    default:
        return RES_INVALID_COMMAND;
    }
}

static void nvram_callback(int tag, u32 data, int err)
{
    int result;

    (void)tag;
    if (err)
        return;
    if (data & SPI_START) {
        g_nv.in_command = 1;
        g_nv.command = (data >> 8) & 0xFFu;
        g_nv.first = data & 0xFFu;
        memset(g_nv.arg, 0, sizeof g_nv.arg);
    } else if (g_nv.in_command) {
        u32 index = (data >> 16) & 0xFFu;

        if (index < sizeof g_nv.arg / sizeof g_nv.arg[0])
            g_nv.arg[index] = data & 0xFFFFu;
    }
    if (!(data & SPI_END) || !g_nv.in_command)
        return;
    g_nv.in_command = 0;
    g_nv.commands++;
    result = run();
    (void)bw_pxi_arm7_send(BW_PXI_TAG_NVRAM,
                           SPI_START | SPI_END | ((g_nv.command | 0x80u) << 8)
                           | (u32)result, 0);
}

void bw_arm7_nvram_init(void)
{
    g_nv.in_command = 0;
    g_nv.status = 0;
    g_nv.commands = 0;
    /* A power-on reads the flash again, user settings refreshed.          */
    if (!g_nv.dirty)
        g_nv.loaded = 0;
    bw_pxi_arm7_set_callback(BW_PXI_TAG_NVRAM, nvram_callback);
}

void bw_arm7_nvram_reset(void)
{
    (void)bw_arm7_nvram_flush();
    g_nv.loaded = 0;
    g_nv.in_command = 0;
    g_nv.status = 0;
}

/* ------------------------------------------------------------------ */
/* Interface                                                           */
/* ------------------------------------------------------------------ */

void bw_arm7_nvram_set_path(const char *path)
{
    (void)bw_arm7_nvram_flush();
    if (path == NULL || strlen(path) >= sizeof g_nv.path) {
        g_nv.path_set = 0;
    } else {
        strcpy(g_nv.path, path);
        g_nv.path_set = 1;
    }
    g_nv.loaded = 0;
    g_nv.dirty = 0;
}

void bw_arm7_nvram_bind(void *temp)
{
    g_nv.temp = (u8 *)temp;
}

const u8 *bw_arm7_nvram_image(u32 *size)
{
    if (ensure_image() != 0) {
        if (size != NULL)
            *size = 0;
        return NULL;
    }
    if (size != NULL)
        *size = NVRAM_SIZE;
    return g_nv.image;
}

u32 bw_arm7_nvram_commands(void)
{
    return g_nv.commands;
}

u32 bw_arm7_nvram_writes(void)
{
    return g_nv.writes;
}
