/*
 * tests/test_mod_event.c -- the event dispatch, with a fake binding.
 *
 * The dispatch core is sdk/mod_event.c; the contract it keeps is the one
 * of docs/mod-events.md, sections 2 and 3:
 *
 *   - every handler bound to the event runs, in load order;
 *   - the modifications chain: each handler sees the payload as the
 *     handlers before it left it, and the last write to a field wins;
 *   - any refusal refuses: the event's refusal function runs and the
 *     slot does not call the game function;
 *   - a refusal does not stop the later handlers;
 *   - a refusal at an event that cannot be refused is ignored, and
 *     counted;
 *   - a binding scoped to a function fires only while that function runs.
 *
 * Nothing here needs game data: the binding is a fake one, registered
 * for a (region, address) pair the game does not use, and the payload is
 * filled by the test's own builder.  The event is money.change, whose
 * `delta` is a field a handler may change (docs/mod-events.md, section 1).
 *
 * What this test does not cover: the area tables (sdk/mod_event_session.c
 * and its siblings), which are the port's own bindings and are checked
 * with the game's own calls; the generator's enter and leave calls
 * (tools/bootlink.py, step 3 of the build order); and the loader's side
 * of the registration, which is the port's own call that says which mod
 * is registering.
 */

#include <stdio.h>
#include <string.h>

#include "mod_event.h"
#include "port_test.h"

/* The pair the fake binding is registered for: a region and an address
 * the game does not use. */
#define T_REGION 0x0042u
#define T_ADDR   0x0210ABCDu

/* Another pair, with no binding at all. */
#define T_OTHER_ADDR 0x0210ABFEu

/* The function a scoped binding is scoped to. */
#define T_SCOPE_REGION 0x0043u
#define T_SCOPE_ADDR   0x0210AB00u

/* ------------------------------------------------------------------ */
/* What the handlers did                                               */
/* ------------------------------------------------------------------ */

/* The handlers, in the order they ran. */
static int g_ran[8];
static int g_ran_count;

/* What each handler saw in the payload's `delta`. */
static int g_saw[8];

/* The mod's own state each handler was given. */
static void *g_state[8];

/* How many times the refusal function ran. */
static int g_refused;

/* The two mods' own states, the values their start-up returned. */
static int g_first_mod;
static int g_second_mod;

static void note(int id, int delta, void *state)
{
    if (g_ran_count < (int)(sizeof(g_ran) / sizeof(g_ran[0]))) {
        g_ran[g_ran_count] = id;
        g_saw[g_ran_count] = delta;
        g_state[g_ran_count] = state;
        g_ran_count++;
    }
}

static void forget(void)
{
    memset(g_ran, 0, sizeof(g_ran));
    memset(g_saw, 0, sizeof(g_saw));
    memset(g_state, 0, sizeof(g_state));
    g_ran_count = 0;
    g_refused = 0;
}

/* ------------------------------------------------------------------ */
/* The fake binding                                                    */
/* ------------------------------------------------------------------ */

/* Fills the payload from the call: the first argument is the amount. */
static void fake_build(const BwEventCall *call, BwEventPayload *out)
{
    out->money_change.delta = (s32)(u32)call->a[0];
    out->money_change.before = 0;
}

/* Writes the changed amount back into the call: the game sees it. */
static void fake_apply(const BwEventPayload *in, BwEventCall *call)
{
    call->a[0] = (uintptr_t)(u32)in->money_change.delta;
}

/* The refused outcome: the amount never changes. */
static void fake_refuse(BwEventCall *call)
{
    g_refused++;
    call->ret = -7;
}

/* The binding itself: money.change, before the function, no scope. */
static BwEventBinding fake_binding(unsigned int addr, u32 kind, u8 point,
                                    u8 scope, unsigned int scope_region,
                                    unsigned int scope_addr,
                                    BwEventRefuse refuse)
{
    BwEventBinding b;

    memset(&b, 0, sizeof(b));
    b.region = T_REGION;
    b.addr = addr;
    b.kind = kind;
    b.point = point;
    b.scope = scope;
    b.scope_region = scope_region;
    b.scope_addr = scope_addr;
    b.build = fake_build;
    b.apply = fake_apply;
    b.refuse = refuse;
    return b;
}

/* ------------------------------------------------------------------ */
/* The handlers                                                        */
/* ------------------------------------------------------------------ */

/* The first handler: it doubles the amount. */
static int handler_first(void *mod_state, BwEvent *event)
{
    BwMoneyChange *p = (BwMoneyChange *)event;

    note(1, p->delta, mod_state);
    p->delta = p->delta * 2;
    return BW_EVENT_CONTINUE;
}

