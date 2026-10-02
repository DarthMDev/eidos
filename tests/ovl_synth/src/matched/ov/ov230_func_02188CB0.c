/*
 * function: func_02188CB0
 * address:  0x02188CB0
 * region:   ov230
 *
 * Synthetic (tests/test_ovlink.c): static initialiser 3 of 5; appends its
 * digit to the counter in the overlay's .bss, so the order shows.
 *
 * symbols:
 *   d_02189A00   0x02189A00  data  ov230
 */

extern int d_02189A00;

void func_02188CB0(void)
{
    d_02189A00 = d_02189A00 * 10 + 3;
}
