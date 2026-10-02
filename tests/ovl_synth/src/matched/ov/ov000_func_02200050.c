/*
 * function: func_02200050
 * address:  0x02200050
 * region:   ov000
 *
 * Synthetic (tests/test_ovlink.c): a file that also carries a copy of
 * func_02200000, which ov000_func_02200000.c owns (its manifest entry
 * claims it); the copy must not clash with the owner's at link time.
 *
 * symbols:
 *   d_02200100   0x02200100  data  ov000
 */

extern unsigned int d_02200100[];

int func_02200000(int x)
{
    return x + (int)d_02200100[0];
}

int func_02200050(int x)
{
    return func_02200000(x) * 10;
}
