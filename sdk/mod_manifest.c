/*
 * sdk/mod_manifest.c -- the run-time manifest: the record, the parse and the
 * apply (docs/mod-state.md, section 3; docs/mod-interface.md, section 3).
 *
 * A mod ships a manifest, `<mod>/eidos_mod.txt`, and the port reads it twice
 * over: sdk/vfs.c reads the `name` and `order` keys to build the mod list and
 * the load order, and this file reads the whole run-time schema into one
 * record per mod, BwModManifest.  The parse fills the record; the apply turns
 * it into registrations: it checks the version the mod's code was built
 * against, derives the state namespace, checks every override against the
 * declared table and leaves the rule list and the settings path for the
 * interpreter and the settings surface.
 *
 * A manifest problem is one report and the default stands, the way sdk/vfs.c
 * reports a bad order:
 *
 *     eidos: mod <name>: <what is wrong>
 *
 * and the one report the port fixes is the declared check:
 *
 *     eidos: mod <name>: override <region>:<addr> is not a declared address
 *
 * The check is a diagnostic, not a sandbox: the port validates nothing about
 * a mod's code (docs/mod-interface.md, section 5), so a mod with an
 * undeclared override still loads and its other keys still apply.
 *
 * The loader (sdk/mod_loader.c) calls, in this order:
 *
 *     void       *bw_mod_manifest_new(const char *dir_name);
 *     int         bw_mod_manifest_parse_dir(void *manifest, const char *root,
 *                                           const char *dir_name);
 *     int         bw_mod_manifest_apply(void *manifest);
 *     u32         bw_mod_manifest_override_count(const void *manifest);
 *     const void *bw_mod_manifest_override_at(const void *manifest, u32 i);
 *
 * `bw_mod_manifest_parse` takes the manifest's text instead of a root, for a
 * caller that read the file itself.  The apply returns 0, or -1 when the mod
 * is refused (a version mismatch), and the loader then skips the mod.  The
 * apply leaves in the record the overrides whose pair is declared; the loader
 * resolves each symbol in the loaded library and calls bw_mod_slot_install
 * (sdk/mod_slot.h) for it, in load order, so a later mod wins over an
 * earlier one for one pair.
 *
 * The apply runs after the declared table is registered and after the event
 * layer is reset (docs/mod-state.md, section 3), so a later mod wins over an
 * earlier one for one pair.
 *
 * The record and its two rows live in sdk/mod_manifest.h: the loader reads
 * an override's region, address and symbol out of the record to install it,
 * so the two files have to agree on the layout.  The calls that take a
 * record take it as a void *, the way sdk/mod_loader.c declares them, so the
 * record's shape stays this file's own.
 *
 * The calls, which a header may carry too:
 *
 *     void bw_mod_manifest_init(void *manifest, const char *dir_name);
 *     void bw_mod_manifest_free(void *manifest);
 *     void *bw_mod_manifest_new(const char *dir_name);
 *     void bw_mod_manifest_delete(void *manifest);
 *     int  bw_mod_manifest_parse(void *manifest, const char *text,
 *                                const char *dir_name);
 *     int  bw_mod_manifest_parse_dir(void *manifest, const char *root,
 *                                    const char *dir_name);
 *     int  bw_mod_manifest_apply(void *manifest);
 *     u32  bw_mod_manifest_override_count(const void *manifest);
 *     const void *bw_mod_manifest_override_at(const void *manifest, u32 i);
 *     u32  bw_mod_manifest_rule_count(const void *manifest);
 *     const void *bw_mod_manifest_rule_at(const void *manifest, u32 i);
 *     const char *bw_mod_manifest_name(const void *manifest);
 *     const char *bw_mod_manifest_code(const void *manifest);
 *     const char *bw_mod_manifest_state(const void *manifest);
 *     const char *bw_mod_manifest_settings(const void *manifest);
 *     int  bw_mod_manifest_has_api(const void *manifest);
 *     int  bw_mod_manifest_namespace_valid(const char *ns);
 *     char *bw_mod_manifest_default_namespace(const char *name);
 *     void bw_mod_manifest_set_port_version(const char *version);
 *     const char *bw_mod_manifest_port_version(void);
 *     u32  bw_mod_manifest_applied_count(void);
 *     const void *bw_mod_manifest_applied_at(u32 i);
 *     void bw_mod_manifest_applied_clear(void);
 *
 * The row the loader reads is sdk/mod_manifest.h's BwModOverride, and the
 * layout is the one the loader repeats:
 *
 *     typedef struct BwModOverride {
 *         unsigned int region;       the region, BW_MOD_REGION_ARM9 or _OVL(id)
 *         unsigned int addr;         the even function start
 *         unsigned int addr_tagged;  the Thumb-tagged form, addr | 1
 *         int          mode;         0 replaces, 1 wraps
 *         char        *symbol;       the symbol the library exports
 *         char        *sig;          the stated prototype, or NULL
 *         char        *switch_name;  the switch name, or NULL
 *     } BwModOverride;
 *
 * The record is the caller's: the parse fills it, the apply reads it, and
 * bw_mod_manifest_free releases what it holds.  The applied list holds the
 * records themselves, so the caller keeps them alive until it clears the
 * list.
 */

