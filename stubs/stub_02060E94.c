/*
 * sub_02060E94 -- the expanded heap's dump, mode 4.  NOT MATCHED.
 *
 * The sibling of sub_02060E6C, called immediately after it by
 * src/matched/sub_02030958.c with a mode of 4 and its answer discarded.
 * `open` in aletheia's layout/worklist.json (packet pkt_02060764, 132
 * bytes).  Same reasoning as stub_02060E6C.c: no arena, and unreachable
 * except on a path that ends in OS_Terminate.
 */
#include "stubs.h"

BW_STUB(sub_02060E94, 0x02060E94, "expanded heap dump: the port has no arena to dump")
