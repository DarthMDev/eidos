/*
 * tests/test_mod_timer.c -- the timer, the splits and the mod's own
 * random source (docs/mod-services.md, "Timer and splits" and "Seeded
 * random source"; sdk/mod_timer.c).
 *
 * The timer is one of the port's services: a mod starts it, stops it and
 * asks it for the time the run has taken, and it records the splits the
 * run went through.  The random source is the mod's own: a mod seeds it
 * and draws from it, so two players who share a seed see the same
 * sequence and the game's own rolls are untouched.
 *
 * What is checked here:
 *
 *   - the timer is zero before it is started, counts the time the game
 *     ran while it is started, and stands still once it is stopped;
 *   - the port's own menu pauses the timer;
 *   - a split records the name it was given and the time the timer was
 *     at when the split was taken, and the splits keep the order they
 *     were taken in;
 *   - the random source: the same seed gives the same sequence, and
 *     another seed gives another one.
 *
 * The timer counts the port's own clock, which is the frame loop's tick
 * (engine/frame/frame.h), so the test drives the frame loop: one frame is
 * 8752 ticks, which is 16.7 milliseconds of the game's time.  Nothing
 * here needs game data.
 *
 * What this test does not cover: the panel the splits are shown in and
 * the file they are written to at the end of a run (the run-status panel
 * is not built yet), and the timer of a mod other than the port itself
 * (the loader gives every mod a table of its own, and the timer keys its
 * states on the mod's name).
 */

#include <stdio.h>
#include <string.h>

#include "../engine/frame/frame.h"
#include "mod_title.h"
#include "mod_timer.h"
#include "port_test.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/* The port's own calls                                                */
/* ------------------------------------------------------------------ */

/*
 * The calls of sdk/mod_timer.c.  The first six are the ones the loader
 * binds into the services table (sdk/mod_state.h); the rest are the
 * port's own, and they name a mod by its namespace.  A mod is not
 * calling here, so the namespace is the empty one -- the same name the
 * saved-data layer hands out when the port itself is the caller.
 */

/* The namespace the port's own calls are made under. */
#define T_NS ""

/* How many values of the random source a sequence is compared over. */
#define T_RAND_COUNT 8

/* The longest split name the test uses. */
#define T_NAME_SIZE 64

/* ------------------------------------------------------------------ */
/* The helpers                                                        */
/* ------------------------------------------------------------------ */

/* The port's frame loop: the clock the timer counts. */
static EidosFrameLoop g_loop;

/*
 * Runs `count` frames of the port's loop.  One frame is 8752 ticks, which
 * is 16.7 milliseconds of the game's time, so the timer's time grows by
 * about that much per frame.
 */
static void run_frames(u32 count)
{
    u32 i;

    for (i = 0; i < count; i++) {
        eidos_frame_run_one(&g_loop);
    }
}