#include "types.h"
#include "mod_api.h"
#include "mod_manifest.h"
#include "mod_slot.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The declared set at run time (docs/mod-state.md, section 4).  The build
 * emits the table beside the slot publications and sdk/mod_slot.c keeps it.
 * sdk/mod_slot.h declares the call; the declaration is repeated here, with
 * the signature the document fixes, so this file compiles whether or not the
 * header carries it.
 */
extern int bw_mod_slot_declared(unsigned int region, unsigned int addr);

/* The manifest file inside a mod's tree, packed or not. */
#define BW_MOD_MANIFEST_NAME "eidos_mod.txt"

/* The longest a state namespace may be: the same cap sdk/mod_savedata.c
 * keeps, so a namespace the manifest layer accepts is one the state layer
 * accepts too. */
#define BW_MOD_NAMESPACE_MAX 64

/* How an override takes the game's place: the modes are in sdk/mod_manifest.h
 * (BW_MOD_OVERRIDE_REPLACE and _WRAP), which the loader reads too. */

/*
 * The mark a record carries when it holds something.  The record is the
 * caller's, and the caller may hand over one it never initialised, so the
 * release path checks the mark before it frees anything: a record that does
 * not carry it is empty as far as the port is concerned.
 */
#define BW_MOD_MANIFEST_MAGIC 0x4D414E46u   /* "MANF" */

/* ------------------------------------------------------------------ */
/* The record                                                         */
/* ------------------------------------------------------------------ */

/*
 * The record and its two rows live in sdk/mod_manifest.h: the loader reads
 * an override's region, address and symbol out of the record to install it,
 * so the two files have to agree on the layout.
 */

/* ------------------------------------------------------------------ */
/* The small helpers                                                  */
/* ------------------------------------------------------------------ */

static char *dup_str(const char *s)
{
    size_t n;
    char *d;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s);
    d = (char *)malloc(n + 1);
    if (d != NULL) {
        memcpy(d, s, n + 1);
    }
    return d;
}

