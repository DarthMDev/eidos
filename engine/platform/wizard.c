/*
 * engine/platform/wizard.c -- the first-run setup window.  See wizard.h
 * for what it does and docs/installer.md for the design.
 *
 * Decisions made here:
 *
 *   - Everything is drawn into one 512 x 320 buffer of the wizard's own
 *     and shown at an integer scale, like the DS screens: no font
 *     rendering library, no native widgets, so the window looks and
 *     behaves the same on every platform and a test can read its pixels.
 *   - The ROM is checked and extracted on an SDL thread
 *     (bw_setup_install), which reports progress through atomics and
 *     stops when the Cancel flag is set; the window keeps drawing and
 *     answering meanwhile.  Nothing else is shared with the thread until
 *     it has finished.
 *   - The native file dialog is modal and called on the main thread
 *     (every platform's dialog insists on it).
 *   - A dropped file is taken on the screens that ask for one and ignored
 *     while a ROM is being checked or written.
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "picker.h"
#include "wizard.h"

#define W EIDOS_WIZARD_WIDTH
#define H EIDOS_WIZARD_HEIGHT

/* Colours, 0xAARRGGBB. */
#define C_BG      0xFF1E2230u
#define C_PANEL   0xFF2A3042u
#define C_TEXT    0xFFE8EAF0u
#define C_DIM     0xFF9AA0B4u
#define C_ACCENT  0xFF6CB4FFu
#define C_BAD     0xFFFF7A70u
#define C_GOOD    0xFF7CD992u
#define C_BUTTON  0xFF3A4360u
#define C_FOCUS   0xFF4F6FB0u
#define C_EDGE    0xFF8FA8E0u

#define MARGIN    16
#define BODY_Y    74
#define BAR_Y     214
#define BUTTON_Y  268
#define BUTTON_H  26
#define FOOT_Y    306

enum { ACT_NONE = 0, ACT_CHOOSE, ACT_QUIT, ACT_CANCEL, ACT_START };

/* ------------------------------------------------------------------ */
/* The worker                                                          */
/* ------------------------------------------------------------------ */

typedef struct Job {
    char rom[BW_SETUP_PATH_MAX];
    char dest[BW_SETUP_PATH_MAX];
    int allow_unverified;
    SDL_atomic_t hashing;       /* 1 while hashing, 0 while writing */
    SDL_atomic_t done;
    SDL_atomic_t total;
    SDL_atomic_t cancel;
    SDL_atomic_t finished;
    /* Read only once `finished` is set. */
    int rc;
    BwSetupOutcome outcome;
    char msg[1024];
} Job;

static int job_progress(void *ctx, u32 done, u32 total, const char *what)
{
    Job *j = (Job *)ctx;

    SDL_AtomicSet(&j->hashing, strcmp(what, "hashing") == 0);
    SDL_AtomicSet(&j->total, (int)total);
    SDL_AtomicSet(&j->done, (int)done);
    return SDL_AtomicGet(&j->cancel);
}

static int SDLCALL job_main(void *arg)
{
    Job *j = (Job *)arg;

    j->rc = bw_setup_install(j->rom, j->dest, j->allow_unverified, 0,
                             job_progress, j, &j->outcome, j->msg, sizeof(j->msg));
    SDL_AtomicSet(&j->finished, 1);
    return 0;
}

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

typedef struct Button {
    int x, y, w, h;
    const char *label;
    int action;
} Button;

typedef struct Wiz {
    const EidosWizardConfig *cfg;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    u32 *fb;
    int scale;

    int screen;
    char rom[BW_SETUP_PATH_MAX];
    char heading[96];
    char body[4096];
    char note[1536];
    char foot[160];
    Button button[2];
    int nbuttons;
    int focus;
    int hover;

    Job *job;
    SDL_Thread *thread;

    u32 frame;
    u32 shots;                  /* screens already written, one bit each */
    int result;                 /* -2 while running */
} Wiz;

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static void fill(Wiz *z, int x, int y, int w, int h, u32 c)
{
    int i, j;

    for (j = y; j < y + h; j++) {
        if (j < 0 || j >= H) {
            continue;
        }
        for (i = x; i < x + w; i++) {
            if (i >= 0 && i < W) {
                z->fb[j * W + i] = c;
            }
        }
    }
}

