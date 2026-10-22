/*
 * sdk/mp.c -- multiplayer: the lobby, the start and the pause (mp.h,
 * docs/multiplayer.md).
 *
 * The host's game decides; the joiners' games follow what the host says.
 * The messages are lines of text under the port's key:
 *
 *   joiner -> host   hello <mp version> <game> <mods>
 *   host -> all      roster <state>            followed by one line per
 *                    <peer>\t<connected>\t<on roster>\t<game>\t<mods ok>\t<name>
 *   host -> all      start
 *   host -> all      pause
 *
 * <game> is B or W (- before the player said), <mods> is "ns=n;ns=n" or "-".
 *
 * A joiner's drop during the game gives the others BW_MP_GRACE_MS; the host
 * then pauses every game until it starts again.  The host's drop holds every
 * joiner's game for BW_MP_HOST_WAIT_MS while the host joins again.  A game
 * that loses the relay joins the same code again every BW_MP_REJOIN_EVERY_MS
 * (its token is kept per code, so it is the same peer).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mp.h"
#include "mod_loader.h"
#include "mod_net.h"
#include "plat_thread.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

/* The version of the messages above. */
#define BW_MP_PROTO 1

/* The longest message the layer sends: the roster of sixteen players. */
#define BW_MP_MSG_MAX 4096u

static const BwMpNet k_net = {
    bw_mod_net_session_host,
    bw_mod_net_session_join,
    bw_mod_net_session_leave,
    bw_mod_net_port_send,
    bw_mod_net_session_remove,
    bw_mod_net_self_peer,
    bw_mod_net_peer_name,
    bw_mod_net_name,
    bw_mod_net_last_error
};

static const BwMpNet *g_net = &k_net;

static struct {
    BwMpState  state;
    int        host;
    int        roster_set;          /* the first start has been */
    char       code[40];
    BwMpPlayer players[BW_MP_PLAYERS_MAX];   /* by peer - 1 */
    char       game;                /* this game's own */
    char       mods[BW_MP_MODS_MAX + 1];
    int        mods_set;
    u64        now_ms;
    u64        grace_until;
    u64        host_gone_until;
    u64        next_rejoin;
    char       blocker[256];
    char       wait[200];
    char       reason[96];
    int        started;             /* the first start, not yet taken */
} g_mp;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static u64 now_ms(void)
{
    return bw_clock_ns() / 1000000ull;
}

static BwMpPlayer *player(u32 peer)
{
    if (peer == 0 || peer > BW_MP_PLAYERS_MAX) {
        return NULL;
    }
    return &g_mp.players[peer - 1u];
}

