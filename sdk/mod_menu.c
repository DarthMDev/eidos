/*
 * sdk/mod_menu.c -- the mod menu's model: the installed mods, the switch,
 * the clear action, the orphan report and a mod's own settings.
 *
 * The model the port's own settings screen draws its Mods section from
 * (docs/mod-services.md, "The mod menu").  It answers questions and holds
 * the state behind the answers; it draws nothing.  The screen asks how
 * many mods there are, what the i-th one is called, whether it is on, what
 * its settings are and which saved-data folders belong to a mod that is no
 * longer installed, and it draws those.
 *
 * The rules the model lives by are the four constraints of docs/mod-rules.md,
 * section 2, as docs/mod-state.md, section 2, states them.  What they mean
 * for this file:
 *
 *   1. The roots are rebuildable at the menu.  The model never installs the
 *      mod list once and leaves it there: when the player leaves the menu
 *      the list is built again from the switches (bw_clear_mods and
 *      bw_add_mod_root), and every change drops the cached merged archives
 *      (bw_vfs_flush, inside the file layer).  The state namespace is per
 *      mod, so a rebuild of the roots moves no state: the namespace is the
 *      manifest's `state` key, and the folder it names is the mod's own.
 *
 *   2. A code mod is disabled in order.  The model drives that order: the
 *      mods' state is flushed first (the saved-data layer's save handoff),
 *      then the mods that are going off are stopped, and the loader reverts
 *      every (region, address) pair the mod installed before it unloads the
 *      library (sdk/mod_loader.c).  The model does not touch the slot
 *      registry itself: the registry holds a pointer into the library, so
 *      only the loader may revert and unload.
 *
 *   3. A disabled mod's effects stay.  The model deletes nothing when a
 *      mod is turned off: the state file stays on the disk and the game
 *      bytes the mod wrote stay until the game writes over them.  The port
 *      restores nothing, and turning the mod on again reads its state
 *      again, because the namespace and the file it names are the same.
 *
 *   4. The port starts with no mods.  The model reads the port's list and
 *      never writes it at start-up, except to restore the list the player
 *      chose last time (bw_mod_menu_start): with no record of the player's
 *      own, the port's list stands as it is, and with no mod root there is
 *      no state read and the state folder is untouched.
 *
 * The port's own record
 * ---------------------
 *
 * The player's list is one file of the port's own, <save dir>/mods.ini:
 * the mods the player knows about and the state of their switches.  It is
 * written the way every other port file is written (a whole file, atomically
 * : sdk/save.h), and it is the only thing of the menu's own that outlives
 * the port, beside the mods' own data.
 *
 * The state the model needs from the other layers
 * ------------------------------------------------
 *
 * The saved data (sdk/mod_savedata.c) owns a mod's own file.  The menu
 * reaches it through two calls: bw_mod_savedata_set_namespace tells the
 * layer which mod it is serving, and bw_mod_savedata_clear deletes that
 * mod's state file and leaves its folder -- exactly the player's "clear
 * saved data".  The layer's own path rule (<save dir>/mods/<namespace>/)
 * is what the menu follows to find the folders that belong to no mod (the
 * orphan report) and to keep the player's settings beside the mod's saved
 * data.
 *
 * The manifest (sdk/mod_manifest.c) owns the record: the name, the state
 * namespace, the settings path and the code path.  The menu parses a
 * manifest per mod, the way the loader does, because the menu shows the
 * mods the loader did not load as well.
 *
 * The loader (sdk/mod_loader.c) owns the libraries: it loads them, it
 * stops them and it unloads them.  The menu calls it and does nothing
 * else with a library.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "mod_event.h"
#include "mod_loader.h"
#include "mod_manifest.h"
#include "mod_menu.h"
#include "mod_rule.h"
#include "plat_dir.h"
#include "save.h"
#include "vfs.h"

/* ------------------------------------------------------------------ */
/* What the other layers provide                                       */
/* ------------------------------------------------------------------ */

/*
 * The manifest layer (sdk/mod_manifest.c).  The record is opaque here: the
 * menu creates one per mod, parses the manifest into it and reads the
 * pieces it needs through these calls, the way sdk/mod_loader.c declares
 * them.  The manifest layer has no header of its own yet, so the
 * prototypes are here.
 */
extern void       *bw_mod_manifest_new(const char *dir_name);
extern void        bw_mod_manifest_delete(void *manifest);
extern int         bw_mod_manifest_parse_dir(void *manifest, const char *root,
                                              const char *dir_name);
extern const char *bw_mod_manifest_name(const void *manifest);
extern const char *bw_mod_manifest_code(const void *manifest);
extern const char *bw_mod_manifest_state(const void *manifest);
extern const char *bw_mod_manifest_settings(const void *manifest);
extern int         bw_mod_manifest_namespace_valid(const char *ns);
extern char       *bw_mod_manifest_default_namespace(const char *name);

/*
 * The saved data (sdk/mod_savedata.c).  The layer has no header yet, so
 * the prototypes are here, the way sdk/mod_loader.c declares them.
 */
extern void bw_mod_savedata_set_namespace(const char *ns);
extern int  bw_mod_savedata_clear(void);
extern void bw_mod_savedata_save_begin(void);
extern int  bw_mod_savedata_save_done(int ok, u32 check);
extern u32  bw_mod_savedata_save_check(void);

