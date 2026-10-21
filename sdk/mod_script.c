/*
 * sdk/mod_script.c -- the port's hook on the two script VMs (docs/mod-implementation.md,
 * section 5; docs/mod-events.md, section 5, "Script command hooks").
 *
 * The field script VM and the battle effect VM each run their commands
 * through one table:
 *
 *   - the field script VM: 609 commands at d_0217062C (ov010), the table
 *     SCRIPT_SYS_CreateVm (0x02158BFC) hands to the VM it creates;
 *   - the battle effect VM: 78 commands at g_02209E88 (ov094), the table
 *     BTLV_EFFVM_Create (0x021F93F4) hands to the VM it creates.
 *
 * Both are the same machine: the game's script VM (src/game/arm9/vm.c), whose
 * dispatch function VM_Control (0x02011298) fetches a u16 opcode and calls
 * the table's entry for it.  The port stands in for that dispatch function
 * (docs/hooks.md) and routes every command through this file: the port looks
 * the opcode up in its registry, runs the handlers a mod bound to it, and
 * calls the game's own handler when no mod handled the command.
 *
 * This file holds three things:
 *
 *   - the registry: one list of handlers per (VM, opcode), in load order,
 *     with the rules of the event dispatch (docs/mod-events.md, section 3):
 *     every handler runs, a replacement does not stop the later handlers,
 *     and the last write to the operands wins;
 *   - the opcode tables: the operand width of every command of both VMs, so
 *     the port knows how far a command reaches and where the VM's program
 *     counter stands after it;
 *   - the hook, bw_mod_script_dispatch, which the stand-in calls.
 *
 * The two VMs are told apart by the size of their command table: the field
 * VM's table has 609 entries and the battle effect VM's 78.  The game's two
 * other VMs (the trainer AI's, in ov096, and the station act script's, in
 * ov132) have tables of their own and are left alone; the stand-in asks
 * bw_mod_script_vm_of() which VM a table belongs to and calls the game's
 * own handler itself when it is neither of the port's two.
 *
 * The widths are built into this file, not read at run time: the aletheia
 * tree is a build-time thing (the port ships the player's game data, not
 * aletheia's sources), and once the widths are here they are the port's own
 * data.  They come from aletheia's tools/assets/scripts.py (OPERAND_WIDTH,
 * the field VM's 609 commands) and from tools/assets/btleff.py
 * (OPERAND_WIDTH, the battle effect VM's 78 commands, read off the handlers
 * of the table that layout/data_objects.json names as g_02209E88).
 *
 * The stand-in's side of the contract, for the port source that takes the
 * game's dispatch function's place:
 *
 *   - it builds the command: the opcode the VM fetched, the operands it
 *     reads off the VM's cursor (the whole operand area, as bytes), and
 *     the program counter before the command;
 *   - it calls bw_mod_script_dispatch() with the game's own handler, which
 *     the stand-in wraps: the wrapper writes the operands the mod's
 *     handlers changed back into the script, and calls the game's handler
 *     out of the VM's command table;
 *   - when the call answers 1, the command was replaced and the game's own
 *     handler did not run, so the stand-in moves the VM's cursor over the
 *     command itself, by the width bw_mod_script_operand_width() gives.
 *
 * The program counter in the view is a number, not a pointer: it is the
 * stand-in's own count of where the command sits in the script, so the view
 * carries no host pointer and reads the same at every pointer width.
 *
 * The operands in the view are bytes, and the view holds all of them: the
 * width table below gives the width of every command of both VMs, and the
 * widest of them (the battle effect VM's opcode 8, at 60 bytes) is the
 * view's capacity (BW_SCRIPT_ARGS_MAX, sdk/mod_api.h).  The stand-in
 * therefore copies the command's width in bytes into the view and writes
 * the bytes back, and no operand of any command is out of the view's
 * reach.  The widths are checked against the capacity when the layer
 * starts, so a table that grows past it is reported and not ignored.
 *
 * This file is the port's own; a mod never sees it.  What a mod sees is
 * sdk/mod_api.h (the commands and their payloads) and sdk/mod_state.h (the
 * state and the service calls).  The types below are the port's working
 * copy of that surface; they move into sdk/mod_api.h when the mod-facing
 * half of the script hooks lands.
 */
#include "types.h"
#include "mod_script.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The port-internal API                                              */
/* ------------------------------------------------------------------ */

/* The API is in sdk/mod_script.h: the two VMs, the command's view, the
 * handler types, and the register, run, dispatch, reset and count calls. */

