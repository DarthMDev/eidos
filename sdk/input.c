/*
 * sdk/input.c -- the buttons, the hinge and the stylus, from the host and a
 * script, onto KEYINPUT and the ARM7 stand-in.
 *
 * Why this exists: sdk/input.h.  It is port glue, not an SDK module: the
 * SDK reads input straight from hardware (KEYINPUT, and two halfwords the
 * ARM7 writes), so what the port has to supply is that hardware's state.
 * KEYINPUT is an ARM9 register and is written here; the XY halfword and the
 * touch samples are the ARM7's to write, so this file only hands the ARM7
 * stand-in its frame (sdk/arm7/pad.c, sdk/arm7/tp.c), and they read the
 * state back through bw_input_current, as the ARM7 reads its pins.
 *
 * Threads.  No locks: the platform layer's pump, the script and the frame
 * driver all run on the thread that drives frames, and the game threads only
 * ever see the result through the register file and main RAM, which the
 * frame driver writes while it holds the run token (docs/boot.md).
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm7/arm7.h"
#include "dsmem.h"
#include "input.h"

enum { EV_PRESS, EV_RELEASE, EV_TOUCH, EV_UNTOUCH, EV_LID };

typedef struct InputEvent {
    u32 frame;
    u32 seq;            /* insertion order, the tie-break within a frame    */
    int kind;
    u32 buttons;
    int x, y;
    int closed;
} InputEvent;

static struct {
    BwInputState host;
    BwInputState current;

    u32 frame;
    u32 seq;

    /* What the script holds.                                              */
    u32 s_buttons;
    int s_touch, s_x, s_y;
    int s_lid;

    /* Events not yet applied, sorted by (frame, seq).                     */
    InputEvent *ev;
    u32 ev_count;
    u32 ev_cap;
} g_in;

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

static const struct { const char *name; u32 bit; } k_names[] = {
    { "A", BW_KEY_A },         { "B", BW_KEY_B },
    { "SELECT", BW_KEY_SELECT }, { "START", BW_KEY_START },
    { "RIGHT", BW_KEY_RIGHT }, { "LEFT", BW_KEY_LEFT },
    { "UP", BW_KEY_UP },       { "DOWN", BW_KEY_DOWN },
    { "R", BW_KEY_R },         { "L", BW_KEY_L },
    { "X", BW_KEY_X },         { "Y", BW_KEY_Y },
    { "DEBUG", BW_KEY_DEBUG },
};

#define N_NAMES (sizeof k_names / sizeof k_names[0])

static int name_eq(const char *a, const char *b, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (b[i] == '\0' ||
            toupper((unsigned char)a[i]) != (unsigned char)b[i])
            return 0;
    }
    return b[n] == '\0';
}

static u32 button_by_name_n(const char *name, size_t n)
{
    size_t i;

    for (i = 0; i < N_NAMES; i++) {
        if (name_eq(name, k_names[i].name, n))
            return k_names[i].bit;
    }
    return 0;
}

u32 bw_input_button_by_name(const char *name)
{
    return name ? button_by_name_n(name, strlen(name)) : 0;
}

