/*
 * engine/platform/settings_ui.c -- see settings_ui.h.
 */
#include "settings_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "mod_menu.h"
#include "mod_net.h"

/*
 * The port's own network settings (engine/platform/settings.c): the
 * relay's address, the relay's port, the control's port and the player's
 * name.  They are declared here because settings.h does not carry them
 * yet: the header is not this wave's file.  The screen shows them and the
 * player changes them there; the boot hands them to the network service
 * at the port's start-up (sdk/mod_net.h).
 */
extern const char *eidos_settings_net_relay(void);
extern void        eidos_settings_net_set_relay(const char *host);
extern int         eidos_settings_net_relay_port(void);
extern void        eidos_settings_net_set_relay_port(int port);
extern int         eidos_settings_net_control_port(void);
extern void        eidos_settings_net_set_control_port(int port);
extern const char *eidos_settings_net_name(void);
extern void        eidos_settings_net_set_name(const char *name);
extern void        eidos_settings_net_relay_text_begin(void);
extern void        eidos_settings_net_name_text_begin(void);
extern int         eidos_settings_net_text_editing(void);
extern int         eidos_settings_net_text_key(int scancode);
extern void        eidos_settings_net_text_cancel(void);
extern void        eidos_settings_net_defaults(void);
extern void        eidos_settings_net_revert(void);

/*
 * The port's own session (engine/platform/settings.c): the session's code
 * is a setting of the port's own, so it is read, changed, reverted and
 * committed with the rest of the settings, and the file keeps it
 * ([session], code).  The calls the screen makes on the peer session
 * service are the port's own too (sdk/mod_net.h, "The port's own
 * session").
 */

/*
 * The port's own run-status panel and the input injection's limit: the
 * settings' model holds them (engine/platform/settings.c), and the screen
 * shows them and the player changes them there.
 */

/* ================================================================== */
/* UI TEXT: every string the settings screen shows.  ASCII only.       */
/* A translation or a mod replaces this block.                         */
/* ================================================================== */

#define STR_TITLE "Settings"
static const char *const k_tab_names[] = {
    "Display", "Layout", "Controls", "Sound", "Mods", "Network", "Session"
};

/* Short button names for the Controls rows (the backend labels are long). */
static const char *const k_button_short[] = {
    "A", "B", "X", "Y", "L", "R", "Start", "Select", "Up", "Down", "Left", "Right"
};

/* Short gamepad control names: the value column is narrow.  Indexes match
 * the backend's EIDOS_PADBIND_BUTTON order (SDL game controller buttons). */
static const char *const k_pad_short[] = {
    "Pad S", "Pad E", "Pad W", "Pad N", "Back", "Guide", "Start", "LS Click",
    "RS Click", "LB", "RB", "DP Up", "DP Down", "DP Left", "DP Right"
};
static const char *const k_axis_pos_short[] = { "LS Right", "LS Down", "RS Right", "RS Down", "LT", "RT" };
static const char *const k_axis_neg_short[] = { "LS Left", "LS Up", "RS Left", "RS Up", "LT -", "RT -" };

/* Help text (bottom screen).  Two lines per option id, then fixed texts. */
static const char *const k_option_help[EIDOS_OPT_COUNT][2] = {
    { "How the game window is shown:", "windowed, fullscreen or borderless." },
    { "Size of the 3D and 2D rendering.", "Fit follows the window size." },
    { NULL, NULL }, { NULL, NULL },
    { "Put the two screens above each", "other or side by side." },
    { "Show the touch screen on top", "and the upper screen below." },
    { "Space between the two screens,", "in pixels." },
    { "Master volume, 0 to 100.", "Left and right change it by 5." },
};
#define HELP_BIND1     "Press A, then press the key or pad"
#define HELP_BIND2     "button to bind. B cancels."
#define HELP_DEFAULTS1 "Set every option and every control"
#define HELP_DEFAULTS2 "to its default."
#define HELP_CAPTURE1  "Waiting for a key or pad button."
#define HELP_CAPTURE2  "Press B to cancel."
#define HELP_SECTION_DISPLAY  "Window mode and resolution."
#define HELP_SECTION_LAYOUT   "Screen arrangement, swap and gap."
#define HELP_SECTION_CONTROLS "Keyboard and gamepad bindings."
#define HELP_SECTION_SOUND    "Master volume."
#define HELP_SECTION_MODS     "The mods, their settings and the panel."
#define HELP_SECTION_NETWORK  "The relay, the control and the press limit."
#define HELP_SECTION_SESSION  "The peer session: host, join, leave."
#define HELP_KEYS_L0   "Up/Down: choose   A: open"
#define HELP_KEYS_L0B  "B: save and go back"
#define HELP_KEYS_L1   "Up/Down: choose   Left/Right: change"
#define HELP_KEYS_L1B  "A: select   B: back"

/* The Mods section: the mods, their switches, their own settings, the
 * clear action and the folders no mod claims. */
#define HELP_MOD_SWITCH1 "Turn the mod on or off."
#define HELP_MOD_SWITCH2 "L and R move it in the list."
#define HELP_MOD_OPTION1 "The mod's own setting."
#define HELP_MOD_OPTION2 "The choice is kept with its data."
#define HELP_MOD_CLEAR1  "Delete the mod's saved data."
#define HELP_MOD_CLEAR2  "The mod's own folder stays."
#define HELP_MOD_REVERT1 "The switches go back as they were."
#define HELP_MOD_REVERT2 "The change applies when you leave."
#define HELP_ORPHAN1     "A folder no installed mod claims."
#define HELP_ORPHAN2     "A removed mod left it behind."

/* The Network section: the port's own settings for the network service. */
#define HELP_NET_RELAY1  "The relay the sessions reach."
#define HELP_NET_RELAY2  "Empty: the port has no relay."
#define HELP_NET_RPORT1  "The port the relay listens on."
#define HELP_NET_RPORT2  "0: no relay."
#define HELP_NET_CPORT1  "The port the control listens on."
#define HELP_NET_CPORT2  "0: the control is off."
#define HELP_NET_NAME1   "The name the other players see."
#define HELP_NET_NAME2   "Empty: the default name."
#define HELP_NET_DEFAULTS1 "Put the network settings back"
#define HELP_NET_DEFAULTS2 "to their defaults."
#define HELP_TEXT_EDIT1  "Type the text on the keyboard."
#define HELP_TEXT_EDIT2  "Enter keeps it, Esc gives it up."

/* The Session section: the port's own peer session (sdk/mod_net.h, "The
 * port's own session"). */
#define HELP_SESSION_CODE1  "The code of the session."
#define HELP_SESSION_CODE2  "Type the code to join one."
#define HELP_SESSION_HOST1  "Ask the relay for a session."
#define HELP_SESSION_HOST2  "The code comes back here."
#define HELP_SESSION_JOIN1  "Join with the code above."
#define HELP_SESSION_JOIN2  "The relay is asked at once."
#define HELP_SESSION_LEAVE1 "Leave the session."
#define HELP_SESSION_LEAVE2 "The other players are told."
#define HELP_SESSION_REVERT1 "The code goes back as it was."
#define HELP_SESSION_REVERT2 "The change applies when you leave."

/* The run-status panel (the Mods section) and the input injection's limit
 * (the Network section). */
