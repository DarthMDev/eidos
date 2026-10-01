/*
 * tests/test_overlay.c -- the overlay registry (sdk/overlay.c) and the
 * generated table (tools/overlay_table.py).
 *
 * Synthetic half: a four-overlay table written here, covering
 *
 *   - the presence check: present, absent with the lowest-addressed missing
 *     function named and its status, unknown (no function known), no such;
 *   - FS_LoadOverlayInfo's fields, the ARM7 and out-of-range refusals;
 *   - FS_LoadOverlay: the image hook runs with the descriptor, the static
 *     initialisers run in order, the overlay is resident; loading an
 *     overlay over the same RAM evicts the first; FS_UnloadOverlay and the
 *     func_020792B8 spelling the game calls;
 *   - FS_Init clearing residency.
 *
 * Generated-table half, no game data: 237 overlays, each descriptor's id
 * its index, every function slice inside the array and sorted, the counts
 * consistent with the statuses, ov230's five static initialisers, and the
 * presence check refusing ov9 in a build with no overlay code, naming its
 * lowest-addressed function.
 *
 * Data-gated half (argv[1] a data root whose parent holds y9.bin, fat.bin
 * and overlays_dec/): every FS_LoadOverlayInfo header compared word for
 * word with the ROM's overlay table, the file extents with the FAT, and
 * ov230's initialiser addresses with the words in its decompressed image.
 *
 * `test_overlay --load <id>` loads one overlay of the generated table and
 * is expected to panic; ctest's test_overlay_refuses checks the message.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "fs.h"
#include "overlay.h"
#include "port_test.h"

extern BOOL FS_LoadOverlayInfo(BwOverlayInfo *info, u32 target, u32 id);
extern BOOL FS_LoadOverlay(u32 target, u32 id);
extern BOOL FS_UnloadOverlay(u32 target, u32 id);
extern BOOL func_020792B8(u32 target, u32 id);
extern void FS_Init(u32 dma);

/* ------------------------------------------------------------------ */
/* The synthetic table                                                 */
/* ------------------------------------------------------------------ */

static int g_order[8];
static int g_order_n;
static u32 g_hooked[8];
static int g_hooked_n;

static void init_a(void) { g_order[g_order_n++] = 1; }
static void init_b(void) { g_order[g_order_n++] = 2; }

static void hook(const BwOverlayDesc *d)
{
    g_hooked[g_hooked_n++] = d->id;
}

static const BwOverlayFunc s_funcs[] = {
    /* ov0: present */
    { 0x02100000u, BW_OVF_PRESENT, NULL },
    { 0x02100040u, BW_OVF_PRESENT, "ov0_helper" },
    /* ov1: one present, one matched but not built, one open */
    { 0x02200000u, BW_OVF_PRESENT, NULL },
    { 0x02200010u, BW_OVF_MATCHED, NULL },
    { 0x02200020u, BW_OVF_OPEN,    "SDK_Thing" },
    /* ov3: present, shares ov0's RAM */
    { 0x02100040u, BW_OVF_PRESENT, NULL },
};

static const BwOverlaySinit s_sinits[] = {
    { 0u, 0x02100040u, init_a },
    { 0u, 0x02100000u, init_b },
};

static const BwOverlayDesc s_ovl[] = {
    { 0u, 0x02100000u, 0x80u, 0x20u, 0x02100070u, 0x0210007Cu, 0u, 0x03000060u,
      0u, 2u, 2u, 2u, 0u, 2u },
    { 1u, 0x02200000u, 0x40u, 0x00u, 0x0220003Cu, 0x02200040u, 1u, 0x02000000u,
      2u, 3u, 2u, 1u, 0u, 0u },
    { 2u, 0x02300000u, 0x20u, 0x00u, 0x02300000u, 0x02300004u, 2u, 0x02000000u,
      5u, 0u, 0u, 0u, 0u, 0u },
    { 3u, 0x02100040u, 0x40u, 0x00u, 0x0210007Cu, 0x02100080u, 3u, 0x02000000u,
      5u, 1u, 1u, 1u, 0u, 0u },
};

static const BwOverlayTable s_table = {
    s_ovl, 4u, s_funcs, 6u, s_sinits, 2u
};