/* ------------------------------------------------------------------ */
/* The opcode tables                                                  */
/* ------------------------------------------------------------------ */

/*
 * The operand width of every command: the number of bytes the command's
 * handler reads after the opcode's own halfword.  BW_MOD_SCRIPT_WIDTH_NONE
 * marks a command the game's table does not have (nine of the field VM's
 * 609: opcodes 137, 143, 144, 145, 153, 154, 156, 157 and 435).
 *
 * The field VM's widths are aletheia's tools/assets/scripts.py, the
 * OPERAND_WIDTH table; the battle effect VM's are the same table in
 * tools/assets/btleff.py.  Nine of the field VM's widths are odd (1, 3, 5,
 * 7): those commands read a byte or two straight off the cursor instead of
 * fetching a halfword.
 */
#define BW_MOD_SCRIPT_WIDTH_NONE 0xFFu

static const u8 g_field_width[BW_MOD_SCRIPT_FIELD_COMMANDS] = {
      0,   0,   0,   2,   4,   0,   2,   2,   2,   2,   2,   0,   0,   0,   0,   0,
      2,   2,   4,   4,   0,   4,   0,   0,   0,   4,   4,   6,  10,   0,   4,   5,
      5,   2,   2,   2,   2,   4,   4,   4,   4,   4,   4,   4,   4,   4,   0,   0,
      0,   0,   0,   2,   4,   4,   0,   2,   3,   0,   8,   2,  10,   8,   0,   0,
      4,   0,   0,   4,   0,   4,   0,   2,  12,  12,   3,   0,   1,   3,   6,   3,
      2,   2,   2,   2,   2,   2,   2,   3,   3,   2,   2,   2,   4,   2,   2,   2,
      2,   3,   3,   2,   6,   0,   4,   6,   4,  12,   4,   2,   2,  10,   2,   4,
     10,   6,   8,   4,   0,   2,   8,   0,   2,   6,   2,   8,   6,   8,   2,   4,
      2,   0,   4,   2,   2,   6,   8,   6,   6, 255,   4,   2,   0,   2,   0, 255,
    255, 255,   4,   4,   8,   2,   2,   4,   2, 255, 255,   4, 255, 255,   0,   2,
      0,   2,   4,   2,   2,   4,   2,   0,   0,   2,   0,   4,   0,   7,   5,   6,
      0,   0,   5,   8,   0,   6,   6,   6,   6,   4,   4,   4,   2,   4,   8,   8,
     10,   6,   8,   0,  10,   0,   2,   0,   2,   0,   2,   4,   2,   2,   2,   2,
      4,   4,   2,   2,   4,   4,   2,   2,   4,   2,   6,   0,  10,   4,   4,   6,
      2,   2,   2,   0,   2,   4,   4,   2,   4,   2,   2,   2,   0,   0,   2,   2,
      2,   2,   4,   4,   4,   4,   4,   2,   4,   2,   2,   4,   4,   6,   4,   4,
      2,   4,   4,   4,   0,   6,   2,   6,   4,   8,   6,   6,   8,   4,  18,   6,
      6,   6,   4,   4,   4,   6,   4,   4,   6,   6,   8,   6,   6,   2,   2,   4,
      4,   4,   8,  18,   4,   0,   8,   8,   2,   4,   2,   6,   2,   8,   6,   2,
      0,   0,   2,   2,   0,   0,   4,   0,   6,   2,   2,   2,   0,   0,   0,   0,
      0,   0,   0,  22,   2,   0,   4,   2,  16,   4,   0,   0,   0,   4,   6,   4,
      2,   4,   0,   2,   4,   2,   2,   0,   4,   0,   8,   2,   0,   2,   0,   2,
      2,   0,   0,   0,   4,   2,   0,   3,   2,   5,   4,   8,   2,   2,   4,   0,
      2,   0,   0,   2,   2,   2,   2,   0,   6,   0,   0,   2,   2,   0,   2,   4,
      2,   2,   2,   0,   2,   2,   2,   2,   2,   2,   0,   0,   2,   2,   2,  16,
      2,   2,   2,   2,   2,   2,   0,   4,   2,   2,   4,   2,   2,   0,   2,   2,
      2,   2,   2,   2,   2,   0,   0,   4,   4,   8,   8,   2,   0,   0,   0,   0,
      0,   0,   0, 255,   0,   0,   0,   0,   0,   4,   4,   0,   2,   4,   4,   6,
      0,   2,   0,   0,   4,   2,   6,   0,   0,   4,   0,   4,   0,   2,   4,   6,
      0,   2,   0,   6,   0,   0,   2,   2,   4,   2,   4,   2,   4,   4,   4,   6,
      8,   2,   2,   2,   4,   6,   6,   6,   4,   4,   8,   8,  10,   8,   4,   4,
      4,   4,   0,   8,   0,   0,   6,  12,   0,   0,   0,   2,   6,   4,   4,   0,
      2,   2,   2,   2,   4,   2,   2,   4,   2,   8,   8,   4,   8,   8,   4,   6,
      8,   0,   2,   6,   4,   4,   2,   2,   2,   2,   1,   1,   4,   0,   2,   0,
      8,  12,   8,   2,   2,   6,   2,   8,   4,   4,   2,   4,   4,   4,   0,   4,
      2,   2,   4,   4,   4,   2,   4,   6,   2,   2,   4,   4,   0,   4,   4,   0,
      2,   2,   4,   4,   2,   4,   2,   8,   4,   8,   4,   2,   2,   4,   0,   0,
      0,   6,   0,   4,   2,  12,   6,   0,   0,   2,  10,   8,  12,   2,   0,   0,
      0,
};