#define HELP_PANEL1   "The run-status panel the mods fill."
#define HELP_PANEL2   "A hidden panel takes no space."
#define HELP_INJECT1  "The longest press the injection allows,"
#define HELP_INJECT2  "in frames. 0 turns it off."

/* Row labels by option id (EIDOS_OPT_*); NULL for hidden options. */
static const char *const k_option_labels[EIDOS_OPT_COUNT] = {
    "Window mode",     /* DISPLAY_MODE        */
    "Resolution",      /* DISPLAY_RESOLUTION  */
    NULL, NULL,        /* width, height: hidden */
    "Screen layout",   /* LAYOUT_ARRANGEMENT  */
    "Swap screens",    /* LAYOUT_SWAP         */
    "Screen gap",      /* LAYOUT_GAP          */
    "Master volume",   /* SOUND_VOLUME        */
};

/* Value names that differ from the backend's lowercase choice names:
 * {option id, choice index, text}. */
typedef struct ValueName { int id; int index; const char *text; } ValueName;
static const ValueName k_value_names[] = {
    { EIDOS_OPT_DISPLAY_MODE,        0, "Windowed" },
    { EIDOS_OPT_DISPLAY_MODE,        1, "Fullscreen" },
    { EIDOS_OPT_DISPLAY_MODE,        2, "Borderless" },
    { EIDOS_OPT_LAYOUT_ARRANGEMENT,  0, "Stacked" },
    { EIDOS_OPT_LAYOUT_ARRANGEMENT,  1, "Side by side" },
    { EIDOS_OPT_LAYOUT_SWAP,         0, "Off" },
    { EIDOS_OPT_LAYOUT_SWAP,         1, "On" },
};

/* Short names for the side-by-side choices (a choice has about 48 pixels). */
static const ValueName k_choice_names[] = {
    { EIDOS_OPT_DISPLAY_MODE,        0, "Window" },
    { EIDOS_OPT_DISPLAY_MODE,        1, "Full" },
    { EIDOS_OPT_DISPLAY_MODE,        2, "Border" },
    { EIDOS_OPT_LAYOUT_ARRANGEMENT,  0, "Stacked" },
    { EIDOS_OPT_LAYOUT_ARRANGEMENT,  1, "Wide" },
};

#define STR_FIT         "Fit"
#define STR_BACK_L0     "Done"
#define STR_BACK_L1     "Back"
#define HELP_BACK_L0    "Save the changes and leave"
#define HELP_BACK_L1    "Go back to the list of sections"
#define STR_DEFAULTS    "Restore defaults"
#define STR_PRESS_KEY   "Press a key"
#define STR_UNBOUND     "None"

/* The Mods and Network sections' own labels. */
#define STR_MOD_CLEAR   "  Clear saved data"
#define STR_MOD_REVERT  "Revert changes"
#define STR_ORPHAN      "orphan"
#define STR_NET_RELAY   "Relay address"
#define STR_NET_RPORT   "Relay port"
#define STR_NET_CPORT   "Control port"
#define STR_NET_NAME    "Player name"
#define STR_NONE        "(none)"

/* The Session section's own labels. */
#define STR_SESSION_CODE  "Session code"
#define STR_SESSION_HOST  "Host session"
#define STR_SESSION_JOIN  "Join session"
#define STR_SESSION_LEAVE "Leave session"
#define STR_SESSION_REVERT "Revert changes"

/* What the session's rows say about the work: the relay takes up to
 * three seconds to answer, so the row says so while it waits. */
#define STR_SESSION_WAIT "Waiting..."
#define STR_SESSION_IN   "In session"
#define STR_SESS_OK      "OK"
#define STR_SESS_NORELAY "No relay"
#define STR_SESS_BUSY    "Busy"
#define STR_SESS_NOSESS  "No session"
#define STR_SESS_REFUSED "Refused"
#define STR_SESS_BIG     "Too big"
#define STR_SESS_IO      "No answer"
#define STR_SESS_ARG     "Bad code"
#define STR_SESS_FAILED  "Failed"

/* The port's own rows: the run-status panel's switch (the Mods section)
 * and the input injection's limit (the Network section). */
#define STR_PANEL       "Status panel"
#define STR_INJECT      "Max press"
#define STR_OFF         "Off"
#define STR_ON          "On"

/* The step the injection's limit moves by: half a second at the DS's 60
 * fps.  The model clamps the value to the port's own ceiling (a minute),
 * so the row never writes a value the port will not take. */
#define INJECT_STEP     30

/* ================================================================== */

/* A text into a buffer the screen hands out. */
static void put(char *dst, int n, const char *s)
{
    if (n > 0) {
        snprintf(dst, (size_t)n, "%s", s);
    }
}

/*
 * The rows of the screen.  The first four kinds are the port's own
 * settings; the rest are the Mods section (the port's own panel switch and
 * the model of sdk/mod_menu.c), the Network section (the port's own
 * network settings and the input injection's limit) and the Session
 * section (the port's own peer session).
 */
enum {
    ROW_OPTION,           /* an option of the port's own                  */
    ROW_BIND,             /* a key binding                               */
    ROW_DEFAULTS,         /* "Restore defaults"                          */
    ROW_SECTION,          /* a section of the list                       */
    ROW_PANEL,            /* the run-status panel's switch               */
    ROW_MOD_SWITCH,       /* a mod's switch: arg is the mod              */
    ROW_MOD_OPTION,       /* a mod's own setting: arg the mod, arg2 it  */
    ROW_MOD_CLEAR,        /* "Clear saved data": arg is the mod          */
    ROW_MOD_REVERT,       /* the switches back to the port's list        */
    ROW_ORPHAN,           /* a saved-data folder no installed mod claims */
    ROW_NET_RELAY,        /* the relay's address (text)                  */
    ROW_NET_RELAY_PORT,   /* the relay's port (a number)                 */
    ROW_NET_CONTROL_PORT, /* the control's port (a number)               */
    ROW_NET_NAME,         /* the player's name (text)                    */
    ROW_INJECT_MAX,       /* the longest press the injection allows     */
    ROW_NET_DEFAULTS,     /* the network settings back to their default  */
    ROW_SESSION_CODE,     /* the session's code (text)                   */
    ROW_SESSION_HOST,     /* start a session                             */
    ROW_SESSION_JOIN,     /* join a session                              */
    ROW_SESSION_LEAVE,    /* leave the session                           */
    ROW_SESSION_REVERT,   /* the code back to the committed state        */
    ROW_SESSION_DEFAULTS  /* the code back to nothing                     */
};

typedef struct Row { int type; int arg; int arg2; } Row;

#define TAB_COUNT 7
#define TAB_MODS 4
#define TAB_NETWORK 5
#define TAB_SESSION 6

static struct {
    int level;        /* 0 section list, 1 inside a section      */
    int tab;          /* the section                             */
    int cursor;       /* index into the tab's rows                */
    int scroll;       /* first visible row                        */
    int capturing;    /* button index + 1, 0 when not capturing   */
    int editing;      /* a text of the Network section is edited  */
    int mods_open;    /* the Mods section was entered             */
    int wait_release; /* a capture just ended: ignore buttons until all are up */
    int hold_dir;     /* held direction bit being repeated        */
    int hold_frames;
    Row *rows;        /* the rows of the section                  */
    int nrows;
    int rowcap;
} g;