static void frame_rect(Wiz *z, int x, int y, int w, int h, u32 c)
{
    fill(z, x, y, w, 1, c);
    fill(z, x, y + h - 1, w, 1, c);
    fill(z, x, y, 1, h, c);
    fill(z, x + w - 1, y, 1, h, c);
}

static void glyph(Wiz *z, int x, int y, u32 ch, u32 c, int s)
{
    const u8 *rows = eidos_font_glyph(ch);
    int r, col;

    for (r = 0; r < EIDOS_FONT_ROWS; r++) {
        for (col = 0; col < EIDOS_FONT_COLS; col++) {
            if (rows[r] & (0x10u >> col)) {
                fill(z, x + col * s, y + r * s, s, s, c);
            }
        }
    }
}

/* One line, no wrapping; returns the x after it. */
static int text(Wiz *z, int x, int y, const char *s, u32 c, int scale)
{
    u32 ch;

    while ((ch = eidos_utf8_next(&s)) != 0) {
        glyph(z, x, y, ch, c, scale);
        x += EIDOS_FONT_ADVANCE * scale;
    }
    return x;
}

static int utf8_len(const char *s, const char *end)
{
    int n = 0;

    while (s < end && *s != '\0') {
        (void)eidos_utf8_next(&s);
        n++;
    }
    return n;
}

/*
 * Wrapped text in a column `cols` characters wide, from `y`; '\n' ends a
 * line.  A word longer than the line (a path) is broken where it reaches
 * the edge.  A paragraph starting with '>' (a path) is drawn in the accent
 * colour, the '>' itself not drawn.  Returns the y below the last line.
 */
static int wrapped(Wiz *z, int x, int y, int cols, const char *s, u32 c)
{
    char line[512];
    int accent = 0;             /* inside a '>' paragraph */

    while (*s != '\0') {
        const char *start;
        const char *brk = NULL;
        const char *p;
        int n = 0;
        size_t len;

        if (*s == '>') {
            accent = 1;
            s++;
        }
        start = s;
        p = s;
        while (*p != '\0' && *p != '\n' && n < cols) {
            if (*p == ' ') {
                brk = p;
            }
            (void)eidos_utf8_next(&p);
            n++;
        }
        if (*p != '\0' && *p != '\n' && n >= cols && brk != NULL && *p != ' ') {
            p = brk;                    /* break at the last space */
        }
        len = (size_t)(p - start);
        if (len >= sizeof(line)) {
            len = sizeof(line) - 1;
        }
        memcpy(line, start, len);
        line[len] = '\0';
        text(z, x, y, line, accent ? C_ACCENT : c, 1);
        y += EIDOS_FONT_LINE;
        s = p;
        if (*s == '\n') {
            accent = 0;
            s++;
        } else if (*s == ' ') {
            s++;
        }
    }
    return y;
}

static void layout_buttons(Wiz *z)
{
    int x = W - MARGIN;
    int i;

    for (i = z->nbuttons - 1; i >= 0; i--) {
        Button *b = &z->button[i];
        b->w = utf8_len(b->label, b->label + strlen(b->label)) * EIDOS_FONT_ADVANCE + 28;
        b->h = BUTTON_H;
        b->x = x - b->w;
        b->y = BUTTON_Y;
        x = b->x - 10;
    }
}

static int progress_permille(const Wiz *z)
{
    int done, total;

    if (z->job == NULL) {
        return 0;
    }
    done = SDL_AtomicGet(&z->job->done);
    total = SDL_AtomicGet(&z->job->total);
    if (total <= 0) {
        return 0;
    }
    return (int)((double)done * 1000.0 / (double)total);
}

