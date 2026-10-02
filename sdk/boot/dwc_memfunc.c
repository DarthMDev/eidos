/*
 * sdk/boot/dwc_memfunc.c -- the DWC library's allocator hooks
 * (dwc_memfunc.o, TwlDWC libdwccommon), with the one table they share
 * (sdk/boot/gx.c says why sdk/boot/ exists).
 *
 * Why a shim.  The three functions of dwc_memfunc.o share a file-local
 * table, `s_mem` at DS 0x02146AB4 ({ free, alloc }).  aletheia splits the
 * object file into one matched file per function, and each keeps its own
 * view of the table: DWCi_SetMemFunc (src/matched/arm9_sdk/func_0205A158.c)
 * and DWC_Free (func_0205A1B0.c) define it `static`, DWC_AllocEx
 * (func_0205A17C.c) declares it `extern`.  On the DS all three are one
 * object at one address.  On a host the two statics are two separate
 * objects and the extern a third (bound to the DS address,
 * docs/armdata.md), so the allocator DWCi_SetMemFunc installs is never the
 * one DWC_AllocEx calls: the boot called a null pointer there, in the
 * network library's initialisation the boot reaches through ov009
 * (docs/overlays.md).  The three functions are written here over one table,
 * as dwc_memfunc.o has them; cmake/boot.cmake lists the matched files as
 * replaced.  The table holds host function pointers, so it is a port object,
 * not the DS bytes at 0x02146AB4; nothing else names that address.
 */

#include <stddef.h>

typedef void *(*BwDwcAlloc)(int name, unsigned long size, int align);
typedef void (*BwDwcFree)(int name, void *ptr, unsigned long size);

/* dwc_memfunc.o's s_mem: free at +0, alloc at +4 on the DS. */
static struct {
    BwDwcFree  free;
    BwDwcAlloc alloc;
} s_mem;

/* The allocation header in front of every DWC block (0x20 bytes), written
 * and found by the matched helpers around these. */
void *DWCi_SetAllocateHeader(void *ptr, unsigned long size);
void *func_0205A12C(void *ptr);

void DWCi_SetMemFunc(BwDwcAlloc alloc, BwDwcFree free);
void *DWC_AllocEx(int name, unsigned long size, int align);
void DWC_Free(int name, void *ptr);

/* DWCi_SetMemFunc, 0x0205A158. */
void DWCi_SetMemFunc(BwDwcAlloc alloc, BwDwcFree free)
{
    s_mem.alloc = alloc;
    s_mem.free = free;
}

/* DWC_AllocEx, 0x0205A17C: the block with its header in front. */
void *DWC_AllocEx(int name, unsigned long size, int align)
{
    void *ptr = s_mem.alloc(name, size + 0x20, align);

    if (ptr == NULL) {
        return NULL;
    }
    return DWCi_SetAllocateHeader(ptr, size);
}

/* DWC_Free, 0x0205A1B0: the header's recorded size plus the header. */
void DWC_Free(int name, void *ptr)
{
    unsigned long *header;

    if (ptr == NULL) {
        return;
    }
    header = (unsigned long *)func_0205A12C(ptr);
    s_mem.free(name, header, header[1] + 32);
}