/* One more row.  The list grows as it needs: the Mods section holds a
 * row per mod, per setting and per folder. */
static void add_row(int type, int arg, int arg2)
{
    if (g.nrows >= g.rowcap) {
        int cap = g.rowcap != 0 ? g.rowcap * 2 : 32;
        Row *rows = (Row *)realloc(g.rows, (size_t)cap * sizeof(Row));

        if (rows == NULL) {
            return;                 /* the row is dropped */
        }
        g.rows = rows;
        g.rowcap = cap;
    }
    g.rows[g.nrows].type = type;
    g.rows[g.nrows].arg = arg;
    g.rows[g.nrows].arg2 = arg2;
    g.nrows++;
}

/*
 * The Mods section: the port's own panel switch, then the installed mods in
 * load order, each with its switch, its own settings under its name and the
 * clear action; then the folders in the save directory that no installed mod
 * claims.
 */
static void build_mods_rows(void)
{
    u32 count = bw_mod_menu_count();
    u32 i, o;

    /* The run-status panel is the port's own: the mods fill it, and the
     * player hides it from here (docs/mod-services.md, "Run-status panel"). */
    add_row(ROW_PANEL, 0, 0);
    for (i = 0; i < count; i++) {
        add_row(ROW_MOD_SWITCH, (int)i, 0);
        for (o = 0; o < bw_mod_menu_option_count(i); o++) {
            add_row(ROW_MOD_OPTION, (int)i, (int)o);
        }
        if (bw_mod_menu_namespace_at(i)[0] != '\0') {
            add_row(ROW_MOD_CLEAR, (int)i, 0);
        }
    }
    for (i = 0; i < bw_mod_menu_orphan_count(); i++) {
        add_row(ROW_ORPHAN, (int)i, 0);
    }
    add_row(ROW_MOD_REVERT, 0, 0);
}

/* The Network section: the port's own settings for the network service, and
 * the input injection's limit, which is the port's own too: a mod may ask
 * for a press, and the player says how long a press may be (0 is off). */
static void build_net_rows(void)
{
    add_row(ROW_NET_RELAY, 0, 0);
    add_row(ROW_NET_RELAY_PORT, 0, 0);
    add_row(ROW_NET_CONTROL_PORT, 0, 0);
    add_row(ROW_NET_NAME, 0, 0);
    add_row(ROW_INJECT_MAX, 0, 0);
    add_row(ROW_NET_DEFAULTS, 0, 0);
}

/*
 * The Session section: the port's own peer session (sdk/mod_net.h, "The
 * port's own session").  The code is a setting of the port's own, so it
 * is in the model with the rest of the settings and the file keeps it;
 * the three rows in the middle are the player's own actions.
 */
static void build_session_rows(void)
{
    add_row(ROW_SESSION_CODE, 0, 0);
    add_row(ROW_SESSION_HOST, 0, 0);
    add_row(ROW_SESSION_JOIN, 0, 0);
    add_row(ROW_SESSION_LEAVE, 0, 0);
    add_row(ROW_SESSION_REVERT, 0, 0);
    add_row(ROW_SESSION_DEFAULTS, 0, 0);
}

static void build_rows(void)
{
    int i;

    g.nrows = 0;
    if (g.level == 0) {
        for (i = 0; i < TAB_COUNT; i++) {
            add_row(ROW_SECTION, i, 0);
        }
        return;
    }
    switch (g.tab) {
    case 0:
        add_row(ROW_OPTION, EIDOS_OPT_DISPLAY_MODE, 0);
        add_row(ROW_OPTION, EIDOS_OPT_DISPLAY_RESOLUTION, 0);
        break;
    case 1:
        add_row(ROW_OPTION, EIDOS_OPT_LAYOUT_ARRANGEMENT, 0);
        add_row(ROW_OPTION, EIDOS_OPT_LAYOUT_SWAP, 0);
        add_row(ROW_OPTION, EIDOS_OPT_LAYOUT_GAP, 0);
        break;
    case 2:
        for (i = 0; i < eidos_settings_button_count(); i++) {
            add_row(ROW_BIND, i, 0);
        }
        break;
    case 3:
        add_row(ROW_OPTION, EIDOS_OPT_SOUND_VOLUME, 0);
        break;
    case TAB_MODS:
        build_mods_rows();
        return;
    case TAB_NETWORK:
        build_net_rows();
        return;
    case TAB_SESSION:
        build_session_rows();
        return;
    default:
        break;
    }
    add_row(ROW_DEFAULTS, 0, 0);
}

/* The cursor runs over the rows and then one more place: the grey bar
 * ("Done" or "Back"), always shown below the visible rows. */
static int on_back(void)
{
    return g.cursor >= g.nrows;
}

static void clamp_scroll(void)
{
    if (on_back()) {
        return;
    }
    if (g.cursor < g.scroll) {
        g.scroll = g.cursor;
    }
    if (g.cursor >= g.scroll + EIDOS_SUI_VISIBLE) {
        g.scroll = g.cursor - EIDOS_SUI_VISIBLE + 1;
    }
    if (g.scroll > g.nrows - EIDOS_SUI_VISIBLE) {
        g.scroll = g.nrows - EIDOS_SUI_VISIBLE;
    }
    if (g.scroll < 0) {
        g.scroll = 0;
    }
}

void eidos_sui_enter(void)
{
    Row *rows = g.rows;
    int rowcap = g.rowcap;

    memset(&g, 0, sizeof g);
    g.rows = rows;               /* the list's own storage is kept */
    g.rowcap = rowcap;
    build_rows();
}

void eidos_sui_leave(void)
{
    if (g.capturing) {
        eidos_settings_capture_cancel();
        g.capturing = 0;
    }
    if (g.editing) {
        /* The text being edited is given up: the value before the edit
         * stands. */
        eidos_settings_net_text_cancel();
        g.editing = 0;
    }
    if (g.mods_open) {
        /* The mods the player changed: the switches apply now, when the
         * player leaves the menu (sdk/mod_menu.h). */
        (void)bw_mod_menu_apply();
        g.mods_open = 0;
    }
    eidos_settings_commit();
}

static void enter_section(int t)
{
    g.tab = t;
    g.level = 1;
    g.cursor = 0;
    g.scroll = 0;
    if (t == TAB_MODS) {
        /* The Mods section: the model is built again from the port's list
         * and the player's record, the way sdk/mod_menu.h says: the
         * screen calls it once, when the section is entered. */
        bw_mod_menu_open();
        g.mods_open = 1;
    }
    build_rows();
}

static void leave_section(void)
{
    g.level = 0;
    g.cursor = g.tab;
    g.scroll = 0;
    build_rows();
    clamp_scroll();
}

static void move_cursor(int d)
{
    g.cursor = (g.cursor + d + g.nrows + 1) % (g.nrows + 1);
    clamp_scroll();
}

/* Left or right on an option row (d = -1 or +1). */
static int change_option(int id, int d, int wrap)
{
    const EidosOption *o = eidos_settings_option(id);
    int v, nv;
    if (o == NULL) {
        return 0;
    }
    v = eidos_settings_get(id);
    nv = v + d * (o->step > 0 ? o->step : 1);
    if (nv > o->max) {
        nv = wrap ? o->min : o->max;
    } else if (nv < o->min) {
        nv = wrap ? o->max : o->min;
    }
    if (nv == v) {
        return 0;
    }
    return eidos_settings_set(id, nv) == 0;
}

