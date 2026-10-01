#!/usr/bin/env python3
"""
test_closure.py -- the source reader tools/closure.py and tools/bootlink.py
share, on synthetic C (no aletheia checkout, no game data).

Each case is a shape a matched file really uses; the names that must and
must not come out as references are spelled in the case.
"""

import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))

import bootlink  # noqa: E402
import closure  # noqa: E402

FAILS = []


def check(what, got, want):
    if got != want:
        FAILS.append("%s: got %r, want %r" % (what, got, want))


def refs(text, universe=lambda n: n.startswith(("func_", "sub_", "OS_"))):
    return closure.SourceScan(text).references(universe)


def main():
    # extern with an attribute, a struct body, several declarators, a
    # function pointer object, a prototype without extern.
    text = """
/*
 * symbols:
 *   OS_Foo      0x02080000  arm
 *   g_02100000  0x02100000  data
 */
typedef unsigned int u32;
extern int strncmp(const char *a, const char *b, unsigned long n) __attribute__((pure));
extern struct { u32 pad000[4]; u32 chain; } g_02100000;
extern u32 g_a, g_b[4];
extern void (*g_hook)(u32 x13);
void func_02001000(u32 v);
struct Obj;
static void helper(void) { }
void func_02002000(struct Obj *o, void (*cb)(void))
{
    u32 n = strncmp("a", "b", 1) + g_a + g_b[0] + g_02100000.chain;
    g_hook(n);
    func_02001000(n);
    cb();
    o->callback(n);
    helper();
    OS_Foo(n);
    sub_02003000(n);
}
"""
    names, implicit = refs(text)
    check("references", names,
          ["OS_Foo", "func_02001000", "g_02100000", "g_a", "g_b", "g_hook",
           "strncmp", "sub_02003000"])
    check("implicit calls", implicit, ["OS_Foo", "sub_02003000"])
    sc = closure.SourceScan(text)
    check("defined", sorted(sc.defined), ["func_02002000", "helper"])
    check("symbols block", sc.symbols.get("g_02100000"), (0x02100000, "data"))
    check("no asm", sc.has_asm, False)

    # A typedef'd local function pointer is not a symbol; a declared name
    # used only in its own declaration is not a reference.
    text = """
typedef void (*Cb)(void);
extern Cb OSi_Hook;
extern void func_02004000(void);
void func_02005000(void) { Cb callback = OSi_Hook; callback(); }
"""
    names, implicit = refs(text)
    check("typedef locals", names, ["OSi_Hook"])
    check("typedef locals implicit", implicit, [])

    # mwcc asm bodies are recognised.
    check("asm body", closure.SourceScan("asm void f(void) { bx lr }").has_asm, True)

    # Link-time constants.
    check("const SDK_", closure.is_link_constant("SDK_IRQ_STACKSIZE", 0x800, "data"), True)
    check("const ov id", closure.is_link_constant("ov010_id", 0xA, "data"), True)
    check("not const", closure.is_link_constant("g_02100000", 0x02100000, "data"), False)
    check("not const code", closure.is_link_constant("OSi_DoBoot", 0x01FF8460, "arm"), False)

    # bootlink's #if evaluation for port sources.
    bootlink.PORT_DEFINES.clear()
    bootlink.PORT_DEFINES["EIDOS_HEAP_MATCHED"] = "0"
    text = """
#if EIDOS_HEAP_MATCHED
void heap_only(void) { }
#else
void host_only(void) { }
#endif
#if defined(_WIN32)
void either(void) { }
#endif
"""
    sc = closure.SourceScan(bootlink.preprocess(text))
    check("preprocess", sorted(sc.defined), ["either", "host_only"])

    # Implicit declarations take mwcc's int, except a 64-bit return.
    check("rtype int", bootlink.return_type("void *func_02006000(u32 a)", "func_02006000"), "int")
    check("rtype void", bootlink.return_type("void func_02006000(void)", "func_02006000"), "int")
    check("rtype u64", bootlink.return_type("u64 func_02006000(void)", "func_02006000"), "long long")

    for line in FAILS:
        print("FAIL " + line)
    print("test_closure: %d failure(s)" % len(FAILS))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