static void draw(Wiz *z)
{
    int i;

    fill(z, 0, 0, W, H, C_BG);
    fill(z, 0, 0, W, 24, C_PANEL);
    text(z, MARGIN, 8, "eidos", C_ACCENT, 1);
    text(z, MARGIN + 6 * EIDOS_FONT_ADVANCE, 8, "first-run setup", C_DIM, 1);

    text(z, MARGIN, 38, z->heading,
         z->screen == EIDOS_WIZARD_REFUSED ? C_BAD
         : z->screen == EIDOS_WIZARD_DONE ? C_GOOD : C_TEXT, 2);

    wrapped(z, MARGIN, BODY_Y, (W - 2 * MARGIN) / EIDOS_FONT_ADVANCE, z->body, C_TEXT);

    if (z->screen == EIDOS_WIZARD_CHECKING || z->screen == EIDOS_WIZARD_WRITING) {
        int pm = progress_permille(z);
        int bw = W - 2 * MARGIN;
        char pct[32];

        frame_rect(z, MARGIN, BAR_Y, bw, 14, C_EDGE);
        fill(z, MARGIN + 2, BAR_Y + 2, (bw - 4) * pm / 1000, 10, C_ACCENT);
        snprintf(pct, sizeof(pct), "%d%%", pm / 10);
        text(z, MARGIN, BAR_Y + 20, pct, C_DIM, 1);
    }
    if (z->note[0] != '\0') {
        wrapped(z, MARGIN, BAR_Y - 2, (W - 2 * MARGIN) / EIDOS_FONT_ADVANCE, z->note, C_BAD);
    }

    for (i = 0; i < z->nbuttons; i++) {
        const Button *b = &z->button[i];
        int lit = i == z->focus || i == z->hover;
        fill(z, b->x, b->y, b->w, b->h, lit ? C_FOCUS : C_BUTTON);
        frame_rect(z, b->x, b->y, b->w, b->h, i == z->focus ? C_EDGE : C_BUTTON);
        text(z, b->x + 14, b->y + 9, b->label, C_TEXT, 1);
    }
    text(z, MARGIN, FOOT_Y, z->foot, C_DIM, 1);
}

static void present(Wiz *z)
{
    if (z->ren == NULL || z->tex == NULL) {
        return;
    }
    SDL_UpdateTexture(z->tex, NULL, z->fb, W * 4);
    SDL_RenderClear(z->ren);
    SDL_RenderCopy(z->ren, z->tex, NULL, NULL);
    SDL_RenderPresent(z->ren);
}

static const char *screen_name(int s)
{
    static const char *const names[] = { "welcome", "checking", "writing",
                                         "refused", "done" };
    return names[s];
}

static void shot(Wiz *z)
{
    char path[BW_SETUP_PATH_MAX];
    SDL_Surface *surf;

    if (z->cfg->shot_prefix == NULL || (z->shots & (1u << z->screen)) != 0) {
        return;
    }
    z->shots |= 1u << z->screen;
    snprintf(path, sizeof(path), "%s-%s.bmp", z->cfg->shot_prefix, screen_name(z->screen));
    surf = SDL_CreateRGBSurfaceWithFormatFrom(z->fb, W, H, 32, W * 4,
                                              SDL_PIXELFORMAT_ARGB8888);
    if (surf != NULL) {
        if (SDL_SaveBMP(surf, path) != 0) {
            fprintf(stderr, "eidos: cannot write %s: %s\n", path, SDL_GetError());
        }
        SDL_FreeSurface(surf);
    }
}

/* ------------------------------------------------------------------ */
/* Screens                                                             */
/* ------------------------------------------------------------------ */

static void buttons(Wiz *z, const char *a, int act_a, const char *b, int act_b)
{
    z->nbuttons = 0;
    if (a != NULL) {
        z->button[z->nbuttons].label = a;
        z->button[z->nbuttons].action = act_a;
        z->nbuttons++;
    }
    if (b != NULL) {
        z->button[z->nbuttons].label = b;
        z->button[z->nbuttons].action = act_b;
        z->nbuttons++;
    }
    z->focus = 0;
    z->hover = -1;
    layout_buttons(z);
}

