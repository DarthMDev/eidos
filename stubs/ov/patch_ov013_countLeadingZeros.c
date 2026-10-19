/*
 * patches: src/game/ov013/vct_audio.c countLeadingZeros
 *
 * countLeadingZeros in vct_audio.c is a static inline wrapper around one
 * ARM `clz` (inline asm), which the host compiler cannot build, so the
 * unit's copy has it cut.  The same count, in C: 32 for zero, as the
 * instruction gives.
 */

typedef unsigned int u32;

u32 countLeadingZeros(u32 x)
{
    return x != 0 ? (u32)__builtin_clz(x) : 32u;
}