/*
 * A mod's own setting: a bool turns on and off, a choice steps through
 * its names and wraps around.  The choice is kept beside the mod's saved
 * data at once (sdk/mod_menu.h).
 */
static int change_mod_option(int mod, int option, int d)
{
    int kind = bw_mod_menu_option_kind((u32)mod, (u32)option);
    int count = bw_mod_menu_option_choice_count((u32)mod, (u32)option);
    int value = bw_mod_menu_option_value((u32)mod, (u32)option);

    if (kind == BW_MOD_SETTING_BOOL) {
        value = value != 0 ? 0 : 1;
    } else {
        if (count <= 0) {
            return 0;
        }
        value += d;
        if (value < 0) {
            value = count - 1;
        } else if (value >= count) {
            value = 0;
        }
    }
    return bw_mod_menu_option_set((u32)mod, (u32)option, value) == 0;
}

/* The port's own port number: the relay's or the control's. */
static void change_net_port(int type, int d)
{
    if (type == ROW_NET_RELAY_PORT) {
        eidos_settings_net_set_relay_port(eidos_settings_net_relay_port() + d);
    } else {
        eidos_settings_net_set_control_port(eidos_settings_net_control_port() + d);
    }
}

/* ------------------------------------------------------------------ */
/* The port's own session                                              */
/* ------------------------------------------------------------------ */

/*
 * What the screen shows of the port's own session.  The state itself is
 * the port's own (sdk/mod_net.h, "The port's own session"); the screen
 * keeps the last answer, so it can say what happened.
 */
static struct {
    int busy;        /* the work is running                     */
    int have;        /* there is an answer                      */
    int rc;          /* the answer of the last work             */
    int host;        /* the last work was a host                */
} g_sess;

/*
 * Ask the port's own session how it is doing.  The relay takes up to
 * three seconds to answer, so the work runs on the port's own worker and
 * the screen asks each frame; the answer is taken once, and the code the
 * relay handed out goes into the model, where the row shows it and the
 * player reads it to the other players.
 *
 * Returns non-zero when the screen has to draw again.
 */
static int poll_session(void)
{
    BwPortSessionState st;
    int redraw = 0;

    memset(&st, 0, sizeof st);
    (void)bw_port_session_poll(&st);
    if (st.busy != g_sess.busy) {
        g_sess.busy = st.busy;
        redraw = 1;
    }
    if (st.done) {
        g_sess.have = 1;
        g_sess.rc = st.rc;
        g_sess.host = st.host;
        if (st.rc == BW_MOD_NET_OK && st.code[0] != '\0') {
            eidos_settings_session_set_code(st.code);
        }
        redraw = 1;
    }
    return redraw;
}

/* The service's own answers, in the screen's own words. */
static const char *session_answer_text(int rc)
{
    switch (rc) {
    case BW_MOD_NET_OK:           return STR_SESS_OK;
    case BW_MOD_NET_E_NO_RELAY:   return STR_SESS_NORELAY;
    case BW_MOD_NET_E_BUSY:       return STR_SESS_BUSY;
    case BW_MOD_NET_E_NO_SESSION: return STR_SESS_NOSESS;
    case BW_MOD_NET_E_RELAY:      return STR_SESS_REFUSED;
    case BW_MOD_NET_E_TOO_BIG:    return STR_SESS_BIG;
    case BW_MOD_NET_E_IO:         return STR_SESS_IO;
    case BW_MOD_NET_E_ARG:        return STR_SESS_ARG;
    default:                      return STR_SESS_FAILED;
    }
}

/* The value of the Host and Join rows: the state of the work. */
static void session_value(int host, char *out, int n)
{
    if (n > 0) {
        out[0] = '\0';
    }
    if (g_sess.busy) {
        put(out, n, STR_SESSION_WAIT);
        return;
    }
    if (g_sess.have && g_sess.host == host) {
        put(out, n, session_answer_text(g_sess.rc));
    }
}

/*
 * The Session section's own rows.  Hosting and joining do not block the
 * frame thread: the port's own worker asks the relay and the screen
 * waits for the answer (poll_session).
 */
