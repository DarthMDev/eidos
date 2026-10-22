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

/*
 * The overlay functions this file calls by name, for the overlay
 * link (tools/ovlink.py): each one gets its plain name, a
 * trampoline that calls the overlay resident over the address.
 *
 * symbols:
 *   FIELDMAP_GetFieldPlayer              0x02188CB4  thumb  ov021
 *   FIELDMAP_GetZoneId                   0x02188C9C  thumb  ov021
 *   FIELD_PLAYER_GetDirDisp              0x021A3050  thumb  ov021
 *   FIELD_PLAYER_GetFormCode             0x021A3098  thumb  ov021
 *   FIELD_PLAYER_GetPos                  0x021A3038  thumb  ov021
 *   FIELD_PLAYER_GetRideEffect           0x021A2F98  thumb  ov021
 *   FIELD_PLAYER_GetSex                  0x021A30B0  thumb  ov021
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mp.h"
#include "mod_loader.h"
#include "mod_net.h"
#include "plat_dir.h"
#include "plat_save.h"
#include "plat_thread.h"
#include "save.h"
#include "save_slots.h"

#include <sys/stat.h>

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
    char       host_id[33];         /* the host's lasting id (SPEC 12)  */
    char       fp[33];              /* a joiner: who it is to this host */
    int        in_game;             /* a joiner: its game runs the save the host sent */
    u32        last_writes;         /* the save writes already acted on */
    int        save_wanted;         /* a joiner: the host saved; save at the next quiet moment */
    int        saving;              /* a joiner: the game's save runs */
    void      *save_gd;             /* the game data the save runs on */
    int        save_test;           /* the port's own check */
    u8         save_pending[BW_MP_PLAYERS_MAX];  /* the host: saves not in yet, by peer - 1 */
} g_mp;

/* ------------------------------------------------------------------ */
/* The saves: files, ids, transfers                                    */
/* ------------------------------------------------------------------ */

#if defined(__GNUC__) || defined(__clang__)
#define BW_MP_WEAK __attribute__((weak))
#else
#define BW_MP_WEAK
#endif

/* The shared table's size (below, "The shared table and the events"). */
#define BW_MP_SHARED_MAX 512u
#define BW_MP_SHARED_KEY 80u

static int  shared_put(const char *key, s32 value);
static int  shared_parse(const char *msg, u32 size, char *key, s32 *value);
static void host_shared_apply(const char *key, s32 value);
static void host_shared_send_all(u32 peer);
static void shared_load(void);
static void forward_event(const void *payload, u32 size);
static void host_on_events(u32 peer, const u8 *buf, u32 size);

/* The event layer (sdk/mod_event.h): this game's events go out, the
 * players' events come in. */
extern void bw_mod_event_set_forward(void (*fn)(const void *payload, u32 size)) BW_MP_WEAK;
extern int  bw_mod_event_remote(u32 player, const void *buf, u32 size) BW_MP_WEAK;

/* The backup chip (sdk/arm7/fs.c): the joiner's game runs on the file the
 * host sent; its writes are uploaded. */
extern void bw_arm7_backup_set_path(const char *path) BW_MP_WEAK;
extern u32  bw_arm7_backup_writes(void) BW_MP_WEAK;
extern int  bw_arm7_backup_dirty(void) BW_MP_WEAK;

/* The game's save, run by the port on a joiner when the host saved
 * (src/game/arm9/game_data.c), and the game data the port has bound. */
extern int GAME_DATA_SaveStart(void *gameData) BW_MP_WEAK;
extern int GAME_DATA_SaveMain(void *gameData) BW_MP_WEAK;

/* The game system (src/game/arm9/gamesystem.c): its global's first word is
 * the running one, and the field and the event are read from it. */
extern void *bw_dsref__GameSystemGlobal BW_MP_WEAK;
extern void *GAMESYSTEM_GetGameData(const void *gameSystem) BW_MP_WEAK;
extern void *GAMESYSTEM_GetEvent(const void *gameSystem) BW_MP_WEAK;
extern void *GAMESYSTEM_GetFieldMap(const void *gameSystem) BW_MP_WEAK;

static void (*g_boot)(int new_game);

/* A transfer's piece: the header line, then up to this many bytes. */
#define BW_MP_PIECE 3840u
/* The pieces sent per frame, so the receiver's queue keeps up. */
#define BW_MP_PIECES_PER_FRAME 6u
#define BW_MP_SENDS_MAX 16u

typedef struct BwMpSend {
    u32  peer;
    u8  *buf;
    u32  size;
    u32  off;
    u32  acked;                 /* the bytes the receiver confirmed */
    u32  idle;                  /* frames without a confirmation */
    u32  crc;
    int  used;
} BwMpSend;

/* The bytes in flight before the receiver's confirmation: a few pieces,
 * so its queue keeps up whatever its frame rate. */
#define BW_MP_WINDOW (4u * BW_MP_PIECE)
/* Frames without a confirmation before the unconfirmed pieces go again. */
#define BW_MP_RESEND_FRAMES 120u

typedef struct BwMpRecv {
    u8  *buf;
    u32  total;
    u32  got;
    u32  crc;
} BwMpRecv;

static BwMpSend g_sends[BW_MP_SENDS_MAX];
static BwMpRecv g_recv[BW_MP_PLAYERS_MAX + 1];

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

static u32 crc32_of(const u8 *p, u32 n)
{
    u32 c = 0xFFFFFFFFu;
    u32 i;
    int k;

    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    return ~c;
}

int bw_mp_dir(char *out, u32 size)
{
    const char *dir = bw_save_dir();

    if (dir == NULL || dir[0] == '\0' ||
        snprintf(out, size, "%s/multiplayer", dir) >= (int)size) {
        return -1;
    }
    (void)bw_plat_mkdirs(out);
    return 0;
}