const char *bw_input_button_name(u32 button)
{
    size_t i;

    for (i = 0; i < N_NAMES; i++) {
        if (k_names[i].bit == button)
            return k_names[i].name;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void merge(void)
{
    BwInputState *c = &g_in.current;

    c->buttons = (g_in.host.buttons | g_in.s_buttons) & BW_KEY_ALL;
    if (g_in.s_touch) {
        c->touch = 1;
        c->touch_x = g_in.s_x;
        c->touch_y = g_in.s_y;
    } else if (g_in.host.touch) {
        c->touch = 1;
        c->touch_x = clampi(g_in.host.touch_x, 0, BW_TOUCH_WIDTH - 1);
        c->touch_y = clampi(g_in.host.touch_y, 0, BW_TOUCH_HEIGHT - 1);
    } else {
        c->touch = 0;
        c->touch_x = 0;
        c->touch_y = 0;
    }
    c->lid_closed = (g_in.host.lid_closed || g_in.s_lid) ? 1 : 0;
}

void bw_input_current(BwInputState *out)
{
    if (out)
        *out = g_in.current;
}

void bw_input_set_host(const BwInputState *s)
{
    if (s)
        g_in.host = *s;
    else
        memset(&g_in.host, 0, sizeof g_in.host);
}

static void write_hardware(void)
{
    /* KEYINPUT is active low; bits 10-15 read 0 on the ARM9.              */
    bw_io_write16(BW_REG_KEYINPUT,
                  (u16)(~g_in.current.buttons & BW_KEY_KEYINPUT));
    bw_arm7_pad_frame();
}

void bw_input_publish(void)
{
    merge();
    write_hardware();
}

/* ------------------------------------------------------------------ */
/* The script                                                          */
/* ------------------------------------------------------------------ */

static int add_event(const InputEvent *e)
{
    InputEvent n = *e;
    u32 i;

    if (g_in.ev_count == g_in.ev_cap) {
        u32 cap = g_in.ev_cap ? g_in.ev_cap * 2u : 64u;
        InputEvent *p = (InputEvent *)realloc(g_in.ev, cap * sizeof *p);

        if (!p)
            return -1;
        g_in.ev = p;
        g_in.ev_cap = cap;
    }
    n.seq = g_in.seq++;
    /* Insertion keeps (frame, seq) order; seq only grows, so a new event
     * goes after every event of its own frame.                            */
    i = g_in.ev_count;
    while (i > 0 && g_in.ev[i - 1].frame > n.frame) {
        g_in.ev[i] = g_in.ev[i - 1];
        i--;
    }
    g_in.ev[i] = n;
    g_in.ev_count++;
    return 0;
}

static int add(int kind, u32 frame, u32 buttons, int x, int y, int closed)
{
    InputEvent e;

    memset(&e, 0, sizeof e);
    e.kind = kind;
    e.frame = frame;
    e.buttons = buttons;
    e.x = x;
    e.y = y;
    e.closed = closed;
    return add_event(&e);
}

static int valid_buttons(u32 b)
{
    return b != 0 && (b & ~BW_KEY_ALL) == 0;
}

static int valid_point(int x, int y)
{
    return x >= 0 && x < BW_TOUCH_WIDTH && y >= 0 && y < BW_TOUCH_HEIGHT;
}

int bw_input_script_press(u32 frame, u32 buttons, u32 hold)
{
    if (!valid_buttons(buttons) || (hold && frame + hold < frame))
        return -1;
    if (add(EV_PRESS, frame, buttons, 0, 0, 0) != 0)
        return -1;
    return hold ? add(EV_RELEASE, frame + hold, buttons, 0, 0, 0) : 0;
}

int bw_input_script_release(u32 frame, u32 buttons)
{
    if (!valid_buttons(buttons))
        return -1;
    return add(EV_RELEASE, frame, buttons, 0, 0, 0);
}

int bw_input_script_touch(u32 frame, int x, int y, u32 hold)
{
    if (!valid_point(x, y) || (hold && frame + hold < frame))
        return -1;
    if (add(EV_TOUCH, frame, 0, x, y, 0) != 0)
        return -1;
    return hold ? add(EV_UNTOUCH, frame + hold, 0, 0, 0, 0) : 0;
}

int bw_input_script_untouch(u32 frame)
{
    return add(EV_UNTOUCH, frame, 0, 0, 0, 0);
}

int bw_input_script_lid(u32 frame, int closed)
{
    return add(EV_LID, frame, 0, 0, 0, closed ? 1 : 0);
}

void bw_input_script_clear(void)
{
    g_in.ev_count = 0;
    g_in.s_buttons = 0;
    g_in.s_touch = 0;
    g_in.s_x = 0;
    g_in.s_y = 0;
    g_in.s_lid = 0;
}

u32 bw_input_script_pending(void)
{
    return g_in.ev_count;
}

static void apply(const InputEvent *e)
{
    switch (e->kind) {
    case EV_PRESS:
        g_in.s_buttons |= e->buttons;
        break;
    case EV_RELEASE:
        g_in.s_buttons &= ~e->buttons;
        break;
    case EV_TOUCH:
        g_in.s_touch = 1;
        g_in.s_x = e->x;
        g_in.s_y = e->y;
        break;
    case EV_UNTOUCH:
        g_in.s_touch = 0;
        break;
    case EV_LID:
        g_in.s_lid = e->closed;
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* The text form                                                       */
/* ------------------------------------------------------------------ */

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r')
        p++;
    return p;
}

/* One whitespace-delimited token: start in *tok, length returned.         */
static size_t token(const char **pp, const char **tok)
{
    const char *p = skip_ws(*pp);
    const char *s = p;

    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' &&
           *p != '#')
        p++;
    *tok = s;
    *pp = p;
    return (size_t)(p - s);
}

static int parse_uint(const char *s, size_t n, u32 *out)
{
    u32 v = 0;
    size_t i;

    if (n == 0 || n > 10)
        return -1;
    for (i = 0; i < n; i++) {
        u32 d;

        if (s[i] < '0' || s[i] > '9')
            return -1;
        d = (u32)(s[i] - '0');
        if (v > (0xFFFFFFFFu - d) / 10u)
            return -1;
        v = v * 10u + d;
    }
    *out = v;
    return 0;
}

static int parse_buttons(const char *s, size_t n, u32 *out)
{
    u32 word = 0;
    size_t i = 0;

    while (i < n) {
        size_t j = i;
        u32 b;

        while (j < n && s[j] != '+')
            j++;
        b = button_by_name_n(s + i, j - i);
        if (b == 0)
            return -1;
        word |= b;
        i = j + 1;
        if (j < n && i == n)
            return -1;                  /* a trailing '+'                  */
    }
    if (word == 0)
        return -1;
    *out = word;
    return 0;
}

static int word_is(const char *s, size_t n, const char *w)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (w[i] == '\0' || tolower((unsigned char)s[i]) != w[i])
            return 0;
    }
    return w[n] == '\0';
}

