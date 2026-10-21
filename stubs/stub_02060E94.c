/*
 * sub_02060E94 -- the expanded heap's dump, mode 4.  NOT MATCHED.
 *
 * LP64 ONLY now.  aletheia has matched the heap environment constructor at
 * 0x0202FC48 and the expanded heap under it, and where EIDOS_HEAP_MATCHED is
 * on (32-bit pointers, the reference configuration) the matched source is
 * compiled in this file's place.  On an LP64 host the expanded heap cannot
 * run (docs/runtime.md, "The heap environment"), so this stays that host's heap
 * floor.  The text below is from before the constructor was matched and
 * records why the stand-in was written the way it was.
 *
 * The sibling of sub_02060E6C, called immediately after it by
 * src/matched/sub_02030958.c with a mode of 4 and its answer discarded.
 * `open` in aletheia's layout/worklist.json (packet pkt_02060764, 132
 * bytes).  Same reasoning as stub_02060E6C.c: no arena, and unreachable
 * except on a path that ends in OS_Terminate.
 */
#include "stubs.h"

BW_STUB(sub_02060E94, 0x02060E94, "expanded heap dump: the port has no arena to dump")