/* ------------------------------------------------------------------ */
/* The limits                                                          */
/* ------------------------------------------------------------------ */

/* How many mods the menu holds. */
#define BW_MOD_MENU_MAX 32

/* The lengths of the port's own buffers. */
#define BW_MOD_MENU_PATH_MAX 1024
#define BW_MOD_MENU_ROOT_MAX 512
#define BW_MOD_MENU_NAME_MAX 96
#define BW_MOD_MENU_NS_MAX 96
#define BW_MOD_MENU_SET_MAX 256
#define BW_MOD_MENU_KEY_MAX 64
#define BW_MOD_MENU_LABEL_MAX 64
#define BW_MOD_MENU_CHOICE_MAX 8
#define BW_MOD_MENU_CHOICE_LEN 32

/* How many options one mod may declare. */
#define BW_MOD_MENU_OPTIONS_MAX 16

/* The port's own record of the player's list. */
#define BW_MOD_MENU_RECORD "mods.ini"

/* The player's choices for one mod, beside the mod's saved data. */
#define BW_MOD_MENU_CHOICES "settings.ini"

/* ------------------------------------------------------------------ */
/* The records                                                         */
/* ------------------------------------------------------------------ */

/*
 * One option of a mod's own settings (docs/mod-services.md, "Mod
 * settings").  The declaration is the file the manifest's `settings` key
 * names:
 *
 *     option nuzlocke.dupes   bool    "Dupes clause"     on
 *     option nuzlocke.cap     choice  "Level caps"       off|gym|ace
 *
 * The key is the name the mod asks setting_get for, the label is the text
 * the screen shows, and the value is the player's choice: 0 or 1 for a
 * bool, the index of the chosen name for a choice.
 */
typedef struct BwModMenuOption {
    char key[BW_MOD_MENU_KEY_MAX];
    char label[BW_MOD_MENU_LABEL_MAX];
    int  kind;                     /* BW_MOD_SETTING_BOOL or _CHOICE */
    int  def;                      /* the default: 0/1, or a choice index */
    int  value;                    /* the player's choice             */
    int  choice_count;
    char choices[BW_MOD_MENU_CHOICE_MAX][BW_MOD_MENU_CHOICE_LEN];
} BwModMenuOption;

/* One mod the menu shows. */
typedef struct BwModMenuMod {
    char root[BW_MOD_MENU_ROOT_MAX];    /* the mod's own folder          */
    char name[BW_MOD_MENU_NAME_MAX];    /* the name the manifest gives   */
    char ns[BW_MOD_MENU_NS_MAX];        /* the state namespace           */
    char settings[BW_MOD_MENU_SET_MAX]; /* the settings declaration      */
    int  order;                         /* the manifest's order key      */
    int  enabled;                       /* the player's switch           */
    int  running;                       /* the root is in the port's list */
    int  present;                       /* the mod's folder is there     */
    int  has_code;                      /* the mod ships code            */
    u32  option_count;
    BwModMenuOption *options;
} BwModMenuMod;

/* One row of the port's own record: a mod's root and its switch. */
typedef struct BwModMenuRow {
    char root[BW_MOD_MENU_ROOT_MAX];
    int  enabled;
} BwModMenuRow;

static BwModMenuMod g_mods[BW_MOD_MENU_MAX];
static u32          g_count;
static int          g_built;

/* The folders in the save directory that no mod claims. */
static char g_orphans[BW_MOD_MENU_MAX][BW_MOD_MENU_NS_MAX];
static u32  g_orphan_count;

/* The mod that is calling: the port sets it around the calls it makes into
 * a mod, the way the loader sets the saved-data layer's. */
static char g_namespace[BW_MOD_MENU_NS_MAX];

/* ------------------------------------------------------------------ */
/* The small helpers                                                   */
/* ------------------------------------------------------------------ */