/* Parse one line into up to two events.  0 ok (count in *n), -1 bad.       */
static int parse_line(const char *p, InputEvent ev[2], int *n,
                      const char **why)
{
    const char *t;
    size_t len;
    u32 frame, a, b, hold = 0;

    *n = 0;
    len = token(&p, &t);
    if (len == 0)
        return 0;                       /* blank or comment                */
    if (parse_uint(t, len, &frame) != 0) {
        *why = "expected a frame number";
        return -1;
    }
    memset(ev, 0, 2 * sizeof ev[0]);
    ev[0].frame = frame;
    len = token(&p, &t);
    if (word_is(t, len, "press") || word_is(t, len, "release")) {
        int press = word_is(t, len, "press");

        len = token(&p, &t);
        if (parse_buttons(t, len, &a) != 0) {
            *why = "expected button names joined by '+'";
            return -1;
        }
        ev[0].kind = press ? EV_PRESS : EV_RELEASE;
        ev[0].buttons = a;
        *n = 1;
        if (press) {
            len = token(&p, &t);
            if (len && (parse_uint(t, len, &hold) != 0 || hold == 0 ||
                        frame + hold < frame)) {
                *why = "bad hold count";
                return -1;
            }
            if (hold) {
                ev[1].kind = EV_RELEASE;
                ev[1].frame = frame + hold;
                ev[1].buttons = a;
                *n = 2;
            }
        }
    } else if (word_is(t, len, "touch")) {
        len = token(&p, &t);
        if (parse_uint(t, len, &a) != 0 || a >= BW_TOUCH_WIDTH) {
            *why = "touch x must be 0..255";
            return -1;
        }
        len = token(&p, &t);
        if (parse_uint(t, len, &b) != 0 || b >= BW_TOUCH_HEIGHT) {
            *why = "touch y must be 0..191";
            return -1;
        }
        ev[0].kind = EV_TOUCH;
        ev[0].x = (int)a;
        ev[0].y = (int)b;
        *n = 1;
        len = token(&p, &t);
        if (len && (parse_uint(t, len, &hold) != 0 || hold == 0 ||
                    frame + hold < frame)) {
            *why = "bad hold count";
            return -1;
        }
        if (hold) {
            ev[1].kind = EV_UNTOUCH;
            ev[1].frame = frame + hold;
            *n = 2;
        }
    } else if (word_is(t, len, "untouch")) {
        ev[0].kind = EV_UNTOUCH;
        *n = 1;
    } else if (word_is(t, len, "lid")) {
        len = token(&p, &t);
        if (word_is(t, len, "open")) {
            ev[0].closed = 0;
        } else if (word_is(t, len, "closed")) {
            ev[0].closed = 1;
        } else {
            *why = "lid takes open or closed";
            return -1;
        }
        ev[0].kind = EV_LID;
        *n = 1;
    } else {
        *why = "unknown action (press, release, touch, untouch, lid)";
        return -1;
    }
    len = token(&p, &t);
    if (len != 0) {
        *why = "trailing text";
        return -1;
    }
    return 0;
}

