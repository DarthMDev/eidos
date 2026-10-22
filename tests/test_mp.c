/*
 * tests/test_mp.c -- multiplayer's lobby, start and pause (sdk/mp.c).
 *
 * The layer is driven the way the network drives it (bw_mp_on_net, the
 * port handler) with a network of the test's own that records what the
 * layer sends, so one process plays the host's game, then a joiner's.
 */

#include <stdio.h>
#include <string.h>

#include "mod_net.h"
#include "mp.h"
#include "save.h"

#include "port_test.h"

/* What the layer sent. */
static char g_sent[16][512];
static u32  g_sent_peer[16];
static int  g_sent_count;
static u32  g_removed;
static int  g_joins;
static int  g_join_fails;

static void sent_reset(void)
{
    g_sent_count = 0;
    g_removed = 0;
}

static const char *g_next_code = "ABC234";
static const char *g_error = "";
static const char *g_peer3 = "Bianca";

static int t_host(char *code, u32 size)
{
    snprintf(code, size, "%s", g_next_code);
    return 0;
}

static const char *t_last_error(void)
{
    return g_error;
}

static int t_join(const char *code)
{
    (void)code;
    g_joins++;
    return g_join_fails ? -1 : 0;
}

static void t_leave(void)
{
}

static int t_send(u32 peer, const void *buf, u32 size)
{
    if (g_sent_count < 16) {
        u32 n = size < 511u ? size : 511u;

        memcpy(g_sent[g_sent_count], buf, n);
        g_sent[g_sent_count][n] = '\0';
        g_sent_peer[g_sent_count] = peer;
        g_sent_count++;
    }
    return 0;
}

static int t_remove(u32 peer)
{
    g_removed = peer;
    return 0;
}

static u32 t_self(void)
{
    return 1;
}

static int t_peer_name(u32 peer, char *out, u32 size)
{
    snprintf(out, size, "%s", peer == 2 ? "Cheren" : peer == 3 ? g_peer3 : "N");
    return 0;
}

static const char *t_own_name(void)
{
    return "Hilda";
}

static const BwMpNet k_test_net = {
    t_host, t_join, t_leave, t_send, t_remove, t_self, t_peer_name, t_own_name,
    t_last_error
};

/* Whether the layer sent `word` (a whole message or a message's start). */
static int sent_has(const char *word)
{
    int i;

    for (i = 0; i < g_sent_count; i++) {
        if (strncmp(g_sent[i], word, strlen(word)) == 0) {
            return 1;
        }
    }
    return 0;
}

static void say(u32 peer, const char *text)
{
    bw_mp_on_net(peer, (int)BW_SESSION_MESSAGE, text, (u32)strlen(text));
}

