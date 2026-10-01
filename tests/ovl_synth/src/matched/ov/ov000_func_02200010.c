/*
 * function: func_02200010
 * address:  0x02200010
 * region:   ov000
 *
 * Synthetic (tests/test_ovlink.c): a call inside the overlay
 *
 * symbols:
 *   func_02200000 0x02200000  thumb  ov000
 */

int func_02200000(int x);

int func_02200010(int x)
{
    return func_02200000(x) + 100;
}