/* The second handler: it adds one to what the first one left. */
static int handler_second(void *mod_state, BwEvent *event)
{
    BwMoneyChange *p = (BwMoneyChange *)event;

    note(2, p->delta, mod_state);
    p->delta = p->delta + 1;
    return BW_EVENT_CONTINUE;
}

/* A handler that refuses. */
static int handler_refuser(void *mod_state, BwEvent *event)
{
    (void)event;
    note(3, 0, mod_state);
    return BW_EVENT_REFUSE;
}

/* A handler registered after the refuser: it still runs. */
static int handler_after(void *mod_state, BwEvent *event)
{
    (void)event;
    note(4, 0, mod_state);
    return BW_EVENT_CONTINUE;
}

/* A handler of the scoped event: it only counts. */
static int handler_scoped(void *mod_state, BwEvent *event)
{
    (void)event;
    note(5, 0, mod_state);
    return BW_EVENT_CONTINUE;
}

/* Multiplayer: the player each handler saw, and the events forwarded. */
static u32 g_seen_player[2];
static int g_seen_count[2];
static u32 g_forwarded;
static s32 g_forwarded_delta;

static int handler_local(void *mod_state, BwEvent *event)
{
    (void)mod_state;
    (void)event;
    g_seen_player[0] = bw_mod_event_player();
    g_seen_count[0]++;
    return BW_EVENT_CONTINUE;
}

static int handler_remote(void *mod_state, BwEvent *event)
{
    (void)mod_state;
    (void)event;
    g_seen_player[1] = bw_mod_event_player();
    g_seen_count[1]++;
    return BW_EVENT_REFUSE;     /* goes nowhere for another player's event */
}

static void forward(const void *payload, u32 size)
{
    const BwMoneyChange *p = (const BwMoneyChange *)payload;

    g_forwarded = size;
    g_forwarded_delta = p->delta;
}

/* The other players' events reach the multiplayer mods' handlers only,
 * with the player's number; this game's own events go out after its own
 * handlers, as they left them. */
