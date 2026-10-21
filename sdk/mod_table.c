/*
 * sdk/mod_table.c -- a mod's own data tables (mod_table.h).
 *
 * A table is a sorted array of (key, value) pairs and a default; a read is
 * a binary search.  The tables are read once, when the loader loads the
 * mod, and live until the mod list changes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_table.h"

typedef struct TableEntry {
    s32 key;
    s32 value;
} TableEntry;

typedef struct ModTable {
    char        ns[96];
    char        name[BW_MOD_TABLE_NAME_MAX];
    TableEntry *entries;
    u32         count;
    s32         fallback;   /* the `default` line, or 0 */
} ModTable;

static ModTable g_tables[BW_MOD_TABLES_MAX];
static u32      g_table_count;

/* The entries a file may hold: a key per species, item or zone is a few
 * thousand; this leaves room for a map of every u16. */
#define TABLE_ENTRIES_MAX 0x10000u

extern const char *bw_mod_savedata_namespace(void);

/* ------------------------------------------------------------------ */
/* Reading a file                                                      */
/* ------------------------------------------------------------------ */

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || n > (1L << 24) ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* A signed number, decimal or 0x hex, the whole of [s, e). */
static int parse_s32(const char *s, const char *e, s32 *out)
{
    int neg = 0;
    unsigned long long v = 0;
    unsigned base = 10;

    if (s < e && (*s == '-' || *s == '+')) {
        neg = *s == '-';
        s++;
    }
    if (e - s > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    if (s >= e) {
        return -1;
    }
    for (; s < e; s++) {
        unsigned d;

        if (*s >= '0' && *s <= '9') {
            d = (unsigned)(*s - '0');
        } else if (base == 16 && *s >= 'a' && *s <= 'f') {
            d = (unsigned)(*s - 'a' + 10);
        } else if (base == 16 && *s >= 'A' && *s <= 'F') {
            d = (unsigned)(*s - 'A' + 10);
        } else {
            return -1;
        }
        v = v * base + d;
        if (v > 0x80000000ull) {
            return -1;
        }
    }
    if (!neg && v > 0x7FFFFFFFull) {
        return -1;
    }
    *out = neg ? (s32)(0 - (s64)v) : (s32)v;
    return 0;
}

/*
 * Parse the text into `t`.  Duplicate keys: the later line wins.  qsort is
 * not stable, so the line number is the sort's tie-break, and a pass after
 * the sort keeps the last line of each run of one key.
 */
typedef struct LineEntry {
    s32 key;
    s32 value;
    u32 line;
} LineEntry;

static int cmp_line_entry(const void *a, const void *b)
{
    const LineEntry *x = (const LineEntry *)a;
    const LineEntry *y = (const LineEntry *)b;

    if (x->key != y->key) {
        return x->key < y->key ? -1 : 1;
    }
    return x->line < y->line ? -1 : (x->line > y->line ? 1 : 0);
}

static int parse_table(const char *text, ModTable *t, const char *label,
                       const char *path)
{
    LineEntry *lines = NULL;
    u32 n = 0, cap = 0, lineno = 0, i, out;
    const char *p = text;

    t->fallback = 0;
    while (*p != '\0') {
        const char *s = p, *e, *eq, *ke, *vs;
        s32 key, value;

        while (*p != '\0' && *p != '\n') {
            p++;
        }
        e = p;
        if (*p == '\n') {
            p++;
        }
        lineno++;
        {
            const char *h = memchr(s, '#', (size_t)(e - s));

            if (h != NULL) {
                e = h;
            }
        }
        while (s < e && is_space(*s)) {
            s++;
        }
        while (e > s && is_space(e[-1])) {
            e--;
        }
        if (s == e) {
            continue;
        }
        eq = memchr(s, '=', (size_t)(e - s));
        if (eq == NULL) {
            fprintf(stderr, "mod %s: %s line %u: '=' is missing\n", label, path,
                    (unsigned)lineno);
            free(lines);
            return -1;
        }
        ke = eq;
        while (ke > s && is_space(ke[-1])) {
            ke--;
        }
        vs = eq + 1;
        while (vs < e && is_space(*vs)) {
            vs++;
        }
        if (parse_s32(vs, e, &value) != 0) {
            fprintf(stderr, "mod %s: %s line %u: the value is not a number\n",
                    label, path, (unsigned)lineno);
            free(lines);
            return -1;
        }
        if ((size_t)(ke - s) == 7 && memcmp(s, "default", 7) == 0) {
            t->fallback = value;
            continue;
        }
        if (parse_s32(s, ke, &key) != 0) {
            fprintf(stderr, "mod %s: %s line %u: the key is not a number\n",
                    label, path, (unsigned)lineno);
            free(lines);
            return -1;
        }
        if (n == cap) {
            u32 ncap = cap == 0 ? 64u : cap * 2u;
            LineEntry *grown;

            if (ncap > TABLE_ENTRIES_MAX) {
                fprintf(stderr, "mod %s: %s has more than %u entries\n", label,
                        path, (unsigned)TABLE_ENTRIES_MAX);
                free(lines);
                return -1;
            }
            grown = (LineEntry *)realloc(lines, (size_t)ncap * sizeof(*grown));
            if (grown == NULL) {
                free(lines);
                return -1;
            }
            lines = grown;
            cap = ncap;
        }
        lines[n].key = key;
        lines[n].value = value;
        lines[n].line = lineno;
        n++;
    }

    t->entries = NULL;
    t->count = 0;
    if (n == 0) {
        free(lines);
        return 0;
    }
    qsort(lines, n, sizeof(*lines), cmp_line_entry);
    t->entries = (TableEntry *)malloc((size_t)n * sizeof(*t->entries));
    if (t->entries == NULL) {
        free(lines);
        return -1;
    }
    out = 0;
    for (i = 0; i < n; i++) {
        if (i + 1 < n && lines[i + 1].key == lines[i].key) {
            continue;   /* a later line has the same key */
        }
        t->entries[out].key = lines[i].key;
        t->entries[out].value = lines[i].value;
        out++;
    }
    t->count = out;
    free(lines);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The registry                                                        */
/* ------------------------------------------------------------------ */

static ModTable *find(const char *ns, const char *name)
{
    u32 i;

    if (ns == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < g_table_count; i++) {
        if (strcmp(g_tables[i].ns, ns) == 0 && strcmp(g_tables[i].name, name) == 0) {
            return &g_tables[i];
        }
    }
    return NULL;
}

int bw_mod_table_load(const char *ns, const char *name, const char *path,
                      const char *label)
{
    ModTable t;
    ModTable *slot;
    char *text;

    if (label == NULL) {
        label = ns != NULL ? ns : "";
    }
    if (ns == NULL || *ns == '\0' || name == NULL || *name == '\0' ||
        path == NULL) {
        return -1;
    }
    if (strlen(name) >= BW_MOD_TABLE_NAME_MAX || strlen(ns) >= sizeof t.ns) {
        fprintf(stderr, "mod %s: the table name '%s' is too long\n", label, name);
        return -1;
    }
    text = read_text(path);
    if (text == NULL) {
        fprintf(stderr, "mod %s: the table '%s' cannot be read from %s\n",
                label, name, path);
        return -1;
    }
    memset(&t, 0, sizeof t);
    if (parse_table(text, &t, label, path) != 0) {
        free(text);
        return -1;
    }
    free(text);
    snprintf(t.ns, sizeof t.ns, "%s", ns);
    snprintf(t.name, sizeof t.name, "%s", name);

    slot = find(ns, name);
    if (slot != NULL) {
        free(slot->entries);
    } else {
        if (g_table_count >= BW_MOD_TABLES_MAX) {
            fprintf(stderr, "mod %s: no room for the table '%s'\n", label, name);
            free(t.entries);
            return -1;
        }
        slot = &g_tables[g_table_count++];
    }
    *slot = t;
    return 0;
}

int bw_mod_table_exists(const char *ns, const char *name)
{
    return find(ns, name) != NULL;
}

int bw_mod_table_get(const char *ns, const char *name, s32 key, s32 *out)
{
    const ModTable *t = find(ns, name);
    u32 lo, hi;

    if (out != NULL) {
        *out = 0;
    }
    if (t == NULL) {
        return -1;
    }
    lo = 0;
    hi = t->count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2u;

        if (t->entries[mid].key == key) {
            if (out != NULL) {
                *out = t->entries[mid].value;
            }
            return 0;
        }
        if (t->entries[mid].key < key) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (out != NULL) {
        *out = t->fallback;
    }
    return 1;
}

int bw_mod_table_get_calling(const char *name, s32 key, s32 *out)
{
    return bw_mod_table_get(bw_mod_savedata_namespace(), name, key, out);
}

void bw_mod_table_reset(void)
{
    u32 i;

    for (i = 0; i < g_table_count; i++) {
        free(g_tables[i].entries);
    }
    memset(g_tables, 0, sizeof g_tables);
    g_table_count = 0;
}