static void copy_str(char *dst, size_t size, const char *src)
{
    size_t n;

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= size) {
        n = size - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Trim [*s, *e) of the blanks at its ends. */
static void trim(const char **s, const char **e)
{
    while (*s < *e && is_space(**s)) {
        (*s)++;
    }
    while (*e > *s && is_space((*e)[-1])) {
        (*e)--;
    }
}

/* The port's reports, the shape the documents give. */
static void report(const char *label, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void report(const char *label, const char *fmt, ...)
{
    va_list ap;

    if (label == NULL || label[0] == '\0') {
        label = "?";
    }
    fprintf(stderr, "eidos: mod %s: ", label);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* Read a whole file into memory.  A NULL result means the file is not
 * there. */
static char *read_file(const char *path)
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
    return buf;
}

/* Grow a text buffer and add a piece to it. */
static int text_add(char **buf, size_t *len, size_t *cap, const char *text)
{
    size_t n = strlen(text);

    if (*len + n + 1 > *cap) {
        size_t grown_cap = (*cap == 0) ? 1024 : *cap;
        char *grown;

        while (grown_cap < *len + n + 1) {
            grown_cap *= 2;
        }
        grown = (char *)realloc(*buf, grown_cap);
        if (grown == NULL) {
            return -1;
        }
        *buf = grown;
        *cap = grown_cap;
    }
    memcpy(*buf + *len, text, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 0;
}

/* ------------------------------------------------------------------ */
/* The paths                                                           */
/* ------------------------------------------------------------------ */

/* The port's own record of the player's list: <save dir>/mods.ini. */
static int record_path(char *out, size_t size)
{
    return bw_save_path(out, size, BW_MOD_MENU_RECORD);
}

/* The folder the mods' own folders live in: <save dir>/mods. */
static int mods_folder_path(char *out, size_t size)
{
    return bw_save_path(out, size, "mods");
}

/* ------------------------------------------------------------------ */
/* The port's own record of the player's list                          */
/* ------------------------------------------------------------------ */

/*
 * Read the record: the mods the player knows about and their switches.
 * The rows come back in the order the file gives them.
 */
static u32 read_record(BwModMenuRow *rows, u32 max)
{
    char path[BW_MOD_MENU_PATH_MAX];
    char *text;
    const char *p;
    u32 count = 0;

    if (record_path(path, sizeof path) != 0) {
        return 0;
    }
    text = read_file(path);
    if (text == NULL) {
        return 0;
    }
    p = text;
    while (*p != '\0') {
        const char *e = p;
        const char *ks;
        const char *ke;
        const char *vs;
        const char *ve;
        char line[512];
        char *hash;
        char *eq;
        size_t n;

        while (*e != '\0' && *e != '\n') {
            e++;
        }
        n = (size_t)(e - p);
        if (n >= sizeof line) {
            n = sizeof line - 1;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        p = (*e == '\0') ? e : e + 1;

        hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        {
            const char *s = line;
            const char *en = line + strlen(line);

            trim(&s, &en);
            if (s == en || *s == '[') {
                continue;               /* an empty line or a section */
            }
            eq = strchr(s, '=');
            if (eq == NULL) {
                continue;
            }
            ks = s;
            ke = eq;
            vs = eq + 1;
            ve = en;
            trim(&ks, &ke);
            trim(&vs, &ve);
            if ((size_t)(ke - ks) == 4 && memcmp(ks, "root", 4) == 0) {
                if (count >= max) {
                    continue;
                }
                copy_str(rows[count].root, sizeof rows[count].root, vs);
                rows[count].enabled = 0;
                count++;
            } else if ((size_t)(ke - ks) == 7 && memcmp(ks, "enabled", 7) == 0) {
                if (count > 0) {
                    rows[count - 1].enabled = (strtol(vs, NULL, 10) != 0);
                }
            }
            /* An unknown key is ignored, the way the manifest reader
             * ignores one: a later version may add keys. */
        }
    }
    free(text);
    return count;
}

/*
 * Write the record: the mods the player knows about and their switches.
 * One block per mod:
 *
 *     [mod]
 *     root = <the mod's folder>
 *     enabled = 1
 */
static int write_record(void)
{
    char path[BW_MOD_MENU_PATH_MAX];
    char *text = NULL;
    size_t len = 0;
    size_t cap = 0;
    u32 i;
    int rc;

    if (record_path(path, sizeof path) != 0) {
        return -1;
    }
    if (text_add(&text, &len, &cap,
                 "# eidos mod record (sdk/mod_menu.c).\n"
                 "# The mods the player knows about and the state of their\n"
                 "# switches.  The port owns this file.\n") != 0) {
        return -1;
    }
    for (i = 0; i < g_count; i++) {
        char block[1024];
        int n = snprintf(block, sizeof block,
                         "\n[mod]\nroot = %s\nenabled = %d\n",
                         g_mods[i].root, g_mods[i].enabled ? 1 : 0);

        if (n < 0 || (size_t)n >= sizeof block) {
            free(text);
            return -1;
        }
        if (text_add(&text, &len, &cap, block) != 0) {
            free(text);
            return -1;
        }
    }
    rc = bw_save_write_atomic(path, text, (u32)len);
    free(text);
    return rc;
}

/* ------------------------------------------------------------------ */
/* The manifest of one mod                                             */
/* ------------------------------------------------------------------ */

/*
 * Read one mod's manifest: the name, the state namespace, the settings
 * declaration, the code path and the order key.
 *
 * The namespace follows the manifest layer's rule: the stated `state` when
 * it is one path component, and the default derived from the name when it
 * is not (docs/mod-state.md, section 1).  The manifest layer reports a bad
 * namespace when it applies a manifest; the menu does not report it again,
 * because the loader does, once, when the mod is loaded.
 */
static void read_manifest(BwModMenuMod *m)
{
    void *record;
    const char *ns;

    record = bw_mod_manifest_new(NULL);
    if (record == NULL) {
        return;
    }
    if (bw_mod_manifest_parse_dir(record, m->root, NULL) != 0) {
        /* The manifest could not be read: the defaults stand, the way
         * they stand for a mod with no manifest at all. */
        bw_mod_manifest_delete(record);
        return;
    }
    copy_str(m->name, sizeof m->name, bw_mod_manifest_name(record));
    ns = bw_mod_manifest_state(record);
    if (ns != NULL && ns[0] != '\0' && bw_mod_manifest_namespace_valid(ns)) {
        copy_str(m->ns, sizeof m->ns, ns);
    } else {
        char *def = bw_mod_manifest_default_namespace(
            bw_mod_manifest_name(record));

        if (def != NULL) {
            copy_str(m->ns, sizeof m->ns, def);
            free(def);
        }
    }
    copy_str(m->settings, sizeof m->settings,
             bw_mod_manifest_settings(record));
    {
        const char *code = bw_mod_manifest_code(record);

        m->has_code = (code != NULL && code[0] != '\0');
    }
    /* The manifest layer has no call for the order key yet; the record is
     * published in sdk/mod_manifest.h, and the loader reads the same
     * field. */
    m->order = ((const BwModManifest *)record)->order;
    bw_mod_manifest_delete(record);
}

/* ------------------------------------------------------------------ */
/* A mod's own settings                                                */
/* ------------------------------------------------------------------ */

/* The next token of a line: a quoted string or a word.  NULL at the end
 * of the line. */
static char *next_token(char **p)
{
    char *s = *p;
    char *start;
    char *out;
    size_t n;

    while (*s != '\0' && is_space(*s)) {
        s++;
    }
    if (*s == '\0') {
        *p = s;
        return NULL;
    }
    if (*s == '"') {
        s++;
        start = s;
        while (*s != '\0' && *s != '"') {
            s++;
        }
        n = (size_t)(s - start);
        if (*s == '"') {
            s++;
        }
        *p = s;
        out = (char *)malloc(n + 1);
        if (out == NULL) {
            return NULL;
        }
        memcpy(out, start, n);
        out[n] = '\0';
        return out;
    }
    start = s;
    while (*s != '\0' && !is_space(*s)) {
        s++;
    }
    n = (size_t)(s - start);
    *p = s;
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return out;
}

/* Is the text the name of a bool's value?  The value is 1 for "on",
 * "true" and "1", and 0 for everything else. */
static int bool_value(const char *text)
{
    if (text == NULL) {
        return 0;
    }
    if (strcmp(text, "on") == 0 || strcmp(text, "true") == 0 ||
        strcmp(text, "1") == 0) {
        return 1;
    }
    return 0;
}

/*
 * One line of the settings declaration:
 *
 *     option <key> <bool|choice> "<label>" <value>
 *
 * The value of a bool is "on" or "off"; the value of a choice is the list
 * of its names, separated with '|', and the first of them is the default.
 * The documents give the example and leave the grammar's details open
 * (docs/mod-services.md, "Mod settings"), so the reading is the port's own.
 */
static int parse_option(BwModMenuMod *m, char *line)
{
    char *p = line;
    char *key;
    char *kind;
    char *label;
    char *value;
    BwModMenuOption *o;

    key = next_token(&p);
    if (key == NULL) {
        return 0;
    }
    if (strcmp(key, "option") != 0) {
        free(key);
        return 0;
    }
    free(key);
    key = next_token(&p);
    kind = next_token(&p);
    label = next_token(&p);
    if (key == NULL || kind == NULL || label == NULL) {
        free(key);
        free(kind);
        free(label);
        return -1;
    }
    /* The value is the rest of the line: the names of a choice are
     * separated with '|' and hold no space. */
    {
        const char *s = p;
        const char *e;

        while (*s != '\0' && is_space(*s)) {
            s++;
        }
        e = s + strlen(s);
        trim(&s, &e);
        value = (char *)malloc((size_t)(e - s) + 1);
        if (value == NULL) {
            free(key);
            free(kind);
            free(label);
            return -1;
        }
        memcpy(value, s, (size_t)(e - s));
        value[e - s] = '\0';
    }
    if (m->option_count >= BW_MOD_MENU_OPTIONS_MAX) {
        report(m->name, "too many options; the rest are ignored");
        free(key);
        free(kind);
        free(label);
        free(value);
        return -1;
    }
    o = &m->options[m->option_count];
    memset(o, 0, sizeof *o);
    copy_str(o->key, sizeof o->key, key);
    copy_str(o->label, sizeof o->label, label);
    if (strcmp(kind, "bool") == 0) {
        o->kind = BW_MOD_SETTING_BOOL;
        o->def = bool_value(value);
        o->value = o->def;
    } else if (strcmp(kind, "choice") == 0) {
        const char *s = value;

        o->kind = BW_MOD_SETTING_CHOICE;
        for (;;) {
            const char *sep = strchr(s, '|');
            const char *name = s;
            const char *name_end = (sep != NULL) ? sep : s + strlen(s);
            size_t n;

            trim(&name, &name_end);
            n = (size_t)(name_end - name);
            if (n > 0 && o->choice_count < BW_MOD_MENU_CHOICE_MAX) {
                if (n >= BW_MOD_MENU_CHOICE_LEN) {
                    n = BW_MOD_MENU_CHOICE_LEN - 1;
                }
                memcpy(o->choices[o->choice_count], name, n);
                o->choices[o->choice_count][n] = '\0';
                o->choice_count++;
            }
            if (sep == NULL) {
                break;
            }
            s = sep + 1;
        }
        if (o->choice_count == 0) {
            report(m->name, "the option '%s' names no choice; it is ignored",
                   o->key);
            free(key);
            free(kind);
            free(label);
            free(value);
            return -1;
        }
        o->def = 0;
        o->value = o->def;
    } else {
        report(m->name, "the option '%s' has the unknown kind '%s'; it is "
               "ignored", key, kind);
        free(key);
        free(kind);
        free(label);
        free(value);
        return -1;
    }
    m->option_count++;
    free(key);
    free(kind);
    free(label);
    free(value);
    return 0;
}

/* The path of the mod's settings declaration: <root>/<settings path>. */
static int declaration_path(char *out, size_t size, const BwModMenuMod *m)
{
    int n;

    if (m->root[0] == '\0' || m->settings[0] == '\0') {
        return -1;
    }
    n = snprintf(out, size, "%s/%s", m->root, m->settings);
    if (n < 0 || (size_t)n >= size) {
        return -1;
    }
    return 0;
}

/* The path of the player's choices for the mod, beside the mod's saved
 * data: <save dir>/mods/<namespace>/settings.ini. */
static int choices_path(char *out, size_t size, const BwModMenuMod *m)
{
    char rel[256];
    int n;

    if (m->ns[0] == '\0') {
        return -1;
    }
    n = snprintf(rel, sizeof rel, "mods/%s/%s", m->ns, BW_MOD_MENU_CHOICES);
    if (n < 0 || (size_t)n >= sizeof rel) {
        return -1;
    }
    return bw_save_path(out, size, rel);
}

/* Read the declaration file and fill the mod's options. */
static void read_declaration(BwModMenuMod *m)
{
    char path[BW_MOD_MENU_PATH_MAX];
    char *text;
    char *p;

    if (declaration_path(path, sizeof path, m) != 0) {
        return;                 /* the manifest names no settings */
    }
    text = read_file(path);
    if (text == NULL) {
        return;                 /* no declaration: the mod has no settings */
    }
    m->options = (BwModMenuOption *)calloc(BW_MOD_MENU_OPTIONS_MAX,
                                           sizeof(BwModMenuOption));
    if (m->options == NULL) {
        free(text);
        return;
    }
    p = text;
    while (*p != '\0') {
        char *e = strchr(p, '\n');
        char line[512];
        char *hash;
        size_t n;

        if (e == NULL) {
            e = p + strlen(p);
        }
        n = (size_t)(e - p);
        if (n >= sizeof line) {
            n = sizeof line - 1;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        p = (*e == '\n') ? e + 1 : e;
        hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        parse_option(m, line);
    }
    free(text);
}

/* Read the player's choices and put them over the defaults. */
static void read_choices(BwModMenuMod *m)
{
    char path[BW_MOD_MENU_PATH_MAX];
    char *text;
    const char *p;

    if (m->option_count == 0) {
        return;
    }
    if (choices_path(path, sizeof path, m) != 0) {
        return;
    }
    text = read_file(path);
    if (text == NULL) {
        return;                 /* no choices yet: the defaults stand */
    }
    p = text;
    while (*p != '\0') {
        const char *e = p;
        const char *ks;
        const char *ke;
        const char *vs;
        const char *ve;
        char line[512];
        char *hash;
        char *eq;
        size_t n;

        while (*e != '\0' && *e != '\n') {
            e++;
        }
        n = (size_t)(e - p);
        if (n >= sizeof line) {
            n = sizeof line - 1;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        p = (*e == '\0') ? e : e + 1;

        hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        {
            const char *s = line;
            const char *en = line + strlen(line);

            trim(&s, &en);
            if (s == en) {
                continue;
            }
            eq = strchr(s, '=');
            if (eq == NULL) {
                continue;
            }
            ks = s;
            ke = eq;
            vs = eq + 1;
            ve = en;
            trim(&ks, &ke);
            trim(&vs, &ve);
            {
                u32 i;

                for (i = 0; i < m->option_count; i++) {
                    BwModMenuOption *o = &m->options[i];
                    long value;

                    if (strcmp(o->key, ks) != 0) {
                        continue;
                    }
                    value = strtol(vs, NULL, 10);
                    if (o->kind == BW_MOD_SETTING_BOOL) {
                        if (value != 0 && value != 1) {
                            break;
                        }
                    } else {
                        if (value < 0 || value >= o->choice_count) {
                            break;
                        }
                    }
                    o->value = (int)value;
                    break;
                }
            }
        }
    }
    free(text);
}

/* The mod's own settings: the declaration and the player's choices. */
static void read_settings(BwModMenuMod *m)
{
    read_declaration(m);
    read_choices(m);
}

/* Write the player's choices for one mod: beside the mod's saved data. */
static int write_choices(BwModMenuMod *m)
{
    char path[BW_MOD_MENU_PATH_MAX];
    char *text = NULL;
    size_t len = 0;
    size_t cap = 0;
    u32 i;
    int rc;

    if (m->option_count == 0) {
        return 0;
    }
    if (choices_path(path, sizeof path, m) != 0) {
        return -1;
    }
    if (text_add(&text, &len, &cap,
                 "# eidos mod settings (sdk/mod_menu.c): the player's\n"
                 "# choices for one mod.  The port owns this file.\n") != 0) {
        return -1;
    }
    for (i = 0; i < m->option_count; i++) {
        BwModMenuOption *o = &m->options[i];
        char line[256];
        int n = snprintf(line, sizeof line, "%s = %d\n", o->key, o->value);

        if (n < 0 || (size_t)n >= sizeof line) {
            free(text);
            return -1;
        }
        if (text_add(&text, &len, &cap, line) != 0) {
            free(text);
            return -1;
        }
    }
    rc = bw_save_write_atomic(path, text, (u32)len);
    free(text);
    return rc;
}

/* ------------------------------------------------------------------ */
/* The model                                                           */
/* ------------------------------------------------------------------ */

/* Release the model. */
static void model_free(void)
{
    u32 i;

    for (i = 0; i < g_count; i++) {
        free(g_mods[i].options);
        g_mods[i].options = NULL;
        g_mods[i].option_count = 0;
    }
    g_count = 0;
    g_built = 0;
    g_orphan_count = 0;
}

/* A new row of the model, or NULL when the model is full. */
static BwModMenuMod *model_add(void)
{
    BwModMenuMod *m;

    if (g_count >= BW_MOD_MENU_MAX) {
        return NULL;
    }
    m = &g_mods[g_count++];
    memset(m, 0, sizeof *m);
    return m;
}

/* The row of the mod whose root is `root`, or NULL. */
static BwModMenuMod *model_find_root(const char *root)
{
    u32 i;

    for (i = 0; i < g_count; i++) {
        if (strcmp(g_mods[i].root, root) == 0) {
            return &g_mods[i];
        }
    }
    return NULL;
}

/* The row of the mod whose namespace is `ns`, or NULL. */
static BwModMenuMod *model_find_ns(const char *ns)
{
    u32 i;

    for (i = 0; i < g_count; i++) {
        if (g_mods[i].ns[0] != '\0' && strcmp(g_mods[i].ns, ns) == 0) {
            return &g_mods[i];
        }
    }
    return NULL;
}

/* The rows in load order: the manifest's order key, and the order the
 * rows were added in for the mods that share one.  The sort is stable, so
 * the mods the port's list holds keep the order the port gave them. */
static void model_sort(void)
{
    u32 i;
    u32 j;

    for (i = 1; i < g_count; i++) {
        BwModMenuMod tmp = g_mods[i];

        for (j = i; j > 0 && g_mods[j - 1].order > tmp.order; j--) {
            g_mods[j] = g_mods[j - 1];
        }
        g_mods[j] = tmp;
    }
}

/*
 * Build the model: the mods the port's list holds, in load order, and the
 * mods the player's record holds that the list does not.
 */
static void model_build(void)
{
    BwModMenuRow rows[BW_MOD_MENU_MAX];
    u32 row_count;
    u32 list_count;
    u32 i;

    model_free();
    list_count = bw_mod_count();
    for (i = 0; i < list_count; i++) {
        const char *root = bw_mod_root_at(i);
        BwModMenuMod *m;

        if (root == NULL || root[0] == '\0') {
            continue;
        }
        m = model_add();
        if (m == NULL) {
            break;
        }
        copy_str(m->root, sizeof m->root, root);
        copy_str(m->name, sizeof m->name, bw_mod_name_at(i));
        m->order = bw_mod_order_at(i);
        m->running = 1;
        m->enabled = 1;
        m->present = (bw_plat_path_kind(root) == BW_PATH_DIR);
    }
    /* The mods the player knows about and the port's list does not hold:
     * the ones the player turned off. */
    row_count = read_record(rows, BW_MOD_MENU_MAX);
    for (i = 0; i < row_count; i++) {
        BwModMenuMod *m;

        if (rows[i].root[0] == '\0' || model_find_root(rows[i].root) != NULL) {
            continue;
        }
        m = model_add();
        if (m == NULL) {
            break;
        }
        copy_str(m->root, sizeof m->root, rows[i].root);
        m->enabled = rows[i].enabled;
        m->running = 0;
        m->present = (bw_plat_path_kind(rows[i].root) == BW_PATH_DIR);
    }
    /* Every mod's own manifest: the name, the namespace, the settings and
     * the order key come from it. */
    for (i = 0; i < g_count; i++) {
        read_manifest(&g_mods[i]);
        if (g_mods[i].present == 0) {
            g_mods[i].present =
                (bw_plat_path_kind(g_mods[i].root) == BW_PATH_DIR);
        }
    }
    model_sort();
    for (i = 0; i < g_count; i++) {
        read_settings(&g_mods[i]);
    }
    g_built = 1;
}

/* The model, built when it is not. */
static void model_ready(void)
{
    if (!g_built) {
        model_build();
    }
}

/* ------------------------------------------------------------------ */
/* The model's own calls                                                */
/* ------------------------------------------------------------------ */

void bw_mod_menu_reset(void)
{
    model_free();
    g_namespace[0] = '\0';
}

void bw_mod_menu_open(void)
{
    model_build();
}

void bw_mod_menu_start(void)
{
    BwModMenuRow rows[BW_MOD_MENU_MAX];
    u32 row_count;
    u32 i;

    model_build();
    row_count = read_record(rows, BW_MOD_MENU_MAX);
    if (row_count == 0) {
        /* No record of the player's own: the port's list stands as it is
         * (the environment's BW_MOD_ROOT, or what an explicit call set),
         * and with no mod root there is no state read and the state
         * folder is untouched (docs/mod-state.md, section 2, constraint
         * 4). */
        return;
    }
    /* The record holds the player's own switches: the mods the player
     * turned off are off, the ones the player turned on are on.  A mod
     * the record does not mention keeps the state the port's list gave
     * it. */
    for (i = 0; i < g_count; i++) {
        u32 k;

        for (k = 0; k < row_count; k++) {
            if (strcmp(rows[k].root, g_mods[i].root) == 0) {
                g_mods[i].enabled = rows[k].enabled;
                break;
            }
        }
    }
    /* The list in force is the player's own: the mods that are on.  The
     * rebuild drops the cached merged archives with it. */
    {
        int changed = 0;

        for (i = 0; i < g_count; i++) {
            if (g_mods[i].enabled != g_mods[i].running) {
                changed = 1;
            }
        }
        if (changed) {
            bw_clear_mods();
            for (i = 0; i < g_count; i++) {
                if (g_mods[i].enabled) {
                    bw_add_mod_root(g_mods[i].root);
                }
            }
        }
    }
    for (i = 0; i < g_count; i++) {
        g_mods[i].running = g_mods[i].enabled;
    }
}

/* ------------------------------------------------------------------ */
/* The list                                                            */
/* ------------------------------------------------------------------ */

u32 bw_mod_menu_count(void)
{
    model_ready();
    return g_count;
}

const char *bw_mod_menu_name_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].name : "";
}

const char *bw_mod_menu_namespace_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].ns : "";
}

const char *bw_mod_menu_settings_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].settings : "";
}