static void show_welcome(Wiz *z)
{
    z->screen = EIDOS_WIZARD_WELCOME;
    snprintf(z->heading, sizeof(z->heading), "Pokemon Black is needed");
    snprintf(z->body, sizeof(z->body),
             "%s%s"
             "eidos runs Pokemon Black from your own copy of the game. It needs "
             "a ROM: the file a dumping tool makes from your cartridge, usually "
             "ending in .nds. Nothing is downloaded, and the ROM is not copied: "
             "eidos reads it once and writes the game data it needs to\n"
             ">%s\n\n"
             "Choose the file, or drag it onto this window.",
             z->cfg->reason != NULL ? z->cfg->reason : "",
             z->cfg->reason != NULL ? "\n\n" : "",
             z->cfg->extract_dir);
    snprintf(z->foot, sizeof(z->foot), "Enter or O: choose the file    Esc: quit");
    buttons(z, "Choose ROM file...", ACT_CHOOSE, "Quit", ACT_QUIT);
}

static void start_job(Wiz *z, const char *rom)
{
    Job *j = (Job *)calloc(1, sizeof(Job));

    z->note[0] = '\0';
    snprintf(z->rom, sizeof(z->rom), "%s", rom);
    if (j == NULL) {
        snprintf(z->note, sizeof(z->note), "Out of memory.");
        return;
    }
    snprintf(j->rom, sizeof(j->rom), "%s", rom);
    snprintf(j->dest, sizeof(j->dest), "%s", z->cfg->extract_dir);
    j->allow_unverified = z->cfg->allow_unverified;
    SDL_AtomicSet(&j->hashing, 1);
    z->job = j;
    z->thread = SDL_CreateThread(job_main, "eidos-install", j);
    if (z->thread == NULL) {
        /* No thread: run it here.  The window does not redraw meanwhile,
         * but the result is the same. */
        job_main(j);
    }
    z->screen = EIDOS_WIZARD_CHECKING;
    snprintf(z->heading, sizeof(z->heading), "Checking your ROM");
    snprintf(z->body, sizeof(z->body), "Reading\n>%s\nto make sure it is the "
             "game eidos needs. This takes a few seconds.", z->rom);
    snprintf(z->foot, sizeof(z->foot), "Esc: cancel");
    buttons(z, "Cancel", ACT_CANCEL, NULL, ACT_NONE);
}

static void show_writing(Wiz *z)
{
    z->screen = EIDOS_WIZARD_WRITING;
    snprintf(z->heading, sizeof(z->heading), "Writing the game data");
    snprintf(z->body, sizeof(z->body), "Your ROM is the right one. The game "
             "data is being written to\n>%s\nCancelling leaves nothing behind.",
             z->cfg->extract_dir);
}

static const char *refusal_heading(const Job *j)
{
    if (j->rc == BW_CANCELLED) {
        return "Cancelled";
    }
    switch (j->outcome.stage) {
    case BW_STAGE_IDENTIFY:
        return "This file cannot be used";
    case BW_STAGE_VERDICT:
        switch (j->outcome.verdict) {
        case BW_VERDICT_NOT_DS:      return "This is not a DS game";
        case BW_VERDICT_OTHER_GAME:  return "This is a different game";
        case BW_VERDICT_UNSUPPORTED: return "Not supported yet";
        case BW_VERDICT_DAMAGED:     return "This dump is damaged";
        case BW_VERDICT_MODIFIED:    return "Not the original game";
        default:                     return "This file cannot be used";
        }
    case BW_STAGE_SPACE:
        return "Not enough free space";
    default:
        return "The game data could not be written";
    }
}