static int mp_path(char *out, u32 size, const char *name)
{
    char dir[400];

    if (bw_mp_dir(dir, sizeof(dir)) != 0 ||
        snprintf(out, size, "%s/%s", dir, name) >= (int)size) {
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The host's games                                                    */
/* ------------------------------------------------------------------ */

/*
 * A host keeps each multiplayer game in a folder of its own,
 * <save>/multiplayer/games/<name>: its own save (host.sav), its players'
 * saves and its own lasting id.  The joiner's own files (secrets.txt and
 * the save it plays) stay in <save>/multiplayer.  The game chosen last is
 * kept through a reset, so it is outside g_mp.
 */
static char g_game[BW_MP_GAME_NAME_MAX];

static int games_dir(char *out, u32 size)
{
    char dir[400];

    if (bw_mp_dir(dir, sizeof(dir)) != 0 ||
        snprintf(out, size, "%s/games", dir) >= (int)size) {
        return -1;
    }
    (void)bw_plat_mkdirs(out);
    return 0;
}

/* A file of the chosen game; with no game chosen, the multiplayer folder
 * itself (the layout before the games, and the tests). */
static int game_path(char *out, u32 size, const char *name)
{
    char dir[440];

    if (g_game[0] == '\0') {
        return mp_path(out, size, name);
    }
    if (games_dir(dir, sizeof(dir)) != 0 ||
        snprintf(out, size, "%s/%s", dir, g_game) >= (int)size) {
        return -1;
    }
    (void)bw_plat_mkdirs(out);
    snprintf(dir, sizeof(dir), "%s", out);
    if (snprintf(out, size, "%s/%s", dir, name) >= (int)size) {
        return -1;
    }
    return 0;
}

/* When a game was last played: its save's time, else its folder's. */
static long game_time(const char *dir, const char *name)
{
    char path[520];
    struct stat st;

    snprintf(path, sizeof(path), "%s/%s/host.sav", dir, name);
    if (stat(path, &st) == 0) {
        return (long)st.st_mtime;
    }
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    return stat(path, &st) == 0 ? (long)st.st_mtime : 0;
}

typedef struct GameList {
    char (*names)[BW_MP_GAME_NAME_MAX];
    long times[BW_MP_GAMES_MAX];
    int  n, max;
    const char *dir;
} GameList;

static int game_list_one(void *ctx, const char *name)
{
    GameList *l = (GameList *)ctx;
    char path[520];
    long t;
    int i;

    snprintf(path, sizeof(path), "%s/%s", l->dir, name);
    if (bw_plat_path_kind(path) != BW_PATH_DIR ||
        strlen(name) >= BW_MP_GAME_NAME_MAX || l->n >= l->max) {
        return 0;
    }
    /* newest first */
    t = game_time(l->dir, name);
    for (i = l->n; i > 0 && l->times[i - 1] < t; i--) {
        memcpy(l->names[i], l->names[i - 1], BW_MP_GAME_NAME_MAX);
        l->times[i] = l->times[i - 1];
    }
    snprintf(l->names[i], BW_MP_GAME_NAME_MAX, "%s", name);
    l->times[i] = t;
    l->n++;
    return 0;
}

int bw_mp_game_list(char names[][BW_MP_GAME_NAME_MAX], int max)
{
    char dir[440];
    GameList l;

    if (games_dir(dir, sizeof(dir)) != 0) {
        return 0;
    }
    memset(&l, 0, sizeof(l));
    l.names = names;
    l.max = max < BW_MP_GAMES_MAX ? max : BW_MP_GAMES_MAX;
    l.dir = dir;
    (void)bw_plat_list_dir(dir, game_list_one, &l);
    return l.n;
}

int bw_mp_game_select(const char *name)
{
    if (name == NULL || name[0] == '\0' ||
        strlen(name) >= BW_MP_GAME_NAME_MAX) {
        return -1;
    }
    snprintf(g_game, sizeof(g_game), "%s", name);
    g_mp.host_id[0] = '\0';         /* each game has its own */
    fprintf(stderr, "eidos: multiplayer: the host's game is \"%s\"\n", g_game);
    return 0;
}

int bw_mp_game_create(const char *name, char *out, u32 size)
{
    char clean[BW_MP_GAME_NAME_MAX], dir[440], path[520];
    int i, n = 0, k;

    /* a folder name: letters, digits, space, '-' and '_' */
    for (i = 0; name != NULL && name[i] != '\0' && n < BW_MP_GAME_NAME_MAX - 4; i++) {
        char c = name[i];

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_') {
            clean[n++] = c;
        }
    }
    while (n > 0 && clean[n - 1] == ' ') {
        n--;
    }
    clean[n] = '\0';
    if (n == 0 || clean[0] == ' ') {
        snprintf(clean, sizeof(clean), "Game");
        n = 4;
    }
    if (games_dir(dir, sizeof(dir)) != 0) {
        return -1;
    }
    /* a name already taken gets a number */
    for (k = 1; k < 100; k++) {
        char cand[BW_MP_GAME_NAME_MAX];

        if (k == 1) {
            snprintf(cand, sizeof(cand), "%s", clean);
        } else {
            snprintf(cand, sizeof(cand), "%s %d", clean, k);
        }
        snprintf(path, sizeof(path), "%s/%s", dir, cand);
        if (bw_plat_path_kind(path) == BW_PATH_NONE) {
            (void)bw_plat_mkdirs(path);
            if (out != NULL && size > 0) {
                snprintf(out, size, "%s", cand);
            }
            fprintf(stderr, "eidos: multiplayer: new game \"%s\"\n", cand);
            return bw_mp_game_select(cand);
        }
    }
    return -1;
}

const char *bw_mp_game(void)
{
    return g_game;
}

static u8 *read_whole(const char *path, u32 *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    u8 *buf;

    *size = 0;
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0 ||
        n > (8L << 20) || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (u8 *)malloc((size_t)n);
    if (buf != NULL && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf != NULL) {
        *size = (u32)n;
    }
    return buf;
}

/* 32 hexadecimal digits from the OS random source (the session tokens'). */
static void random_hex(char out[33])
{
    bw_mod_net_random_hex(out);
}

/* The host's lasting id: made once, kept in the multiplayer folder. */
static const char *host_id(void)
{
    char path[512];
    FILE *f;

    if (g_mp.host_id[0] != '\0') {
        return g_mp.host_id;
    }
    if (game_path(path, sizeof(path), "host_id.txt") == 0 &&
        (f = fopen(path, "rb")) != NULL) {
        if (fscanf(f, "%32s", g_mp.host_id) != 1) {
            g_mp.host_id[0] = '\0';
        }
        fclose(f);
    }
    if (strlen(g_mp.host_id) != 32) {
        random_hex(g_mp.host_id);
        if (game_path(path, sizeof(path), "host_id.txt") == 0) {
            (void)bw_save_write_atomic(path, g_mp.host_id, 32);
        }
    }
    return g_mp.host_id;
}

/* What a secret is to the host: a fingerprint, so the host's save never
 * holds the secret itself. */
static void fingerprint(const char *secret, char out[33])
{
    static const char digits[] = "0123456789abcdef";
    u64 a = 0xcbf29ce484222325ull, b = 0x84222325cbf29ce4ull;
    const char *s;
    int i;

    for (s = secret; *s != '\0'; s++) {
        a = (a ^ (u8)*s) * 0x100000001b3ull;
        b = (b ^ (u8)*s) * 0x100000001b3ull;
        b ^= b >> 29;
    }
    for (i = 0; i < 16; i++) {
        out[i] = digits[(a >> (60 - 4 * i)) & 0xFu];
        out[16 + i] = digits[(b >> (60 - 4 * i)) & 0xFu];
    }
    out[32] = '\0';
}

/* A joiner's secret for one host: kept in the multiplayer folder, made the
 * first time that host is met. */
static void joiner_secret(const char *hid, char out[33])
{
    char path[512];
    char line[128];
    FILE *f;

    out[0] = '\0';
    if (mp_path(path, sizeof(path), "secrets.txt") != 0) {
        random_hex(out);
        return;
    }
    if ((f = fopen(path, "rb")) != NULL) {
        while (fgets(line, sizeof(line), f) != NULL) {
            char h[40], s[40];

            if (sscanf(line, "%39s %39s", h, s) == 2 && strcmp(h, hid) == 0 &&
                strlen(s) == 32) {
                memcpy(out, s, 33);
                break;
            }
        }
        fclose(f);
    }
    if (out[0] == '\0') {
        random_hex(out);
        if ((f = fopen(path, "ab")) != NULL) {
            fprintf(f, "%s %s\n", hid, out);
            fclose(f);
        }
    }
}

/* A file to one peer, in pieces, a few per frame (bw_mp_frame). */
/* Whether a file is on its way to the peer. */
static int sending_to(u32 peer)
{
    u32 i;

    for (i = 0; i < BW_MP_SENDS_MAX; i++) {
        if (g_sends[i].used && g_sends[i].peer == peer) {
            return 1;
        }
    }
    return 0;
}

/* One file at a time per peer: its confirmations ("saveack <got>
 * <total>") name no file, so a second one would take the first one's.  A
 * new file for the peer replaces the one on its way. */
static void send_bytes(u32 peer, const u8 *buf, u32 size)
{
    u32 i;

    for (i = 0; i < BW_MP_SENDS_MAX; i++) {
        if (g_sends[i].used && g_sends[i].peer == peer) {
            free(g_sends[i].buf);
            memset(&g_sends[i], 0, sizeof(g_sends[i]));
        }
    }
    if (size == 0) {
        (void)g_net->send(peer, "save 0 0 0\n", 11);
        return;
    }
    for (i = 0; i < BW_MP_SENDS_MAX; i++) {
        if (!g_sends[i].used) {
            g_sends[i].buf = (u8 *)malloc(size);
            if (g_sends[i].buf == NULL) {
                return;
            }
            memcpy(g_sends[i].buf, buf, size);
            g_sends[i].peer = peer;
            g_sends[i].size = size;
            g_sends[i].off = 0;
            g_sends[i].crc = crc32_of(buf, size);
            g_sends[i].used = 1;
            return;
        }
    }
}

static void sends_pump(void)
{
    u32 budget = BW_MP_PIECES_PER_FRAME;
    u32 i;

    for (i = 0; i < BW_MP_SENDS_MAX && budget > 0; i++) {
        BwMpSend *s = &g_sends[i];

        if (s->used && s->off - s->acked >= BW_MP_WINDOW) {
            /* the window is full: wait for the receiver; send the
             * unconfirmed pieces again when nothing comes */
            if (++s->idle >= BW_MP_RESEND_FRAMES) {
                fprintf(stderr, "eidos: multiplayer: no answer for a save "
                        "piece; sending it again from %u\n", (unsigned)s->acked);
                s->off = s->acked;
                s->idle = 0;
            }
            continue;
        }
        while (s->used && budget > 0 && s->off - s->acked < BW_MP_WINDOW &&
               s->off < s->size) {
            u8 msg[BW_MP_PIECE + 64];
            u32 n = s->size - s->off;
            int h;

            if (n > BW_MP_PIECE) {
                n = BW_MP_PIECE;
            }
            h = snprintf((char *)msg, 64, "save %u %u %u\n", (unsigned)s->size,
                         (unsigned)s->off, (unsigned)s->crc);
            memcpy(msg + h, s->buf + s->off, n);
            if (g_net->send(s->peer, msg, (u32)h + n) != 0) {
                break;                  /* tried again next frame */
            }
            s->off += n;
            budget--;
        }
    }
}

/* The receiver confirmed `got` bytes of the save it is getting from us:
 * "saveack <got> <total>".  The whole file confirmed ends the transfer. */
static void on_save_ack(u32 peer, const char *msg, u32 size)
{
    char text[64];
    unsigned got = 0, total = 0;
    u32 i;

    copy_n(text, sizeof(text), msg, size);
    if (sscanf(text, "saveack %u %u", &got, &total) != 2) {
        return;
    }
    for (i = 0; i < BW_MP_SENDS_MAX; i++) {
        BwMpSend *s = &g_sends[i];

        if (!s->used || s->peer != peer || s->size != total) {
            continue;
        }
        if (got > s->acked) {
            s->acked = got;
            s->idle = 0;
        } else if (got < s->off && ++s->idle >= 2) {
            /* the receiver lost a piece: send again from what it has */
            s->off = got;
            s->idle = 0;
        }
        if (s->acked >= s->size) {
            free(s->buf);
            memset(s, 0, sizeof(*s));
        }
        return;
    }
}

static void sends_clear(void)
{
    u32 i;

    for (i = 0; i < BW_MP_SENDS_MAX; i++) {
        free(g_sends[i].buf);
    }
    memset(g_sends, 0, sizeof(g_sends));
    for (i = 0; i <= BW_MP_PLAYERS_MAX; i++) {
        free(g_recv[i].buf);
    }
    memset(g_recv, 0, sizeof(g_recv));
}

/* The host keeps a joiner's save: the old one goes to .1 (and .1 to .2,
 * .2 to .3), and the new one is written whole before it replaces it. */
static void host_store(const BwMpPlayer *p, const u8 *buf, u32 size)
{
    char path[512], a[520], b[520];
    char name[48];
    int i;

    if (p->fp[0] == '\0') {
        return;
    }
    snprintf(name, sizeof(name), "%s.sav", p->fp);
    if (game_path(path, sizeof(path), name) != 0) {
        return;
    }
    for (i = 3; i > 0; i--) {
        snprintf(a, sizeof(a), "%s.%d", path, i);
        if (i == 1) {
            snprintf(b, sizeof(b), "%s", path);
        } else {
            snprintf(b, sizeof(b), "%s.%d", path, i - 1);
        }
        (void)remove(a);
        (void)rename(b, a);
    }
    if (bw_save_write_atomic(path, buf, size) != 0) {
        fprintf(stderr, "eidos: multiplayer: %s's save could not be written\n",
                p->name);
        return;
    }
    {
        /* the game the save belongs to */
        char gpath[520];

        snprintf(gpath, sizeof(gpath), "%s.game", path);
        (void)bw_save_write_atomic(gpath, &p->game, 1);
    }
    fprintf(stderr, "eidos: multiplayer: %s's save is kept (%u bytes)\n",
            p->name, (unsigned)size);
}

/* The host sends a joiner its save (or nothing: a new game). */
static void host_send_save(const BwMpPlayer *p)
{
    char path[512], name[48];
    u8 *buf = NULL;
    u32 size = 0;

    if (p->fp[0] != '\0') {
        snprintf(name, sizeof(name), "%s.sav", p->fp);
        if (game_path(path, sizeof(path), name) == 0) {
            buf = read_whole(path, &size);
        }
    }
    fprintf(stderr, "eidos: multiplayer: sending %s their save (%s)\n",
            p->name, size > 0 ? "their last save" : "none yet: a new game");
    send_bytes(p->peer, buf, size);
    free(buf);
}

/* The host's own game in the multiplayer save: host.sav, not one of the
 * single-player slots. */
static void host_install(void)
{
    char path[512];
    FILE *f;
    int have = 0;

    if (game_path(path, sizeof(path), "host.sav") != 0) {
        return;
    }
    if ((f = fopen(path, "rb")) != NULL) {
        have = fseek(f, 0, SEEK_END) == 0 && ftell(f) > 0;
        fclose(f);
    }
    fprintf(stderr, "eidos: multiplayer: the host plays %s\n",
            have ? "its multiplayer save" : "a new game");
    (void)bw_slots_load_file(path);
    g_mp.in_game = 1;
    g_mp.last_writes = bw_arm7_backup_writes != NULL ? bw_arm7_backup_writes() : 0;
    /* the title menu goes straight into it: Continue, or New Game */
    g_mp.started = 1;
}

/* A joiner's game starts on the save the host sent: the file is the
 * backup chip's from now on, and the game boots on it (or on a new game
 * when the host has none for this player yet). */
static void joiner_install(const u8 *buf, u32 size)
{
    char path[512];

    if (mp_path(path, sizeof(path), "session.sav") != 0) {
        return;
    }
    fprintf(stderr, "eidos: multiplayer: the host sent %s\n",
            size > 0 ? "this player's save" : "no save: a new game");
    (void)remove(path);
    if (size > 0 && bw_save_write_atomic(path, buf, size) != 0) {
        fprintf(stderr, "eidos: multiplayer: the save could not be written\n");
        return;
    }
    (void)bw_slots_load_file(path);
    g_mp.in_game = 1;
    g_mp.last_writes = bw_arm7_backup_writes != NULL ? bw_arm7_backup_writes() : 0;
    g_mp.started = 1;
}

/* A joiner's game wrote its save: the host gets it. */
static void joiner_upload(void)
{
    char path[512];
    u8 *buf;
    u32 size = 0;

    if (mp_path(path, sizeof(path), "session.sav") != 0) {
        return;
    }
    buf = read_whole(path, &size);
    if (buf != NULL) {
        send_bytes(1, buf, size);
        free(buf);
    }
}

/* One piece of a save: "save <total> <offset> <crc>\n<bytes>".  The whole
 * file goes to `done` when its last piece is in and the sum matches. */
static void on_save_piece(u32 peer, const u8 *msg, u32 size,
                          void (*done)(u32 peer, const u8 *buf, u32 size))
{
    const u8 *nl = (const u8 *)memchr(msg, '\n', size);
    unsigned total = 0, off = 0, crc = 0;
    char head[64];
    BwMpRecv *r;
    u32 n;

    if (nl == NULL || peer > BW_MP_PLAYERS_MAX) {
        return;
    }
    copy_n(head, sizeof(head), (const char *)msg, (size_t)(nl - msg));
    if (sscanf(head, "save %u %u %u", &total, &off, &crc) != 3) {
        return;
    }
    if (total == 0) {
        done(peer, NULL, 0);
        return;
    }
    r = &g_recv[peer];
    if (off == 0 || r->buf == NULL || r->total != total || r->crc != crc) {
        free(r->buf);
        r->buf = (u8 *)malloc(total);
        r->total = total;
        r->got = 0;
        r->crc = crc;
        if (r->buf == NULL) {
            return;
        }
    }
    n = size - (u32)(nl + 1 - msg);
    if (off == r->got && off + n <= total) {
        memcpy(r->buf + off, nl + 1, n);
        r->got += n;
    }
    {
        /* what has arrived, so the sender sends on (or again) */
        char ack[48];
        int a = snprintf(ack, sizeof(ack), "saveack %u %u", (unsigned)r->got,
                         total);

        (void)g_net->send(peer, ack, (u32)a);
    }
    if (r->got == total) {
        if (crc32_of(r->buf, total) == crc) {
            done(peer, r->buf, total);
        } else {
            fprintf(stderr, "eidos: multiplayer: a save arrived damaged\n");
        }
        free(r->buf);
        memset(r, 0, sizeof(*r));
    }
}

static void host_got_save(u32 peer, const u8 *buf, u32 size)
{
    BwMpPlayer *p = player(peer);

    if (p != NULL && p->peer == peer && size > 0) {
        host_store(p, buf, size);
        if (g_mp.save_pending[peer - 1]) {
            g_mp.save_pending[peer - 1] = 0;
            if (bw_mp_saves_pending() == 0) {
                fprintf(stderr, "eidos: multiplayer: every player's save is in\n");
            }
        }
    }
}

static void joiner_got_save(u32 peer, const u8 *buf, u32 size)
{
    (void)peer;
    if (!g_mp.in_game) {
        joiner_install(buf, size);
    }
}

void bw_mp_set_boot(void (*fn)(int new_game))
{
    g_boot = fn;
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
                     "no answer from %s", p->name);
            return 0;
        }
        if (p->fp[0] != '\0') {
            /* a save belongs to one game: Black or White */
            char name[48], gpath[512];
            FILE *f;
            int c = 0;

            snprintf(name, sizeof(name), "%s.sav.game", p->fp);
            if (game_path(gpath, sizeof(gpath), name) == 0 &&
                (f = fopen(gpath, "rb")) != NULL) {
                c = fgetc(f);
                fclose(f);
            }
            if ((c == 'B' || c == 'W') && c != p->game) {
                snprintf(g_mp.blocker, sizeof(g_mp.blocker),
                         "%s's save is %s", p->name,
                         c == 'W' ? "White" : "Black");
                return 0;
            }
        }
        if (!mods_match(mine, p->mods, why, sizeof(why))) {
            snprintf(g_mp.blocker, sizeof(g_mp.blocker), "%s: %s", p->name, why);
            return 0;
        }
    }
    if (!someone) {
        snprintf(g_mp.blocker, sizeof(g_mp.blocker), "nobody has joined");
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

/* The host says who it is: when a player joins, and when a player asks
 * ("who"). */
static void host_send_id(u32 peer)
{
    char msg[48];
    int n = snprintf(msg, sizeof(msg), "hostid %s", host_id());

    (void)g_net->send(peer, msg, (u32)n);
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
    fprintf(stderr, "eidos: multiplayer: player %u joined\n", (unsigned)peer);
    if (g_net->peer_name == NULL ||
        g_net->peer_name(peer, p->name, (u32)sizeof(p->name)) != 0 ||
        p->name[0] == '\0') {
        snprintf(p->name, sizeof(p->name), "player %u", (unsigned)peer);
    }
    host_send_id(peer);
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
    g_mp.save_pending[peer - 1] = 0;    /* its save cannot come now */
    fprintf(stderr, "eidos: multiplayer: %s left\n", p->name);
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
    if (size >= 8 && memcmp(msg, "saveack ", 8) == 0) {
        on_save_ack(peer, msg, size);
        return;
    }
    if (size >= 5 && memcmp(msg, "save ", 5) == 0) {
        on_save_piece(peer, (const u8 *)msg, size, host_got_save);
        return;
    }
    if (size > 4 && memcmp(msg, "evs ", 4) == 0) {
        /* a player's events, for the multiplayer mods */
        host_on_events(peer, (const u8 *)msg + 4, size - 4);
        return;
    }
    if (size > 5 && memcmp(msg, "treq ", 5) == 0) {
        char key[BW_MP_SHARED_KEY];
        s32 value;

        if (shared_parse(msg, size, key, &value) == 0) {
            host_shared_apply(key, value);
        }
        return;
    }
    copy_n(text, sizeof(text), msg, size);
    if (strcmp(text, "who") == 0) {
        host_send_id(peer);
        return;
    }
    if (strncmp(text, "name ", 5) == 0 && text[5] != '\0') {
        copy_n(p->name, sizeof(p->name), text + 5, strlen(text + 5));
        host_broadcast_roster();
        return;
    }
    if (strncmp(text, "hello ", 6) == 0 &&
        sscanf(text + 6, "%d %7s %512s", &version, game, mods) == 3) {
        char fp[40];
        int in_game = 0;

        if (sscanf(text + 6, "%*d %*7s %*512s %39s %d", fp, &in_game) == 2 &&
            strlen(fp) == 32) {
            copy_n(p->fp, sizeof(p->fp), fp, 32);
            p->in_game = in_game;
        }
        p->game = (game[0] == 'W') ? 'W' : 'B';
        if (strcmp(mods, "-") == 0) {
            mods[0] = '\0';
        }
        copy_n(p->mods, sizeof(p->mods), mods, strlen(mods));
        host_refresh_mods_ok();
        host_broadcast_roster();
        host_shared_send_all(peer);
        /* a player who comes back while the game runs is told so */
        if (g_mp.state == BW_MP_PLAYING || g_mp.state == BW_MP_GRACE) {
            if (!p->in_game && !sending_to(peer)) {
                /* a hello said again while its save is on its way does
                 * not start the save over */
                host_send_save(p);
            }
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

    if (g_mp.fp[0] == '\0') {
        /* the host has not said who it is yet, or it said so before this
         * game was listening (the join answers before the screen attaches
         * the layer): it is asked again */
        (void)g_net->send(1, "who", 3);
        return;
    }
    n = snprintf(msg, sizeof(msg), "hello %d %c %s %s %d", BW_MP_PROTO,
                 local_game(), mods[0] != '\0' ? mods : "-", g_mp.fp,
                 g_mp.in_game);
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
    if (size >= 8 && memcmp(msg, "saveack ", 8) == 0) {
        on_save_ack(peer, msg, size);
        return;
    }
    if (size >= 5 && memcmp(msg, "save ", 5) == 0) {
        on_save_piece(peer, (const u8 *)msg, size, joiner_got_save);
        return;
    }
    if (size > 7 && memcmp(msg, "hostid ", 7) == 0) {
        char hid[40], secret[33];

        copy_n(hid, sizeof(hid), msg + 7, size - 7);
        copy_n(g_mp.host_id, sizeof(g_mp.host_id), hid, strlen(hid));
        joiner_secret(hid, secret);
        fingerprint(secret, g_mp.fp);
        joiner_hello();
        return;
    }
    if (size >= 7 && memcmp(msg, "roster ", 7) == 0) {
        joiner_on_roster(msg, size);
    } else if (size == 5 && memcmp(msg, "start", 5) == 0) {
        g_mp.state = BW_MP_PLAYING;
    } else if (size == 5 && memcmp(msg, "pause", 5) == 0) {
        g_mp.state = BW_MP_PAUSED;
    } else if (size > 5 && memcmp(msg, "tset ", 5) == 0) {
        char key[BW_MP_SHARED_KEY];
        s32 value;

        if (shared_parse(msg, size, key, &value) == 0) {
            (void)shared_put(key, value);
        }
    } else if (size == 7 && memcmp(msg, "savenow", 7) == 0) {
        g_mp.save_wanted = 1;
    }
}

/* ------------------------------------------------------------------ */
/* The shared table and the events (SPEC sections 16 and 17)          */
/* ------------------------------------------------------------------ */

/*
 * The shared table: numbers every player's mods see, keyed by the mod's
 * namespace and its own key ("<namespace>/<key>").  The host's copy is the
 * real one and is kept with its game (shared.txt in the game's folder).  A
 * joiner's change goes to the host as "treq <key> <value>"; the host
 * applies the changes in the order they come and sends every one to
 * everyone as "tset <key> <value>", and a joiner that says hello gets the
 * whole table that way.
 */
typedef struct BwMpShared {
    char key[BW_MP_SHARED_KEY];
    s32  value;
} BwMpShared;

static BwMpShared g_shared[BW_MP_SHARED_MAX];
static u32        g_nshared;

extern const char *bw_mod_savedata_namespace(void) BW_MP_WEAK;

static int shared_find(const char *key)
{
    u32 i;

    for (i = 0; i < g_nshared; i++) {
        if (strcmp(g_shared[i].key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int shared_put(const char *key, s32 value)
{
    int i = shared_find(key);

    if (i < 0) {
        if (g_nshared >= BW_MP_SHARED_MAX) {
            fprintf(stderr, "eidos: multiplayer: the shared table is full "
                    "(%u keys); %s is dropped\n", (unsigned)BW_MP_SHARED_MAX, key);
            return -1;
        }
        i = (int)g_nshared++;
        copy_n(g_shared[i].key, sizeof(g_shared[i].key), key, strlen(key));
    }
    g_shared[i].value = value;
    return 0;
}

/* A key a message can carry: printable, no space, not empty. */
static int shared_key_ok(const char *key)
{
    size_t i, n = strlen(key);

    if (n == 0 || n >= BW_MP_SHARED_KEY) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if ((unsigned char)key[i] <= ' ' || (unsigned char)key[i] >= 0x7F) {
            return 0;
        }
    }
    return 1;
}

static void shared_save(void)
{
    char path[512];
    char *buf;
    size_t at = 0, cap = (size_t)g_nshared * (BW_MP_SHARED_KEY + 16u) + 1u;
    u32 i;

    if (!g_mp.host || game_path(path, sizeof(path), "shared.txt") != 0) {
        return;
    }
    buf = (char *)malloc(cap);
    if (buf == NULL) {
        return;
    }
    for (i = 0; i < g_nshared; i++) {
        at += (size_t)snprintf(buf + at, cap - at, "%s\t%ld\n", g_shared[i].key,
                               (long)g_shared[i].value);
    }
    (void)bw_save_write_atomic(path, buf, (u32)at);
    free(buf);
}

static void shared_load(void)
{
    char path[512], line[160], key[BW_MP_SHARED_KEY];
    long v;
    FILE *f;

    g_nshared = 0;
    if (game_path(path, sizeof(path), "shared.txt") != 0 ||
        (f = fopen(path, "rb")) == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        if (sscanf(line, "%79s %ld", key, &v) == 2 && shared_key_ok(key)) {
            (void)shared_put(key, (s32)v);
        }
    }
    fclose(f);
}

/* The host applies a change and sends it to everyone. */
static void host_shared_apply(const char *key, s32 value)
{
    char msg[BW_MP_SHARED_KEY + 32];
    int n;

    if (shared_put(key, value) != 0) {
        return;
    }
    shared_save();
    n = snprintf(msg, sizeof(msg), "tset %s %ld", key, (long)value);
    (void)g_net->send(0, msg, (u32)n);
}

/* The whole table, to a player that said hello. */
static void host_shared_send_all(u32 peer)
{
    char msg[BW_MP_SHARED_KEY + 32];
    u32 i;

    for (i = 0; i < g_nshared; i++) {
        int n = snprintf(msg, sizeof(msg), "tset %s %ld", g_shared[i].key,
                         (long)g_shared[i].value);

        (void)g_net->send(peer, msg, (u32)n);
    }
}

/* "treq <key> <value>" (to the host) and "tset <key> <value>" (from it). */
static int shared_parse(const char *msg, u32 size, char *key, s32 *value)
{
    char text[BW_MP_SHARED_KEY + 32];
    long v;

    copy_n(text, sizeof(text), msg, size);
    if (sscanf(text + 5, "%79s %ld", key, &v) != 2 || !shared_key_ok(key)) {
        return -1;
    }
    *value = (s32)v;
    return 0;
}

static int shared_active(void)
{
    return g_mp.state != BW_MP_OFF && g_mp.state != BW_MP_ENDED;
}

static int shared_full_key(const char *ns, const char *key, char *out, u32 size)
{
    if (key == NULL || snprintf(out, size, "%s/%s", ns != NULL ? ns : "",
                                key) >= (int)size) {
        return -1;
    }
    return shared_key_ok(out) ? 0 : -1;
}

int bw_mp_shared_get(const char *ns, const char *key, s32 *out)
{
    char k[BW_MP_SHARED_KEY];
    int i;

    if (out != NULL) {
        *out = 0;
    }
    if (!shared_active() || shared_full_key(ns, key, k, sizeof(k)) != 0) {
        return -1;
    }
    i = shared_find(k);
    if (i < 0) {
        return 1;
    }
    if (out != NULL) {
        *out = g_shared[i].value;
    }
    return 0;
}

int bw_mp_shared_set(const char *ns, const char *key, s32 value)
{
    char k[BW_MP_SHARED_KEY], msg[BW_MP_SHARED_KEY + 32];
    int n;

    if (!shared_active() || shared_full_key(ns, key, k, sizeof(k)) != 0) {
        return -1;
    }
    if (g_mp.host) {
        host_shared_apply(k, value);
        return 0;
    }
    n = snprintf(msg, sizeof(msg), "treq %s %ld", k, (long)value);
    return g_net->send(1, msg, (u32)n) == 0 ? 0 : -1;
}

int bw_mp_shared_get_calling(const char *key, s32 *out)
{
    return bw_mp_shared_get(bw_mod_savedata_namespace != NULL
                            ? bw_mod_savedata_namespace() : "", key, out);
}

int bw_mp_shared_set_calling(const char *key, s32 value)
{
    return bw_mp_shared_set(bw_mod_savedata_namespace != NULL
                            ? bw_mod_savedata_namespace() : "", key, value);
}

/*
 * The events: every event a joiner's game fires goes to the host ("ev "
 * and the payload's bytes), where the multiplayer mods' handlers run over
 * it (sdk/mod_event.h, bw_mod_event_remote).
 */
/*
 * The relay takes about 60 messages a second from a player (metaxy
 * SPEC section 6) and drops the rest without a word, and some events fire
 * every frame.  So the events go out together: they wait in one message
 * ("evs " and the payloads one after the other, each starting with its own
 * size) that leaves every BW_MP_EVENTS_EVERY_MS, or when it is full.
 */
#define BW_MP_EVENTS_EVERY_MS 100u
#define BW_MP_EVENTS_BYTES    3800u

static u8  g_evs[BW_MP_EVENTS_BYTES];
static u32 g_evs_len;
static u64 g_evs_sent_ms;

static void events_flush(void)
{
    if (g_evs_len > 4u) {
        (void)g_net->send(1, g_evs, g_evs_len);
    }
    g_evs_len = 0;
    g_evs_sent_ms = g_mp.now_ms;
}

static void forward_event(const void *payload, u32 size)
{
    if (g_mp.host || !g_mp.in_game ||
        (g_mp.state != BW_MP_PLAYING && g_mp.state != BW_MP_GRACE) ||
        size < 8u || size > BW_MP_EVENTS_BYTES - 4u) {
        return;
    }
    if (g_evs_len + size > BW_MP_EVENTS_BYTES) {
        events_flush();
    }
    if (g_evs_len == 0) {
        memcpy(g_evs, "evs ", 4);
        g_evs_len = 4;
    }
    memcpy(g_evs + g_evs_len, payload, size);
    g_evs_len += size;
}

/* The events that waited, at most every BW_MP_EVENTS_EVERY_MS. */
static void events_frame(void)
{
    if (g_evs_len > 0 && g_mp.now_ms >= g_evs_sent_ms + BW_MP_EVENTS_EVERY_MS) {
        events_flush();
    }
}

/* The host: the payloads of one "evs " message, each to the mods. */
static void host_on_events(u32 peer, const u8 *buf, u32 size)
{
    u32 at = 0;

    while (bw_mod_event_remote != NULL && at + 8u <= size) {
        u32 n;

        memcpy(&n, buf + at, sizeof(n));          /* the payload's own size */
        if (n < 8u || at + n > size) {
            break;
        }
        (void)bw_mod_event_remote(peer, buf + at, n);
        at += n;
    }
}

/* ------------------------------------------------------------------ */
/* Positions (SPEC section 16)                                         */
/* ------------------------------------------------------------------ */

/*
 * Every player appears on the others' maps.  Each game sends its player's
 * place to everyone else ("pos <zone> <x> <y> <z> <dir> <mode> <sprite>")
 * when it changes, and once in a while when it does not.  Each game shows
 * the players in its own zone as field avatars (sdk/mod_avatar.h).  A map
 * change, a battle or a menu that leaves the field empties the game's
 * entity pool, so the avatars are made again when the field comes back.
 */
typedef struct BwMpPos {
    int  have;
    u16  zone;
    s16  x, y, z;
    u8   dir, mode;
    u16  sprite;
    u32  avatar;                /* 0: not shown here */
} BwMpPos;

static BwMpPos g_pos[BW_MP_PLAYERS_MAX];        /* by peer - 1 */

static struct {
    int   have;
    u16   zone;
    s16   x, y, z;
    u8    dir, mode;
    u16   sprite;
    u64   sent_ms;
    int   changed;              /* moved since the place last went out */
    void *field;                /* the field the avatars were made in */
} g_me;

/* How often an unchanged place is sent again, and how soon a changed one
 * may go after the last (the relay takes about 60 messages a second). */
#define BW_MP_POS_EVERY_MS 2000u
#define BW_MP_POS_MIN_MS   100u

/* The avatar sprites: the hero and the heroine (FIELD_PLAYER_OBJ_CODE_*). */
#define BW_MP_SPRITE_HERO    1u
#define BW_MP_SPRITE_HEROINE 4u

typedef struct BwMpVec { s32 x, y, z; } BwMpVec;

extern void *FIELDMAP_GetFieldPlayer(void *fieldMap) BW_MP_WEAK;
extern u16   FIELDMAP_GetZoneId(void *fieldMap) BW_MP_WEAK;
extern void  FIELD_PLAYER_GetPos(void *player, BwMpVec *out) BW_MP_WEAK;
extern u16   FIELD_PLAYER_GetDirDisp(void *player) BW_MP_WEAK;
extern u32   FIELD_PLAYER_GetSex(void *player) BW_MP_WEAK;
extern u32   FIELD_PLAYER_GetFormCode(void *player) BW_MP_WEAK;
extern u32   FIELD_PLAYER_GetRideEffect(void *player) BW_MP_WEAK;

extern u32  bw_mod_avatar_create(u32 sprite, s16 x, s16 z, u8 dir) BW_MP_WEAK;
extern void bw_mod_avatar_move(u32 avatar, s16 x, s16 y, s16 z, u8 dir, u8 mode) BW_MP_WEAK;
extern void bw_mod_avatar_delete(u32 avatar) BW_MP_WEAK;
extern void bw_mod_avatar_reset(void) BW_MP_WEAK;
extern void bw_mod_avatar_set_game_data(void *game_data) BW_MP_WEAK;

static void *field_running(void);

static s16 cell_of(s32 v)
{
    return (s16)((v >> 4) / 4096);
}

/* The avatars go; the places stay, so they are shown again. */
static void pos_hide_all(void)
{
    u32 i;

    for (i = 0; i < BW_MP_PLAYERS_MAX; i++) {
        g_pos[i].avatar = 0;
    }
    if (bw_mod_avatar_reset != NULL) {
        bw_mod_avatar_reset();
    }
}

/* A player is gone: its avatar goes with it. */
static void pos_forget(u32 peer)
{
    BwMpPos *p;

    if (peer < 1u || peer > BW_MP_PLAYERS_MAX) {
        return;
    }
    p = &g_pos[peer - 1];
    if (p->avatar != 0 && bw_mod_avatar_delete != NULL) {
        bw_mod_avatar_delete(p->avatar);
    }
    memset(p, 0, sizeof(*p));
}

static void pos_clear(void)
{
    u32 i;

    for (i = 1; i <= BW_MP_PLAYERS_MAX; i++) {
        pos_forget(i);
    }
    memset(&g_me, 0, sizeof(g_me));
}

/* Another player's place: "pos <zone> <x> <y> <z> <dir> <mode> <sprite>". */
static void on_pos(u32 peer, const char *msg, u32 size)
{
    char text[96];
    unsigned zone, dir, mode, sprite;
    int x, y, z;
    BwMpPos *p;

    if (peer < 1u || peer > BW_MP_PLAYERS_MAX) {
        return;
    }
    copy_n(text, sizeof(text), msg, size);
    if (sscanf(text + 4, "%u %d %d %d %u %u %u", &zone, &x, &y, &z, &dir,
               &mode, &sprite) != 7) {
        return;
    }
    p = &g_pos[peer - 1];
    if (p->have && p->zone != zone && p->avatar != 0 &&
        bw_mod_avatar_delete != NULL) {
        bw_mod_avatar_delete(p->avatar);     /* it left this zone */
        p->avatar = 0;
    }
    p->have = 1;
    p->zone = (u16)zone;
    p->x = (s16)x;
    p->y = (s16)y;
    p->z = (s16)z;
    p->dir = (u8)dir;
    p->mode = (u8)mode;
    p->sprite = (u16)sprite;
}

/* This player's own place, read from the field; 0 when there is none. */
static int read_me(void *fm)
{
    void *pl;
    BwMpVec v;
    u32 form;

    if (fm == NULL || FIELDMAP_GetFieldPlayer == NULL ||
        FIELDMAP_GetZoneId == NULL || FIELD_PLAYER_GetPos == NULL) {
        return 0;
    }
    pl = FIELDMAP_GetFieldPlayer(fm);
    if (pl == NULL) {
        return 0;
    }
    memset(&v, 0, sizeof(v));
    FIELD_PLAYER_GetPos(pl, &v);
    g_me.zone = FIELDMAP_GetZoneId(fm);
    g_me.x = cell_of(v.x);
    g_me.y = cell_of(v.y);
    g_me.z = cell_of(v.z);
    g_me.dir = FIELD_PLAYER_GetDirDisp != NULL ? (u8)FIELD_PLAYER_GetDirDisp(pl) : 0;
    form = FIELD_PLAYER_GetFormCode != NULL ? FIELD_PLAYER_GetFormCode(pl) : 0;
    g_me.mode = (form == 2u || form == 3u) ? 3 :           /* surfing, diving */
                (FIELD_PLAYER_GetRideEffect != NULL &&
                 FIELD_PLAYER_GetRideEffect(pl) != 0) ? 2 : 0;
    g_me.sprite = (u16)(FIELD_PLAYER_GetSex != NULL && FIELD_PLAYER_GetSex(pl) != 0
                        ? BW_MP_SPRITE_HEROINE : BW_MP_SPRITE_HERO);
    return 1;
}

/* Once a frame while a multiplayer game runs: this player's place goes
 * out, and the others in this zone are shown. */
static void pos_frame(void)
{
    void *fm = field_running();
    u16 zone = g_me.zone;
    s16 x = g_me.x, y = g_me.y, z = g_me.z;
    u8 dir = g_me.dir, mode = g_me.mode;
    u32 i;

    if (fm != g_me.field) {
        /* the field came, went or was made again: its pool is new */
        pos_hide_all();
        g_me.field = fm;
    }
    if (fm == NULL || !read_me(fm)) {
        return;
    }
    if (g_me.have && zone != g_me.zone) {
        pos_hide_all();              /* another zone: other people */
    }
    if (!g_me.have || zone != g_me.zone || x != g_me.x || y != g_me.y ||
        z != g_me.z || dir != g_me.dir || mode != g_me.mode) {
        g_me.changed = 1;
    }
    if (!g_me.have ||
        (g_me.changed && g_mp.now_ms >= g_me.sent_ms + BW_MP_POS_MIN_MS) ||
        g_mp.now_ms >= g_me.sent_ms + BW_MP_POS_EVERY_MS) {
        char msg[96];
        int n;

        n = snprintf(msg, sizeof(msg), "pos %u %d %d %d %u %u %u",
                     (unsigned)g_me.zone, g_me.x, g_me.y, g_me.z,
                     (unsigned)g_me.dir, (unsigned)g_me.mode,
                     (unsigned)g_me.sprite);
        (void)g_net->send(0, msg, (u32)n);
        g_me.sent_ms = g_mp.now_ms;
        g_me.have = 1;
        g_me.changed = 0;
    }
    for (i = 0; i < BW_MP_PLAYERS_MAX; i++) {
        BwMpPos *p = &g_pos[i];

        if (!p->have || p->zone != g_me.zone) {
            continue;
        }
        if (p->avatar == 0 && bw_mod_avatar_create != NULL) {
            p->avatar = bw_mod_avatar_create(p->sprite, p->x, p->z, p->dir);
            fprintf(stderr, "eidos: multiplayer: player %u shown here (avatar %u)\n", (unsigned)(i + 1), (unsigned)p->avatar);
        }
        if (p->avatar != 0 && bw_mod_avatar_move != NULL) {
            bw_mod_avatar_move(p->avatar, p->x, p->y, p->z, p->dir, p->mode);
        }
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
        pos_clear();
        return;
    }
    if (kind == (int)BW_SESSION_LEAVE) {
        pos_forget(peer);
    }
    if (kind == (int)BW_SESSION_MESSAGE && size > 4 && memcmp(buf, "pos ", 4) == 0) {
        /* a player's place, from any player */
        on_pos(peer, (const char *)buf, size);
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
    sends_clear();
    pos_clear();
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
    /* the host's table is its game's; a joiner gets the host's */
    if (host) {
        shared_load();
    } else {
        g_nshared = 0;
    }
    if (bw_mod_event_set_forward != NULL) {
        bw_mod_event_set_forward(forward_event);
    }
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
    if (g_mp.in_game) {
        /* the player's own save is the game's again, loaded in place */
        char own[512];

        if (bw_slots_path(bw_slots_current(), own, sizeof(own)) == 0) {
            (void)bw_slots_load_file(own);
        }
    }
    g_mp.in_game = 0;
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

/* For the title menu: a multiplayer game runs on its own file. */
int bw_mp_take_started_peek(void)
{
    return g_mp.in_game && g_mp.state != BW_MP_OFF && g_mp.state != BW_MP_ENDED;
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

int bw_mp_hide_save(void)
{
    return !g_mp.host && g_mp.in_game && g_mp.state != BW_MP_OFF;
}

/* The game data when the field runs and no field event does (the game's
 * own save runs inside the save menu's event, so nothing else runs then
 * either), else NULL. */
static void *field_quiet_game_data(void)
{
    void *gs;

    if (&bw_dsref__GameSystemGlobal == NULL || bw_dsref__GameSystemGlobal == NULL ||
        GAMESYSTEM_GetGameData == NULL ||
        GAMESYSTEM_GetEvent == NULL || GAMESYSTEM_GetFieldMap == NULL) {
        return NULL;
    }
    gs = *(void **)bw_dsref__GameSystemGlobal;
    if (gs == NULL || GAMESYSTEM_GetFieldMap(gs) == NULL ||
        GAMESYSTEM_GetEvent(gs) != NULL) {
        return NULL;
    }
    return GAMESYSTEM_GetGameData(gs);
}

/* The field, when it runs and no field event does (a warp, a menu, a
 * script, a battle's start); NULL otherwise.  The avatars' game data is
 * bound here too. */
static void *field_running(void)
{
    void *gd = field_quiet_game_data();
    void *gs;

    if (gd == NULL) {
        return NULL;
    }
    if (bw_mod_avatar_set_game_data != NULL) {
        bw_mod_avatar_set_game_data(gd);
    }
    gs = *(void **)bw_dsref__GameSystemGlobal;
    return GAMESYSTEM_GetFieldMap(gs);
}

/* A joiner's game saves at the first quiet moment after the host saved. */
static void field_quiet_save(void)
{
    void *gd;

    if (!g_mp.save_wanted || g_mp.saving ||
        (g_mp.host && !g_mp.save_test) || GAME_DATA_SaveStart == NULL) {
        return;
    }
    gd = field_quiet_game_data();
    if (gd == NULL) {
        return;
    }
    fprintf(stderr, "eidos: multiplayer: saving this game (the host saved)\n");
    g_mp.save_wanted = 0;
    g_mp.saving = 1;
    g_mp.save_gd = gd;
    (void)GAME_DATA_SaveStart(gd);
}

void bw_mp_request_save_for_test(void)
{
    g_mp.save_wanted = 1;
    g_mp.save_test = 1;
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
    for (i = 1; i < BW_MP_PLAYERS_MAX; i++) {
        BwMpPlayer *p = &g_mp.players[i];

        if (p->peer != 0 && p->on_roster && p->connected && !p->in_game) {
            host_send_save(p);
            p->in_game = 1;
        }
    }
    host_send_word("start");
    if (!g_mp.in_game) {
        host_install();
    }
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

int bw_mp_saves_pending(void)
{
    int i, n = 0;

    for (i = 0; i < BW_MP_PLAYERS_MAX; i++) {
        n += g_mp.save_pending[i] != 0;
    }
    return g_mp.host ? n : 0;
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

/* The host saved: every player's game saves now. */
void bw_mp_host_saved(void)
{
    u32 i;

    if (!g_mp.host) {
        return;
    }
    for (i = 1; i < BW_MP_PLAYERS_MAX; i++) {
        const BwMpPlayer *p = &g_mp.players[i];

        g_mp.save_pending[i] = p->peer != 0 && p->connected &&
                               (p->on_roster || p->in_game);
    }
    fprintf(stderr, "eidos: multiplayer: the host saved; every "
            "player's game saves too (%d to come)\n", bw_mp_saves_pending());
    host_send_word("savenow");
}

void bw_mp_frame_at(u64 t)
{
    g_mp.now_ms = t;
    sends_pump();
    if (g_mp.in_game && !g_mp.saving && bw_arm7_backup_writes != NULL &&
        bw_arm7_backup_writes() != g_mp.last_writes &&
        (bw_arm7_backup_dirty == NULL || !bw_arm7_backup_dirty())) {
        g_mp.last_writes = bw_arm7_backup_writes();
        if (g_mp.host) {
            bw_mp_host_saved();
        } else {
            fprintf(stderr, "eidos: multiplayer: this game saved; the save "
                    "goes to the host\n");
            joiner_upload();
        }
    }
    field_quiet_save();
    if (g_mp.in_game && (g_mp.state == BW_MP_PLAYING || g_mp.state == BW_MP_GRACE)) {
        pos_frame();
        events_frame();
    }
    if (g_mp.saving && GAME_DATA_SaveMain != NULL) {
        int r = GAME_DATA_SaveMain(g_mp.save_gd);

        if (r == 2 || r == 3) {
            g_mp.saving = 0;
            fprintf(stderr, "eidos: multiplayer: the save %s\n",
                    r == 2 ? "is written" : "failed");
        }
    }
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
    if (g_mp.state != BW_MP_OFF || g_mp.save_test) {
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
    sends_clear();
    memset(&g_mp, 0, sizeof(g_mp));
    g_net = &k_net;
}

void bw_mp_set_net(const BwMpNet *net)
{
    g_net = net != NULL ? net : &k_net;
}
