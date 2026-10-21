/*
 * sub_02060F90 -- the expanded heap's "how big is this block" query.
 *
 * LP64 ONLY now.  aletheia has matched the heap environment constructor at
 * 0x0202FC48 and the expanded heap under it, and where EIDOS_HEAP_MATCHED is
 * on (32-bit pointers, the reference configuration) the matched source is
 * compiled in this file's place.  On an LP64 host the expanded heap cannot
 * run (docs/runtime.md, "The heap environment"), so this stays that host's heap
 * floor.  The text below is from before the constructor was matched and
 * records why the stand-in was written the way it was.
 *
 * src/matched/sub_020309B8.c, the free-side audit, hands it the block header
 * address (`payload - 0x1c`, the same address the expanded-heap allocator
 * handed back) and discards the answer; the audit then dumps the heap the
 * block's id names.  Reproducing it means reading the block header
 * func_02060888 writes, and func_02060888 is open, so there is nothing here
 * to be honest about yet.
 *
 * It is unreachable in practice, and for the same reason
 * stubs/stub_02060E6C.c is: src/matched/sub_02030A2C.c runs the audit only
 * when the watched-heap id g_020A7820 names this block's heap, and the
 * port's own value for that word is -1 ("watch nothing"), which is the
 * retail one.  If a run ever reaches this the port would rather say so than
 * answer a size it invented.
 */

#include "stubs.h"

BW_STUB(sub_02060F90, 0x02060F90,
        "the expanded heap's block-size query; needs func_02060888's block "
        "header, which is still open")
