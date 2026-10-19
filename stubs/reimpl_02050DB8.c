/*
 * PARTICLE_DeleteSystem -- REIMPLEMENTATION of the pinned aletheia's
 * src/matched/arm9/func_02050DB8.c, for one call.
 *
 * The matched C calls `PARTICLE_KillAllEffects()` with no argument.  The
 * callee takes the particle system, and on the DS it finds it in r0, where
 * PARTICLE_DeleteSystem's own argument still is; on a host it reads whatever
 * is on the stack and walks a garbage list (the intro crashed here when it
 * closed, after the player picks boy or girl).  Here the system is passed.
 * aletheia's src/game passes it (particle.c), so this file goes when the
 * port compiles src/game (docs/game-tree.md).  Everything else is the
 * matched function, unchanged; the record's fields are reached by offset
 * (Obj01319: f4 +0x04, f8 +0x08, f20 +0x20, f32 +0x32, the mode byte
 * +0x258).  cmake/boot.cmake lists the matched file as replaced.
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

extern void PARTICLE_KillAllEffects(void *sys);
extern void NNS_GfdLoadVramManager(u32 *src);
extern u32 NNS_GfdFreeTexVram(u32 h);
extern void NNSi_GfdSetVramManagerWork(void *p);
extern int NNS_GfdFreePlttVram(u32 id);
extern u32 func_020307B0(void *p);
extern u32 G3D_CAMERA_Delete(void *ptr);
extern void *g_pParticleMan;

#define F32(p, off) (*(u32 *)((p) + (off)))

void PARTICLE_DeleteSystem(u8 *p)
{
    int i;
    u32 z = 0;

    PARTICLE_KillAllEffects(p);

    if (p[0x258] & 1) {
        NNS_GfdLoadVramManager((u32 *)(p + 0x58));
    } else if (p[0x258] & 2) {
        for (i = 0; i < 0x40; i++) {
            if (((u32 *)(p + 0x58))[i] != 0) {
                NNS_GfdFreeTexVram(((u32 *)(p + 0x58))[i]);
                *(u32 *)(p + i * 4 + 0x58) = z;
            }
        }
    }

    if (p[0x258] & 4) {
        NNSi_GfdSetVramManagerWork(p + 0x158);
    } else if (p[0x258] & 8) {
        for (i = 0; i < 0x40; i++) {
            if (((u32 *)(p + 0x158))[i] != 0) {
                NNS_GfdFreePlttVram(((u32 *)(p + 0x158))[i]);
                *(u32 *)(p + i * 4 + 0x158) = z;
            }
        }
    }

    {
        u32 h = F32(p, 0x04);
        p[0x258] = (u8)z;
        F32(p, 0x08) = z;
        if (h != 0) {
            if ((*(u16 *)(p + 0x32) & 1) == 0) {
                func_020307B0((void *)h);
                F32(p, 0x04) = z;
            }
        }
    }

    {
        int j;
        for (j = 0; j < 0x10; j++) {
            u32 *slots = (u32 *)g_pParticleMan;
            if (slots[j] == (u32)p) {
                slots[j] = z;
                break;
            }
        }
    }

    if (F32(p, 0x20) != 0)
        G3D_CAMERA_Delete((void *)F32(p, 0x20));

    func_020307B0(p);
}
