/*
 * function: func_02200000
 * address:  0x02200000
 * region:   ov001
 *
 * Synthetic (tests/test_ovlink.c): the same name and window as ov000's, another function
 *
 * symbols:
 *   d_02200100   0x02200100  data  ov001
 */

extern unsigned int d_02200100[];

int func_02200000(int x)
{
    return x * (int)d_02200100[0];
}