static void test_host(void)
{
    char code[16];

    printf("the host's lobby\n");
    bw_mp_reset();
    bw_mp_set_net(&k_test_net);
    bw_mp_set_local('B', "soul=2");
    BW_CHECK_EQ("the host opens a session", bw_mp_host(code, sizeof code), 0);
    BW_CHECK_EQ("with the relay's code", strcmp(code, "ABC234"), 0);
    BW_CHECK_EQ("in the lobby", (long)bw_mp_state(), (long)BW_MP_LOBBY);
    bw_mp_frame_at(1000);
    BW_CHECK_EQ("nobody to start with", bw_mp_start(), -1);
    BW_CHECK_EQ("and it says so",
                strcmp(bw_mp_start_blocker(), "nobody has joined yet"), 0);

    sent_reset();
    bw_mp_on_net(2, (int)BW_SESSION_JOIN, NULL, 0);
    BW_CHECK_EQ("a join sends the roster", sent_has("roster lobby"), 1);
    BW_CHECK_EQ("the lobby has two players", (long)bw_mp_player_count(), 2);
    BW_CHECK_EQ("no start before the player's game answers", bw_mp_start(), -1);
    say(2, "hello 1 W soul=1");
    BW_CHECK_EQ("another multiplayer version blocks the start",
                bw_mp_start(), -1);
    BW_CHECK_EQ("and the lobby names it",
                strstr(bw_mp_start_blocker(), "soul") != NULL, 1);
    say(2, "hello 1 W soul=2");
    sent_reset();
    BW_CHECK_EQ("the host starts", bw_mp_start(), 0);
    BW_CHECK_EQ("the game runs", (long)bw_mp_state(), (long)BW_MP_PLAYING);
    BW_CHECK_EQ("every game is told to start", sent_has("start"), 1);
    {
        BwMpPlayer p;

        BW_CHECK_EQ("the joiner is listed", bw_mp_player_at(1, &p), 0);
        BW_CHECK_EQ("by its name", strcmp(p.name, "Cheren"), 0);
        BW_CHECK_EQ("on the roster", p.on_roster, 1);
        BW_CHECK_EQ("with its game", p.game, 'W');
    }

    printf("a stranger after the start\n");
    bw_mp_on_net(3, (int)BW_SESSION_JOIN, NULL, 0);
    BW_CHECK_EQ("is removed", (long)g_removed, 3);

    printf("a drop and a return within the grace\n");
    bw_mp_on_net(2, (int)BW_SESSION_LEAVE, NULL, 0);
    BW_CHECK_EQ("the others play on", (long)bw_mp_state(), (long)BW_MP_GRACE);
    BW_CHECK_EQ("not held", bw_mp_paused(), 0);
    bw_mp_frame_at(1000 + BW_MP_GRACE_MS - 1);
    BW_CHECK_EQ("still within the grace", (long)bw_mp_state(), (long)BW_MP_GRACE);
    bw_mp_on_net(2, (int)BW_SESSION_JOIN, NULL, 0);
    BW_CHECK_EQ("back in time: play goes on", (long)bw_mp_state(),
                (long)BW_MP_PLAYING);

    printf("a drop past the grace\n");
    bw_mp_frame_at(20000);
    bw_mp_on_net(2, (int)BW_SESSION_LEAVE, NULL, 0);
    sent_reset();
    bw_mp_frame_at(20000 + BW_MP_GRACE_MS);
    BW_CHECK_EQ("every game pauses", (long)bw_mp_state(), (long)BW_MP_PAUSED);
    BW_CHECK_EQ("the host is held", bw_mp_paused(), 1);
    BW_CHECK_EQ("the others are told", sent_has("pause"), 1);
    BW_CHECK_EQ("the wait names the missing player",
                strstr(bw_mp_wait_text(), "Cheren") != NULL, 1);
    BW_CHECK_EQ("no start while a roster player is missing", bw_mp_start(), -1);
    bw_mp_on_net(2, (int)BW_SESSION_JOIN, NULL, 0);
    say(2, "hello 1 W soul=2");
    BW_CHECK_EQ("still held until the host starts", bw_mp_paused(), 1);
    BW_CHECK_EQ("the screen says the host may start",
                strstr(bw_mp_wait_text(), "START") != NULL, 1);
    BW_CHECK_EQ("the host starts again", bw_mp_start(), 0);
    BW_CHECK_EQ("the game runs again", (long)bw_mp_state(), (long)BW_MP_PLAYING);

    printf("the host loses the relay\n");
    bw_mp_on_net(0, BW_MOD_NET_PORT_ENDED, "lost", 4);
    BW_CHECK_EQ("the host joins again", (long)bw_mp_state(),
                (long)BW_MP_RECONNECTING);
    BW_CHECK_EQ("held meanwhile", bw_mp_paused(), 1);
    g_joins = 0;
    sent_reset();
    bw_mp_frame_at(35000 + BW_MP_REJOIN_EVERY_MS);
    BW_CHECK_EQ("it tried", g_joins, 1);
    BW_CHECK_EQ("back in: every game waits for the host's start",
                (long)bw_mp_state(), (long)BW_MP_PAUSED);
    BW_CHECK_EQ("and the others are told", sent_has("pause"), 1);
    bw_mp_reset();
}