const char *bw_mod_menu_root_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].root : "";
}

int bw_mod_menu_order_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].order : 0;
}

int bw_mod_menu_enabled_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].enabled : 0;
}

int bw_mod_menu_running_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].running : 0;
}

int bw_mod_menu_present_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].present : 0;
}

int bw_mod_menu_has_code_at(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].has_code : 0;
}

/* ------------------------------------------------------------------ */
/* The switch                                                          */
/* ------------------------------------------------------------------ */

int bw_mod_menu_set_enabled(u32 index, int on)
{
    model_ready();
    if (index >= g_count) {
        return -1;
    }
    g_mods[index].enabled = on ? 1 : 0;
    return 0;
}

int bw_mod_menu_dirty(void)
{
    u32 i;

    model_ready();
    for (i = 0; i < g_count; i++) {
        if (g_mods[i].enabled != g_mods[i].running) {
            return 1;
        }
    }
    return 0;
}

void bw_mod_menu_revert(void)
{
    u32 i;

    model_ready();
    for (i = 0; i < g_count; i++) {
        g_mods[i].enabled = g_mods[i].running;
    }
}

int bw_mod_menu_apply(void)
{
    u32 i;

    model_ready();
    if (!bw_mod_menu_dirty()) {
        return 0;               /* nothing changed: the mods keep running */
    }

    /*
     * 1. The mods' state is flushed before anything is stopped
     *    (docs/mod-state.md, section 2, constraint 2).  The saved-data
     *    layer holds a payload while a save is in progress and writes it
     *    when the save ends; the flush is what ends it, with the checksum
     *    of the save in force.
     */
    bw_mod_savedata_save_begin();
    (void)bw_mod_savedata_save_done(1, bw_mod_savedata_save_check());

    /*
     * 2. The mods that are running are stopped, the last loaded first.
     *    The loader reverts every (region, address) pair the mod
     *    installed before it unloads the library, because the registry
     *    holds a pointer into the library (sdk/mod_loader.c).
     */
    bw_mod_loader_stop_all();

    /*
     * 3. The event layer is built again: a handler of a stopped mod
     *    still points into the library that was unloaded, so the port's
     *    own rows are bound again and the rules are dropped (the loader
     *    binds them again when it loads the mods that are wanted).
     */
    bw_mod_event_reset();
    bw_mod_event_bind_all();
    bw_mod_rule_reset();

    /*
     * 4. The roots are built again from the switches.  Every change
     *    drops the cached merged archives, so the next open of an archive
     *    sees the new list (sdk/vfs.c).
     */
    bw_clear_mods();
    for (i = 0; i < g_count; i++) {
        if (!g_mods[i].enabled) {
            continue;
        }
        if (bw_add_mod_root(g_mods[i].root) != 0) {
            report(g_mods[i].name, "the mod's folder could not be added to "
                   "the list");
        }
    }

    /*
     * 5. The mods that are wanted are loaded again, so the load point
     *    reads their state: a state file that stayed on the disk is read
     *    again, and a mod that was off keeps its file.
     */
    (void)bw_mod_loader_load_all();

    /*
     * 6. The player's list is written: the switches are the player's own
     *    and outlive the port.
     */
    (void)write_record();

    for (i = 0; i < g_count; i++) {
        g_mods[i].running = g_mods[i].enabled;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The clear action                                                    */
/* ------------------------------------------------------------------ */

int bw_mod_menu_clear(u32 index)
{
    char saved[BW_MOD_MENU_NS_MAX];
    int rc;

    model_ready();
    if (index >= g_count) {
        return -1;
    }
    if (g_mods[index].ns[0] == '\0') {
        return -1;
    }
    /*
     * The saved-data layer owns the mod's own file.  The layer deletes the
     * state file and leaves the folder, which is the rule of
     * docs/mod-state.md, section 1: the port does not delete a mod's
     * folder when the mod is removed, so a re-enable reads the file again.
     */
    copy_str(saved, sizeof saved, g_namespace);
    bw_mod_savedata_set_namespace(g_mods[index].ns);
    rc = bw_mod_savedata_clear();
    bw_mod_savedata_set_namespace(saved[0] != '\0' ? saved : NULL);
    return rc;
}

/* ------------------------------------------------------------------ */
/* The orphan report                                                   */
/* ------------------------------------------------------------------ */

/* One entry of the save directory's mods folder. */
typedef struct BwModMenuScan {
    const char *path;
    u32         count;
} BwModMenuScan;

static int scan_entry(void *ctx, const char *name)
{
    BwModMenuScan *scan = (BwModMenuScan *)ctx;
    char path[BW_MOD_MENU_PATH_MAX];
    int n;

    if (scan->count >= BW_MOD_MENU_MAX) {
        return 0;
    }
    n = snprintf(path, sizeof path, "%s/%s", scan->path, name);
    if (n < 0 || (size_t)n >= sizeof path) {
        return 0;
    }
    if (bw_plat_path_kind(path) != BW_PATH_DIR) {
        return 0;               /* only the folders are state */
    }
    if (model_find_ns(name) != NULL) {
        return 0;               /* a mod claims this folder */
    }
    copy_str(g_orphans[scan->count], sizeof g_orphans[0], name);
    scan->count++;
    return 0;
}

/*
 * The folders in <save dir>/mods/ that no mod claims: a mod that was
 * removed and whose folder was left behind.  The port reports them at the
 * menu (docs/mod-state.md, section 1, "Removal").
 *
 * The folder is only read, never made: with no mods the state folder is
 * untouched.
 */
static void scan_orphans(void)
{
    char path[BW_MOD_MENU_PATH_MAX];
    BwModMenuScan scan;

    g_orphan_count = 0;
    if (mods_folder_path(path, sizeof path) != 0) {
        return;
    }
    scan.path = path;
    scan.count = 0;
    (void)bw_plat_list_dir(path, scan_entry, &scan);
    g_orphan_count = scan.count;
}

u32 bw_mod_menu_orphan_count(void)
{
    model_ready();
    scan_orphans();
    return g_orphan_count;
}

const char *bw_mod_menu_orphan_at(u32 index)
{
    model_ready();
    scan_orphans();
    return index < g_orphan_count ? g_orphans[index] : NULL;
}

/* ------------------------------------------------------------------ */
/* A mod's own settings                                                */
/* ------------------------------------------------------------------ */

u32 bw_mod_menu_option_count(u32 index)
{
    model_ready();
    return index < g_count ? g_mods[index].option_count : 0u;
}

const char *bw_mod_menu_option_key(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return "";
    }
    return g_mods[index].options[option].key;
}

const char *bw_mod_menu_option_label(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return "";
    }
    return g_mods[index].options[option].label;
}

