/*
 * tests/test_ovlink_title.c -- every overlay of the EIDOS_OVERLAYS set
 * (`bw_ovl_set_boot`, the title path by default) loaded from the player's
 * extract through FS_LoadOverlay, in turn, each checked resident and its
 * image in its window (docs/overlays.md).
 *
 * A load copies the file, binds the overlay's names and checks every listed
 * pointer word against the metadata before relocating it, so a pass says
 * the generated tables and the player's files agree for the whole set.
 * Built only when EIDOS_OVERLAYS names a set; without the extract it
 * reports itself skipped.
 */

#include <stdio.h>
#include <string.h>

#include "port.h"
#include "dsmem.h"
#include "armdata.h"
#include "overlay.h"
#include "overlay_image.h"
#include "port_test.h"

extern BOOL FS_LoadOverlay(u32 target, u32 id);

extern const BwOvlImageSet bw_ovl_set_boot;

int main(int argc, char **argv)
{
    u32 i;

    BW_TEST_SETUP(argc, argv);
    for (i = 0; i < bw_ovl_set_boot.count; i++) {
        if (bw_ovl_file_bytes(bw_ovl_set_boot.images[i]->id, 0, 4) == NULL) {
            printf("SKIPPED: overlay %lu's file is not in the extract\n",
                   (unsigned long)bw_ovl_set_boot.images[i]->id);
            return 0;
        }
    }
    bw_ds_reset();
    bw_overlay_sdk_init();
    bw_ovl_install(&bw_ovl_set_boot, BW_DATA_ALLOW_UNRELOCATED);

    for (i = 0; i < bw_ovl_set_boot.count; i++) {
        const BwOvlImage *img = bw_ovl_set_boot.images[i];
        const BwOverlayDesc *d = bw_overlay_desc(img->id);
        const u8 *file;
        char label[64];

        BW_REQUIRE("descriptor", d != NULL);
        snprintf(label, sizeof(label), "load ov%03lu", (unsigned long)img->id);
        BW_REQUIRE(label, FS_LoadOverlay(0, img->id));
        snprintf(label, sizeof(label), "ov%03lu resident", (unsigned long)img->id);
        BW_CHECK_EQ(label, bw_ovl_resident_at(d->ram_address), (long)img->id);
        /* The first word of the window is the file's (code: never relocated). */
        file = (const u8 *)bw_ovl_file_bytes(img->id, 0, 4);
        snprintf(label, sizeof(label), "ov%03lu first word", (unsigned long)img->id);
        BW_CHECK_EQ(label, bw_rd32((const u8 *)bw_ds_ptr(d->ram_address)), bw_rd32(file));
        printf("  ov%03lu: %lu functions compiled, %lu stubs, %lu pointer words, %lu names\n",
               (unsigned long)img->id, (unsigned long)img->defined_count,
               (unsigned long)img->stub_count, (unsigned long)img->data->reloc_count,
               (unsigned long)img->data->name_count);
    }
    bw_ovl_install(NULL, 0);
    BW_REPORT("test_ovlink_title");
}
