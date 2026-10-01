/*
 * tests/os_dsmem_double.c -- TEST DOUBLE of sdk/dsmem.c, for the OS tests
 * only.
 *
 * The OS shims are written against sdk/dsmem.h, whose implementation
 * (sdk/dsmem.c) is a separate lane's work.  Until it is in the tree,
 * CMakeLists.txt compiles this file into tests/test_os_*.c and nothing
 * else; once sdk/dsmem.c exists the tests link the real one and this file
 * is not built.  It implements just enough of the contract for the OS
 * layer: main RAM and the register file as plain little-endian memory,
 * write hooks run by bw_io_write16/32 and by bw_io_sync on a change.  No
 * VRAM mapping, no object registry.
 */

#include <stdlib.h>
#include <string.h>

#include "dsmem.h"
#include "port.h"

static u8 *g_main;
static u8  g_io[BW_DS_IO_SIZE];
static u8  g_io_synced[BW_DS_IO_SIZE];

typedef struct Hook { u32 addr; u32 width; BwIoWriteHook fn; } Hook;
static Hook g_hooks[64];
static int  g_nhooks;

static void ensure_main(void)
{
    if (g_main == NULL) {
        g_main = (u8 *)calloc(1, BW_DS_MAIN_SIZE);
        if (g_main == NULL) {
            bw_panic("os_dsmem_double", 0, "out of memory");
        }
    }
}

void *bw_ds_try_ptr(u32 addr)
{
    if (addr >= BW_DS_MAIN_BASE && addr - BW_DS_MAIN_BASE < BW_DS_MAIN_SIZE) {
        ensure_main();
        return g_main + (addr - BW_DS_MAIN_BASE);
    }
    if (addr >= BW_DS_IO_BASE && addr - BW_DS_IO_BASE < BW_DS_IO_SIZE) {
        return g_io + (addr - BW_DS_IO_BASE);
    }
    return NULL;
}

void *bw_ds_ptr(u32 addr)
{
    void *p = bw_ds_try_ptr(addr);
    if (p == NULL) {
        bw_panic("bw_ds_ptr (test double)", addr, "address not backed");
    }
    return p;
}

u32 bw_ds_addr_of(const void *host)
{
    (void)host;
    return 0;
}

void bw_ds_register(u32 addr, void *host, u32 size, const char *name)
{
    (void)addr; (void)host; (void)size; (void)name;
}

volatile u8 *bw_io(u32 addr)
{
    return (volatile u8 *)bw_ds_ptr(addr);
}

static u32 rd(const u8 *base, u32 off, u32 width)
{
    return width == 4 ? bw_rd32(base + off)
         : width == 2 ? bw_rd16(base + off) : base[off];
}

static Hook *hook_at(u32 addr)
{
    int i;
    for (i = 0; i < g_nhooks; i++) {
        if (g_hooks[i].addr == addr) {
            return &g_hooks[i];
        }
    }
    return NULL;
}

static void store(u32 addr, u32 v, u32 width)
{
    u32 off = addr - BW_DS_IO_BASE;
    u32 old = rd(g_io, off, width);
    Hook *h = hook_at(addr);

    if (width == 4) {
        bw_wr32(g_io + off, v);
    } else {
        bw_wr16(g_io + off, (u16)v);
    }
    memcpy(g_io_synced + off, g_io + off, width);
    if (h != NULL && h->fn != NULL) {
        h->fn(addr, old, v);
        memcpy(g_io_synced + off, g_io + off, width);
    }
}

u16  bw_io_read16(u32 addr)        { return (u16)rd(g_io, addr - BW_DS_IO_BASE, 2); }
u32  bw_io_read32(u32 addr)        { return rd(g_io, addr - BW_DS_IO_BASE, 4); }
void bw_io_write16(u32 addr, u16 v){ store(addr, v, 2); }
void bw_io_write32(u32 addr, u32 v){ store(addr, v, 4); }

void bw_io_hook(u32 addr, u32 width, BwIoWriteHook hook)
{
    Hook *h = hook_at(addr);
    if (h == NULL) {
        if (g_nhooks == (int)(sizeof(g_hooks) / sizeof(g_hooks[0]))) {
            bw_panic("bw_io_hook (test double)", addr, "too many hooks");
        }
        h = &g_hooks[g_nhooks++];
    }
    h->addr = addr;
    h->width = width;
    h->fn = hook;
}

void bw_io_sync(void)
{
    int i;
    for (i = 0; i < g_nhooks; i++) {
        u32 off = g_hooks[i].addr - BW_DS_IO_BASE;
        u32 w = g_hooks[i].width;
        if (memcmp(g_io + off, g_io_synced + off, w) != 0) {
            u32 old = rd(g_io_synced, off, w);
            u32 v = rd(g_io, off, w);
            memcpy(g_io_synced + off, g_io + off, w);
            g_hooks[i].fn(g_hooks[i].addr, old, v);
            memcpy(g_io_synced + off, g_io + off, w);
        }
    }
}

u8 *bw_vram_bank(int bank, u32 *size_out)
{
    (void)bank;
    if (size_out != NULL) {
        *size_out = 0;
    }
    return NULL;
}

void bw_vram_remap(void)
{
}

void bw_ds_reset(void)
{
    ensure_main();
    memset(g_main, 0, BW_DS_MAIN_SIZE);
    memset(g_io, 0, sizeof(g_io));
    memset(g_io_synced, 0, sizeof(g_io_synced));
    g_nhooks = 0;
}