int bw_mod_menu_option_kind(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return BW_MOD_SETTING_BOOL;
    }
    return g_mods[index].options[option].kind;
}

int bw_mod_menu_option_value(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return 0;
    }
    return g_mods[index].options[option].value;
}

int bw_mod_menu_option_default(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return 0;
    }
    return g_mods[index].options[option].def;
}

int bw_mod_menu_option_set(u32 index, u32 option, int value)
{
    BwModMenuMod *m;
    BwModMenuOption *o;

    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return -1;
    }
    m = &g_mods[index];
    o = &m->options[option];
    if (o->kind == BW_MOD_SETTING_BOOL) {
        if (value != 0 && value != 1) {
            return -1;
        }
    } else {
        if (value < 0 || value >= o->choice_count) {
            return -1;
        }
    }
    o->value = value;
    /* The choice is kept beside the mod's saved data. */
    (void)write_choices(m);
    return 0;
}

int bw_mod_menu_option_choice_count(u32 index, u32 option)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return 0;
    }
    return g_mods[index].options[option].choice_count;
}

const char *bw_mod_menu_option_choice(u32 index, u32 option, int choice)
{
    model_ready();
    if (index >= g_count || option >= g_mods[index].option_count) {
        return NULL;
    }
    if (choice < 0 || choice >= g_mods[index].options[option].choice_count) {
        return NULL;
    }
    return g_mods[index].options[option].choices[choice];
}

