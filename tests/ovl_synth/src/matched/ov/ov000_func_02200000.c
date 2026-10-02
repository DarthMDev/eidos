/*
 * function: func_02200000
 * address:  0x02200000
 * region:   ov000
 *
 * Synthetic (tests/test_ovlink.c): adds the first word of the overlay's table
 *
 * symbols:
 *   d_02200100   0x02200100  data  ov000
 */

extern unsigned int d_02200100[];

int func_02200000(int x)
{
    return x + (int)d_02200100[0];
}