static void test_multiplayer(void)
{
    BwEventPayload payload;

    printf("multiplayer\n");
    bw_mod_event_reset();
    BW_CHECK_EQ("a mod that is not multiplayer",
                bw_mod_event_register(BW_EVENT_MONEY_CHANGE, handler_local), 0);
    bw_mod_event_set_registering_remote(1);
    BW_CHECK_EQ("a multiplayer mod",
                bw_mod_event_register(BW_EVENT_MONEY_CHANGE, handler_remote), 0);
    bw_mod_event_set_registering_remote(0);

    memset(&payload, 0, sizeof(payload));
    payload.money_change.h.kind = BW_EVENT_MONEY_CHANGE;
    payload.money_change.h.size = (u32)sizeof(BwMoneyChange);
    payload.money_change.delta = 50;
    BW_CHECK_EQ("another player's event is taken",
                bw_mod_event_remote(3, &payload, (u32)sizeof(BwMoneyChange)), 0);
    BW_CHECK_EQ("the multiplayer mod sees it", g_seen_count[1], 1);
    BW_CHECK_EQ("with the player's number", (int)g_seen_player[1], 3);
    BW_CHECK_EQ("the other mod does not", g_seen_count[0], 0);
    BW_CHECK_EQ("and the number goes back to 0", (int)bw_mod_event_player(), 0);
    BW_CHECK_EQ("a payload of the wrong size is refused",
                bw_mod_event_remote(3, &payload, 6), -1);

    bw_mod_event_set_forward(forward);
    memset(&payload, 0, sizeof(payload));
    payload.money_change.delta = 7;
    g_seen_count[0] = g_seen_count[1] = 0;
    (void)bw_mod_event_fire(BW_EVENT_MONEY_CHANGE, &payload);
    BW_CHECK_EQ("this game's event: every handler", g_seen_count[0] + g_seen_count[1], 2);
    BW_CHECK_EQ("as this game's", (int)g_seen_player[0], 0);
    BW_CHECK_EQ("a refused event does not go out", (int)g_forwarded, 0);
    bw_mod_event_reset();
    (void)bw_mod_event_register(BW_EVENT_MONEY_CHANGE, handler_local);
    (void)bw_mod_event_fire(BW_EVENT_MONEY_CHANGE, &payload);
    BW_CHECK_EQ("an event that happened goes out whole",
                (int)g_forwarded, (int)sizeof(BwMoneyChange));
    BW_CHECK_EQ("as the handlers left it", (int)g_forwarded_delta, 7);
    bw_mod_event_set_forward(NULL);
    bw_mod_event_reset();
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

int main(void)
{
    BwEventCall    call;
    BwEventPayload payload;
    BwEventBinding b;
    u32 ignored_before;

    printf("the order, the chain and the apply\n");
    bw_mod_event_reset();
    forget();

    b = fake_binding(T_ADDR, BW_EVENT_MONEY_CHANGE, BW_EVENT_BEFORE,
                     BW_SCOPE_ANY, 0, 0, fake_refuse);
    BW_REQUIRE("the fake binding is registered", bw_mod_event_bind(&b) == 0);
    /* Two mods, each with its own state: the loader tells the layer
     * which mod is registering before it does. */
    bw_mod_event_set_registering_state(&g_first_mod);
    BW_REQUIRE("the first handler is bound",
               bw_mod_event_register(BW_EVENT_MONEY_CHANGE, handler_first) == 0);
    bw_mod_event_set_registering_state(&g_second_mod);
    BW_REQUIRE("the second handler is bound",
               bw_mod_event_register(BW_EVENT_MONEY_CHANGE, handler_second) == 0);
    bw_mod_event_set_registering_state(NULL);
    BW_CHECK_EQ("two handlers are bound to the event",
                (long)bw_mod_event_handler_count(BW_EVENT_MONEY_CHANGE), 2);

    memset(&call, 0, sizeof(call));
    memset(&payload, 0, sizeof(payload));
    call.nargs = 1;
    call.a[0] = 10;

    BW_CHECK_EQ("the event goes on", bw_mod_event_enter(T_REGION, T_ADDR,
                                                         &call, &payload), 0);
    BW_CHECK_EQ("both handlers ran", g_ran_count, 2);
    BW_CHECK_EQ("the first handler ran first", g_ran[0], 1);
    BW_CHECK_EQ("the second handler ran second", g_ran[1], 2);
    BW_CHECK_EQ("the first handler saw the amount the call carried",
                g_saw[0], 10);
    BW_CHECK_EQ("the second handler saw the first one's change",
                g_saw[1], 20);
    BW_REQUIRE("the first handler got its own mod's state",
               g_state[0] == (void *)&g_first_mod);
    BW_REQUIRE("the second handler got its own mod's state",
               g_state[1] == (void *)&g_second_mod);
    BW_CHECK_EQ("the call carries the chained amount", (long)call.a[0], 21);
    BW_CHECK_EQ("the payload names the event", (long)payload.h.kind,
                (long)BW_EVENT_MONEY_CHANGE);
    BW_CHECK_EQ("the payload says how much of it the port filled",
                (long)payload.h.size, (long)sizeof(BwMoneyChange));

    printf("a pair with no binding\n");
    forget();
    BW_CHECK_EQ("the event goes on", bw_mod_event_enter(T_REGION, T_OTHER_ADDR,
                                                         &call, &payload), 0);
    BW_CHECK_EQ("no handler ran", g_ran_count, 0);

    printf("a call with no payload\n");
    BW_CHECK_EQ("the event goes on", bw_mod_event_enter(T_REGION, T_ADDR,
                                                         &call, NULL), 0);
    BW_CHECK_EQ("no handler ran", g_ran_count, 0);

    printf("one refusal refuses\n");
    bw_mod_event_reset();
    forget();

    b = fake_binding(T_ADDR, BW_EVENT_MONEY_CHANGE, BW_EVENT_BEFORE,
                     BW_SCOPE_ANY, 0, 0, fake_refuse);
    BW_REQUIRE("the fake binding is registered", bw_mod_event_bind(&b) == 0);
    BW_REQUIRE("the refuser is bound",
               bw_mod_event_register(BW_EVENT_MONEY_CHANGE, handler_refuser) == 0);
    BW_REQUIRE("the handler after it is bound",
               bw_mod_event_register(BW_EVENT_MONEY_CHANGE, handler_after) == 0);

    memset(&call, 0, sizeof(call));
    memset(&payload, 0, sizeof(payload));
    call.nargs = 1;
    call.a[0] = 10;

    BW_CHECK_EQ("the event is refused", bw_mod_event_enter(T_REGION, T_ADDR,
                                                            &call, &payload), 1);
    BW_CHECK_EQ("the refusal function ran", g_refused, 1);
    BW_CHECK_EQ("the refusal set the result the caller sees",
                (long)call.ret, -7);
    BW_CHECK_EQ("both handlers ran", g_ran_count, 2);
    BW_CHECK_EQ("the refuser ran first", g_ran[0], 3);
    BW_CHECK_EQ("the handler after it ran too", g_ran[1], 4);

    printf("a refusal at an event that cannot be refused\n");
    bw_mod_event_reset();
    forget();
    ignored_before = bw_mod_event_ignored_refusals();

    b = fake_binding(T_ADDR, BW_EVENT_MONEY_CHANGE, BW_EVENT_BEFORE,
                     BW_SCOPE_ANY, 0, 0, NULL);
    BW_REQUIRE("the fake binding is registered", bw_mod_event_bind(&b) == 0);
    BW_REQUIRE("the refuser is bound",
               bw_mod_event_register(BW_EVENT_MONEY_CHANGE, handler_refuser) == 0);

    memset(&call, 0, sizeof(call));
    memset(&payload, 0, sizeof(payload));
    call.nargs = 1;
    call.a[0] = 10;

    BW_CHECK_EQ("the event goes on", bw_mod_event_enter(T_REGION, T_ADDR,
                                                         &call, &payload), 0);
    BW_CHECK_EQ("the refusal was ignored", g_refused, 0);
    BW_CHECK_EQ("and it was counted", (long)bw_mod_event_ignored_refusals(),
                (long)(ignored_before + 1));

    printf("an event that fires after the function\n");
    bw_mod_event_reset();
    forget();

    b = fake_binding(T_ADDR, BW_EVENT_MONEY_CHANGE, BW_EVENT_AFTER,
                     BW_SCOPE_ANY, 0, 0, fake_refuse);
    BW_REQUIRE("the fake binding is registered", bw_mod_event_bind(&b) == 0);
    BW_REQUIRE("the first handler is bound",
               bw_mod_event_register(BW_EVENT_MONEY_CHANGE, handler_first) == 0);

    memset(&call, 0, sizeof(call));
    memset(&payload, 0, sizeof(payload));
    call.nargs = 1;
    call.a[0] = 10;
    call.ret = 5;

    bw_mod_event_enter(T_REGION, T_ADDR, &call, &payload);
    BW_CHECK_EQ("nothing ran before the function", g_ran_count, 0);

    bw_mod_event_leave(T_REGION, T_ADDR, &call, &payload);
    BW_CHECK_EQ("the handler ran after the function", g_ran_count, 1);
    BW_CHECK_EQ("it saw the amount the call carried", g_saw[0], 10);
    BW_CHECK_EQ("the apply wrote the call's argument back",
                (long)call.a[0], 20);

    printf("a binding scoped to a function\n");
    bw_mod_event_reset();
    forget();

    b = fake_binding(T_ADDR, BW_EVENT_ITEM_ADD, BW_EVENT_BEFORE,
                     BW_SCOPE_INSIDE, T_SCOPE_REGION, T_SCOPE_ADDR, fake_refuse);
    BW_REQUIRE("the scoped binding is registered", bw_mod_event_bind(&b) == 0);
    BW_REQUIRE("the handler is bound",
               bw_mod_event_register(BW_EVENT_ITEM_ADD, handler_scoped) == 0);

    memset(&call, 0, sizeof(call));
    memset(&payload, 0, sizeof(payload));

    BW_CHECK_EQ("the scope is not running", bw_mod_event_scope_matches(
                T_SCOPE_REGION, T_SCOPE_ADDR, BW_SCOPE_INSIDE), 0);
    BW_CHECK_EQ("so the event does not fire",
                bw_mod_event_enter(T_REGION, T_ADDR, &call, &payload), 0);
    BW_CHECK_EQ("no handler ran", g_ran_count, 0);

    bw_mod_event_push(T_SCOPE_REGION, T_SCOPE_ADDR);
    BW_CHECK_EQ("the scope is running", bw_mod_event_scope_matches(
                T_SCOPE_REGION, T_SCOPE_ADDR, BW_SCOPE_INSIDE), 1);
    bw_mod_event_enter(T_REGION, T_ADDR, &call, &payload);
    BW_CHECK_EQ("the handler ran", g_ran_count, 1);
    bw_mod_event_pop();

    BW_CHECK_EQ("the scope is not running again",
                bw_mod_event_scope_matches(T_SCOPE_REGION, T_SCOPE_ADDR,
                                           BW_SCOPE_INSIDE), 0);
    bw_mod_event_enter(T_REGION, T_ADDR, &call, &payload);
    BW_CHECK_EQ("the handler did not run again", g_ran_count, 1);

    printf("a scope that must not be running\n");
    BW_CHECK_EQ("the scope is outside",
                bw_mod_event_scope_matches(T_SCOPE_REGION, T_SCOPE_ADDR,
                                           BW_SCOPE_OUTSIDE), 1);

    printf("the reset\n");
    bw_mod_event_reset();
    BW_CHECK_EQ("no handler is bound",
                (long)bw_mod_event_handler_count(BW_EVENT_ITEM_ADD), 0);
    forget();
    BW_CHECK_EQ("the binding is gone too",
                bw_mod_event_enter(T_REGION, T_ADDR, &call, &payload), 0);
    BW_CHECK_EQ("no handler ran", g_ran_count, 0);

    test_multiplayer();
    BW_REPORT("test_mod_event");
}