static void copy_n(char *dst, size_t size, const char *src, size_t len)
{
    if (size == 0) {
        return;
    }
    if (len >= size) {
        len = size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* This game's multiplayer mods: what the port set, or the loaded mods that
 * state a multiplayer number. */
static const char *local_mods(void)
{
    static char list[BW_MP_MODS_MAX + 1];
    u32 i;
    u32 n;
    size_t at = 0;

    if (g_mp.mods_set) {
        return g_mp.mods;
    }
    list[0] = '\0';
    n = bw_mod_loader_count();
    for (i = 0; i < n; i++) {
        int v = bw_mod_loader_multiplayer_at(i);
        const char *ns = bw_mod_loader_namespace_at(i);
        int w;

        if (v <= 0 || ns == NULL) {
            continue;
        }
        w = snprintf(list + at, sizeof(list) - at, "%s%s=%d",
                     at > 0 ? ";" : "", ns, v);
        if (w < 0 || (size_t)w >= sizeof(list) - at) {
            break;
        }
        at += (size_t)w;
    }
    return list;
}

static char local_game(void)
{
    return g_mp.game != 0 ? g_mp.game : 'B';
}

/* Whether `mods` names the same multiplayer mods with the same numbers as
 * `want`, in any order.  The first difference goes to `why`. */
static int mods_match(const char *want, const char *have, char *why,
                      size_t why_size)
{
    const char *lists[2];
    int pass;

    lists[0] = want;
    lists[1] = have;
    for (pass = 0; pass < 2; pass++) {
        const char *p = lists[pass];
        const char *other = lists[1 - pass];

        while (p != NULL && *p != '\0') {
            const char *end = strchr(p, ';');
            size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
            const char *eq = memchr(p, '=', len);
            size_t ns_len = eq != NULL ? (size_t)(eq - p) : len;
            const char *q = other;
            int found = 0;
            int same = 0;

            while (q != NULL && *q != '\0') {
                const char *qend = strchr(q, ';');
                size_t qlen = qend != NULL ? (size_t)(qend - q) : strlen(q);
                const char *qeq = memchr(q, '=', qlen);
                size_t qns = qeq != NULL ? (size_t)(qeq - q) : qlen;

                if (qns == ns_len && memcmp(q, p, ns_len) == 0) {
                    found = 1;
                    same = qlen == len && memcmp(q, p, len) == 0;
                    break;
                }
                q = qend != NULL ? qend + 1 : NULL;
            }
            if (!found || !same) {
                if (why != NULL) {
                    snprintf(why, why_size, "mod %.*s %s", (int)ns_len, p,
                             !found ? (pass == 0 ? "missing" : "not on the host")
                                    : "is another multiplayer version");
                }
                return 0;
            }
            p = end != NULL ? end + 1 : NULL;
        }
    }
    return 1;
}

static const char *state_word(BwMpState s)
{
    switch (s) {
    case BW_MP_LOBBY:   return "lobby";
    case BW_MP_PLAYING: return "playing";
    case BW_MP_GRACE:   return "grace";
    case BW_MP_PAUSED:  return "paused";
    default:            return "lobby";
    }
}

static BwMpState word_state(const char *w, size_t n)
{
    if (n == 7 && memcmp(w, "playing", 7) == 0) {
        return BW_MP_PLAYING;
    }
    if (n == 5 && memcmp(w, "grace", 5) == 0) {
        return BW_MP_GRACE;
    }
    if (n == 6 && memcmp(w, "paused", 6) == 0) {
        return BW_MP_PAUSED;
    }
    return BW_MP_LOBBY;
}

/* ------------------------------------------------------------------ */
/* The host                                                            */
/* ------------------------------------------------------------------ */

/* Tell every player the roster and the state. */
static void host_broadcast_roster(void)
{
    char msg[BW_MP_MSG_MAX];
    size_t at;
    u32 i;
    int w;

    w = snprintf(msg, sizeof(msg), "roster %s\n", state_word(g_mp.state));
    if (w < 0) {
        return;
    }
    at = (size_t)w;
    for (i = 0; i < BW_MP_PLAYERS_MAX; i++) {
        const BwMpPlayer *p = &g_mp.players[i];

        if (p->peer == 0) {
            continue;
        }
        w = snprintf(msg + at, sizeof(msg) - at, "%u\t%d\t%d\t%c\t%d\t%s\n",
                     (unsigned)p->peer, p->connected, p->on_roster,
                     p->game != 0 ? p->game : '-', p->mods_ok, p->name);
        if (w < 0 || (size_t)w >= sizeof(msg) - at) {
            break;
        }
        at += (size_t)w;
    }
    (void)g_net->send(0, msg, (u32)at);
}

static void host_send_word(const char *word)
{
    (void)g_net->send(0, word, (u32)strlen(word));
}

/* Whether every player the start needs is there and ready; the reason goes
 * to g_mp.blocker when not. */
static int host_ready(void)
{
    u32 i;
    const char *mine = local_mods();
    int someone = 0;

    g_mp.blocker[0] = '\0';
    for (i = 1; i < BW_MP_PLAYERS_MAX; i++) {
        BwMpPlayer *p = &g_mp.players[i];
        char why[96];

        if (p->peer == 0) {
            continue;
        }
        if (g_mp.roster_set && !p->on_roster) {
            continue;
        }
        if (!p->connected) {
            if (g_mp.roster_set) {
                snprintf(g_mp.blocker, sizeof(g_mp.blocker),
                         "waiting for %s", p->name);
                return 0;
            }
            continue;
        }
        someone = 1;
        if (p->game == 0) {
            snprintf(g_mp.blocker, sizeof(g_mp.blocker),
                     "waiting for %s's game to answer", p->name);
            return 0;
        }
        if (!mods_match(mine, p->mods, why, sizeof(why))) {
            snprintf(g_mp.blocker, sizeof(g_mp.blocker), "%s: %s", p->name, why);
            return 0;
        }
    }
    if (!someone) {
        snprintf(g_mp.blocker, sizeof(g_mp.blocker), "nobody has joined yet");
        return 0;
    }
    return 1;
}

static void host_refresh_mods_ok(void)
{
    u32 i;
    const char *mine = local_mods();

    for (i = 1; i < BW_MP_PLAYERS_MAX; i++) {
        BwMpPlayer *p = &g_mp.players[i];

        if (p->peer != 0) {
            p->mods_ok = p->game != 0 && mods_match(mine, p->mods, NULL, 0);
        }
    }
}

static void host_on_join(u32 peer)
{
    BwMpPlayer *p = player(peer);

    if (p == NULL || peer == 1u) {
        return;
    }
    if (g_mp.roster_set && !(p->peer == peer && p->on_roster)) {
        /* After a relay restart the numbers are new: a missing roster
         * player with the same name takes its place under the new number
         * (SPEC section 12's identity replaces the name).  Anyone else is
         * not on the roster and is removed. */
        char name[BW_MP_NAME_MAX + 1];
        u32 i;

        name[0] = '\0';
        if (g_net->peer_name != NULL) {
            (void)g_net->peer_name(peer, name, (u32)sizeof(name));
        }
        for (i = 1; i < BW_MP_PLAYERS_MAX && name[0] != '\0'; i++) {
            BwMpPlayer *old = &g_mp.players[i];

            if (old != p && old->peer != 0 && old->on_roster &&
                !old->connected && strcmp(old->name, name) == 0 &&
                !p->connected) {
                /* swap: whoever had this slot keeps the other one, and is
                 * found by name in turn when it joins */
                BwMpPlayer tmp = *p;

                *p = *old;
                *old = tmp;
                old->peer = old->peer != 0 ? i + 1u : 0u;
                break;
            }
        }
        if (!(p->peer != 0 && p->on_roster)) {
            (void)g_net->remove(peer);
            return;
        }
    }
    p->peer = peer;
    p->connected = 1;
    if (g_net->peer_name == NULL ||
        g_net->peer_name(peer, p->name, (u32)sizeof(p->name)) != 0 ||
        p->name[0] == '\0') {
        snprintf(p->name, sizeof(p->name), "player %u", (unsigned)peer);
    }
    if (g_mp.state == BW_MP_GRACE && host_ready()) {
        /* back within the grace: play goes on */
        g_mp.state = BW_MP_PLAYING;
    }
    host_broadcast_roster();
}

static void host_on_leave(u32 peer)
{
    BwMpPlayer *p = player(peer);

    if (p == NULL || p->peer != peer) {
        return;
    }
    p->connected = 0;
    if (!g_mp.roster_set) {
        /* in the first lobby a player who leaves is forgotten */
        memset(p, 0, sizeof(*p));
    } else if (p->on_roster && g_mp.state == BW_MP_PLAYING) {
        g_mp.state = BW_MP_GRACE;
        g_mp.grace_until = g_mp.now_ms + BW_MP_GRACE_MS;
    }
    host_broadcast_roster();
}

static void host_on_message(u32 peer, const char *msg, u32 size)
{
    BwMpPlayer *p = player(peer);
    char text[BW_MP_MODS_MAX + 64];
    char game[8];
    char mods[BW_MP_MODS_MAX + 1];
    int version = 0;

    if (p == NULL || p->peer != peer) {
        return;
    }
    copy_n(text, sizeof(text), msg, size);
    if (strncmp(text, "name ", 5) == 0 && text[5] != '\0') {
        copy_n(p->name, sizeof(p->name), text + 5, strlen(text + 5));
        host_broadcast_roster();
        return;
    }
    if (strncmp(text, "hello ", 6) == 0 &&
        sscanf(text + 6, "%d %7s %512s", &version, game, mods) == 3) {
        p->game = (game[0] == 'W') ? 'W' : 'B';
        if (strcmp(mods, "-") == 0) {
            mods[0] = '\0';
        }
        copy_n(p->mods, sizeof(p->mods), mods, strlen(mods));
        host_refresh_mods_ok();
        host_broadcast_roster();
        /* a player who comes back while the game runs is told so */
        if (g_mp.state == BW_MP_PLAYING || g_mp.state == BW_MP_GRACE) {
            (void)g_net->send(peer, "start", 5);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The joiner                                                          */
/* ------------------------------------------------------------------ */

static void joiner_hello(void)
{
    char msg[BW_MP_MODS_MAX + 32];
    const char *mods = local_mods();
    int n;

    n = snprintf(msg, sizeof(msg), "hello %d %c %s", BW_MP_PROTO, local_game(),
                 mods[0] != '\0' ? mods : "-");
    if (n > 0) {
        (void)g_net->send(1, msg, (u32)n);
    }
}

static void joiner_on_roster(const char *msg, u32 size)
{
    const char *p = msg;
    const char *end = msg + size;
    const char *nl = memchr(p, '\n', size);
    BwMpState s;

    if (nl == NULL) {
        return;
    }
    s = word_state(p + 7, (size_t)(nl - (p + 7)));
    memset(g_mp.players, 0, sizeof(g_mp.players));
    p = nl + 1;
    while (p < end) {
        const char *le = memchr(p, '\n', (size_t)(end - p));
        char line[160];
        unsigned peer;
        int connected, on_roster, ok;
        char game;
        int name_at = 0;

        if (le == NULL) {
            le = end;
        }
        copy_n(line, sizeof(line), p, (size_t)(le - p));
        if (sscanf(line, "%u\t%d\t%d\t%c\t%d\t%n", &peer, &connected,
                   &on_roster, &game, &ok, &name_at) >= 5 &&
            name_at > 0 && peer >= 1 && peer <= BW_MP_PLAYERS_MAX) {
            BwMpPlayer *pl = &g_mp.players[peer - 1];

            pl->peer = peer;
            pl->connected = connected;
            pl->on_roster = on_roster;
            pl->game = game == '-' ? 0 : game;
            pl->mods_ok = ok;
            copy_n(pl->name, sizeof(pl->name), line + name_at,
                   strlen(line + name_at));
        }
        p = le + 1;
    }
    /* the host's word on the state, except that a held game stays held
     * until the host says start */
    if (g_mp.state == BW_MP_LOBBY || s == BW_MP_LOBBY) {
        g_mp.state = s == BW_MP_GRACE ? BW_MP_PLAYING : s;
    }
}

static void joiner_on_message(u32 peer, const char *msg, u32 size)
{
    if (peer != 1u) {
        return;
    }
    if (size >= 7 && memcmp(msg, "roster ", 7) == 0) {
        joiner_on_roster(msg, size);
    } else if (size == 5 && memcmp(msg, "start", 5) == 0) {
        if (g_mp.state == BW_MP_LOBBY) {
            g_mp.started = 1;
        }
        g_mp.state = BW_MP_PLAYING;
    } else if (size == 5 && memcmp(msg, "pause", 5) == 0) {
        g_mp.state = BW_MP_PAUSED;
    }
}

/* ------------------------------------------------------------------ */
/* The network's events                                                */
/* ------------------------------------------------------------------ */

void bw_mp_on_net(u32 peer, int kind, const void *buf, u32 size)
{
    if (g_mp.state == BW_MP_OFF || g_mp.state == BW_MP_ENDED) {
        return;
    }
    if (kind == BW_MOD_NET_PORT_ENDED) {
        char why[64];

        copy_n(why, sizeof(why), (const char *)buf, size);
        if (strcmp(why, "lost") == 0 && g_mp.state != BW_MP_LOBBY) {
            /* the relay is gone for this game: join the same code again */
            g_mp.state = BW_MP_RECONNECTING;
            g_mp.next_rejoin = g_mp.now_ms + BW_MP_REJOIN_EVERY_MS;
            g_mp.host_gone_until = g_mp.now_ms + BW_MP_HOST_WAIT_MS;
            return;
        }
        g_mp.state = BW_MP_ENDED;
        copy_n(g_mp.reason, sizeof(g_mp.reason), why, strlen(why));
        return;
    }
    if (g_mp.host) {
        if (kind == (int)BW_SESSION_JOIN) {
            host_on_join(peer);
        } else if (kind == (int)BW_SESSION_LEAVE) {
            host_on_leave(peer);
        } else if (kind == (int)BW_SESSION_MESSAGE) {
            host_on_message(peer, (const char *)buf, size);
        }
        return;
    }
    if (kind == (int)BW_SESSION_LEAVE && peer == 1u) {
        /* the host is gone: hold the game while it comes back */
        if (g_mp.state != BW_MP_LOBBY) {
            g_mp.state = BW_MP_HOST_GONE;
            g_mp.host_gone_until = g_mp.now_ms + BW_MP_HOST_WAIT_MS;
        }
        if (g_mp.players[0].peer == 1u) {
            g_mp.players[0].connected = 0;
        }
        return;
    }
    if (kind == (int)BW_SESSION_JOIN && peer == 1u) {
        /* the host is back: say hello again; its roster says the rest */
        joiner_hello();
        return;
    }
    if (kind == (int)BW_SESSION_MESSAGE) {
        joiner_on_message(peer, (const char *)buf, size);
    }
}

/* ------------------------------------------------------------------ */
/* The calls                                                           */
/* ------------------------------------------------------------------ */

static void begin(int host, const char *code)
{
    char game = g_mp.game;
    char mods[BW_MP_MODS_MAX + 1];
    int mods_set = g_mp.mods_set;

    memcpy(mods, g_mp.mods, sizeof(mods));
    memset(&g_mp, 0, sizeof(g_mp));
    g_mp.game = game;
    memcpy(g_mp.mods, mods, sizeof(mods));
    g_mp.mods_set = mods_set;
    g_mp.host = host;
    g_mp.state = BW_MP_LOBBY;
    g_mp.now_ms = now_ms();
    copy_n(g_mp.code, sizeof(g_mp.code), code, strlen(code));
}

void bw_mp_attach(int host, const char *code)
{
    bw_mod_net_set_port_handler(bw_mp_on_net);
    begin(host != 0, code != NULL ? code : "");
    if (host) {
        const char *me = g_net->own_name != NULL ? g_net->own_name() : "host";

        g_mp.players[0].peer = 1u;
        g_mp.players[0].connected = 1;
        g_mp.players[0].game = local_game();
        g_mp.players[0].mods_ok = 1;
        copy_n(g_mp.players[0].name, sizeof(g_mp.players[0].name), me,
               strlen(me));
    } else {
        joiner_hello();
    }
}

int bw_mp_host(char *code, u32 size)
{
    char got[40];
    int rc;

    bw_mod_net_set_port_handler(bw_mp_on_net);
    rc = g_net->host(got, sizeof(got));
    if (rc != 0) {
        return rc;
    }
    begin(1, got);
    g_mp.players[0].peer = 1u;
    g_mp.players[0].connected = 1;
    g_mp.players[0].game = local_game();
    g_mp.players[0].mods_ok = 1;
    {
        const char *me = g_net->own_name != NULL ? g_net->own_name() : "host";

        copy_n(g_mp.players[0].name, sizeof(g_mp.players[0].name), me,
               strlen(me));
    }
    if (code != NULL && size > 0) {
        copy_n(code, size, got, strlen(got));
    }
    return 0;
}

int bw_mp_join(const char *code)
{
    int rc;

    if (code == NULL) {
        return -1;
    }
    bw_mod_net_set_port_handler(bw_mp_on_net);
    rc = g_net->join(code);
    if (rc != 0) {
        return rc;
    }
    begin(0, code);
    joiner_hello();
    return 0;
}

void bw_mp_leave(void)
{
    g_net->leave();
    g_mp.state = BW_MP_OFF;
    snprintf(g_mp.reason, sizeof(g_mp.reason), "left");
}

int bw_mp_take_started(void)
{
    int s = g_mp.started;

    g_mp.started = 0;
    return s;
}

int bw_mp_started_pending(void)
{
    return g_mp.started;
}

void bw_mp_rename(const char *name)
{
    char msg[BW_MP_NAME_MAX + 8];
    int n;

    if (name == NULL || name[0] == '\0' || g_mp.state == BW_MP_OFF) {
        return;
    }
    if (g_mp.host) {
        copy_n(g_mp.players[0].name, sizeof(g_mp.players[0].name), name,
               strlen(name));
        host_broadcast_roster();
        return;
    }
    n = snprintf(msg, sizeof(msg), "name %s", name);
    if (n > 0) {
        (void)g_net->send(1, msg, (u32)n);
    }
}

const char *bw_mp_code(void)
{
    return g_mp.code;
}

/* The relay no longer knows the code: it restarted, and it keeps nothing.
 * The host opens a new session and keeps its lobby: the players join the
 * new code, and each one takes its place on the roster again by its name
 * (the identity of SPEC section 12 replaces the name).  A joiner cannot
 * learn the new code by itself: its session ends with the reason, and the
 * player joins the code the host gives them. */
static void relay_restarted(void)
{
    char code[40];
    u32 i;

    if (!g_mp.host) {
        g_mp.state = BW_MP_ENDED;
        snprintf(g_mp.reason, sizeof(g_mp.reason),
                 "the relay restarted: ask the host for the new code");
        return;
    }
    if (g_net->host(code, sizeof(code)) != 0) {
        return;                         /* tried again at the next attempt */
    }
    copy_n(g_mp.code, sizeof(g_mp.code), code, strlen(code));
    for (i = 1; i < BW_MP_PLAYERS_MAX; i++) {
        g_mp.players[i].connected = 0;
    }
    g_mp.state = g_mp.roster_set ? BW_MP_PAUSED : BW_MP_LOBBY;
    snprintf(g_mp.reason, sizeof(g_mp.reason),
             "the relay restarted: the new code is %s", code);
    fprintf(stderr, "eidos: %s\n", g_mp.reason);
}

u32 bw_mp_stamp(void)
{
    u32 h = 2166136261u;
    u32 i;

    h = (h ^ (u32)g_mp.state) * 16777619u;
    for (i = 0; i < BW_MP_PLAYERS_MAX; i++) {
        const BwMpPlayer *p = &g_mp.players[i];
        u32 v = p->peer | ((u32)p->connected << 8) | ((u32)p->on_roster << 9) |
                ((u32)p->mods_ok << 10) | ((u32)(unsigned char)p->game << 16);

        h = (h ^ v) * 16777619u;
    }
    return h;
}

int bw_mp_start(void)
{
    u32 i;

    if (!g_mp.host || (g_mp.state != BW_MP_LOBBY && g_mp.state != BW_MP_PAUSED)) {
        snprintf(g_mp.blocker, sizeof(g_mp.blocker), "only the host starts, "
                 "from the lobby or a pause");
        return -1;
    }
    if (!host_ready()) {
        return -1;
    }
    if (g_mp.state == BW_MP_LOBBY) {
        g_mp.started = 1;
    }
    if (!g_mp.roster_set) {
        /* the first start sets the roster: the players here now */
        for (i = 0; i < BW_MP_PLAYERS_MAX; i++) {
            if (g_mp.players[i].peer != 0 && g_mp.players[i].connected) {
                g_mp.players[i].on_roster = 1;
            }
        }
        g_mp.roster_set = 1;
    }
    g_mp.state = BW_MP_PLAYING;
    host_broadcast_roster();
    host_send_word("start");
    return 0;
}

const char *bw_mp_start_blocker(void)
{
    if (g_mp.host && (g_mp.state == BW_MP_LOBBY || g_mp.state == BW_MP_PAUSED)) {
        (void)host_ready();
    }
    return g_mp.blocker;
}

BwMpState bw_mp_state(void)
{
    return g_mp.state;
}

int bw_mp_is_host(void)
{
    return g_mp.state != BW_MP_OFF && g_mp.host;
}

int bw_mp_paused(void)
{
    return g_mp.state == BW_MP_PAUSED || g_mp.state == BW_MP_HOST_GONE ||
           g_mp.state == BW_MP_RECONNECTING;
}

const char *bw_mp_wait_text(void)
{
    u32 i;
    size_t at;

    switch (g_mp.state) {
    case BW_MP_HOST_GONE:
        snprintf(g_mp.wait, sizeof(g_mp.wait), "Waiting for the host to come back");
        return g_mp.wait;
    case BW_MP_RECONNECTING:
        snprintf(g_mp.wait, sizeof(g_mp.wait), "Connection lost. Joining again...");
        return g_mp.wait;
    case BW_MP_PAUSED:
        break;
    default:
        g_mp.wait[0] = '\0';
        return g_mp.wait;
    }
    at = (size_t)snprintf(g_mp.wait, sizeof(g_mp.wait), "Waiting for players:");
    for (i = 0; i < BW_MP_PLAYERS_MAX; i++) {
        const BwMpPlayer *p = &g_mp.players[i];
        int w;

        if (p->peer == 0 || !p->on_roster || p->connected) {
            continue;
        }
        w = snprintf(g_mp.wait + at, sizeof(g_mp.wait) - at, " %s", p->name);
        if (w < 0 || (size_t)w >= sizeof(g_mp.wait) - at) {
            break;
        }
        at += (size_t)w;
    }
    if (g_mp.host && host_ready()) {
        snprintf(g_mp.wait, sizeof(g_mp.wait),
                 "Everyone is back. Press START to play.");
    } else if (!g_mp.host && at == strlen("Waiting for players:")) {
        snprintf(g_mp.wait, sizeof(g_mp.wait), "Waiting for the host to start");
    }
    return g_mp.wait;
}

const char *bw_mp_end_reason(void)
{
    return g_mp.reason;
}

u32 bw_mp_player_count(void)
{
    u32 i;
    u32 n = 0;

    for (i = 0; i < BW_MP_PLAYERS_MAX; i++) {
        if (g_mp.players[i].peer != 0) {
            n++;
        }
    }
    return n;
}

int bw_mp_player_at(u32 index, BwMpPlayer *out)
{
    u32 i;
    u32 seen = 0;

    for (i = 0; i < BW_MP_PLAYERS_MAX; i++) {
        if (g_mp.players[i].peer == 0) {
            continue;
        }
        if (seen == index) {
            if (out != NULL) {
                *out = g_mp.players[i];
            }
            return 0;
        }
        seen++;
    }
    return -1;
}

void bw_mp_frame_at(u64 t)
{
    g_mp.now_ms = t;
    switch (g_mp.state) {
    case BW_MP_GRACE:
        if (g_mp.host && t >= g_mp.grace_until) {
            g_mp.state = BW_MP_PAUSED;
            host_broadcast_roster();
            host_send_word("pause");
        }
        break;
    case BW_MP_HOST_GONE:
        if (t >= g_mp.host_gone_until) {
            g_mp.state = BW_MP_ENDED;
            snprintf(g_mp.reason, sizeof(g_mp.reason), "the host did not come back");
            g_net->leave();
        }
        break;
    case BW_MP_RECONNECTING:
        if (t >= g_mp.host_gone_until) {
            g_mp.state = BW_MP_ENDED;
            snprintf(g_mp.reason, sizeof(g_mp.reason), "the relay could not be reached");
        } else if (t >= g_mp.next_rejoin) {
            g_mp.next_rejoin = t + BW_MP_REJOIN_EVERY_MS;
            if (g_net->join(g_mp.code) != 0) {
                const char *why = g_net->last_error != NULL
                                  ? g_net->last_error() : "";

                if (strcmp(why, "unknown-code") == 0) {
                    relay_restarted();
                }
            } else {
                if (g_mp.host) {
                    /* the host is back: every game waits for its start */
                    g_mp.state = BW_MP_PAUSED;
                    host_broadcast_roster();
                    host_send_word("pause");
                } else {
                    g_mp.state = BW_MP_PAUSED;
                    joiner_hello();
                }
            }
        }
        break;
    default:
        break;
    }
}

void bw_mp_hold_wait(void)
{
#ifdef _WIN32
    Sleep(16);
#else
    struct timespec ts;

    ts.tv_sec = 0;
    ts.tv_nsec = 16L * 1000000L;
    (void)nanosleep(&ts, NULL);
#endif
}

void bw_mp_frame(void)
{
    if (g_mp.state != BW_MP_OFF) {
        bw_mp_frame_at(now_ms());
    }
}

void bw_mp_set_local(char game, const char *mods)
{
    g_mp.game = game;
    if (mods != NULL) {
        copy_n(g_mp.mods, sizeof(g_mp.mods), mods, strlen(mods));
        g_mp.mods_set = 1;
    } else {
        g_mp.mods_set = 0;
    }
}

void bw_mp_reset(void)
{
    memset(&g_mp, 0, sizeof(g_mp));
    g_net = &k_net;
}

void bw_mp_set_net(const BwMpNet *net)
{
    g_net = net != NULL ? net : &k_net;
}