int bw_input_script_load(const char *text, char *err, size_t err_size)
{
    InputEvent *list = NULL;
    size_t count = 0, cap = 0, i;
    const char *line = text;
    u32 lineno = 0;

    if (err && err_size)
        err[0] = '\0';
    if (!text)
        return -1;
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t llen = end ? (size_t)(end - line) : strlen(line);
        char buf[256];
        InputEvent ev[2];
        const char *why = "line too long";
        int n = 0;

        lineno++;
        if (llen < sizeof buf) {
            memcpy(buf, line, llen);
            buf[llen] = '\0';
        }
        if (llen >= sizeof buf || parse_line(buf, ev, &n, &why) != 0) {
            if (err && err_size)
                snprintf(err, err_size, "line %lu: %s",
                         (unsigned long)lineno, why);
            free(list);
            return -1;
        }
        if (count + (size_t)n > cap) {
            size_t ncap = cap ? cap * 2 : 32;
            InputEvent *p = (InputEvent *)realloc(list, ncap * sizeof *p);

            if (!p) {
                if (err && err_size)
                    snprintf(err, err_size, "out of memory");
                free(list);
                return -1;
            }
            list = p;
            cap = ncap;
        }
        for (i = 0; i < (size_t)n; i++)
            list[count++] = ev[i];
        line = end ? end + 1 : line + llen;
    }
    for (i = 0; i < count; i++) {
        if (add_event(&list[i]) != 0) {
            if (err && err_size)
                snprintf(err, err_size, "out of memory");
            free(list);
            return -1;
        }
    }
    free(list);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Life cycle and the frame                                            */
/* ------------------------------------------------------------------ */

void bw_input_reset(void)
{
    free(g_in.ev);
    memset(&g_in, 0, sizeof g_in);
}

u32 bw_input_frame_number(void)
{
    return g_in.frame;
}

void bw_input_frame(void)
{
    u32 done = 0, i;

    while (done < g_in.ev_count && g_in.ev[done].frame <= g_in.frame)
        apply(&g_in.ev[done++]);
    if (done) {
        for (i = done; i < g_in.ev_count; i++)
            g_in.ev[i - done] = g_in.ev[i];
        g_in.ev_count -= done;
    }
    merge();
    write_hardware();
    bw_arm7_tp_frame();
    g_in.frame++;
}