/* A copy of [s, s + n) as a string. */
static char *dup_n(const char *s, size_t n)
{
    char *d = (char *)malloc(n + 1);

    if (d == NULL) {
        return NULL;
    }
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

/* Trim [*s, *e) of surrounding blanks. */
static void trim(const char **s, const char **e)
{
    while (*s < *e && is_space(**s)) {
        (*s)++;
    }
    while (*e > *s && is_space((*e)[-1])) {
        (*e)--;
    }
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int to_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

/* A C identifier: the symbol a mod's library exports. */
static int is_ident(const char *s, size_t n)
{
    size_t i;

    if (n == 0) {
        return 0;
    }
    if (!is_alpha(s[0]) && s[0] != '_') {
        return 0;
    }
    for (i = 1; i < n; i++) {
        if (!is_alpha(s[i]) && !is_digit(s[i]) && s[i] != '_') {
            return 0;
        }
    }
    return 1;
}

/* A stable event name, as `battle.faint` spells it. */
static int is_event_name(const char *s, size_t n)
{
    size_t i;

    if (n == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char c = s[i];

        if (!is_alpha(c) && !is_digit(c) && c != '.' && c != '_' && c != '-') {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The reports                                                        */
/* ------------------------------------------------------------------ */

/*
 * Every manifest problem is reported in the port's one shape:
 *
 *     eidos: mod <name>: <what is wrong>
 *
 * and the default stands: the line is ignored and the mod keeps loading.
 */
static void mod_report(const BwModManifest *m, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "eidos: mod %s: ",
            (m != NULL && m->name != NULL) ? m->name : "(unnamed)");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* The region's own spelling, as a manifest writes it. */
static const char *region_token(unsigned int region, char *buf, size_t size)
{
    if (region == BW_MOD_REGION_ARM9) {
        return "arm9";
    }
    snprintf(buf, size, "ov%03u", region);
    return buf;
}

/* ------------------------------------------------------------------ */
/* The record's life                                                  */
/* ------------------------------------------------------------------ */

/*
 * Set the defaults: the name is the directory name, the order is 0, and
 * every other key is absent.  A name that cannot be copied leaves the record
 * without one, and the reports fall back to "(unnamed)".
 *
 * The record is the caller's, and the caller may hand over one it never
 * filled, so the release path checks the mark before it frees anything.
 */
void bw_mod_manifest_free(void *manifest);   /* below */

void bw_mod_manifest_init(void *manifest, const char *dir_name)
{
    BwModManifest *m = (BwModManifest *)manifest;

    if (m == NULL) {
        return;
    }
    bw_mod_manifest_free(m);
    m->magic = BW_MOD_MANIFEST_MAGIC;
    m->name = dup_str(dir_name);
}

/* Release everything the record holds and leave it empty. */
void bw_mod_manifest_free(void *manifest)
{
    BwModManifest *m = (BwModManifest *)manifest;
    u32 i;

    if (m == NULL) {
        return;
    }
    if (m->magic == BW_MOD_MANIFEST_MAGIC) {
        for (i = 0; i < m->override_count; i++) {
            free(m->overrides[i].symbol);
            free(m->overrides[i].sig);
            free(m->overrides[i].switch_name);
        }
        for (i = 0; i < m->rule_count; i++) {
            free(m->rules[i].event);
            free(m->rules[i].path);
        }
        free(m->name);
        free(m->port);
        free(m->code);
        free(m->state);
        free(m->settings);
        free(m->overrides);
        free(m->rules);
    }
    memset(m, 0, sizeof(*m));
}

/* A record of the caller's own, on the heap.  NULL when there is no memory. */
void *bw_mod_manifest_new(const char *dir_name)
{
    BwModManifest *m = (BwModManifest *)malloc(sizeof(*m));

    if (m == NULL) {
        return NULL;
    }
    memset(m, 0, sizeof(*m));
    bw_mod_manifest_init(m, dir_name);
    return m;
}

/* Release a record from bw_mod_manifest_new. */
void bw_mod_manifest_delete(void *manifest)
{
    BwModManifest *m = (BwModManifest *)manifest;

    if (m == NULL) {
        return;
    }
    bw_mod_manifest_free(m);
    free(m);
}

/* Replace a string the record holds.  0, or -1 (no memory). */
static int set_str_n(char **slot, const char *value, size_t len)
{
    char *d = dup_n(value, len);

    if (d == NULL) {
        return -1;
    }
    free(*slot);
    *slot = d;
    return 0;
}

/* ------------------------------------------------------------------ */
/* The namespace                                                      */
/* ------------------------------------------------------------------ */

/*
 * The reserved device names of the host file system: a folder of that name
 * is a device, not a folder, so a namespace may not be one.
 */
static const char *const g_reserved_names[] = {
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
};

/* The characters a namespace may hold. */
static int is_namespace_char(char c)
{
    return is_alpha(c) || is_digit(c) || c == '-' || c == '_' || c == '.';
}

/*
 * Is `ns` one path component?  The state path is
 * `<save dir>/mods/<namespace>/<save>.bin` and bw_save_path copies the name
 * verbatim (sdk/save.c), so a namespace that carries a separator or a ".."
 * would leave the `mods/` folder.  A namespace must be:
 *
 *   - not empty, and at most BW_MOD_NAMESPACE_MAX characters;
 *   - no separator ('/', '\') and no drive separator (':');
 *   - not the "." or the ".." component;
 *   - not a reserved device name, with or without an extension;
 *   - only letters, digits, '-', '_' and '.'.
 */
int bw_mod_manifest_namespace_valid(const char *ns)
{
    char stem[8];
    size_t n;
    size_t i;
    size_t stem_len;
    u32 r;

    if (ns == NULL) {
        return 0;
    }
    n = strlen(ns);
    if (n == 0 || n > BW_MOD_NAMESPACE_MAX) {
        return 0;
    }
    if (strcmp(ns, ".") == 0 || strcmp(ns, "..") == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char c = ns[i];

        if (c == '/' || c == '\\' || c == ':') {
            return 0;
        }
        if (!is_namespace_char(c)) {
            return 0;
        }
    }
    /* The device name is the part before the first dot: "CON.txt" is CON. */
    stem_len = 0;
    while (stem_len < n && ns[stem_len] != '.' && stem_len < sizeof(stem) - 1) {
        stem[stem_len] = ns[stem_len];
        stem_len++;
    }
    stem[stem_len] = '\0';
    for (r = 0; r < sizeof(g_reserved_names) / sizeof(g_reserved_names[0]); r++) {
        const char *name = g_reserved_names[r];
        size_t k;

        if (strlen(name) != stem_len) {
            continue;
        }
        for (k = 0; k < stem_len; k++) {
            if (to_lower((unsigned char)stem[k]) != to_lower((unsigned char)name[k])) {
                break;
            }
        }
        if (k == stem_len) {
            return 0;
        }
    }
    return 1;
}

/*
 * The default namespace, derived from the mod's name: lower case, the first
 * word, a dash, and the rest of the name joined.  The example of
 * docs/mod-interface.md, section 3, is the rule: the name "Nuzlocke Soul
 * Link" gives the namespace "nuzlocke-soullink".
 *
 * Every character that is not a letter or a digit becomes a dash, and a run
 * of them becomes one dash, so the result is one path component by
 * construction and needs no check.
 */
char *bw_mod_manifest_default_namespace(const char *name)
{
    char word[128];
    char *out;
    size_t n;
    size_t i;
    size_t o;
    u32 words;

    if (name == NULL) {
        name = "";
    }
    n = strlen(name);
    out = (char *)malloc(BW_MOD_NAMESPACE_MAX + 1);
    if (out == NULL) {
        return NULL;
    }
    o = 0;
    words = 0;
    for (i = 0; i < n; ) {
        size_t j;
        size_t w = 0;

        while (i < n && is_space(name[i])) {
            i++;
        }
        if (i >= n) {
            break;
        }
        j = i;
        while (j < n && !is_space(name[j])) {
            j++;
        }
        while (i < j) {
            int c = to_lower((unsigned char)name[i]);

            if (!is_alpha((char)c) && !is_digit((char)c) && c != '-' && c != '_') {
                c = '-';
            }
            if (c == '-' && w > 0 && word[w - 1] == '-') {
                i++;
                continue;
            }
            if (w < sizeof(word) - 1) {
                word[w++] = (char)c;
            }
            i++;
        }
        while (w > 0 && word[w - 1] == '-') {
            w--;
        }
        if (w == 0) {
            continue;
        }
        /* The first word, a dash, and the rest of the name joined. */
        if (words == 1 && o < BW_MOD_NAMESPACE_MAX) {
            out[o++] = '-';
        }
        if (w > BW_MOD_NAMESPACE_MAX - o) {
            w = BW_MOD_NAMESPACE_MAX - o;
        }
        memcpy(out + o, word, w);
        o += w;
        words++;
    }
    if (o == 0) {
        out[o++] = 'm';
        out[o++] = 'o';
        out[o++] = 'd';
    }
    out[o] = '\0';
    return out;
}

/* ------------------------------------------------------------------ */
/* The port version                                                   */
/* ------------------------------------------------------------------ */

/*
 * The port version a mod with raw hooks is pinned to (docs/mod-overrides.md,
 * "Version pinning").  The port sets it at start-up; until it does, the pin
 * cannot be checked and the manifest says so once.
 */
static const char *g_port_version;

void bw_mod_manifest_set_port_version(const char *version)
{
    g_port_version = version;
}

const char *bw_mod_manifest_port_version(void)
{
    return g_port_version;
}

/* ------------------------------------------------------------------ */
/* The parse                                                          */
/* ------------------------------------------------------------------ */

/* The region of a manifest's `<region>:<address>`: "arm9" or "ovNNN". */
static int parse_region(const char *s, size_t n, unsigned int *out)
{
    if (n == 4 && memcmp(s, "arm9", 4) == 0) {
        *out = BW_MOD_REGION_ARM9;
        return 0;
    }
    if (n > 2 && s[0] == 'o' && s[1] == 'v') {
        u32 v = 0;
        size_t i;

        for (i = 2; i < n; i++) {
            if (!is_digit(s[i])) {
                return -1;
            }
            v = v * 10u + (u32)(s[i] - '0');
            if (v > 0xFFFFu) {
                return -1;
            }
        }
        *out = BW_MOD_REGION_OVL(v);
        return 0;
    }
    return -1;
}

/* The address of a manifest's `<region>:<address>`, in hex. */
static int parse_addr(const char *s, size_t n, unsigned int *out)
{
    u64 v = 0;
    size_t i = 0;

    if (n > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        i = 2;
    }
    if (i >= n) {
        return -1;
    }
    for (; i < n; i++) {
        char c = s[i];
        u32 d;

        if (c >= '0' && c <= '9') {
            d = (u32)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = (u32)(c - 'a') + 10u;
        } else if (c >= 'A' && c <= 'F') {
            d = (u32)(c - 'A') + 10u;
        } else {
            return -1;
        }
        v = v * 16u + (u64)d;
        if (v > 0xFFFFFFFFull) {
            return -1;
        }
    }
    *out = (unsigned int)v;
    return 0;
}

/*
 * Split a value into its tokens.  A token is a run of blanks, except that a
 * quoted part may hold blanks: `sig="int f(int)"` is one token.  At most
 * `max` tokens are taken; the count is returned.
 */
static int tokenize(const char *s, const char *e,
                    const char **tok, size_t *len, int max)
{
    int n = 0;

    while (s < e && n < max) {
        const char *start;

        while (s < e && is_space(*s)) {
            s++;
        }
        if (s >= e) {
            break;
        }
        start = s;
        while (s < e) {
            if (*s == '"') {
                s++;
                while (s < e && *s != '"') {
                    s++;
                }
                if (s < e) {
                    s++;
                }
                continue;
            }
            if (is_space(*s)) {
                break;
            }
            s++;
        }
        tok[n] = start;
        len[n] = (size_t)(s - start);
        n++;
    }
    return n;
}

/* Is the token the word `word`?  The length comes from the word, so a
 * spelling cannot drift. */
static int token_is(const char *tok, size_t len, const char *word)
{
    size_t n = strlen(word);

    return len == n && memcmp(tok, word, n) == 0;
}

/* The value of a `key=value` token, without the quotes around it. */
static void option_value(const char *tok, size_t len, const char **value, size_t *value_len)
{
    const char *eq = memchr(tok, '=', len);
    const char *v;
    size_t n;

    *value = "";
    *value_len = 0;
    if (eq == NULL) {
        return;
    }
    v = eq + 1;
    n = len - (size_t)(v - tok);
    if (n >= 2 && v[0] == '"' && v[n - 1] == '"') {
        v++;
        n -= 2;
    }
    *value = v;
    *value_len = n;
}

/* Append one override to the record.  0, or -1 (no memory). */
static int push_override(BwModManifest *m, const BwModOverride *o)
{
    if (m->override_count == m->override_cap) {
        u32 cap = m->override_cap == 0 ? 4u : m->override_cap * 2u;
        BwModOverride *grown =
            (BwModOverride *)realloc(m->overrides, (size_t)cap * sizeof(*grown));

        if (grown == NULL) {
            return -1;
        }
        m->overrides = grown;
        m->override_cap = cap;
    }
    m->overrides[m->override_count++] = *o;
    return 0;
}

/* Append one rule to the record.  0, or -1 (no memory). */
static int push_rule(BwModManifest *m, const BwModRule *r)
{
    if (m->rule_count == m->rule_cap) {
        u32 cap = m->rule_cap == 0 ? 4u : m->rule_cap * 2u;
        BwModRule *grown = (BwModRule *)realloc(m->rules, (size_t)cap * sizeof(*grown));

        if (grown == NULL) {
            return -1;
        }
        m->rules = grown;
        m->rule_cap = cap;
    }
    m->rules[m->rule_count++] = *r;
    return 0;
}

/*
 * Parse one `override` value:
 *
 *     <region>:<address> <mode> <symbol> [sig="<prototype>"] [switch=<name>]
 *
 * A bare address is refused: the region is part of the key, because two
 * overlays share one RAM window (docs/mod-interface.md, section 1).
 */
static int parse_override(BwModManifest *m, const char *value, size_t value_len)
{
    const char *tok[6];
    size_t len[6];
    const char *colon;
    size_t region_len;
    const char *addr_text;
    size_t addr_len;
    unsigned int region;
    unsigned int addr;
    BwModOverride o;
    const char *opt_value;
    size_t opt_len;
    int ntok;
    int i;

    memset(&o, 0, sizeof(o));
    ntok = tokenize(value, value + value_len, tok, len, 6);
    if (ntok < 3) {
        return -1;
    }
    colon = memchr(tok[0], ':', len[0]);
    if (colon == NULL) {
        return -1;
    }
    region_len = (size_t)(colon - tok[0]);
    addr_text = colon + 1;
    addr_len = len[0] - region_len - 1;
    if (parse_region(tok[0], region_len, &region) != 0) {
        return -1;
    }
    if (parse_addr(addr_text, addr_len, &addr) != 0) {
        return -1;
    }
    if (token_is(tok[1], len[1], "replace")) {
        o.mode = BW_MOD_OVERRIDE_REPLACE;
    } else if (token_is(tok[1], len[1], "wrap")) {
        o.mode = BW_MOD_OVERRIDE_WRAP;
    } else {
        return -1;
    }
    if (!is_ident(tok[2], len[2])) {
        return -1;
    }
    o.region = region;
    /* The low bit is masked: the even start and the Thumb-tagged value a
     * code pointer holds are the same function. */
    o.addr = addr & ~1u;
    o.addr_tagged = o.addr | 1u;
    o.symbol = dup_n(tok[2], len[2]);
    if (o.symbol == NULL) {
        return -1;
    }
    for (i = 3; i < ntok; i++) {
        const char *key = tok[i];
        size_t key_len = len[i];

        option_value(key, key_len, &opt_value, &opt_len);
        if (key_len > 4 && memcmp(key, "sig=", 4) == 0) {
            if (o.sig != NULL) {
                free(o.symbol);
                free(o.sig);
                return -1;
            }
            o.sig = dup_n(opt_value, opt_len);
            if (o.sig == NULL) {
                free(o.symbol);
                return -1;
            }
        } else if (key_len > 7 && memcmp(key, "switch=", 7) == 0) {
            if (o.switch_name != NULL || opt_len == 0) {
                free(o.symbol);
                free(o.sig);
                return -1;
            }
            o.switch_name = dup_n(opt_value, opt_len);
            if (o.switch_name == NULL) {
                free(o.symbol);
                free(o.sig);
                return -1;
            }
        } else {
            free(o.symbol);
            free(o.sig);
            free(o.switch_name);
            return -1;
        }
    }
    if (push_override(m, &o) != 0) {
        free(o.symbol);
        free(o.sig);
        free(o.switch_name);
        return -1;
    }
    return 0;
}

/* Parse one `rule` value: `<event> <path>`. */
static int parse_rule(BwModManifest *m, const char *value, size_t value_len)
{
    const char *tok[4];
    size_t len[4];
    BwModRule r;
    int ntok;

    memset(&r, 0, sizeof(r));
    ntok = tokenize(value, value + value_len, tok, len, 4);
    if (ntok != 2) {
        return -1;
    }
    if (!is_event_name(tok[0], len[0]) || len[1] == 0) {
        return -1;
    }
    r.event = dup_n(tok[0], len[0]);
    r.path = dup_n(tok[1], len[1]);
    if (r.event == NULL || r.path == NULL) {
        free(r.event);
        free(r.path);
        return -1;
    }
    if (push_rule(m, &r) != 0) {
        free(r.event);
        free(r.path);
        return -1;
    }
    return 0;
}

/* A signed integer, whole text only. */
static int parse_int(const char *s, size_t n, long *out)
{
    long v = 0;
    size_t i = 0;
    int neg = 0;

    if (n == 0) {
        return -1;
    }
    if (s[0] == '-' || s[0] == '+') {
        neg = s[0] == '-';
        i = 1;
    }
    if (i >= n) {
        return -1;
    }
    for (; i < n; i++) {
        if (!is_digit(s[i])) {
            return -1;
        }
        v = v * 10 + (s[i] - '0');
        if (v > 2000000000L) {
            return -1;
        }
    }
    *out = neg ? -v : v;
    return 0;
}

/*
 * Parse the manifest's text into the record.
 *
 * `#` starts a comment, each line is `key = value`, a line with no `=` is
 * skipped, and an unknown key is ignored on purpose, so a later version can
 * add keys (sdk/vfs.c).  A malformed line is reported once and the default
 * stands.
 */
int bw_mod_manifest_parse(void *manifest, const char *text, const char *dir_name)
{
    BwModManifest *m = (BwModManifest *)manifest;
    const char *p;

    if (m == NULL) {
        return -1;
    }
    bw_mod_manifest_init(m, dir_name);
    if (text == NULL) {
        return 0;
    }
    p = text;
    while (*p != '\0') {
        char line[1024];
        const char *e = p;
        const char *ks;
        const char *ke;
        const char *vs;
        const char *ve;
        char *hash;
        char *eq;
        size_t n;
        size_t key_len;

        while (*e != '\0' && *e != '\n') {
            e++;
        }
        n = (size_t)(e - p);
        if (n >= sizeof(line)) {
            n = sizeof(line) - 1;
        }
        memcpy(line, p, n);
        line[n] = '\0';

        hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        eq = strchr(line, '=');
        if (eq == NULL) {
            p = (*e == '\0') ? e : e + 1;
            continue;
        }
        ks = line;
        ke = eq;
        vs = eq + 1;
        ve = line + strlen(line);
        trim(&ks, &ke);
        trim(&vs, &ve);
        key_len = (size_t)(ke - ks);

        if (key_len == 4 && memcmp(ks, "name", 4) == 0) {
            if (ve == vs) {
                mod_report(m, "empty name, ignored");
            } else if (set_str_n(&m->name, vs, (size_t)(ve - vs)) != 0) {
                return -1;
            }
        } else if (key_len == 5 && memcmp(ks, "order", 5) == 0) {
            long v;

            if (parse_int(vs, (size_t)(ve - vs), &v) != 0 ||
                v < -1000000L || v > 1000000L) {
                mod_report(m, "bad order value '%.*s', ignored", (int)(ve - vs), vs);
            } else {
                m->order = (int)v;
            }
        } else if (key_len == 3 && memcmp(ks, "api", 3) == 0) {
            long v;

            if (parse_int(vs, (size_t)(ve - vs), &v) != 0) {
                mod_report(m, "bad api value '%.*s', ignored", (int)(ve - vs), vs);
            } else {
                m->api = (int)v;
                m->has_api = 1;
            }
        } else if (key_len == 4 && memcmp(ks, "port", 4) == 0) {
            if (ve == vs) {
                mod_report(m, "empty port version, ignored");
            } else if (set_str_n(&m->port, vs, (size_t)(ve - vs)) != 0) {
                return -1;
            }
        } else if (key_len == 4 && memcmp(ks, "code", 4) == 0) {
            if (ve == vs) {
                mod_report(m, "empty code path, ignored");
            } else if (set_str_n(&m->code, vs, (size_t)(ve - vs)) != 0) {
                return -1;
            }
        } else if (key_len == 5 && memcmp(ks, "state", 5) == 0) {
            if (ve == vs) {
                mod_report(m, "empty state namespace, ignored");
            } else if (set_str_n(&m->state, vs, (size_t)(ve - vs)) != 0) {
                return -1;
            }
        } else if (key_len == 8 && memcmp(ks, "override", 8) == 0) {
            if (parse_override(m, vs, (size_t)(ve - vs)) != 0) {
                mod_report(m, "bad override '%.*s', ignored", (int)(ve - vs), vs);
            }
        } else if (key_len == 4 && memcmp(ks, "rule", 4) == 0) {
            if (parse_rule(m, vs, (size_t)(ve - vs)) != 0) {
                mod_report(m, "bad rule '%.*s', ignored", (int)(ve - vs), vs);
            }
        } else if (key_len == 8 && memcmp(ks, "settings", 8) == 0) {
            if (ve == vs) {
                mod_report(m, "empty settings path, ignored");
            } else if (set_str_n(&m->settings, vs, (size_t)(ve - vs)) != 0) {
                return -1;
            }
        }
        /* An unknown key is ignored on purpose: a later version may add
         * keys, and an old port must not refuse a newer manifest. */
        p = (*e == '\0') ? e : e + 1;
    }
    return 0;
}

/* The name of the directory a root names: its last path component. */
static int root_dir_name(const char *root, char *buf, size_t size)
{
    const char *end;
    const char *start;
    size_t n;

    if (root == NULL || size == 0) {
        return -1;
    }
    end = root + strlen(root);
    while (end > root && (end[-1] == '/' || end[-1] == '\\')) {
        end--;
    }
    start = end;
    while (start > root && start[-1] != '/' && start[-1] != '\\') {
        start--;
    }
    if (start == end) {
        return -1;
    }
    n = (size_t)(end - start);
    if (n >= size) {
        n = size - 1;
    }
    memcpy(buf, start, n);
    buf[n] = '\0';
    return 0;
}

/* Read a file into memory.  A NULL result means the file is not there. */
static char *read_file(const char *path, size_t *size_out)
{
    FILE *fp;
    char *buf;
    size_t cap = 4096;
    size_t len = 0;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    buf = (char *)malloc(cap);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    for (;;) {
        size_t got;

        if (len + 1 >= cap) {
            char *grown = (char *)realloc(buf, cap * 2);

            if (grown == NULL) {
                free(buf);
                fclose(fp);
                return NULL;
            }
            buf = grown;
            cap *= 2;
        }
        got = fread(buf + len, 1, cap - len - 1, fp);
        len += got;
        if (got == 0) {
            break;
        }
    }
    buf[len] = '\0';
    fclose(fp);
    if (size_out != NULL) {
        *size_out = len;
    }
    return buf;
}

/*
 * Read `<root>/eidos_mod.txt` and parse it.  A mod with no manifest is not an
 * error: the defaults stand.  `dir_name` is the name the namespace is
 * derived from when the manifest states none; NULL takes it from the root.
 */
int bw_mod_manifest_parse_dir(void *manifest, const char *root, const char *dir_name)
{
    BwModManifest *m = (BwModManifest *)manifest;
    char path[1200];
    char fallback[256];
    char *text;
    const char *name = dir_name;
    int result;

    if (m == NULL || root == NULL) {
        return -1;
    }
    if (name == NULL) {
        if (root_dir_name(root, fallback, sizeof(fallback)) != 0) {
            fallback[0] = '\0';
        }
        name = fallback;
    }
    if (strlen(root) + 1 + sizeof(BW_MOD_MANIFEST_NAME) > sizeof(path)) {
        return -1;
    }
    sprintf(path, "%s/%s", root, BW_MOD_MANIFEST_NAME);
    text = read_file(path, NULL);
    if (text == NULL) {
        bw_mod_manifest_init(m, name);
        return 0;
    }
    result = bw_mod_manifest_parse(m, text, name);
    free(text);
    return result;
}

/* ------------------------------------------------------------------ */
/* The applied manifests                                              */
/* ------------------------------------------------------------------ */

/*
 * The manifests the apply has taken, in load order.  The interpreter and
 * the settings surface read the rule list and the settings path from here.
 * The port forgets the list when it resets the mod layer.
 */
static const BwModManifest **g_applied;
static u32 g_applied_count;
static u32 g_applied_cap;

static void applied_add(const BwModManifest *m)
{
    u32 i;

    for (i = 0; i < g_applied_count; i++) {
        if (g_applied[i] == m) {
            return;
        }
    }
    if (g_applied_count == g_applied_cap) {
        u32 cap = g_applied_cap == 0 ? 8u : g_applied_cap * 2u;
        const BwModManifest **grown =
            (const BwModManifest **)realloc(g_applied, (size_t)cap * sizeof(*grown));

        if (grown == NULL) {
            return;
        }
        g_applied = grown;
        g_applied_cap = cap;
    }
    g_applied[g_applied_count++] = m;
}

u32 bw_mod_manifest_applied_count(void)
{
    return g_applied_count;
}

const void *bw_mod_manifest_applied_at(u32 i)
{
    if (i >= g_applied_count) {
        return NULL;
    }
    return g_applied[i];
}

/* Forget the applied manifests: the port's reset. */
void bw_mod_manifest_applied_clear(void)
{
    free(g_applied);
    g_applied = NULL;
    g_applied_count = 0;
    g_applied_cap = 0;
}

/* ------------------------------------------------------------------ */
/* The apply                                                          */
/* ------------------------------------------------------------------ */

/*
 * Turn one manifest into registrations.  One call per mod, in load order.
 *
 *   1. Check `api`, and `port` when the mod has raw hooks, against the
 *      port's version; a mismatch refuses the mod.
 *   2. Derive the namespace from `state`, or from the name when `state` is
 *      absent, and check it is one path component.
 *   3. Check every override against the declared table; a pair that is not
 *      declared is reported and skipped.
 *   4. Leave the rule list and the settings path in the record, for the
 *      interpreter and the settings surface.
 *
 * The overrides themselves are installed by the loader, which resolves the
 * symbol in the loaded library (docs/mod-implementation.md, section 6).  The
 * apply leaves in the record the overrides whose pair is declared, in the
 * order the manifest gave them, and the loader installs them in that order.
 *
 * 0, or -1 when the mod is refused and the loader skips it.
 */
int bw_mod_manifest_apply(void *manifest)
{
    BwModManifest *m = (BwModManifest *)manifest;
    u32 i;
    u32 keep;

    if (m == NULL) {
        return -1;
    }

    /* 1. The versions. */
    if (m->has_api && m->api != BW_MOD_API_VERSION) {
        mod_report(m, "api %d does not match the port's %d",
                   m->api, BW_MOD_API_VERSION);
        return -1;
    }
    if (m->override_count > 0 && m->port != NULL) {
        if (g_port_version == NULL || *g_port_version == '\0') {
            mod_report(m, "the port version is unknown, the pin on '%s' is not checked",
                       m->port);
        } else if (strcmp(m->port, g_port_version) != 0) {
            mod_report(m, "port '%s' does not match the port's '%s'",
                       m->port, g_port_version);
            return -1;
        }
    }

    /* 2. The namespace.  A stated namespace that is not one path component
     * is reported and the default stands. */
    if (m->state != NULL && !bw_mod_manifest_namespace_valid(m->state)) {
        mod_report(m, "bad state namespace '%s', the default stands", m->state);
        free(m->state);
        m->state = NULL;
    }
    if (m->state == NULL) {
        m->state = bw_mod_manifest_default_namespace(m->name);
        if (m->state == NULL) {
            return -1;
        }
    }

    /* 3. The declared check.  The address is normalised first: the low bit
     * is masked, so the even start and the Thumb-tagged value a code pointer
     * holds are the same function. */
    keep = 0;
    for (i = 0; i < m->override_count; i++) {
        BwModOverride *o = &m->overrides[i];
        char region_buf[16];

        if (!bw_mod_slot_declared(o->region, o->addr)) {
            mod_report(m, "override %s:0x%08X is not a declared address",
                       region_token(o->region, region_buf, sizeof(region_buf)),
                       o->addr);
            free(o->symbol);
            free(o->sig);
            free(o->switch_name);
            continue;
        }
        if (keep != i) {
            m->overrides[keep] = m->overrides[i];
        }
        keep++;
    }
    m->override_count = keep;

    /* 4. The rules and the settings path are in the record; the applied
     * list is what the interpreter and the settings surface read. */
    applied_add(m);
    return 0;
}

/* ------------------------------------------------------------------ */
/* What the loader reads                                              */
/* ------------------------------------------------------------------ */

u32 bw_mod_manifest_override_count(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->override_count : 0u;
}

const void *bw_mod_manifest_override_at(const void *manifest, u32 i)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    if (m == NULL || i >= m->override_count) {
        return NULL;
    }
    return &m->overrides[i];
}

u32 bw_mod_manifest_rule_count(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->rule_count : 0u;
}

const void *bw_mod_manifest_rule_at(const void *manifest, u32 i)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    if (m == NULL || i >= m->rule_count) {
        return NULL;
    }
    return &m->rules[i];
}

/*
 * Whether the manifest stated an api version at all.  A mod with code and
 * no api is refused by the loader: the version cannot be checked, and the
 * rule is that it cannot be forgotten (docs/mod-abi.md, section 3).
 */
int bw_mod_manifest_has_api(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->has_api : 0;
}

const char *bw_mod_manifest_name(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->name : NULL;
}

const char *bw_mod_manifest_code(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->code : NULL;
}

const char *bw_mod_manifest_state(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->state : NULL;
}

const char *bw_mod_manifest_settings(const void *manifest)
{
    const BwModManifest *m = (const BwModManifest *)manifest;

    return m != NULL ? m->settings : NULL;
}
