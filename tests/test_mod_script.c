/*
 * tests/test_mod_script.c -- the script command hooks: the registry, the
 * handlers and the program counter (docs/mod-events.md, section 5,
 * "Script command hooks"; docs/mod-implementation.md, section 5).
 *
 * The field script VM and the battle effect VM each dispatch every command
 * through one table.  The port puts one hook on each dispatch, keyed by
 * opcode, so a mod can observe or replace any script command by its opcode
 * without one declared row per command.  The port's side of that hook is
 * sdk/mod_script.c; the contract it keeps is the one of the two documents:
 *
 *   - the registry: the hook looks the opcode up in a registry, and every
 *     handler bound to that opcode runs, in the mods' load order;
 *   - a handler that observes a command sees the command the game is
 *     running: its opcode and its operands;
 *   - a handler that changes the operands changes what the game's own
 *     handler sees: the handlers chain, as the event handlers do;
 *   - a handler that replaces the command keeps the game's own handler from
 *     running at all;
 *   - the program counter: a command the port replaces is still stepped
 *     over, so the script goes on where it was.
 *
 * Nothing here needs game data: the game's own handler for the command is
 * the test's own, and the command is the test's own.
 */

#include <stdio.h>
#include <string.h>

#include "mod_script.h"
#include "port_test.h"

/* ------------------------------------------------------------------ */
/* The commands the test uses                                          */
/* ------------------------------------------------------------------ */

/* The field VM's commands the test binds handlers to, and one it does
 * not.  They are real commands: the game's table has them, so a handler
 * for them would run. */
#define T_OPCODE        3u
#define T_OTHER_OPCODE  4u

/* The battle effect VM's command the test binds a handler to. */
#define T_BATTLE_OPCODE 6u

/* A command the game's table does not have: nine of the field VM's 609
 * are not commands at all (the port's file marks them, and a handler for
 * one would never run). */
#define T_NO_SUCH_OPCODE 137u

/* The operands the test's command carries, and what a handler changes
 * them to. */
#define T_ARG0        0x1111u
#define T_ARG1        0x2222u
#define T_CHANGED     0x3333u

/* The program counter the test's command starts at. */
#define T_PC          0x0200u

/* ------------------------------------------------------------------ */
/* What the handlers did                                               */
/* ------------------------------------------------------------------ */

/* The handlers, in the order they ran. */
static int g_ran[8];
static int g_ran_count;

/* The opcode each handler saw. */
static u16 g_saw_opcode[8];

/* The first operand each handler saw. */
static u32 g_saw_arg[8];

/* The mod's own state each handler was given. */
static void *g_saw_state[8];

/* How many times the game's own handler ran, and what it saw. */
static int g_game_calls;
static u16 g_game_opcode;
static u32 g_game_arg;

/* The two mods' own states, the values their start-up returned. */
static int g_first_state;
static int g_second_state;

static void note(int id, void *state, BwModScriptCommand *cmd)
{
    if (g_ran_count < (int)(sizeof(g_ran) / sizeof(g_ran[0]))) {
        g_ran[g_ran_count] = id;
        g_saw_opcode[g_ran_count] = cmd->opcode;
        g_saw_arg[g_ran_count] = cmd->nargs > 0u ? cmd->args[0] : 0u;
        g_saw_state[g_ran_count] = state;
        g_ran_count++;
    }
}

static void forget(void)
{
    memset(g_ran, 0, sizeof(g_ran));
    memset(g_saw_opcode, 0, sizeof(g_saw_opcode));
    memset(g_saw_arg, 0, sizeof(g_saw_arg));
    memset(g_saw_state, 0, sizeof(g_saw_state));
    g_ran_count = 0;
    g_game_calls = 0;
    g_game_opcode = 0;
    g_game_arg = 0;
}

/* ------------------------------------------------------------------ */
/* The handlers                                                        */
/* ------------------------------------------------------------------ */

/* The game's own handler: it runs when no mod replaced the command, and it
 * sees the operands the mod's handlers left. */
static int game_handler(void *ctx, BwModScriptCommand *cmd)
{
    (void)ctx;
    g_game_calls++;
    g_game_opcode = cmd->opcode;
    g_game_arg = cmd->nargs > 0u ? cmd->args[0] : 0u;
    return 0;
}

/* The first handler: it only looks at the command. */
static int handler_observe(void *mod_state, BwModScriptCommand *cmd)
{
    note(1, mod_state, cmd);
    return BW_MOD_SCRIPT_CONTINUE;
}

/* The second handler: it changes the command's first operand. */
static int handler_change(void *mod_state, BwModScriptCommand *cmd)
{
    note(2, mod_state, cmd);
    if (cmd->nargs > 0u) {
        cmd->args[0] = T_CHANGED;
    }
    return BW_MOD_SCRIPT_CONTINUE;
}

