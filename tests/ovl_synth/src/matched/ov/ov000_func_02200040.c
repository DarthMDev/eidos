/*
 * function: func_02200040
 * address:  0x02200040
 * region:   ov000
 *
 * Synthetic (tests/test_ovlink.c): counts in the overlay's .bss
 *
 * symbols:
 *   d_02200200   0x02200200  data  ov000
 */

extern int d_02200200;

int func_02200040(void)
{
    d_02200200++;
    return d_02200200;
}