const char *bw_mod_menu_option_text(u32 index, u32 option, char *buf,
                                     size_t size)
{
    BwModMenuOption *o;

    model_ready();
    if (buf == NULL || size == 0) {
        return "";
    }
    buf[0] = '\0';
    if (index >= g_count || option >= g_mods[index].option_count) {
        return buf;
    }
    o = &g_mods[index].options[option];
    if (o->kind == BW_MOD_SETTING_BOOL) {
        snprintf(buf, size, "%s", o->value ? "on" : "off");
    } else if (o->choice_count > 0) {
        int choice = o->value;

        if (choice < 0 || choice >= o->choice_count) {
            choice = 0;
        }
        snprintf(buf, size, "%s", o->choices[choice]);
    }
    return buf;
}

/* ------------------------------------------------------------------ */
/* The mod that is calling                                             */
/* ------------------------------------------------------------------ */

void bw_mod_menu_set_namespace(const char *ns)
{
    if (ns == NULL || ns[0] == '\0') {
        g_namespace[0] = '\0';
        return;
    }
    copy_str(g_namespace, sizeof g_namespace, ns);
}

const char *bw_mod_menu_namespace(void)
{
    return g_namespace;
}

/*
 * The services table's setting_get (docs/mod-services.md, "Mod settings").
 * The loader does not bind the call into the services table yet, so a mod
 * that calls it gets nothing; when it does, it binds this one and sets the
 * namespace around the calls it makes into the mod.
 */
int bw_mod_menu_setting_get(const char *key)
{
    BwModMenuMod *m;
    u32 i;

    if (key == NULL || g_namespace[0] == '\0') {
        return -1;
    }
    model_ready();
    m = model_find_ns(g_namespace);
    if (m == NULL) {
        return -1;
    }
    for (i = 0; i < m->option_count; i++) {
        if (strcmp(m->options[i].key, key) == 0) {
            return m->options[i].value;
        }
    }
    return -1;
}