static void finish_job(Wiz *z)
{
    Job *j = z->job;

    if (z->thread != NULL) {
        SDL_WaitThread(z->thread, NULL);
        z->thread = NULL;
    }
    if (j->rc == 0) {
        z->screen = EIDOS_WIZARD_DONE;
        snprintf(z->heading, sizeof(z->heading), "Ready to play");
        snprintf(z->body, sizeof(z->body),
                 "%s\n>%s\n\nIt was made from\n>%s\n\n"
                 "eidos remembers both. The ROM is not needed again unless a "
                 "later version of eidos asks for it.",
                 j->outcome.already ? "This ROM's game data was already in"
                                    : "The game data is in",
                 z->cfg->extract_dir, z->rom);
        snprintf(z->foot, sizeof(z->foot), "Enter: start");
        buttons(z, "Start", ACT_START, NULL, ACT_NONE);
    } else {
        z->screen = EIDOS_WIZARD_REFUSED;
        snprintf(z->heading, sizeof(z->heading), "%s", refusal_heading(j));
        snprintf(z->body, sizeof(z->body), "%s\n\nThe file:\n>%s",
                 j->rc == BW_CANCELLED
                     ? "Nothing was written. Choose the file again when you are ready."
                     : j->msg,
                 z->rom);
        snprintf(z->foot, sizeof(z->foot),
                 "Enter or O: choose another file    Esc: quit");
        buttons(z, "Choose another file...", ACT_CHOOSE, "Quit", ACT_QUIT);
    }
    free(j);
    z->job = NULL;
}

/* ------------------------------------------------------------------ */
/* Actions                                                             */
/* ------------------------------------------------------------------ */

static void *native_parent(Wiz *z)
{
    SDL_SysWMinfo info;

    SDL_VERSION(&info.version);
    if (z->win == NULL || !SDL_GetWindowWMInfo(z->win, &info)) {
        return NULL;
    }
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
    if (info.subsystem == SDL_SYSWM_WINDOWS) {
        return (void *)info.info.win.window;
    }
#endif
#if defined(SDL_VIDEO_DRIVER_COCOA)
    if (info.subsystem == SDL_SYSWM_COCOA) {
        return (void *)info.info.cocoa.window;
    }
#endif
    return NULL;
}

static void choose(Wiz *z)
{
    char path[BW_SETUP_PATH_MAX];
    int rc;

    path[0] = '\0';
    if (z->cfg->pick != NULL) {
        rc = z->cfg->pick(z->cfg->user, path, sizeof(path));
    } else {
        rc = bw_pick_rom(native_parent(z), "Choose your Pokemon Black ROM",
                         path, sizeof(path));
    }
    if (rc == BW_PICK_OK) {
        start_job(z, path);
    } else if (rc == BW_PICK_UNAVAILABLE) {
        snprintf(z->note, sizeof(z->note), "%s Drag the ROM file onto this "
                 "window instead, or start eidos with --rom <file>.", path);
    }
}

static void cancel(Wiz *z)
{
    if (z->job != NULL) {
        SDL_AtomicSet(&z->job->cancel, 1);
    }
}

static void act(Wiz *z, int action)
{
    switch (action) {
    case ACT_CHOOSE:
        choose(z);
        break;
    case ACT_QUIT:
        z->result = EIDOS_WIZARD_QUIT;
        break;
    case ACT_CANCEL:
        cancel(z);
        break;
    case ACT_START:
        z->result = EIDOS_WIZARD_INSTALLED;
        break;
    default:
        break;
    }
}

static int button_at(const Wiz *z, int wx, int wy)
{
    int ww = W * z->scale, wh = H * z->scale;
    int x, y, i;

    if (z->win != NULL) {
        SDL_GetWindowSize(z->win, &ww, &wh);
    }
    if (ww <= 0 || wh <= 0) {
        return -1;
    }
    x = wx * W / ww;
    y = wy * H / wh;
    for (i = 0; i < z->nbuttons; i++) {
        const Button *b = &z->button[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
            return i;
        }
    }
    return -1;
}

static int wants_file(const Wiz *z)
{
    return z->screen == EIDOS_WIZARD_WELCOME || z->screen == EIDOS_WIZARD_REFUSED;
}

