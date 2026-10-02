/*
 * function: func_02100000
 * address:  0x02100000
 * region:   arm9
 *
 * Synthetic (tests/test_ovlink.c): ARM9 code calling into the window
 *
 * symbols:
 *   func_02200000 0x02200000  thumb  ov000
 */

int func_02200000(int x);

int func_02100000(int x)
{
    return func_02200000(x);
}
