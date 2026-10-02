/*
 * function: func_02300000
 * address:  0x02300000
 * region:   ov002
 *
 * Synthetic (tests/test_ovlink.c): calls into the other window, whichever overlay is there
 *
 * symbols:
 *   func_02200000 0x02200000  thumb  ov000
 */

int func_02200000(int x);

int func_02300000(int x)
{
    return func_02200000(x) * 2;
}
