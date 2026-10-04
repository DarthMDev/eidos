/* engine/frame/prof.c -- see prof.h. */

#include <stdio.h>

#include "prof.h"
#include "plat_thread.h"

int eidos_prof_on;

static unsigned g_every;
static unsigned g_n;
static unsigned long long g_sum[EIDOS_PROF_COUNT];
static unsigned long long g_max[EIDOS_PROF_COUNT];
static unsigned long long g_win_start;

static const char *const k_names[EIDOS_PROF_COUNT] = {
    "step", "g3d", "main2d", "sub2d", "present", "audio", "wait",
    "geom", "raster", "snap", "texdec", "gldraw", "glup", "render", "boot3d", "glread", "unprem"
};

void eidos_prof_enable(unsigned every)
{
    g_every = every != 0 ? every : 300;
    eidos_prof_on = 1;
}

unsigned long long eidos_prof_begin(void)
{
    return eidos_prof_on ? bw_clock_ns() : 0;
}

void eidos_prof_end(int phase, unsigned long long t0)
{
    unsigned long long d;

    if (!eidos_prof_on) {
        return;
    }
    d = bw_clock_ns() - t0;
    g_sum[phase] += d;
    if (d > g_max[phase]) {
        g_max[phase] = d;
    }
}

void eidos_prof_frame(unsigned long long frame_no)
{
    int i;
    unsigned long long now;

    if (!eidos_prof_on) {
        return;
    }
    if (g_n == 0) {
        g_win_start = bw_clock_ns();
    }
    if (++g_n < g_every) {
        return;
    }
    now = bw_clock_ns();
    printf("prof frame %llu (%u frames, %.2f ms/frame wall):", frame_no, g_n,
           (double)(now - g_win_start) / 1e6 / (double)g_n);
    for (i = 0; i < EIDOS_PROF_COUNT; i++) {
        printf(" %s %.2f/%.2f", k_names[i],
               (double)g_sum[i] / 1e6 / (double)g_n, (double)g_max[i] / 1e6);
        g_sum[i] = 0;
        g_max[i] = 0;
    }
    printf(" (avg/max ms)\n");
    fflush(stdout);
    g_n = 0;
}