static int session_action(int type)
{
    switch (type) {
    case ROW_SESSION_HOST:
        if (bw_port_session_start(1, NULL) == 0) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
    case ROW_SESSION_JOIN: {
        const char *code = eidos_settings_session_code();

        if (code == NULL || code[0] == '\0') {
            /* no code to join with: the player types one first */
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        if (bw_port_session_start(0, code) == 0) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
    }
    case ROW_SESSION_LEAVE:
        /* The service's leave does not block. */
        bw_port_session_leave();
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
    case ROW_SESSION_REVERT:
        /* The code back to the state the last commit left. */
        eidos_settings_session_revert();
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
    case ROW_SESSION_DEFAULTS:
        /* The code back to nothing. */
        eidos_settings_session_defaults();
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
    default:
        break;
    }
    return 0;
}

/* The player's switch of a mod. */
static int toggle_mod(int mod)
{
    int on = bw_mod_menu_enabled_at((u32)mod);

    return bw_mod_menu_set_enabled((u32)mod, !on) == 0;
}

/* The row of a mod's switch in the list that is built now. */
static int mod_row(u32 mod)
{
    int i;

    for (i = 0; i < g.nrows; i++) {
        if (g.rows[i].type == ROW_MOD_SWITCH && (u32)g.rows[i].arg == mod) {
            return i;
        }
    }
    return -1;
}

/*
 * The player's own order: the order the mods load in is the order the
 * player chose (sdk/mod_menu.h, "The order").  L and R move the mod the
 * cursor is on; the list is built again and the cursor follows the mod.
 */
static int move_mod(int mod, int up)
{
    u32 index = (u32)mod;
    int row;

    if (up) {
        if (bw_mod_menu_move_up(index) != 0) {
            return 0;
        }
        index--;
    } else {
        if (bw_mod_menu_move_down(index) != 0) {
            return 0;
        }
        index++;
    }
    build_rows();
    row = mod_row(index);
    if (row >= 0) {
        g.cursor = row;
    }
    clamp_scroll();
    return 1;
}

/*
 * The text being edited: the player types on the host keyboard, and the
 * keys come in one at a time through the platform's capture, the same one
 * the key bindings use.
 */
static void begin_text_edit(int type)
{
    if (type == ROW_NET_RELAY) {
        eidos_settings_net_relay_text_begin();
    } else if (type == ROW_NET_NAME) {
        eidos_settings_net_name_text_begin();
    } else {
        eidos_settings_session_code_text_begin();
    }
    g.editing = 1;
    eidos_settings_capture_begin();
}

static int poll_capture(void)
{
    EidosCaptured c;
    int r;
    memset(&c, 0, sizeof c);
    r = eidos_settings_capture_poll(&c);
    if (r == EIDOS_CAPTURE_KEY) {
        eidos_settings_set_key(g.capturing - 1, 0, c.scancode);
    } else if (r == EIDOS_CAPTURE_PAD) {
        eidos_settings_set_pad(g.capturing - 1, 0, c.pad_kind, c.pad_index);
    } else if (r == EIDOS_CAPTURE_CANCELLED) {
        g.capturing = 0;
        g.wait_release = 1;
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
    } else {
        return 0;
    }
    g.capturing = 0;
    g.wait_release = 1;
    return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
}

/*
 * One key of the host keyboard goes into the text being edited.  The
 * capture is armed again for the next key, and the edit ends when the
 * player presses Enter (the value is kept) or Escape (it is dropped).
 */
static int poll_text(void)
{
    EidosCaptured c;
    int r, done = 0;

    memset(&c, 0, sizeof c);
    r = eidos_settings_capture_poll(&c);
    if (r == EIDOS_CAPTURE_KEY) {
        done = eidos_settings_net_text_key(c.scancode);
        eidos_settings_capture_begin();     /* the next key */
    } else if (r == EIDOS_CAPTURE_CANCELLED) {
        eidos_settings_net_text_cancel();
        done = 2;
    } else if (r == EIDOS_CAPTURE_NONE) {
        /* No keyboard to type on: the text ends with the value it has. */
        done = 1;
    } else {
        return 0;
    }
    if (done != 0) {
        eidos_settings_capture_cancel();
        g.editing = 0;
        g.wait_release = 1;
        return EIDOS_SUI_REDRAW |
               (done == 1 ? EIDOS_SUI_SFX_SELECT : EIDOS_SUI_SFX_BACK);
    }
    return EIDOS_SUI_REDRAW;
}

/* Direction repeat: the first press acts at once, then after 20 frames every
 * 3 frames while held. */
static unsigned repeat_dirs(unsigned pressed, unsigned held)
{
    static const unsigned dirs[] = { 0x40u, 0x80u, 0x10u, 0x20u };
    unsigned out = pressed & 0xF0u;
    unsigned h = held & 0xF0u;
    int i;
    for (i = 0; i < 4; i++) {
        if (h & dirs[i]) {
            if (g.hold_dir != (int)dirs[i]) {
                g.hold_dir = (int)dirs[i];
                g.hold_frames = 0;
            } else {
                g.hold_frames++;
                if (g.hold_frames >= 20 && (g.hold_frames - 20) % 3 == 0) {
                    out |= dirs[i];
                }
            }
            return out;
        }
    }
    g.hold_dir = 0;
    g.hold_frames = 0;
    return out;
}

/* The screen's own input: the buttons the player pressed. */
static int sui_input(unsigned pressed, unsigned held);

int eidos_sui_input(unsigned pressed, unsigned held)
{
    int res = sui_input(pressed, held);

    /*
     * The port's own session: the work runs on the port's own worker (the
     * relay takes up to three seconds to answer), so the screen asks for
     * the answer here and draws the screen again when it comes.
     */
    if (poll_session()) {
        res |= EIDOS_SUI_REDRAW;
    }
    return res;
}

static int sui_input(unsigned pressed, unsigned held)
{
    unsigned act;
    const Row *r;
    int res;

    if (g.editing) {
        return poll_text();
    }

    if (g.capturing) {
        res = poll_capture();
        if (g.capturing && (pressed & 2u)) {          /* B cancels when no platform arms the capture */
            eidos_settings_capture_cancel();
            g.capturing = 0;
            g.wait_release = 1;
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        }
        return res;
    }

    if (g.wait_release) {
        /* the press that set a key must not also act as that button */
        if ((pressed | held) == 0u) {
            g.wait_release = 0;
        }
        return 0;
    }

    act = repeat_dirs(pressed, held);
    if (pressed & 2u) {
        if (g.level == 0) {
            return EIDOS_SUI_EXIT | EIDOS_SUI_SFX_BACK;
        }
        leave_section();
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
    }
    if (act & 0x40u) {
        move_cursor(-1);
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
    }
    if (act & 0x80u) {
        move_cursor(1);
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
    }
    if (on_back()) {
        if (pressed & 1u) {
            if (g.level == 0) {
                return EIDOS_SUI_EXIT | EIDOS_SUI_SFX_SELECT;
            }
            leave_section();
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    r = &g.rows[g.cursor];
    if (r->type == ROW_SECTION) {
        if (pressed & 1u) {
            enter_section(r->arg);
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    if (r->type == ROW_OPTION) {
        int d = (act & 0x10u) ? 1 : (act & 0x20u) ? -1 : 0;
        const EidosOption *o = eidos_settings_option(r->arg);
        int choice = o != NULL && o->kind == EIDOS_OPTKIND_CHOICE;
        if (d == 0 && (pressed & 1u) && choice) {
            d = 1;
        }
        if (d != 0 && change_option(r->arg, d, choice)) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    if (r->type == ROW_PANEL) {
        /* The run-status panel's switch: left and right turn it on and off
         * alike, and the model's own value is what the drawing asks for. */
        int d = (act & 0x10u) ? 1 : (act & 0x20u) ? -1 : 0;
        if (d == 0 && (pressed & 1u)) {
            d = 1;
        }
        if (d != 0) {
            eidos_settings_panel_set_visible(!eidos_settings_panel_visible());
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    if (r->type == ROW_MOD_SWITCH) {
        int d = (act & 0x10u) ? 1 : (act & 0x20u) ? -1 : 0;
        if (d == 0 && (pressed & 1u)) {
            d = 1;
        }
        if (d != 0 && toggle_mod(r->arg)) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        /* The player's own order: L moves the mod towards the front of the
         * list, R towards the back. */
        if (pressed & 0x200u) {
            if (move_mod(r->arg, 1)) {
                return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
            }
        }
        if (pressed & 0x100u) {
            if (move_mod(r->arg, 0)) {
                return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;
            }
        }
        return 0;
    }
    if (r->type == ROW_MOD_OPTION) {
        int d = (act & 0x10u) ? 1 : (act & 0x20u) ? -1 : 0;
        if (d == 0 && (pressed & 1u)) {
            d = 1;
        }
        if (d != 0 && change_mod_option(r->arg, r->arg2, d)) {
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    if (r->type == ROW_NET_RELAY_PORT || r->type == ROW_NET_CONTROL_PORT) {
        int d = (act & 0x10u) ? 1 : (act & 0x20u) ? -1 : 0;
        if (d == 0 && (pressed & 1u)) {
            d = 1;
        }
        if (d != 0) {
            change_net_port(r->type, d);
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    if (r->type == ROW_INJECT_MAX) {
        /* The longest press the injection allows: the model's own value, in
         * frames.  0 turns the injection off. */
        int d = (act & 0x10u) ? 1 : (act & 0x20u) ? -1 : 0;
        if (d != 0) {
            int v = eidos_settings_input_inject_max() + d * INJECT_STEP;

            if (v != eidos_settings_input_inject_max()) {
                eidos_settings_input_set_inject_max(v);
                return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
        }
        return 0;
    }
    if (r->type == ROW_NET_RELAY || r->type == ROW_NET_NAME ||
        r->type == ROW_SESSION_CODE) {
        if (pressed & 1u) {
            begin_text_edit(r->type);
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        return 0;
    }
    if (r->type == ROW_SESSION_HOST || r->type == ROW_SESSION_JOIN ||
        r->type == ROW_SESSION_LEAVE || r->type == ROW_SESSION_REVERT ||
        r->type == ROW_SESSION_DEFAULTS) {
        if (pressed & 1u) {
            return session_action(r->type);
        }
        return 0;
    }
    if (pressed & 1u) {
        switch (r->type) {
        case ROW_BIND:
            eidos_settings_capture_begin();
            g.capturing = r->arg + 1;
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        case ROW_DEFAULTS:
            eidos_settings_defaults();
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        case ROW_MOD_CLEAR:
            /* The player's "clear saved data" for one mod: the mod's own
             * state file goes and its folder stays (sdk/mod_menu.h). */
            if (bw_mod_menu_clear((u32)r->arg) == 0) {
                return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
        case ROW_MOD_REVERT:
            /* The switches go back to the port's list, and the panel's own
             * switch goes back to the state the last commit left. */
            bw_mod_menu_revert();
            eidos_settings_panel_revert();
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        case ROW_NET_DEFAULTS:
            /* The network settings back to their defaults, and the
             * injection's limit with them: it is this section's own row. */
            eidos_settings_net_defaults();
            eidos_settings_input_defaults();
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        default:
            break;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */

int eidos_sui_row_count(void)
{
    int n = g.nrows - g.scroll;
    if (n > EIDOS_SUI_VISIBLE) {
        n = EIDOS_SUI_VISIBLE;
    }
    return 1 + n + 1;        /* the header, the visible rows, the grey bar */
}

int eidos_sui_cursor_row(void)
{
    if (on_back()) {
        return eidos_sui_row_count() - 1;
    }
    return 1 + g.cursor - g.scroll;
}

/* 1 when left and right change the row's value (an option row). */
int eidos_sui_row_adjustable(int row)
{
    const Row *r;

    if (row < 1 || row == eidos_sui_row_count() - 1
        || g.scroll + row - 1 >= g.nrows) {
        return 0;
    }
    r = &g.rows[g.scroll + row - 1];
    switch (r->type) {
    case ROW_OPTION:
    case ROW_MOD_OPTION:
    case ROW_PANEL:
    case ROW_INJECT_MAX:
    case ROW_NET_RELAY_PORT:
    case ROW_NET_CONTROL_PORT:
        return 1;
    default:
        return 0;
    }
}

/*
 * The stylus went down on a row (a row of eidos_sui_row_count()).  `zone`
 * says where: 0..2 one of the choices drawn on the row, EIDOS_SUI_ZONE_DEC
 * or _INC the arrows around a value, EIDOS_SUI_ZONE_ROW anywhere else.  The
 * cursor goes to the row.  A choice is set at once; the arrows act when
 * the row was already the selected one (they are only drawn there); a row
 * without a value acts as it does for A.  Returns what eidos_sui_input does.
 */
int eidos_sui_touch(int row, int zone)
{
    const Row *r;
    int was, res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_MOVE;

    if (g.wait_release) {
        return 0;
    }
    if (g.capturing) {
        eidos_settings_capture_cancel();
        g.capturing = 0;
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
    }
    if (g.editing) {
        /* The text is being edited: the keyboard is the player's now. */
        return 0;
    }
    if (row == eidos_sui_row_count() - 1) {
        if (g.level == 0) {
            return EIDOS_SUI_EXIT | EIDOS_SUI_SFX_SELECT;
        }
        leave_section();
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
    }
    if (row < 1 || g.scroll + row - 1 >= g.nrows) {
        return 0;
    }
    was = g.cursor == g.scroll + row - 1;
    g.cursor = g.scroll + row - 1;
    r = &g.rows[g.cursor];
    switch (r->type) {
    case ROW_SECTION:
        enter_section(r->arg);
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
    case ROW_OPTION: {
        const EidosOption *o = eidos_settings_option(r->arg);

        if (o == NULL) {
            break;
        }
        if (zone >= 0 && o->kind == EIDOS_OPTKIND_CHOICE && o->min + zone <= o->max) {
            if (eidos_settings_get(r->arg) != o->min + zone
                && eidos_settings_set(r->arg, o->min + zone) == 0) {
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
        } else if (was && (zone == EIDOS_SUI_ZONE_DEC || zone == EIDOS_SUI_ZONE_INC)) {
            if (change_option(r->arg, zone == EIDOS_SUI_ZONE_INC ? 1 : -1, 0)) {
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
        }
        break;
    }
    case ROW_PANEL:
        /* The switch is drawn as two choices, off and on. */
        if (zone >= 0 && zone <= 1) {
            eidos_settings_panel_set_visible(zone);
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        } else if (was) {
            eidos_settings_panel_set_visible(!eidos_settings_panel_visible());
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        break;
    case ROW_MOD_SWITCH:
        /* The switch is drawn as two choices, off and on. */
        if (zone >= 0 && zone <= 1) {
            if (bw_mod_menu_set_enabled((u32)r->arg, zone) == 0) {
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
        } else if (was && toggle_mod(r->arg)) {
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        break;
    case ROW_MOD_OPTION:
        if (was && (zone == EIDOS_SUI_ZONE_DEC || zone == EIDOS_SUI_ZONE_INC)) {
            if (change_mod_option(r->arg, r->arg2,
                                  zone == EIDOS_SUI_ZONE_INC ? 1 : -1)) {
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
        }
        break;
    case ROW_MOD_CLEAR:
        if (was) {
            if (bw_mod_menu_clear((u32)r->arg) == 0) {
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            } else {
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_BACK;
            }
        }
        break;
    case ROW_MOD_REVERT:
        if (was) {
            bw_mod_menu_revert();
            eidos_settings_panel_revert();
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        break;
    case ROW_NET_RELAY:
    case ROW_NET_NAME:
    case ROW_SESSION_CODE:
        if (was) {
            begin_text_edit(r->type);
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        break;
    case ROW_SESSION_HOST:
    case ROW_SESSION_JOIN:
    case ROW_SESSION_LEAVE:
    case ROW_SESSION_REVERT:
    case ROW_SESSION_DEFAULTS:
        if (was) {
            res = session_action(r->type);
        }
        break;
    case ROW_NET_RELAY_PORT:
    case ROW_NET_CONTROL_PORT:
        if (was && (zone == EIDOS_SUI_ZONE_DEC || zone == EIDOS_SUI_ZONE_INC)) {
            change_net_port(r->type, zone == EIDOS_SUI_ZONE_INC ? 1 : -1);
            res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        break;
    case ROW_INJECT_MAX:
        if (was && (zone == EIDOS_SUI_ZONE_DEC || zone == EIDOS_SUI_ZONE_INC)) {
            int v = eidos_settings_input_inject_max() +
                    (zone == EIDOS_SUI_ZONE_INC ? INJECT_STEP : -INJECT_STEP);

            if (v != eidos_settings_input_inject_max()) {
                eidos_settings_input_set_inject_max(v);
                res = EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
            }
        }
        break;
    case ROW_BIND:
        eidos_settings_capture_begin();
        g.capturing = r->arg + 1;
        return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
    case ROW_DEFAULTS:
        if (was) {
            eidos_settings_defaults();
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        break;
    case ROW_NET_DEFAULTS:
        if (was) {
            eidos_settings_net_defaults();
            eidos_settings_input_defaults();
            return EIDOS_SUI_REDRAW | EIDOS_SUI_SFX_SELECT;
        }
        break;
    default:
        break;
    }
    return res;
}

int eidos_sui_capturing(void)
{
    return g.capturing;
}

int eidos_sui_tab(void)
{
    return g.level == 0 ? -1 : g.tab;
}

int eidos_sui_level(void)
{
    return g.level;
}

int eidos_sui_tab_count(void)
{
    return TAB_COUNT;
}

static void option_value_of(int id, int v, char *out, int n)
{
    const EidosOption *o = eidos_settings_option(id);
    size_t i;
    if (o != NULL && o->kind == EIDOS_OPTKIND_CHOICE) {
        for (i = 0; i < sizeof k_value_names / sizeof k_value_names[0]; i++) {
            if (k_value_names[i].id == id && k_value_names[i].index == v) {
                put(out, n, k_value_names[i].text);
                return;
            }
        }
    }
    if (o != NULL && o->zero_name != NULL && v == 0) {
        put(out, n, STR_FIT);
        return;
    }
    eidos_settings_value_text(id, out, (size_t)n);
}

static void option_value(int id, char *out, int n)
{
    option_value_of(id, eidos_settings_get(id), out, n);
}

static void pad_short(int kind, int index, char *out, int n)
{
    char tmp[48];
    const char *t = NULL;
    if (kind == EIDOS_PADBIND_BUTTON) {
        if (index >= 0 && index < (int)(sizeof k_pad_short / sizeof k_pad_short[0])) {
            t = k_pad_short[index];
        }
    } else if (index >= 0 && index < 6) {
        t = kind == EIDOS_PADBIND_AXIS_POS ? k_axis_pos_short[index] : k_axis_neg_short[index];
    }
    if (t == NULL) {
        eidos_pad_control_name(kind, index, tmp, sizeof tmp);
        t = tmp;
    }
    put(out, n, t);
}

/* "Key / Pad": the first key and the first pad control, in short names. */
static void bind_value(int button, char *out, int n)
{
    char key[48], pad[32];
    int slot, kind, index;
    key[0] = '\0';
    pad[0] = '\0';
    for (slot = 0; slot < EIDOS_BIND_SLOTS && key[0] == '\0'; slot++) {
        int sc = eidos_settings_get_key(button, slot);
        if (sc != 0) {
            eidos_scancode_name(sc, key, sizeof key);
        }
    }
    for (slot = 0; slot < EIDOS_BIND_SLOTS; slot++) {
        if (eidos_settings_get_pad(button, slot, &kind, &index) == 0 && kind >= 0) {
            pad_short(kind, index, pad, sizeof pad);
            break;
        }
    }
    if (key[0] == '\0' && pad[0] == '\0') {
        put(out, n, STR_UNBOUND);
    } else if (pad[0] == '\0') {
        put(out, n, key);
    } else if (key[0] == '\0') {
        snprintf(out, (size_t)n, "- / %s", pad);
    } else {
        snprintf(out, (size_t)n, "%s / %s", key, pad);
    }
}

/* A text of the port's own settings: what is not set says so. */
static const char *net_text(const char *s)
{
    return (s != NULL && s[0] != '\0') ? s : STR_NONE;
}

void eidos_sui_row_text(int row, char *label, int nl, char *value, int nv)
{
    const Row *r;
    char buf[96];

    if (nl > 0) { label[0] = '\0'; }
    if (nv > 0) { value[0] = '\0'; }
    if (row == 0) {
        put(label, nl, g.level == 0 ? STR_TITLE : k_tab_names[g.tab]);
        return;
    }
    if (row == eidos_sui_row_count() - 1) {
        put(label, nl, g.level == 0 ? STR_BACK_L0 : STR_BACK_L1);
        return;
    }
    if (row < 1 || g.scroll + row - 1 >= g.nrows) {
        return;
    }
    r = &g.rows[g.scroll + row - 1];
    switch (r->type) {
    case ROW_SECTION:
        put(label, nl, k_tab_names[r->arg]);
        break;
    case ROW_OPTION:
        put(label, nl, k_option_labels[r->arg] != NULL ? k_option_labels[r->arg] : "");
        option_value(r->arg, value, nv);
        break;
    case ROW_BIND: {
        const EidosButtonInfo *b = eidos_settings_button(r->arg);
        put(label, nl, r->arg >= 0 && r->arg < (int)(sizeof k_button_short / sizeof k_button_short[0])
                           ? k_button_short[r->arg] : (b != NULL ? b->label : ""));
        if (g.capturing == r->arg + 1) {
            put(value, nv, STR_PRESS_KEY);
        } else {
            bind_value(r->arg, value, nv);
        }
        break;
    }
    case ROW_PANEL:
        /* The run-status panel and the player's switch. */
        put(label, nl, STR_PANEL);
        put(value, nv, eidos_settings_panel_visible() ? STR_ON : STR_OFF);
        break;
    case ROW_MOD_SWITCH:
        /* The mod's name and its switch. */
        put(label, nl, bw_mod_menu_name_at((u32)r->arg));
        put(value, nv, bw_mod_menu_enabled_at((u32)r->arg) ? STR_ON : STR_OFF);
        break;
    case ROW_MOD_OPTION:
        /* The mod's own setting, under the mod's name. */
        snprintf(buf, sizeof buf, "  %s",
                 bw_mod_menu_option_label((u32)r->arg, (u32)r->arg2));
        put(label, nl, buf);
        bw_mod_menu_option_text((u32)r->arg, (u32)r->arg2, buf, sizeof buf);
        put(value, nv, buf);
        break;
    case ROW_MOD_CLEAR:
        put(label, nl, STR_MOD_CLEAR);
        break;
    case ROW_MOD_REVERT:
        put(label, nl, STR_MOD_REVERT);
        break;
    case ROW_ORPHAN: {
        const char *name = bw_mod_menu_orphan_at((u32)r->arg);
        put(label, nl, name != NULL ? name : "");
        put(value, nv, STR_ORPHAN);
        break;
    }
    case ROW_NET_RELAY:
        put(label, nl, STR_NET_RELAY);
        put(value, nv, net_text(eidos_settings_net_relay()));
        break;
    case ROW_NET_RELAY_PORT:
        put(label, nl, STR_NET_RPORT);
        snprintf(buf, sizeof buf, "%d", eidos_settings_net_relay_port());
        put(value, nv, buf);
        break;
    case ROW_NET_CONTROL_PORT:
        put(label, nl, STR_NET_CPORT);
        snprintf(buf, sizeof buf, "%d", eidos_settings_net_control_port());
        put(value, nv, buf);
        break;
    case ROW_NET_NAME:
        put(label, nl, STR_NET_NAME);
        put(value, nv, net_text(eidos_settings_net_name()));
        break;
    case ROW_INJECT_MAX:
        /* The longest press the injection allows, in frames.  0 is off. */
        put(label, nl, STR_INJECT);
        if (eidos_settings_input_inject_max() == 0) {
            put(value, nv, STR_OFF);
        } else {
            snprintf(buf, sizeof buf, "%d", eidos_settings_input_inject_max());
            put(value, nv, buf);
        }
        break;
    case ROW_SESSION_CODE:
        /* The session's code: the one the relay handed out, or the one the
         * player types to join a session. */
        put(label, nl, STR_SESSION_CODE);
        put(value, nv, net_text(eidos_settings_session_code()));
        break;
    case ROW_SESSION_HOST:
        put(label, nl, STR_SESSION_HOST);
        session_value(1, value, nv);
        break;
    case ROW_SESSION_JOIN:
        put(label, nl, STR_SESSION_JOIN);
        session_value(0, value, nv);
        break;
    case ROW_SESSION_LEAVE:
        put(label, nl, STR_SESSION_LEAVE);
        put(value, nv, bw_port_session_active() ? STR_SESSION_IN : "");
        break;
    case ROW_SESSION_REVERT:
        put(label, nl, STR_SESSION_REVERT);
        break;
    case ROW_SESSION_DEFAULTS:
        put(label, nl, STR_DEFAULTS);
        break;
    default:
        put(label, nl, STR_DEFAULTS);
        break;
    }
}

int eidos_sui_row_choices(int row, int *active, char names[3][20])
{
    const Row *r;
    const EidosOption *o;
    int i, count;

    if (row < 1 || row == eidos_sui_row_count() - 1
        || g.scroll + row - 1 >= g.nrows) {
        return 0;
    }
    r = &g.rows[g.scroll + row - 1];
    if (r->type == ROW_PANEL) {
        /* The panel's switch: two choices, off and on. */
        put(names[0], 20, STR_OFF);
        put(names[1], 20, STR_ON);
        if (active != NULL) {
            *active = eidos_settings_panel_visible() ? 1 : 0;
        }
        return 2;
    }
    if (r->type == ROW_MOD_SWITCH) {
        /* The mod's switch: two choices, off and on. */
        put(names[0], 20, STR_OFF);
        put(names[1], 20, STR_ON);
        if (active != NULL) {
            *active = bw_mod_menu_enabled_at((u32)r->arg) ? 1 : 0;
        }
        return 2;
    }
    if (r->type != ROW_OPTION) {
        return 0;
    }
    o = eidos_settings_option(r->arg);
    if (o == NULL || o->kind != EIDOS_OPTKIND_CHOICE) {
        return 0;
    }
    count = o->max - o->min + 1;
    if (count < 2 || count > 3) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        size_t k;
        int found = 0;

        for (k = 0; k < sizeof k_choice_names / sizeof k_choice_names[0]; k++) {
            if (k_choice_names[k].id == r->arg && k_choice_names[k].index == o->min + i) {
                put(names[i], 20, k_choice_names[k].text);
                found = 1;
            }
        }
        if (!found) {
            option_value_of(r->arg, o->min + i, names[i], 20);
        }
    }
    if (active != NULL) {
        *active = eidos_settings_get(r->arg) - o->min;
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Help text for the bottom screen */

int eidos_sui_help_count(void)
{
    return 4;
}

void eidos_sui_help_line(int i, char *out, int n)
{
    const char *l[4] = { "", "", "", "" };
    const Row *r = (g.nrows > 0 && !on_back()) ? &g.rows[g.cursor] : NULL;
    if (n > 0) {
        out[0] = '\0';
    }
    if (g.capturing) {
        l[0] = HELP_CAPTURE1;
        l[1] = HELP_CAPTURE2;
    } else if (g.editing) {
        l[0] = HELP_TEXT_EDIT1;
        l[1] = HELP_TEXT_EDIT2;
    } else if (on_back()) {
        l[0] = g.level == 0 ? HELP_BACK_L0 : HELP_BACK_L1;
        l[2] = g.level == 0 ? HELP_KEYS_L0 : HELP_KEYS_L1;
        l[3] = g.level == 0 ? HELP_KEYS_L0B : HELP_KEYS_L1B;
    } else if (g.level == 0 && r != NULL) {
        static const char *const sec[] = {
            HELP_SECTION_DISPLAY, HELP_SECTION_LAYOUT, HELP_SECTION_CONTROLS,
            HELP_SECTION_SOUND, HELP_SECTION_MODS, HELP_SECTION_NETWORK,
            HELP_SECTION_SESSION };
        l[0] = sec[r->arg];
        l[2] = HELP_KEYS_L0;
        l[3] = HELP_KEYS_L0B;
    } else if (r != NULL) {
        switch (r->type) {
        case ROW_OPTION:
            l[0] = k_option_help[r->arg][0] != NULL ? k_option_help[r->arg][0] : "";
            l[1] = k_option_help[r->arg][1] != NULL ? k_option_help[r->arg][1] : "";
            break;
        case ROW_BIND:
            l[0] = HELP_BIND1;
            l[1] = HELP_BIND2;
            break;
        case ROW_PANEL:
            l[0] = HELP_PANEL1;
            l[1] = HELP_PANEL2;
            break;
        case ROW_MOD_SWITCH:
            l[0] = HELP_MOD_SWITCH1;
            l[1] = HELP_MOD_SWITCH2;
            break;
        case ROW_MOD_OPTION:
            l[0] = HELP_MOD_OPTION1;
            l[1] = HELP_MOD_OPTION2;
            break;
        case ROW_MOD_CLEAR:
            l[0] = HELP_MOD_CLEAR1;
            l[1] = HELP_MOD_CLEAR2;
            break;
        case ROW_MOD_REVERT:
            l[0] = HELP_MOD_REVERT1;
            l[1] = HELP_MOD_REVERT2;
            break;
        case ROW_ORPHAN:
            l[0] = HELP_ORPHAN1;
            l[1] = HELP_ORPHAN2;
            break;
        case ROW_NET_RELAY:
            l[0] = HELP_NET_RELAY1;
            l[1] = HELP_NET_RELAY2;
            break;
        case ROW_NET_RELAY_PORT:
            l[0] = HELP_NET_RPORT1;
            l[1] = HELP_NET_RPORT2;
            break;
        case ROW_NET_CONTROL_PORT:
            l[0] = HELP_NET_CPORT1;
            l[1] = HELP_NET_CPORT2;
            break;
        case ROW_NET_NAME:
            l[0] = HELP_NET_NAME1;
            l[1] = HELP_NET_NAME2;
            break;
        case ROW_INJECT_MAX:
            l[0] = HELP_INJECT1;
            l[1] = HELP_INJECT2;
            break;
        case ROW_NET_DEFAULTS:
            l[0] = HELP_NET_DEFAULTS1;
            l[1] = HELP_NET_DEFAULTS2;
            break;
        case ROW_SESSION_CODE:
            l[0] = HELP_SESSION_CODE1;
            l[1] = HELP_SESSION_CODE2;
            break;
        case ROW_SESSION_HOST:
            l[0] = HELP_SESSION_HOST1;
            l[1] = HELP_SESSION_HOST2;
            break;
        case ROW_SESSION_JOIN:
            l[0] = HELP_SESSION_JOIN1;
            l[1] = HELP_SESSION_JOIN2;
            break;
        case ROW_SESSION_LEAVE:
            l[0] = HELP_SESSION_LEAVE1;
            l[1] = HELP_SESSION_LEAVE2;
            break;
        case ROW_SESSION_REVERT:
            l[0] = HELP_SESSION_REVERT1;
            l[1] = HELP_SESSION_REVERT2;
            break;
        case ROW_SESSION_DEFAULTS:
            l[0] = HELP_DEFAULTS1;
            l[1] = HELP_DEFAULTS2;
            break;
        default:
            l[0] = HELP_DEFAULTS1;
            l[1] = HELP_DEFAULTS2;
            break;
        }
        l[2] = HELP_KEYS_L1;
        l[3] = HELP_KEYS_L1B;
    }
    if (i >= 0 && i < 4) {
        put(out, n, l[i]);
    }
}