static const u8 g_battle_width[BW_MOD_SCRIPT_BATTLE_COMMANDS] = {
     20,  40,  24,  24,   8,   0,   4,  44,  60,  44,  40,   4,  44,  52,  44,  52,
      0,   0,  28,  36,  24,  28,  24,  24,  24,  16,   8,  20,   8,   8,  28,   4,
     20,  32,   8,   4,   4,  24,  16,  20,   4,  24,  20,   8,  28,  28,  28,  28,
      8,  20,   4,   8,  36,   4,  32,  36,   4,   4,   4,  28,  28,  12,   4,   4,
      8,  12,   0,  28,   4,  24,  12,   0,   4,   0,   4,   4,   4,   0,
};

/* Whether the widths have been checked against the view's capacity. */
static int g_widths_reported;

/* ------------------------------------------------------------------ */
/* The registry                                                       */
/* ------------------------------------------------------------------ */

/* How many handlers the table holds over both VMs together. */
#define BW_MOD_SCRIPT_HANDLERS_MAX 256

/* How many commands carry handlers at once. */
#define BW_MOD_SCRIPT_COMMANDS_MAX 128

/* The hash over the (VM, opcode) pairs; a power of two. */
#define BW_MOD_SCRIPT_HASH_SIZE 64

/*
 * One handler of one command.  The lists are per command and run in load
 * order, so the record holds the next handler of its own command.
 */
typedef struct BwModScriptHandlerRec {
    BwModScriptHandler fn;
    void              *state;   /* the mod's own state, the value it returned */
    const char        *ns;      /* the mod's state namespace, for its state calls */
    u16                next;    /* the next handler of the command, index + 1 */
} BwModScriptHandlerRec;

/* One command that has handlers: the (VM, opcode) pair and its list. */
typedef struct BwModScriptCommandRec {
    u32 vm;
    u16 opcode;
    u16 head;    /* the first handler of the command, index + 1 */
    u16 tail;    /* the last handler of the command, index + 1 */
} BwModScriptCommandRec;

static BwModScriptHandlerRec g_handlers[BW_MOD_SCRIPT_HANDLERS_MAX];
static u32                    g_handler_count;

static BwModScriptCommandRec g_commands[BW_MOD_SCRIPT_COMMANDS_MAX];
static u32                    g_command_count;

static u16 g_hash[BW_MOD_SCRIPT_HASH_SIZE];              /* the command, index + 1 */
static u16 g_hash_next[BW_MOD_SCRIPT_COMMANDS_MAX];       /* the next in the bucket */

/* The state of the mod that is registering a handler right now. */
static void *g_registering_state;

/*
 * The saved-data layer's namespace, the identity a mod's own file is under
 * (docs/mod-state.md).  It is the port's own layer, and it may be absent
 * (the layer that only runs the commands), so the call is weak.
 */
extern void bw_mod_savedata_set_namespace(const char *ns) __attribute__((weak));
extern const char *bw_mod_savedata_namespace(void) __attribute__((weak));

/* The hash over a (VM, opcode) pair. */
static u32 command_hash(u32 vm, u16 opcode)
{
    return (((vm * 131u) + (u32)opcode) & (BW_MOD_SCRIPT_HASH_SIZE - 1));
}

/* The command record of the pair, or NULL when it has none. */
static BwModScriptCommandRec *command_find(u32 vm, u16 opcode)
{
    u32 i = g_hash[command_hash(vm, opcode)];

    while (i != 0) {
        BwModScriptCommandRec *c = &g_commands[i - 1];

        if (c->vm == vm && c->opcode == opcode) {
            return c;
        }
        i = g_hash_next[i - 1];
    }
    return NULL;
}