static int contains(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

static int synthetic(void)
{
    const BwOverlayFunc *m;
    BwOverlayInfo info;
    char why[512];

    printf("synthetic table\n");
    bw_overlay_use_table(&s_table);
    bw_overlay_set_image_hook(hook);

    BW_CHECK_EQ("ov0 present", bw_overlay_check(0, &m), BW_OVL_PRESENT);
    BW_CHECK_EQ("  nothing missing", m == NULL, 1);
    BW_CHECK_EQ("ov1 absent", bw_overlay_check(1, &m), BW_OVL_ABSENT);
    BW_REQUIRE("  a missing function", m != NULL);
    BW_CHECK_EQ("  the lowest-addressed one", m->address, 0x02200010u);
    BW_CHECK_EQ("  matched, not built", m->status, BW_OVF_MATCHED);
    BW_CHECK_EQ("ov2 unknown", bw_overlay_check(2, &m), BW_OVL_UNKNOWN);
    BW_CHECK_EQ("ov4 no such", bw_overlay_check(4, NULL), BW_OVL_NO_SUCH);

    bw_overlay_describe(1, why, sizeof(why));
    printf("  %s\n", why);
    BW_CHECK_EQ("describe names the overlay", contains(why, "overlay 1 "), 1);
    BW_CHECK_EQ("describe names the function", contains(why, "func_02200010"), 1);
    BW_CHECK_EQ("describe says why", contains(why, "matched in aletheia, not compiled"), 1);
    bw_overlay_describe(2, why, sizeof(why));
    BW_CHECK_EQ("describe unknown", contains(why, "no function of it is known"), 1);
    bw_overlay_describe(9, why, sizeof(why));
    BW_CHECK_EQ("describe no such", contains(why, "no such overlay"), 1);

    memset(&info, 0xAA, sizeof(info));
    BW_CHECK_EQ("info ov0", FS_LoadOverlayInfo(&info, 0, 0), 1);
    BW_CHECK_EQ("  id", info.id, 0);
    BW_CHECK_EQ("  ram", info.ram_address, 0x02100000u);
    BW_CHECK_EQ("  size", info.ram_size, 0x80);
    BW_CHECK_EQ("  bss", info.bss_size, 0x20);
    BW_CHECK_EQ("  sinit", info.sinit_init, 0x02100070u);
    BW_CHECK_EQ("  sinit end", info.sinit_init_end, 0x0210007Cu);
    BW_CHECK_EQ("  file", info.file_id, 0);
    BW_CHECK_EQ("  compressed word", info.compressed, 0x03000060u);
    BW_CHECK_EQ("  target", info.target, 0);
    BW_CHECK_EQ("info for the ARM7", FS_LoadOverlayInfo(&info, 1, 0), 0);
    BW_CHECK_EQ("info past the table", FS_LoadOverlayInfo(&info, 0, 4), 0);
    BW_CHECK_EQ("info for a negative handle", FS_LoadOverlayInfo(&info, 0, 0xFFFFFFFFu), 0);

    BW_CHECK_EQ("load ov0", FS_LoadOverlay(0, 0), 1);
    BW_CHECK_EQ("  hook ran once", g_hooked_n, 1);
    BW_CHECK_EQ("  for ov0", g_hooked[0], 0);
    BW_CHECK_EQ("  two initialisers ran", g_order_n, 2);
    BW_CHECK_EQ("  in table order", g_order[0] == 1 && g_order[1] == 2, 1);
    BW_CHECK_EQ("  resident", bw_overlay_is_loaded(0), 1);
    BW_CHECK_EQ("load ov0 for the ARM7", FS_LoadOverlay(1, 0), 0);
    BW_CHECK_EQ("load past the table", FS_LoadOverlay(0, 7), 0);

    BW_CHECK_EQ("load ov3 over ov0's RAM", FS_LoadOverlay(0, 3), 1);
    BW_CHECK_EQ("  ov3 resident", bw_overlay_is_loaded(3), 1);
    BW_CHECK_EQ("  ov0 evicted", bw_overlay_is_loaded(0), 0);
    BW_CHECK_EQ("  no initialisers for ov3", g_order_n, 2);

    BW_CHECK_EQ("unload ov3 (the game's spelling)", func_020792B8(0, 3), 1);
    BW_CHECK_EQ("  not resident", bw_overlay_is_loaded(3), 0);
    BW_CHECK_EQ("load ov0 again", FS_LoadOverlay(0, 0), 1);
    BW_CHECK_EQ("  hook ran again", g_hooked_n, 3);
    BW_CHECK_EQ("  initialisers ran again", g_order_n, 4);
    BW_CHECK_EQ("unload ov0", FS_UnloadOverlay(0, 0), 1);
    BW_CHECK_EQ("  not resident", bw_overlay_is_loaded(0), 0);
    BW_CHECK_EQ("unload past the table", FS_UnloadOverlay(0, 9), 0);

    FS_LoadOverlay(0, 0);
    FS_Init(1);
    BW_CHECK_EQ("FS_Init clears residency", bw_overlay_is_loaded(0), 0);

    bw_overlay_set_image_hook(NULL);
    bw_overlay_use_table(NULL);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The generated table                                                 */
/* ------------------------------------------------------------------ */

static int generated(void)
{
    const BwOverlayTable *t = bw_overlay_table();
    const BwOverlayFunc *m;
    u32 i;
    u32 bad_slice = 0;
    u32 bad_sort = 0;
    u32 bad_count = 0;
    u32 bad_id = 0;
    u32 total_present = 0;
    char why[512];

    printf("generated table\n");
    BW_REQUIRE("the generated table is the default", t == &bw_overlay_table_generated);
    BW_CHECK_EQ("237 overlays", t->overlay_count, 237);
    for (i = 0; i < t->overlay_count; i++) {
        const BwOverlayDesc *d = &t->overlays[i];
        u32 k;
        u32 matched = 0;
        u32 present = 0;

        if (d->id != i) {
            bad_id++;
        }
        if (d->func_first + d->func_count > t->func_count ||
            d->sinit_first + d->sinit_count > t->sinit_count) {
            bad_slice++;
            continue;
        }
        for (k = 0; k < d->func_count; k++) {
            const BwOverlayFunc *f = &t->funcs[d->func_first + k];

            if (k > 0 && f->address <= t->funcs[d->func_first + k - 1].address) {
                bad_sort++;
            }
            if (f->address < d->ram_address || f->address >= d->ram_address + d->ram_size) {
                bad_slice++;
            }
            matched += f->status != BW_OVF_OPEN;
            present += f->status == BW_OVF_PRESENT;
        }
        if (matched != d->matched_count || present != d->present_count) {
            bad_count++;
        }
        total_present += present;
    }
    BW_CHECK_EQ("every id is its index", bad_id, 0);
    BW_CHECK_EQ("every slice in bounds, every address in its overlay", bad_slice, 0);
    BW_CHECK_EQ("every slice sorted", bad_sort, 0);
    BW_CHECK_EQ("counts agree with statuses", bad_count, 0);
    printf("  %lu known functions, %lu present in this build\n",
           (unsigned long)t->func_count, (unsigned long)total_present);

    BW_CHECK_EQ("ov9 ram", t->overlays[9].ram_address, 0x02155160u);
    BW_CHECK_EQ("ov9 code size", t->overlays[9].ram_size, 0x53100u);
    BW_CHECK_EQ("ov9 bss", t->overlays[9].bss_size, 0x68C0u);
    BW_CHECK_EQ("ov230 has five initialisers", t->overlays[230].sinit_count, 5);
    BW_CHECK_EQ("  the first", t->sinits[t->overlays[230].sinit_first].address, 0x02188408u);
    BW_CHECK_EQ("  the last", t->sinits[t->overlays[230].sinit_first + 4].address, 0x02189908u);
    BW_CHECK_EQ("  initialiser list length",
                t->overlays[230].sinit_init_end - t->overlays[230].sinit_init, 6 * 4);
    BW_CHECK_EQ("ov0 has none", t->overlays[0].sinit_count, 0);

    if (t->overlays[9].present_count < t->overlays[9].func_count) {
        BW_CHECK_EQ("ov9 refused", bw_overlay_check(9, &m), BW_OVL_ABSENT);
        BW_REQUIRE("  with a missing function", m != NULL);
        BW_CHECK_EQ("  in ov9's range", m->address >= 0x02155160u && m->address < 0x021A8260u, 1);
        bw_overlay_describe(9, why, sizeof(why));
        printf("  %s\n", why);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Against the ROM's own tables                                         */
/* ------------------------------------------------------------------ */

static u8 *slurp(const char *path, u32 *size)
{
    FILE *fp = fopen(path, "rb");
    long end;
    u8 *b;

    *size = 0;
    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    end = ftell(fp);
    rewind(fp);
    b = (u8 *)malloc((size_t)end + 1);
    if (b != NULL && fread(b, 1, (size_t)end, fp) != (size_t)end) {
        free(b);
        b = NULL;
    }
    fclose(fp);
    *size = (u32)end;
    return b;
}

static int real_data(const char *root)
{
    char path[1200];
    size_t n = strlen(root);
    u8 *y9;
    u8 *img;
    u32 y9_size;
    u32 img_size;
    u32 i;
    u32 bad_hdr = 0;
    u32 bad_ext = 0;
    BwOverlayInfo info;
    const BwOverlayDesc *d;

    while (n > 0 && root[n - 1] != '/' && root[n - 1] != '\\') {
        n--;
    }
    snprintf(path, sizeof(path), "%.*sy9.bin", (int)n, root);
    y9 = slurp(path, &y9_size);
    if (y9 == NULL) {
        printf("real data: no y9.bin beside %s, skipped\n", root);
        return 0;
    }
    printf("real data: %s\n", root);
    bw_set_data_root(root);
    bw_fs_set_tables(NULL, 0, NULL, 0);
    BW_CHECK_EQ("y9.bin has 237 entries", y9_size / 32, 237);
    for (i = 0; i < y9_size / 32; i++) {
        const u8 *e = y9 + i * 32;
        u32 top;
        u32 bottom;

        if (!FS_LoadOverlayInfo(&info, 0, i) ||
            info.id != bw_rd32(e) || info.ram_address != bw_rd32(e + 4) ||
            info.ram_size != bw_rd32(e + 8) || info.bss_size != bw_rd32(e + 12) ||
            info.sinit_init != bw_rd32(e + 16) || info.sinit_init_end != bw_rd32(e + 20) ||
            info.file_id != bw_rd32(e + 24) || info.compressed != bw_rd32(e + 28)) {
            bad_hdr++;
            continue;
        }
        if (bw_fs_file_extent(info.file_id, &top, &bottom) != 0 ||
            info.file_offset != top || info.file_length != bottom - top) {
            bad_ext++;
        }
    }
    BW_CHECK_EQ("every header word matches y9.bin", bad_hdr, 0);
    BW_CHECK_EQ("every file extent matches the FAT", bad_ext, 0);
    free(y9);

    d = bw_overlay_desc(230);
    snprintf(path, sizeof(path), "%.*soverlays_dec/y9_0230.bin", (int)n, root);
    img = slurp(path, &img_size);
    if (img == NULL || d == NULL) {
        printf("  no decompressed ov230, its initialiser check skipped\n");
    } else {
        u32 off = d->sinit_init - d->ram_address;
        u32 k;
        u32 bad = 0;
        const BwOverlayTable *t = bw_overlay_table();

        BW_REQUIRE("ov230's list is inside its image", off + 24 <= img_size);
        for (k = 0; k < d->sinit_count; k++) {
            if (bw_rd32(img + off + k * 4) != t->sinits[d->sinit_first + k].address) {
                bad++;
            }
        }
        BW_CHECK_EQ("ov230's initialisers are the image's", bad, 0);
        BW_CHECK_EQ("  and the list is NULL-terminated", bw_rd32(img + off + d->sinit_count * 4), 0);
    }
    free(img);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2 && strcmp(argv[1], "--load") == 0) {
        /* Expected to panic: ctest's test_overlay_refuses reads the message. */
        FS_Init(1);
        FS_LoadOverlay(0, (u32)strtoul(argv[2], NULL, 0));
        printf("FS_LoadOverlay returned\n");
        return 0;
    }
    if (synthetic() != 0) {
        BW_REPORT("test_overlay");
    }
    if (generated() != 0) {
        BW_REPORT("test_overlay");
    }
    if (argc > 1 && argv[1] != NULL && argv[1][0] != '\0') {
        (void)real_data(argv[1]);
    } else {
        printf("real data: no data root given, skipped\n");
    }
    BW_REPORT("test_overlay");
}
