/*
 * tools/lp64/w2c_host/main.c -- run a wasm2c-translated WASI command module
 * natively: instantiate it, hand it argv, call _start, report its exit code.
 *
 * Why this exists: docs/lp64.md's measurements.  The module's name is a
 * build flag (-DW2C_MODULE=bench -DW2C_MODULE_HEADER="bench.h") because
 * wasm2c prefixes every name with it.  A trap (an out-of-bounds access, a
 * call through a mismatched pointer, an `unreachable`) is reported with its
 * kind and exit code 3.
 */

#include <stdio.h>
#include <setjmp.h>

#include "wasi_min.h"
#include "wasm-rt-exceptions.h"
#include "wasm-rt-impl.h"
#include W2C_MODULE_HEADER

#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)
#define MOD_T            CAT(w2c_, W2C_MODULE)
#define MOD_INSTANTIATE  CAT(CAT(wasm2c_, W2C_MODULE), _instantiate)
#define MOD_FREE         CAT(CAT(wasm2c_, W2C_MODULE), _free)
#define MOD_MEMORY       CAT(CAT(w2c_, W2C_MODULE), _memory)
#define MOD_START        CAT(CAT(w2c_, W2C_MODULE), _0x5Fstart)

int main(int argc, char **argv)
{
    static MOD_T inst;
    static struct w2c_wasi__snapshot__preview1 wasi;
    wasm_rt_trap_t trap;

    wasm_rt_init();
    wasi.argc = argc;
    wasi.argv = argv;
    MOD_INSTANTIATE(&inst, &wasi);
    wasi.mem = MOD_MEMORY(&inst);

    if (setjmp(wasi.exit_to) == 0) {
        trap = (wasm_rt_trap_t)wasm_rt_impl_try();
        if (trap != WASM_RT_TRAP_NONE) {
            fprintf(stderr, "w2c_host: trap: %s\n", wasm_rt_strerror(trap));
            return 3;
        }
        MOD_START(&inst);
    }
    MOD_FREE(&inst);
    wasm_rt_free();
    return wasi.exit_code;
}
