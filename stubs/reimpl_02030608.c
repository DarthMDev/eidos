/*
 * sub_02030608 -- build the heap environment or stop.  REIMPLEMENTATION of
 * aletheia's EXACT src/matched/arm9/sub_02030608.c, for the reason
 * stubs/reimpl_0203064C.c gives: a register-forwarded argument.
 *
 * The matched source is
 *
 *     void sub_02030608(u32 a, u32 b, u32 c)
 *     {
 *         int ok = sub_0202FC48(a, b, c);
 *         sub_02086580(0);                  OS_GetArenaLo, result unused
 *         sub_0208656C(0);                  OS_GetArenaHi, result unused
 *         if (ok != 0) return;
 *         sub_02088234(sub_02030524());     OS_Terminate
 *     }
 *
 * while its caller passes four arguments (src/matched/arm9/sub_0200545C.c:
 * `(d_0209D8B0, 2, 0xAD, 0)`, the resource table, the heap count, the id
 * count and an extra reservation) and the constructor
 * (src/matched/arm9/sub_0202FC48.c) takes four: the fourth reaches it in r3,
 * which nothing in between writes.  On the host it is said out loud; an
 * indeterminate fourth argument would carve that many bytes off arena 0
 * before the heaps.  cmake/boot.cmake lists the matched file as replaced.
 */

#include "stubs.h"

struct BwEnvRes;
int   sub_0202FC48(const struct BwEnvRes *res, u16 count, u16 idCount, u32 extra);
void *OS_GetArenaLo(u32 id);
void *OS_GetArenaHi(u32 id);
u16   sub_02030524(void);
void  OS_Terminate(void);
void  sub_02030608(const struct BwEnvRes *res, u32 count, u32 idCount, u32 extra);

void sub_02030608(const struct BwEnvRes *res, u32 count, u32 idCount, u32 extra)
{
    int ok = sub_0202FC48(res, (u16)count, (u16)idCount, extra);

    (void)OS_GetArenaLo(0);
    (void)OS_GetArenaHi(0);
    if (ok != 0) {
        return;
    }
    (void)sub_02030524();
    OS_Terminate();
}
