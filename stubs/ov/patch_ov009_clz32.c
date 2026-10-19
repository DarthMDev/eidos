/*
 * patches: src/game/ov009/wcm_wmif.c clz32
 *
 * clz32 in wcm_wmif.c is a static inline wrapper around one ARM `clz`
 * (inline asm), which the host compiler cannot build, so the unit's copy
 * has it cut.  The same count, in C: the number of leading zero bits, 32
 * for zero as the instruction gives.
 */

unsigned long clz32(unsigned long x)
{
    return x != 0 ? (unsigned long)__builtin_clz((unsigned int)x) : 32u;
}