static void test_joiner(void)
{
    printf("a joiner\n");
    bw_mp_reset();
    bw_mp_set_net(&k_test_net);
    bw_mp_set_local('W', "");
    sent_reset();
    BW_CHECK_EQ("the joiner joins", bw_mp_join("abc234"), 0);
    BW_CHECK_EQ("no hello before the host says who it is", sent_has("hello"), 0);
    say(1, "hostid 0123456789abcdef0123456789abcdef");
    BW_CHECK_EQ("then hello, with who the player is to this host",
                sent_has("hello 1 W - "), 1);
    BW_CHECK_EQ("to the host", (long)g_sent_peer[0], 1);
    say(1, "roster lobby\n1\t1\t0\tB\t1\tHilda\n2\t1\t0\tW\t1\tCheren\n");
    BW_CHECK_EQ("the roster is read", (long)bw_mp_player_count(), 2);
    {
        BwMpPlayer p;

        (void)bw_mp_player_at(0, &p);
        BW_CHECK_EQ("the host's name", strcmp(p.name, "Hilda"), 0);
    }
    BW_CHECK_EQ("in the lobby", (long)bw_mp_state(), (long)BW_MP_LOBBY);
    say(1, "start");
    BW_CHECK_EQ("the host's start runs the game", (long)bw_mp_state(),
                (long)BW_MP_PLAYING);
    say(1, "pause");
    BW_CHECK_EQ("the host's pause holds it", bw_mp_paused(), 1);
    say(1, "start");

    printf("the host goes\n");
    bw_mp_frame_at(5000);
    bw_mp_on_net(1, (int)BW_SESSION_LEAVE, NULL, 0);
    BW_CHECK_EQ("the game is held", (long)bw_mp_state(), (long)BW_MP_HOST_GONE);
    sent_reset();
    bw_mp_on_net(1, (int)BW_SESSION_JOIN, NULL, 0);
    BW_CHECK_EQ("the host is back: hello again", sent_has("hello"), 1);
    say(1, "pause");
    say(1, "start");
    BW_CHECK_EQ("and the host's start goes on", (long)bw_mp_state(),
                (long)BW_MP_PLAYING);
    bw_mp_on_net(1, (int)BW_SESSION_LEAVE, NULL, 0);
    bw_mp_frame_at(5000 + BW_MP_HOST_WAIT_MS);
    BW_CHECK_EQ("a host that does not come back ends it", (long)bw_mp_state(),
                (long)BW_MP_ENDED);

    printf("a removed joiner\n");
    bw_mp_reset();
    bw_mp_set_net(&k_test_net);
    (void)bw_mp_join("ABC234");
    bw_mp_on_net(0, BW_MOD_NET_PORT_ENDED, "removed", 7);
    BW_CHECK_EQ("the session is over", (long)bw_mp_state(), (long)BW_MP_ENDED);
    BW_CHECK_EQ("with the reason", strcmp(bw_mp_end_reason(), "removed"), 0);
    bw_mp_reset();
}

static void test_relay_restart(void)
{
    char code[16];

    printf("the relay restarts under the host\n");
    bw_mp_reset();
    bw_mp_set_net(&k_test_net);
    bw_mp_set_local('B', "");
    g_next_code = "ABC234";
    (void)bw_mp_host(code, sizeof code);
    bw_mp_frame_at(1000);
    bw_mp_on_net(2, (int)BW_SESSION_JOIN, NULL, 0);
    say(2, "hello 1 W -");
    BW_CHECK_EQ("the game starts", bw_mp_start(), 0);
    bw_mp_on_net(0, BW_MOD_NET_PORT_ENDED, "lost", 4);
    g_join_fails = 1;
    g_error = "unknown-code";
    g_next_code = "NEW567";
    bw_mp_frame_at(1000 + BW_MP_REJOIN_EVERY_MS);
    BW_CHECK_EQ("the host opens a new session", strcmp(bw_mp_code(), "NEW567"), 0);
    BW_CHECK_EQ("and says so", strstr(bw_mp_end_reason(), "NEW567") != NULL, 1);
    BW_CHECK_EQ("every game waits for the host's start", (long)bw_mp_state(),
                (long)BW_MP_PAUSED);
    g_peer3 = "Cheren";
    sent_reset();
    bw_mp_on_net(3, (int)BW_SESSION_JOIN, NULL, 0);
    BW_CHECK_EQ("the player who comes back under a new number is not removed",
                (long)g_removed, 0);
    say(3, "hello 1 W -");
    BW_CHECK_EQ("it takes its roster place: the host may start",
                bw_mp_start(), 0);
    g_peer3 = "Bianca";
    g_join_fails = 0;
    g_error = "";
    g_next_code = "ABC234";

    printf("the relay restarts under a joiner\n");
    bw_mp_reset();
    bw_mp_set_net(&k_test_net);
    (void)bw_mp_join("ABC234");
    say(1, "start");
    bw_mp_frame_at(1000);
    bw_mp_on_net(0, BW_MOD_NET_PORT_ENDED, "lost", 4);
    g_join_fails = 1;
    g_error = "unknown-code";
    bw_mp_frame_at(1000 + BW_MP_REJOIN_EVERY_MS);
    BW_CHECK_EQ("the joiner's session ends", (long)bw_mp_state(),
                (long)BW_MP_ENDED);
    BW_CHECK_EQ("asking for the new code",
                strstr(bw_mp_end_reason(), "new code") != NULL, 1);
    g_join_fails = 0;
    g_error = "";

    printf("a plain leave\n");
    bw_mp_leave();
    BW_CHECK_EQ("says why", strcmp(bw_mp_end_reason(), "left"), 0);
    bw_mp_reset();
}