static void handle(Wiz *z, const SDL_Event *e)
{
    switch (e->type) {
    case SDL_QUIT:
        z->result = EIDOS_WIZARD_QUIT;
        break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_CLOSE) {
            z->result = EIDOS_WIZARD_QUIT;
        }
        break;
    case SDL_DROPFILE:
        if (wants_file(z) && e->drop.file != NULL) {
            start_job(z, e->drop.file);
        }
        SDL_free(e->drop.file);
        break;
    case SDL_KEYDOWN:
        switch (e->key.keysym.sym) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
            if (z->focus >= 0 && z->focus < z->nbuttons) {
                act(z, z->button[z->focus].action);
            }
            break;
        case SDLK_ESCAPE:
            if (z->job != NULL) {
                cancel(z);
            } else if (z->screen != EIDOS_WIZARD_DONE) {
                z->result = EIDOS_WIZARD_QUIT;
            }
            break;
        case SDLK_TAB:
        case SDLK_RIGHT:
        case SDLK_LEFT:
            if (z->nbuttons > 0) {
                z->focus = (z->focus + 1) % z->nbuttons;
            }
            break;
        case SDLK_o:
            if (wants_file(z)) {
                choose(z);
            }
            break;
        default:
            break;
        }
        break;
    case SDL_MOUSEMOTION:
        z->hover = button_at(z, e->motion.x, e->motion.y);
        break;
    case SDL_MOUSEBUTTONUP:
        if (e->button.button == SDL_BUTTON_LEFT) {
            int b = button_at(z, e->button.x, e->button.y);
            if (b >= 0) {
                z->focus = b;
                act(z, z->button[b].action);
            }
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* The window                                                          */
/* ------------------------------------------------------------------ */

static int pick_scale(const EidosWizardConfig *cfg)
{
    SDL_Rect r;
    int s;

    if (cfg->scale > 0) {
        return cfg->scale;
    }
    if (SDL_GetDisplayUsableBounds(0, &r) != 0) {
        return 2;
    }
    s = r.w / W < r.h / H ? r.w / W : r.h / H;
    return s < 1 ? 1 : s > 3 ? 3 : s;
}

static void view_of(const Wiz *z, EidosWizardView *v)
{
    int i;

    memset(v, 0, sizeof(*v));
    v->screen = z->screen;
    v->frame = z->frame;
    v->heading = z->heading;
    v->body = z->body;
    v->note = z->note;
    v->progress = progress_permille(z);
    v->nbuttons = z->nbuttons;
    for (i = 0; i < z->nbuttons; i++) {
        v->button[i].x = z->button[i].x * z->scale;
        v->button[i].y = z->button[i].y * z->scale;
        v->button[i].w = z->button[i].w * z->scale;
        v->button[i].h = z->button[i].h * z->scale;
        v->button[i].label = z->button[i].label;
    }
    v->focus = z->focus;
    v->pixels = z->fb;
    v->background = C_BG;
    v->scale = z->scale;
}

int eidos_wizard_run(const EidosWizardConfig *cfg, char *rom_out, size_t n)
{
    Wiz z;
    int own_video = 0;

    memset(&z, 0, sizeof(z));
    z.cfg = cfg;
    z.result = -2;
    z.fb = (u32 *)calloc((size_t)W * H, sizeof(u32));
    if (z.fb == NULL) {
        return EIDOS_WIZARD_NO_WINDOW;
    }

    SDL_SetMainReady();
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "eidos: no window for setup: %s\n", SDL_GetError());
            free(z.fb);
            return EIDOS_WIZARD_NO_WINDOW;
        }
        own_video = 1;
    }
    z.scale = pick_scale(cfg);
    z.win = SDL_CreateWindow("eidos setup", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, W * z.scale, H * z.scale, 0);
    if (z.win == NULL) {
        fprintf(stderr, "eidos: no window for setup: %s\n", SDL_GetError());
        if (own_video) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
        free(z.fb);
        return EIDOS_WIZARD_NO_WINDOW;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    z.ren = SDL_CreateRenderer(z.win, -1, 0);
    if (z.ren != NULL) {
        z.tex = SDL_CreateTexture(z.ren, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, W, H);
    }
    if (z.ren == NULL || z.tex == NULL) {
        /* The window still opens and takes events; it just shows nothing.
         * Reported, since a player would see a blank window. */
        fprintf(stderr, "eidos: setup window has no renderer: %s\n", SDL_GetError());
    }
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    if (cfg->rom != NULL && cfg->rom[0] != '\0') {
        start_job(&z, cfg->rom);
    } else {
        show_welcome(&z);
    }

    while (z.result == -2) {
        SDL_Event e;
        EidosWizardView v;

        if (z.job != NULL) {
            if (SDL_AtomicGet(&z.job->finished)) {
                finish_job(&z);
            } else if (z.screen == EIDOS_WIZARD_CHECKING
                       && !SDL_AtomicGet(&z.job->hashing)) {
                show_writing(&z);
            }
        }
        draw(&z);
        present(&z);
        shot(&z);
        if (cfg->script != NULL) {
            view_of(&z, &v);
            cfg->script(cfg->user, &v);
        }
        z.frame++;
        if (SDL_WaitEventTimeout(&e, 30)) {
            handle(&z, &e);
            while (z.result == -2 && SDL_PollEvent(&e)) {
                handle(&z, &e);
            }
        }
    }

    if (z.job != NULL) {
        cancel(&z);
        if (z.thread != NULL) {
            SDL_WaitThread(z.thread, NULL);
        }
        free(z.job);
    }
    if (z.result == EIDOS_WIZARD_INSTALLED && rom_out != NULL && n > 0) {
        snprintf(rom_out, n, "%s", z.rom);
    }
    if (z.tex != NULL) {
        SDL_DestroyTexture(z.tex);
    }
    if (z.ren != NULL) {
        SDL_DestroyRenderer(z.ren);
    }
    SDL_DestroyWindow(z.win);
    if (own_video) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    free(z.fb);
    return z.result;
}