/* The two sequences of the random source, and whether they differ. */
static int sequences_differ(const u32 *a, const u32 *b, u32 count)
{
    u32 i;

    for (i = 0; i < count; i++) {
        if (a[i] != b[i]) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

int main(void)
{
    u64 t0, t1, t2;
    u64 before, after, split_ms;
    char name[T_NAME_SIZE];
    u32 first[T_RAND_COUNT];
    u32 second[T_RAND_COUNT];
    u32 other[T_RAND_COUNT];
    u32 i;

    eidos_frame_init(&g_loop, NULL, NULL, NULL);
    eidos_frame_set_current(&g_loop);
    bw_mod_timer_reset();

    printf("the timer before it is started\n");
    BW_CHECK_EQ("the timer is zero before it is started",
                (long)bw_mod_timer_ms(), 0);
    run_frames(5);
    BW_CHECK_EQ("and it stays zero while the game runs",
                (long)bw_mod_timer_ms(), 0);

    printf("the timer runs\n");
    bw_mod_timer_start();
    BW_CHECK_EQ("the timer starts at zero", (long)bw_mod_timer_ms(), 0);
    run_frames(10);
    t0 = bw_mod_timer_ms();
    BW_CHECK_EQ("the time grows while the timer runs",
                (long)(t0 >= 160), 1);
    BW_CHECK_EQ("and it is about the time the frames took",
                (long)(t0 <= 175), 1);

    printf("the timer stops\n");
    bw_mod_timer_stop();
    t1 = bw_mod_timer_ms();
    run_frames(10);
    t2 = bw_mod_timer_ms();
    BW_CHECK_EQ("the time stands still while the timer is stopped",
                (long)(t2 == t1), 1);
    BW_CHECK_EQ("the timer keeps the time it was stopped at",
                (long)(t1 == t0), 1);

    printf("the timer runs again\n");
    bw_mod_timer_start();
    BW_CHECK_EQ("the timer starts again at zero",
                (long)bw_mod_timer_ms(), 0);
    run_frames(5);
    BW_CHECK_EQ("and it runs again", (long)(bw_mod_timer_ms() >= 80), 1);

    printf("the port's own menu pauses the timer\n");
    t0 = bw_mod_timer_ms();
    bw_mod_title_set_in_settings(1);
    run_frames(5);
    t1 = bw_mod_timer_ms();
    bw_mod_title_set_in_settings(0);
    BW_CHECK_EQ("the time stands still while the menu is open",
                (long)(t1 == t0), 1);

    printf("the frame hook samples the timer\n");
    bw_mod_timer_start();
    for (i = 0; i < 10; i++) {
        eidos_frame_run_one(&g_loop);
        bw_mod_timer_frame();
    }
    t0 = bw_mod_timer_ms();
    BW_CHECK_EQ("the timer counts even when only the frame hook asks",
                (long)(t0 >= 160), 1);
    BW_CHECK_EQ("and the port's own answer agrees with the service's",
                (long)(bw_mod_timer_elapsed_ms(T_NS) == t0), 1);

    printf("a split records its name and the time\n");
    BW_CHECK_EQ("no split has been taken yet",
                (long)bw_mod_timer_splits_count(T_NS), 0);
    run_frames(3);
    before = bw_mod_timer_ms();
    bw_mod_timer_split("Route 201");
    after = bw_mod_timer_ms();
    BW_CHECK_EQ("the split is recorded",
                (long)bw_mod_timer_splits_count(T_NS), 1);

    memset(name, 0, sizeof(name));
    split_ms = 0;
    BW_REQUIRE("the split can be read back",
               bw_mod_timer_split_at(T_NS, 0, name, (u32)sizeof(name),
                                     &split_ms) == 0);
    BW_CHECK_EQ("the split carries the name it was given",
                strcmp(name, "Route 201"), 0);
    BW_CHECK_EQ("the split carries the time the timer was at",
                (long)(split_ms >= before && split_ms <= after), 1);

    printf("the splits keep their order\n");
    run_frames(3);
    bw_mod_timer_split("Union Cave");
    BW_CHECK_EQ("both splits are recorded",
                (long)bw_mod_timer_splits_count(T_NS), 2);
    memset(name, 0, sizeof(name));
    split_ms = 0;
    BW_REQUIRE("the second split can be read back",
               bw_mod_timer_split_at(T_NS, 1, name, (u32)sizeof(name),
                                     &split_ms) == 0);
    BW_CHECK_EQ("the second split carries its own name",
                strcmp(name, "Union Cave"), 0);
    BW_CHECK_EQ("the second split is later than the first",
                (long)(split_ms >= after), 1);

    printf("a split that does not exist\n");
    memset(name, 0, sizeof(name));
    split_ms = 0;
    BW_CHECK_EQ("reading a split that was never taken fails",
                (long)(bw_mod_timer_split_at(T_NS, 9, name,
                                             (u32)sizeof(name),
                                             &split_ms) != 0), 1);

    printf("the random source\n");
    bw_mod_rand_seed(12345u);
    for (i = 0; i < T_RAND_COUNT; i++) {
        first[i] = bw_mod_rand_next();
    }
    bw_mod_rand_seed(12345u);
    for (i = 0; i < T_RAND_COUNT; i++) {
        second[i] = bw_mod_rand_next();
    }
    BW_CHECK_EQ("the same seed gives the same sequence",
                memcmp(first, second, sizeof(first)), 0);

    bw_mod_rand_seed(54321u);
    for (i = 0; i < T_RAND_COUNT; i++) {
        other[i] = bw_mod_rand_next();
    }
    BW_CHECK_EQ("another seed gives another sequence",
                (long)sequences_differ(first, other, T_RAND_COUNT), 1);

    BW_REPORT("test_mod_timer");
}
