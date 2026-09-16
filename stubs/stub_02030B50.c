/*
 * sub_02030B50 -- the heap report printed just before the game gives up.
 * NOT MATCHED.
 *
 * Reached from exactly one place: src/matched/sub_02030958.c, the
 * out-of-memory reporter, which src/matched/sub_02030734.c calls on the
 * failure arm one instruction before OS_Terminate.  aletheia has it at
 * src/pending/sub_02030B50.c, status MATCH (all 84 concrete bytes correct,
 * three of nine relocation sites with no witness), and its header describes
 * the body: four whole-heap queries whose answers are thrown away, then a
 * walk of every live block through the visitor sub_02030A7C, bracketed by
 * calls to 0x0209128C.  A release build's remains of four printed lines.
 *
 * The port aborts rather than reimplementing it.  It is unreachable unless
 * the host allocator has already failed, and at that point the process is
 * one call from OS_Terminate anyway; printing the port's own diagnostic is
 * more use than a reconstruction of a diagnostic that was compiled out.
 */
#include "stubs.h"

BW_STUB(sub_02030B50, 0x02030B50, "heap report: reached only after the host allocator failed")