/* The width table of a VM, or NULL when the VM is not one of the port's. */
static const u8 *width_table(u32 vm)
{
    switch (vm) {
    case BW_MOD_SCRIPT_FIELD:  return g_field_width;
    case BW_MOD_SCRIPT_BATTLE: return g_battle_width;
    default:                   return NULL;
    }
}

/* How many commands the VM's table holds, or 0 when it is not one of the
 * port's. */
static u32 vm_opcode_count(u32 vm)
{
    switch (vm) {
    case BW_MOD_SCRIPT_FIELD:  return (u32)BW_MOD_SCRIPT_FIELD_COMMANDS;
    case BW_MOD_SCRIPT_BATTLE: return (u32)BW_MOD_SCRIPT_BATTLE_COMMANDS;
    default:                   return 0;
    }
}

/*
 * Whether every command's operands fit the view.
 *
 * The view's capacity is the ABI's number (BW_SCRIPT_ARGS_MAX,
 * sdk/mod_api.h) and it is the width of the widest command of the two
 * tables: the battle effect VM's opcode 8 carries 60 bytes of operands
 * and the field VM's widest (its opcode 323) carries 22.  A width above
 * the capacity would be a command whose operands the view cannot carry,
 * so the tables are checked against it when the layer starts: the widest
 * of them has to fit, or the view is too small for the game.
 */
