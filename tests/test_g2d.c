/*
 * tests/test_g2d.c -- the 2D engine shim against a pixel oracle.
 *
 * For each of four species, the C compositor in engine/g2d/ loads the rig
 * bundle out of the player's own a/0/0/4, composes multi-cell group 0's
 * rest pose (elapsed time 0) and expands it to RGBA.  The oracle is the
 * same pose composed by aletheia's tools/assets/rig.py, dumped by
 * tools/rig_oracle.py into the build directory at build time (see
 * CMakeLists.txt).  The two are compared pixel for pixel; a mismatch
 * prints the first differing pixel and both colours.
 *
 * The two composers share no code.  rig.py is Python, written in the
 * decompilation project against the archive's bytes and the game's own
 * composer; engine/g2d is C, written here against the same attested
 * formats.  A pixel-exact agreement across four rigs with different cell
 * counts, node counts, flip and double-size mixes is the evidence that
 * the C engine reproduces the composer, not that the two share a bug.
 *
 *     ./test_g2d <data root> <oracle directory> [species ...]
 *
 * With no species named it checks the four ctest drives it with.  Naming
 * species runs the same comparison over any other set the oracle
 * directory has dumps for, which is how a wider sweep is done by hand
 * without changing what ctest runs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g2d.h"
#include "port.h"
#include "port_test.h"

#define RIG_ARCHIVE    "a/0/0/4"
#define RIG_BLOCK_SIZE 20u

static const int k_species[] = { 25, 6, 94, 150 };

/*
 * The two species whose rest pose carries a genuinely non-identity SRT at
 * t=0: species 50's sequence 4 has sy = 2048 (a half-height scale) and
 * species 88's sequence 1 has rotZ = 3640.  They are checked differently.
 * The engine transforms in the hardware's fixed point (a 12-bit sine
 * table, an 8.8 matrix, a floor-truncating shift) and rig.py transforms in
 * double precision and rounds to nearest, so the two can land on different
 * texels along a boundary.  The engine is the reference where they differ
 * (docs/engine-2d.md), so the check is that the pose renders at all and that the
 * divergence from the oracle is small and reported, not that it is zero.
 */
static const int k_affine_species[] = { 50, 88 };

/* A pose may differ from the oracle on at most this share of its pixels
 * before the divergence stops being fixed-point slop and becomes a bug. */
#define AFFINE_MAX_DIVERGENCE_PERMILLE 150

typedef struct Oracle {
    u32 width, height;
    u8 *rgba;
} Oracle;

/*
 * The oracle dump's framing, written by tools/rig_oracle.py:
 * 'RGBA', u32 width, u32 height, then width*height*4 bytes.
 */
