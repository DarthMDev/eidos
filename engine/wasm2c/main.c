#include "game.h"
#include "host_thread.h"
#include "host_wasi.h"
#include "wasm-rt-impl.h"
#include "wasm-rt-exceptions.h"
#include <stdio.h>

static void dispatch(void *guest, uint32_t callback, uint32_t arg)
{ w2c_game_eidos_thread_dispatch(guest, callback, arg); }
static uint64_t progress(void *guest, uint32_t callback)
{ return w2c_game_eidos_progress_dispatch(guest, callback); }
static void context(void *guest) { w2c_game_eidos_crash_context(guest); }

int main(int argc, char **argv)
{
    static w2c_game game;
    static struct w2c_eidos host;
    static struct w2c_wasi__snapshot__preview1 wasi;
    wasm_rt_trap_t trap;
    int result;
    wasm_rt_init();
    wasm2c_game_instantiate(&game, &host, &wasi);
    eidos_wasi_init(&wasi, w2c_game_memory(&game), argc, argv);
    eidos_host_threads_init(&host, &game, w2c_game_0x5F_stack_pointer(&game), dispatch);
    host.memory = w2c_game_memory(&game);
    host.progress = progress;
    host.context = context;
    eidos_host_enter(&host);
    if (setjmp(wasi.exit_to) == 0) {
        trap = wasm_rt_impl_try();
        if (trap != WASM_RT_TRAP_NONE) {
            fprintf(stderr, "eidos guest trap: %s\n", wasm_rt_strerror(trap));
            return 3;
        }
        w2c_game_0x5Fstart(&game);
    }
    result = wasi.exit_code;
    eidos_host_leave(&host);
    eidos_host_threads_destroy(&host);
    eidos_wasi_destroy(&wasi);
    wasm2c_game_free(&game);
    wasm_rt_free();
    return result;
}