/* The third handler: it replaces the command, so the game's own handler
 * does not run. */
static int handler_replace(void *mod_state, BwModScriptCommand *cmd)
{
    note(3, mod_state, cmd);
    return BW_MOD_SCRIPT_REPLACE;
}

/* ------------------------------------------------------------------ */
/* The command                                                         */
/* ------------------------------------------------------------------ */

/* The command the test dispatches: the opcode, two operands and the
 * program counter it starts at. */
static BwModScriptCommand make_command(u16 opcode)
{
    BwModScriptCommand cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = opcode;
    cmd.nargs = 2;
    cmd.args[0] = T_ARG0;
    cmd.args[1] = T_ARG1;
    cmd.pc = T_PC;
    cmd.next_pc = 0;
    return cmd;
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

int main(void)
{
    BwModScriptCommand cmd;
    u32 width;

    printf("the two VMs\n");
    bw_mod_script_reset();
    BW_CHECK_EQ("the field VM has 609 commands",
                (long)bw_mod_script_opcode_count(BW_MOD_SCRIPT_FIELD), 609);
    BW_CHECK_EQ("the battle effect VM has 78",
                (long)bw_mod_script_opcode_count(BW_MOD_SCRIPT_BATTLE), 78);
    BW_CHECK_EQ("a table of 609 commands is the field VM's",
                (long)bw_mod_script_vm_of(609), (long)BW_MOD_SCRIPT_FIELD);
    BW_CHECK_EQ("a table of 78 commands is the battle effect VM's",
                (long)bw_mod_script_vm_of(78), (long)BW_MOD_SCRIPT_BATTLE);
    BW_CHECK_EQ("any other table is the port's neither",
                (long)bw_mod_script_vm_of(50), -1);

    printf("the registry\n");
    bw_mod_script_reset();
    forget();

    /* Two mods, each with its own state: the loader tells the layer which
     * mod is registering before it does. */
    bw_mod_script_set_registering_state(&g_first_state);
    BW_REQUIRE("the first handler is bound",
               bw_mod_script_register(BW_MOD_SCRIPT_FIELD, T_OPCODE,
                                      handler_observe) == 0);
    bw_mod_script_set_registering_state(&g_second_state);
    BW_REQUIRE("the second handler is bound",
               bw_mod_script_register(BW_MOD_SCRIPT_FIELD, T_OPCODE,
                                      handler_change) == 0);
    bw_mod_script_set_registering_state(NULL);
    BW_CHECK_EQ("two handlers are bound to the command",
                (long)bw_mod_script_handler_count(BW_MOD_SCRIPT_FIELD,
                                                  T_OPCODE), 2);
    BW_CHECK_EQ("no handler is bound to the other command",
                (long)bw_mod_script_handler_count(BW_MOD_SCRIPT_FIELD,
                                                  T_OTHER_OPCODE), 0);
    BW_CHECK_EQ("the other VM has its own registry",
                (long)bw_mod_script_handler_count(BW_MOD_SCRIPT_BATTLE,
                                                  T_OPCODE), 0);
    BW_CHECK_EQ("a command the game's table does not have is refused",
                bw_mod_script_register(BW_MOD_SCRIPT_FIELD, T_NO_SUCH_OPCODE,
                                       handler_observe), -1);

    printf("a handler that observes a command\n");
    cmd = make_command(T_OPCODE);
    BW_CHECK_EQ("the command goes through", bw_mod_script_dispatch(
                     BW_MOD_SCRIPT_FIELD, &cmd, game_handler, NULL), 0);
    BW_CHECK_EQ("both handlers ran", g_ran_count, 2);
    BW_CHECK_EQ("the first handler ran first", g_ran[0], 1);
    BW_CHECK_EQ("the second handler ran second", g_ran[1], 2);
    BW_CHECK_EQ("the handler saw the opcode", (long)g_saw_opcode[0],
                (long)T_OPCODE);
    BW_CHECK_EQ("the handler saw the operands", (long)g_saw_arg[0],
                (long)T_ARG0);
    BW_REQUIRE("the first handler got its own mod's state",
               g_saw_state[0] == (void *)&g_first_state);
    BW_REQUIRE("the second handler got its own mod's state",
               g_saw_state[1] == (void *)&g_second_state);
    BW_CHECK_EQ("the game's own handler ran", g_game_calls, 1);

    printf("a handler that changes the operands\n");
    /* The handlers chain: the second one saw what the first one left, and
     * the game's own handler saw what the second one left. */
    BW_CHECK_EQ("the second handler saw the operand the command carried",
                (long)g_saw_arg[1], (long)T_ARG0);
    BW_CHECK_EQ("the game's handler saw the change", (long)g_game_arg,
                (long)T_CHANGED);
    BW_CHECK_EQ("the game's handler saw the opcode", (long)g_game_opcode,
                (long)T_OPCODE);

    printf("a handler that replaces the command\n");
    bw_mod_script_reset();
    forget();

    BW_REQUIRE("the handler is bound",
               bw_mod_script_register(BW_MOD_SCRIPT_FIELD, T_OPCODE,
                                      handler_replace) == 0);
    cmd = make_command(T_OPCODE);
    BW_CHECK_EQ("the command was replaced", bw_mod_script_dispatch(
                     BW_MOD_SCRIPT_FIELD, &cmd, game_handler, NULL), 1);
    BW_CHECK_EQ("the handler ran", g_ran_count, 1);
    BW_CHECK_EQ("the game's own handler did not run", g_game_calls, 0);

    printf("the program counter over a replaced command\n");
    /* The command the port replaces is stepped over as well, so the script
     * goes on where it was: the program counter after a replaced command
     * is the one after the same command the game ran. */
    bw_mod_script_reset();
    forget();

    width = bw_mod_script_operand_width(BW_MOD_SCRIPT_FIELD, T_OPCODE);
    cmd = make_command(T_OPCODE);
    bw_mod_script_dispatch(BW_MOD_SCRIPT_FIELD, &cmd, game_handler, NULL);
    BW_CHECK_EQ("the game's own handler ran", g_game_calls, 1);
    BW_CHECK_EQ("the program counter advanced over the command",
                (long)cmd.next_pc, (long)(cmd.pc + 2u + width));
    {
        u32 after_game = cmd.next_pc;

        BW_REQUIRE("the replacing handler is bound",
                   bw_mod_script_register(BW_MOD_SCRIPT_FIELD, T_OPCODE,
                                          handler_replace) == 0);
        cmd = make_command(T_OPCODE);
        bw_mod_script_dispatch(BW_MOD_SCRIPT_FIELD, &cmd, game_handler, NULL);
        BW_CHECK_EQ("the game's own handler did not run", g_game_calls, 1);
        BW_CHECK_EQ("the program counter advanced over the command",
                    (long)cmd.next_pc, (long)(cmd.pc + 2u + width));
        BW_CHECK_EQ("and it advanced as it does for the game's own command",
                    (long)cmd.next_pc, (long)after_game);
    }

    printf("a command nothing is bound to\n");
    bw_mod_script_reset();
    forget();

    cmd = make_command(T_OTHER_OPCODE);
    BW_CHECK_EQ("the command goes through", bw_mod_script_dispatch(
                     BW_MOD_SCRIPT_FIELD, &cmd, game_handler, NULL), 0);
    BW_CHECK_EQ("no handler ran", g_ran_count, 0);
    BW_CHECK_EQ("the game's own handler ran", g_game_calls, 1);
    BW_CHECK_EQ("it saw the opcode", (long)g_game_opcode,
                (long)T_OTHER_OPCODE);
    BW_CHECK_EQ("it saw the operands the command carried", (long)g_game_arg,
                (long)T_ARG0);

    printf("the other VM's registry\n");
    bw_mod_script_reset();
    forget();

    BW_REQUIRE("the handler is bound to the battle VM",
               bw_mod_script_register(BW_MOD_SCRIPT_BATTLE, T_BATTLE_OPCODE,
                                      handler_observe) == 0);
    cmd = make_command(T_BATTLE_OPCODE);
    BW_CHECK_EQ("the battle command goes through", bw_mod_script_dispatch(
                     BW_MOD_SCRIPT_BATTLE, &cmd, game_handler, NULL), 0);
    BW_CHECK_EQ("the handler ran", g_ran_count, 1);

    cmd = make_command(T_BATTLE_OPCODE);
    BW_CHECK_EQ("the field command goes through", bw_mod_script_dispatch(
                     BW_MOD_SCRIPT_FIELD, &cmd, game_handler, NULL), 0);
    BW_CHECK_EQ("the field VM has no handler for it", g_ran_count, 1);
    BW_CHECK_EQ("and the game's own handler ran", g_game_calls, 2);

    printf("the reset\n");
    bw_mod_script_reset();
    BW_CHECK_EQ("no handler is bound",
                (long)bw_mod_script_handler_count(BW_MOD_SCRIPT_FIELD,
                                                  T_OPCODE), 0);
    forget();
    cmd = make_command(T_OPCODE);
    bw_mod_script_dispatch(BW_MOD_SCRIPT_FIELD, &cmd, game_handler, NULL);
    BW_CHECK_EQ("no handler ran", g_ran_count, 0);
    BW_CHECK_EQ("the game's own handler ran", g_game_calls, 1);

    BW_REPORT("test_mod_script");
}
