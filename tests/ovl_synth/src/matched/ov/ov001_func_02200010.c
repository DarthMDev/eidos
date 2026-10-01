/*
 * function: func_02200010
 * address:  0x02200010
 * region:   ov001
 *
 * Synthetic (tests/test_ovlink.c): calls func_02200000 with no declaration in scope (C89 implicit int)
 *
 * symbols:
 *   (none)
 */

int func_02200010(int x)
{
    return func_02200000(x) + 1;
}
