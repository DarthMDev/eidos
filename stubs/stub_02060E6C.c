/*
 * sub_02060E6C -- the expanded heap's first-block query.  NOT MATCHED.
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