/* ------------------------------------------------------------------ */
/* First run                                                           */
/* ------------------------------------------------------------------ */

int eidos_firstrun(const BwSetupRequest *req, char *data_dir, size_t n,
                   int *exit_status)
{
    BwSetupResolved r;
    BwSetupRequest none;
    EidosWizardConfig cfg;
    char rom[BW_SETUP_PATH_MAX];
    int rc;

    if (req == NULL) {
        memset(&none, 0, sizeof(none));
        req = &none;
    }
#ifdef __ANDROID__
    {
        /* Context.getFilesDir(): only the Java side knows it. */
        const char *files = SDL_AndroidGetInternalStoragePath();
        if (files != NULL) {
            char home[BW_SETUP_PATH_MAX];
            snprintf(home, sizeof(home), "%s/eidos", files);
            bw_setup_set_home(home);
        }
    }
#endif
    *exit_status = 1;
    switch (bw_setup_prepare(req, &r)) {
    case BW_PREPARE_READY:
        snprintf(data_dir, n, "%s", r.data_dir);
        return 1;
    case BW_PREPARE_EXIT_OK:
        *exit_status = 0;
        return 0;
    case BW_PREPARE_WIZARD:
        break;
    default:
        return 0;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.extract_dir = r.extract_dir;
    cfg.reason = r.state != BW_ROOT_MISSING ? r.why : NULL;
    if (r.state == BW_ROOT_OLD && r.rom[0] != '\0'
        && bw_path_kind_utf8(r.rom) == BW_PATH_FILE) {
        cfg.rom = r.rom;                /* make it again, with progress */
    }
    rc = eidos_wizard_run(&cfg, rom, sizeof(rom));
    if (rc == EIDOS_WIZARD_QUIT) {
        *exit_status = 0;
        return 0;
    }
    if (rc != EIDOS_WIZARD_INSTALLED) {
        BwSetupRequest headless = *req;
        headless.no_window = 1;
        (void)bw_setup_prepare(&headless, &r);     /* prints what to do */
        return 0;
    }
    if (r.source != BW_SOURCE_ENV && bw_setup_remember(r.extract_dir, rom) != 0) {
        fprintf(stderr, "eidos: the settings file could not be written; next "
                        "time pass --data-root \"%s\"\n", r.extract_dir);
    }
    if (bw_setup_resolve(req, &r) != 0 || r.state != BW_ROOT_OK) {
        fprintf(stderr, "eidos: %s\n", r.why);
        return 0;
    }
    snprintf(data_dir, n, "%s", r.data_dir);
    return 1;
}