static int g_booted = -1;

static void t_boot(int new_game)
{
    g_booted = new_game;
}

static int file_size(const char *name)
{
    char dir[512], path[600];
    FILE *f;
    long n;

    if (bw_mp_dir(dir, sizeof dir) != 0) {
        return -1;
    }
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return (int)n;
}

/* The saves: the host sends a joiner its save at the start (none: a new
 * game), and keeps what a joiner's game uploads. */
static void test_saves(void)
{
    char code[16];
    char fp[33];
    char msg[256];
    static unsigned char data[9000];
    unsigned crc = 0xFFFFFFFFu;
    unsigned i;
    int k, n;

    printf("the saves\n");
    {
        /* what an earlier run kept is gone first */
        static const char *const old[] = {
            "test_mp_tmp/multiplayer/ffffeeeeddddccccbbbbaaaa99998888.sav",
            "test_mp_tmp/multiplayer/ffffeeeeddddccccbbbbaaaa99998888.sav.1",
            "test_mp_tmp/multiplayer/ffffeeeeddddccccbbbbaaaa99998888.sav.2",
            "test_mp_tmp/multiplayer/ffffeeeeddddccccbbbbaaaa99998888.sav.3"
        };

        for (k = 0; k < 4; k++) {
            remove(old[k]);
        }
    }
    bw_mp_reset();
    bw_mp_set_net(&k_test_net);
    bw_mp_set_local('B', "");
    bw_mp_set_boot(t_boot);
    (void)bw_mp_host(code, sizeof code);
    bw_mp_frame_at(1000);
    sent_reset();
    bw_mp_on_net(2, (int)BW_SESSION_JOIN, NULL, 0);
    BW_CHECK_EQ("the host says who it is", sent_has("hostid "), 1);
    snprintf(fp, sizeof fp, "%s", "ffffeeeeddddccccbbbbaaaa99998888");
    snprintf(msg, sizeof msg, "hello 1 W - %s 0", fp);
    say(2, msg);
    sent_reset();
    BW_CHECK_EQ("the host starts", bw_mp_start(), 0);
    BW_CHECK_EQ("a player the host has no save for: a new game",
                sent_has("save 0 0 0"), 1);

    /* the joiner's game uploads its save: two pieces, then the host keeps it */
    for (i = 0; i < sizeof data; i++) {
        data[i] = (unsigned char)(i * 7u);
    }
    for (i = 0; i < sizeof data; i++) {
        crc ^= data[i];
        for (k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    crc = ~crc;
    for (i = 0; i < sizeof data; i += 3840u) {
        static unsigned char piece[4096];
        unsigned m = sizeof data - i < 3840u ? (unsigned)sizeof data - i : 3840u;

        n = snprintf((char *)piece, 64, "save %u %u %u\n", (unsigned)sizeof data,
                     i, crc);
        memcpy(piece + n, data + i, m);
        bw_mp_on_net(2, (int)BW_SESSION_MESSAGE, piece, (u32)n + m);
    }
    snprintf(msg, sizeof msg, "%s.sav", fp);
    BW_CHECK_EQ("the host keeps the joiner's save", file_size(msg),
                (int)sizeof data);
    bw_mp_reset();

    /* a joiner: an empty save from the host starts a new game */
    bw_mp_set_net(&k_test_net);
    bw_mp_set_boot(t_boot);
    (void)bw_mp_join("ABC234");
    say(1, "hostid 0123456789abcdef0123456789abcdef");
    g_booted = -1;
    bw_mp_on_net(1, (int)BW_SESSION_MESSAGE, "save 0 0 0\n", 11);
    BW_CHECK_EQ("the joiner's game boots on a new game", g_booted, 1);
    bw_mp_leave();
    bw_mp_reset();
}

int main(void)
{
    /* every file the layer writes goes to the test's own folder */
    bw_set_save_dir("test_mp_tmp");
    test_host();
    test_joiner();
    test_relay_restart();
    test_saves();
    BW_REPORT("test_mp");
}
