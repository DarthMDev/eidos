/*
 * sub_02060E6C -- the expanded heap's first-block query.  NOT MATCHED.
 *
 * LP64 ONLY now.  aletheia has matched the heap environment constructor at
 * 0x0202FC48 and the expanded heap under it, and where EIDOS_HEAP_MATCHED is
 * on (32-bit pointers, the reference configuration) the matched source is
 * compiled in this file's place.  On an LP64 host the expanded heap cannot
 * run (README.md, "The heap environment"), so this stays that host's heap
 * floor.  The text below is from before the constructor was matched and
 * records why the stand-in was written the way it was.
 *
 * One caller, src/matched/sub_02030958.c, the out-of-memory reporter: it
 * takes the arena handle sub_02030428 resolved, walks it with this function
 * and dumps it with sub_02060E94, discarding both answers.  It belongs to
 * the same library block around 0x02060000 as sub_02060CA0 and is `open` in
 * aletheia's layout/worklist.json (packet pkt_02060764, 40 bytes).
 *
 * The port has no arena to walk: stubs/reimpl_02060A08.c and
 * stubs/reimpl_02060ABC.c take memory from the host rather than from one.  There is nothing to reimplement and the
 * call is unreachable unless an allocation has already failed.
 */
#include "stubs.h"

BW_STUB(sub_02060E6C, 0x02060E6C, "expanded heap walk: the port has no arena to walk")