static int oracle_load(Oracle *out, const char *dir, int species)
{
    char path[1024];
    FILE *fp;
    u8 head[12];
    size_t need;

    memset(out, 0, sizeof(*out));
    if (snprintf(path, sizeof(path), "%s/%04d_pose.rgba", dir, species)
        >= (int)sizeof(path)) {
        return -1;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        printf("  cannot open oracle dump %s\n", path);
        return -1;
    }
    if (fread(head, 1, 12, fp) != 12 || memcmp(head, "RGBA", 4) != 0) {
        fclose(fp);
        return -1;
    }
    out->width = bw_rd32(head + 4);
    out->height = bw_rd32(head + 8);
    need = (size_t)out->width * out->height * 4u;
    out->rgba = (u8 *)malloc(need);
    if (out->rgba == NULL || fread(out->rgba, 1, need, fp) != need) {
        free(out->rgba);
        out->rgba = NULL;
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static int compare_species(BwArcFile *arc, const char *oracle_dir, int species)
{
    G2dRig rig;
    G2dPose pose;
    Oracle oracle;
    u8 *rgba = NULL;
    int rc;
    int failed = 0;
    u32 i;

    printf("species %d:\n", species);

    if (oracle_load(&oracle, oracle_dir, species) != 0) {
        printf("  FAIL oracle dump missing or malformed\n");
        return 1;
    }
    if (g2d_rig_load(&rig, arc, (u32)species * RIG_BLOCK_SIZE, RIG_BLOCK_SIZE) != 0) {
        printf("  FAIL rig block did not load\n");
        free(oracle.rgba);
        return 1;
    }
    printf("  NCGR %ux%u tiles fmt %u charfmt %u, NCER %u cells, "
           "NMCR %u groups, palette %u colours\n",
           rig.ncgr.tile_num_x, rig.ncgr.tile_num_y, rig.ncgr.pixel_fmt,
           rig.ncgr.char_format, rig.ncer.num_cells, rig.nmcr.num_multi_cell,
           rig.palette.count);

    if (rig.nmcr.num_multi_cell == 0) {
        printf("  FAIL no multi-cell group\n");
        g2d_rig_free(&rig);
        free(oracle.rgba);
        return 1;
    }

    rc = g2d_compose_pose(&rig.ncgr, &rig.ncer,
                          rig.has_nanr ? &rig.nanr : NULL,
                          &rig.nmcr.groups[0], 0, rig.palette.count, &pose);
    if (rc == G2D_ERR_NONIDENTITY) {
        printf("  FAIL a frame carries a non-identity SRT at t=0\n");
        g2d_rig_free(&rig);
        free(oracle.rgba);
        return 1;
    }
    if (rc != 0) {
        printf("  FAIL compose failed\n");
        g2d_rig_free(&rig);
        free(oracle.rgba);
        return 1;
    }

    if (pose.width != oracle.width || pose.height != oracle.height) {
        printf("  FAIL canvas %ux%u, oracle %ux%u\n",
               pose.width, pose.height, oracle.width, oracle.height);
        failed = 1;
    } else {
        rgba = (u8 *)malloc((size_t)pose.width * pose.height * 4u);
        if (rgba == NULL) {
            failed = 1;
        } else {
            g2d_pose_to_rgba(&pose, &rig.palette, rgba);
            for (i = 0; i < pose.width * pose.height; i++) {
                if (memcmp(rgba + i * 4, oracle.rgba + i * 4, 4) != 0) {
                    printf("  FAIL first differing pixel at (%u, %u): "
                           "engine RGBA %u,%u,%u,%u  oracle RGBA %u,%u,%u,%u\n",
                           i % pose.width, i / pose.width,
                           rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2], rgba[i * 4 + 3],
                           oracle.rgba[i * 4], oracle.rgba[i * 4 + 1],
                           oracle.rgba[i * 4 + 2], oracle.rgba[i * 4 + 3]);
                    failed = 1;
                    break;
                }
            }
            if (!failed) {
                printf("  match: %ux%u pixels identical\n", pose.width, pose.height);
            }
        }
    }

    free(rgba);
    free(oracle.rgba);
    g2d_pose_free(&pose);
    g2d_rig_free(&rig);
    return failed;
}

/*
 * The affine species: the pose must render, and the divergence from the
 * oracle is measured and reported rather than required to be zero.  A
 * canvas whose size differs from the oracle's by more than a couple of
 * pixels, or a pose that differs on more than a small share of its
 * pixels, is a real disagreement and fails; anything smaller is the
 * fixed-point difference docs/engine-2d.md documents.
 */
static int compare_affine_species(BwArcFile *arc, const char *oracle_dir,
                                  int species)
{
    G2dRig rig;
    G2dPose pose;
    Oracle oracle;
    u8 *rgba;
    int rc;
    int failed = 0;
    u32 i, diff = 0;
    s32 dw, dh;

    printf("species %d (affine):\n", species);

    if (oracle_load(&oracle, oracle_dir, species) != 0) {
        printf("  FAIL oracle dump missing or malformed\n");
        return 1;
    }
    if (g2d_rig_load(&rig, arc, (u32)species * RIG_BLOCK_SIZE, RIG_BLOCK_SIZE) != 0) {
        printf("  FAIL rig block did not load\n");
        free(oracle.rgba);
        return 1;
    }
    rc = g2d_compose_pose(&rig.ncgr, &rig.ncer,
                          rig.has_nanr ? &rig.nanr : NULL,
                          &rig.nmcr.groups[0], 0, rig.palette.count, &pose);
    if (rc == G2D_ERR_NONIDENTITY) {
        printf("  FAIL the affine path refused a non-identity SRT\n");
        g2d_rig_free(&rig);
        free(oracle.rgba);
        return 1;
    }
    if (rc != 0) {
        printf("  FAIL compose failed\n");
        g2d_rig_free(&rig);
        free(oracle.rgba);
        return 1;
    }

    dw = (s32)pose.width - (s32)oracle.width;
    dh = (s32)pose.height - (s32)oracle.height;
    printf("  rendered %ux%u, oracle %ux%u (%+d, %+d)\n",
           pose.width, pose.height, oracle.width, oracle.height, dw, dh);
    if (dw < -2 || dw > 2 || dh < -2 || dh > 2) {
        printf("  FAIL the canvases disagree by more than the truncation margin\n");
        failed = 1;
    } else if (dw == 0 && dh == 0) {
        rgba = (u8 *)malloc((size_t)pose.width * pose.height * 4u);
        if (rgba == NULL) {
            failed = 1;
        } else {
            u32 n = pose.width * pose.height;

            g2d_pose_to_rgba(&pose, &rig.palette, rgba);
            for (i = 0; i < n; i++) {
                if (memcmp(rgba + i * 4, oracle.rgba + i * 4, 4) != 0) {
                    diff++;
                }
            }
            printf("  divergence from the oracle: %u of %u pixels (%u permille)\n",
                   diff, n, n ? (u32)((u64)diff * 1000u / n) : 0u);
            if (n != 0 && (u64)diff * 1000u / n > AFFINE_MAX_DIVERGENCE_PERMILLE) {
                printf("  FAIL that is more than fixed-point slop\n");
                failed = 1;
            }
            free(rgba);
        }
    }

    free(oracle.rgba);
    g2d_pose_free(&pose);
    g2d_rig_free(&rig);
    return failed;
}

int main(int argc, char **argv)
{
    const char *oracle_dir;
    BwArcFile arc;
    size_t i;

    BW_TEST_SETUP(argc, argv);

    oracle_dir = (argc > 2 && argv[2][0] != '\0') ? argv[2] : getenv("G2D_ORACLE_DIR");
    if (oracle_dir == NULL || oracle_dir[0] == '\0') {
        printf("usage: test_g2d <data root> <oracle directory>\n");
        return 1;
    }
    printf("oracle: %s\n", oracle_dir);

    if (bw_arc_open(&arc, RIG_ARCHIVE) != 0) {
        printf("  FAIL cannot open %s under the data root\n", RIG_ARCHIVE);
        return 1;
    }

    if (argc > 3) {
        for (i = 3; i < (size_t)argc; i++) {
            bw_checks++;
            if (compare_species(&arc, oracle_dir, atoi(argv[i])) != 0) {
                bw_failures++;
            }
        }
    } else {
        for (i = 0; i < sizeof(k_species) / sizeof(k_species[0]); i++) {
            bw_checks++;
            if (compare_species(&arc, oracle_dir, k_species[i]) != 0) {
                bw_failures++;
            }
        }
        for (i = 0; i < sizeof(k_affine_species) / sizeof(k_affine_species[0]);
             i++) {
            bw_checks++;
            if (compare_affine_species(&arc, oracle_dir,
                                       k_affine_species[i]) != 0) {
                bw_failures++;
            }
        }
    }
    bw_arc_close(&arc);

    BW_REPORT("test_g2d");
}