static int widths_fit(void)
{
    u32 vm;

    for (vm = 0; vm < (u32)BW_MOD_SCRIPT_VM_COUNT; vm++) {
        u32 count = vm_opcode_count(vm);
        u32 op;

        for (op = 0; op < count; op++) {
            u32 width = bw_mod_script_operand_width(vm, op);

            if (width != BW_MOD_SCRIPT_WIDTH_NONE &&
                width > (u32)BW_MOD_SCRIPT_ARGS_MAX) {
                return 0;
            }
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The registration                                                   */
/* ------------------------------------------------------------------ */

int bw_mod_script_register(u32 vm, u16 opcode, BwModScriptHandler handler,
                           const char *ns)
{
    const u8 *widths = width_table(vm);
    u32 count = vm_opcode_count(vm);
    BwModScriptCommandRec *c;
    BwModScriptHandlerRec *rec;

    if (handler == NULL || widths == NULL) {
        return -1;
    }
    if ((u32)opcode >= count) {
        /* The game's table has no command there: the VM never dispatches
         * that opcode, so a handler for it would never run. */
        return -1;
    }
    if (widths[opcode] == BW_MOD_SCRIPT_WIDTH_NONE) {
        /* The game's table marks that opcode as having no command either
         * (the nine the field VM's table leaves empty). */
        return -1;
    }
    if (g_handler_count >= BW_MOD_SCRIPT_HANDLERS_MAX) {
        fprintf(stderr, "eidos: the script command table is full "
                "(%u handlers)\n", (unsigned int)BW_MOD_SCRIPT_HANDLERS_MAX);
        return -1;
    }
    c = command_find(vm, opcode);
    if (c == NULL) {
        u32 bucket;

        if (g_command_count >= BW_MOD_SCRIPT_COMMANDS_MAX) {
            fprintf(stderr, "eidos: the script command table is full "
                    "(%u commands)\n", (unsigned int)BW_MOD_SCRIPT_COMMANDS_MAX);
            return -1;
        }
        bucket = command_hash(vm, opcode);
        c = &g_commands[g_command_count];
        c->vm = vm;
        c->opcode = opcode;
        c->head = 0;
        c->tail = 0;
        g_hash_next[g_command_count] = g_hash[bucket];
        g_hash[bucket] = (u16)(g_command_count + 1);
        g_command_count++;
    }
    rec = &g_handlers[g_handler_count];
    rec->fn = handler;
    rec->state = g_registering_state;
    rec->ns = ns;
    rec->next = 0;
    g_handler_count++;
    if (c->tail != 0) {
        g_handlers[c->tail - 1].next = (u16)g_handler_count;
    } else {
        c->head = (u16)g_handler_count;
    }
    c->tail = (u16)g_handler_count;
    return 0;
}

void bw_mod_script_set_registering_state(void *state)
{
    g_registering_state = state;
}

/* ------------------------------------------------------------------ */
/* The dispatch                                                       */
/* ------------------------------------------------------------------ */

/*
 * Every handler of the command runs, in load order.  Each one sees the
 * command as the ones before it left it, so the writes to the operands
 * chain and the last write wins.  A handler that replaces the command does
 * not stop the later handlers, but the command stays replaced.
 */
int bw_mod_script_run(u32 vm, u16 opcode, BwModScriptCommand *cmd)
{
    BwModScriptCommandRec *c;
    u32 i;
    int replaced = 0;

    if (cmd == NULL) {
        return 0;
    }
    c = command_find(vm, opcode);
    if (c == NULL) {
        return 0;
    }
    for (i = c->head; i != 0; i = g_handlers[i - 1].next) {
        const char *saved = NULL;

        if (bw_mod_savedata_set_namespace != NULL && g_handlers[i - 1].ns != NULL) {
            /* The mod's own state calls are about its own file, so its
             * namespace is in force while its handler runs (the event path
             * does the same: docs/mod-state.md, section 2). */
            saved = bw_mod_savedata_namespace();
            bw_mod_savedata_set_namespace(g_handlers[i - 1].ns);
        }
        if (g_handlers[i - 1].fn(g_handlers[i - 1].state, cmd) ==
            BW_MOD_SCRIPT_REPLACE) {
            replaced = 1;
        }
        if (saved != NULL) {
            bw_mod_savedata_set_namespace(saved);
        }
    }
    return replaced;
}

int bw_mod_script_dispatch(u32 vm, BwModScriptCommand *cmd,
                           BwModScriptGameHandler game, void *ctx)
{
    u32 width;
    int replaced;

    if (cmd == NULL) {
        return 0;
    }
    replaced = bw_mod_script_run(vm, cmd->opcode, cmd);

    /*
     * The program counter after the command: the opcode's own halfword and
     * the command's operands.  A command the port replaces is stepped over
     * as well, so the script goes on where it was: the stand-in moves the
     * VM's own cursor to this value, and the game's own handler never runs
     * to move it.
     */
    width = bw_mod_script_operand_width(vm, cmd->opcode);
    if (width != BW_MOD_SCRIPT_WIDTH_NONE) {
        cmd->next_pc = cmd->pc + 2u + width;
    }

    if (replaced) {
        return 1;
    }
    if (game != NULL) {
        (void)game(ctx, cmd);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The queries                                                        */
/* ------------------------------------------------------------------ */

u32 bw_mod_script_handler_count(u32 vm, u16 opcode)
{
    BwModScriptCommandRec *c = command_find(vm, opcode);
    u32 i;
    u32 n = 0;

    if (c == NULL) {
        return 0;
    }
    for (i = c->head; i != 0; i = g_handlers[i - 1].next) {
        n++;
    }
    return n;
}

u32 bw_mod_script_operand_width(u32 vm, u32 opcode)
{
    const u8 *widths = width_table(vm);
    u32 count = vm_opcode_count(vm);

    if (widths == NULL || opcode >= count) {
        return BW_MOD_SCRIPT_WIDTH_NONE;
    }
    return widths[opcode];
}

u32 bw_mod_script_opcode_count(u32 vm)
{
    return vm_opcode_count(vm);
}

int bw_mod_script_vm_of(u32 cmd_count)
{
    if (cmd_count == (u32)BW_MOD_SCRIPT_FIELD_COMMANDS) {
        return (int)BW_MOD_SCRIPT_FIELD;
    }
    if (cmd_count == (u32)BW_MOD_SCRIPT_BATTLE_COMMANDS) {
        return (int)BW_MOD_SCRIPT_BATTLE;
    }
    return -1;
}

const char *bw_mod_script_vm_name(u32 vm)
{
    switch (vm) {
    case BW_MOD_SCRIPT_FIELD:  return "field";
    case BW_MOD_SCRIPT_BATTLE: return "battle-effect";
    default:                   return "none";
    }
}

/* ------------------------------------------------------------------ */
/* The reset                                                          */
/* ------------------------------------------------------------------ */

void bw_mod_script_reset(void)
{
    memset(g_handlers, 0, sizeof(g_handlers));
    memset(g_commands, 0, sizeof(g_commands));
    memset(g_hash, 0, sizeof(g_hash));
    memset(g_hash_next, 0, sizeof(g_hash_next));
    g_handler_count = 0;
    g_command_count = 0;
    g_registering_state = NULL;

    if (!g_widths_reported) {
        g_widths_reported = 1;
        if (!widths_fit()) {
            fprintf(stderr, "eidos: a script command's operands do not fit "
                    "the view (%u bytes); the operands past that are the "
                    "game's own\n", (unsigned int)BW_MOD_SCRIPT_ARGS_MAX);
        }
    }
}
