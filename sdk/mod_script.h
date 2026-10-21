/*
 * sdk/mod_script.h -- the script command hooks (docs/mod-events.md,
 * section 5; docs/mod-implementation.md, section 5).
 *
 * The field script VM and the battle effect VM dispatch every command
 * through one table, and the port hooks the dispatch: a mod can observe a
 * command, change its operands, or replace it, without one declared row per
 * command.
 *
 * The API here is the port's own, and sdk/mod_event.h is its model: a
 * register call, a run call, a reset and a count.  What a mod sees is the
 * command's view (BwModScriptCommand), which is part of the mod ABI in
 * sdk/mod_api.h.
 */

#ifndef BW_PORT_MOD_SCRIPT_H
#define BW_PORT_MOD_SCRIPT_H

#include "types.h"
#include "mod_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The command's view and the handler type are the mod ABI's (sdk/mod_api.h),
 * so the port's own working copy and the one a mod sees are one definition.
 */
typedef BwScriptCommand BwModScriptCommand;
typedef BwScriptHandler BwModScriptHandler;

/* The two VMs the port hooks, in the ABI's names. */
#define BW_MOD_SCRIPT_FIELD  BW_SCRIPT_VM_FIELD
#define BW_MOD_SCRIPT_BATTLE BW_SCRIPT_VM_BATTLE
#define BW_MOD_SCRIPT_VM_COUNT 2

/* What a handler answers, in the ABI's names. */
#define BW_MOD_SCRIPT_CONTINUE BW_SCRIPT_CONTINUE
#define BW_MOD_SCRIPT_REPLACE  BW_SCRIPT_REPLACE

/* How many operand bytes a command may carry in the view: the widest
 * command of the two tables, in bytes, which is the ABI's number. */
#define BW_MOD_SCRIPT_ARGS_MAX BW_SCRIPT_ARGS_MAX

/* How many commands each VM's table holds. */
#define BW_MOD_SCRIPT_FIELD_COMMANDS  609u
#define BW_MOD_SCRIPT_BATTLE_COMMANDS  78u

/*
 * The game's own handler for the command, as the hook calls it: the game's
 * own context first, then the command the mod's handlers saw.
 */
typedef int (*BwModScriptGameHandler)(void *ctx, BwModScriptCommand *cmd);

/*
 * Bind a handler to a command.  `vm` is BW_MOD_SCRIPT_*, `opcode` the
 * command's opcode, `ns` the mod's state namespace, so its state calls
 * reach its own file while its handler runs.  Every handler bound to a
 * command runs, in load order (docs/mod-events.md, section 3).  Returns 0, or
 * -1 when the VM is not one of the port's two, when the game's table has no
 * command at that opcode, or when the handler table is full.
 */
int bw_mod_script_register(u32 vm, u16 opcode, BwModScriptHandler handler,
                           const char *ns);

/*
 * The port's own hook into the registration: the state of the mod that is
 * registering.  A handler receives the state its own mod's start-up
 * returned, so the loader tells the layer which mod is registering before it
 * does.  Not part of the mod ABI.
 */
void bw_mod_script_set_registering_state(void *state);

/*
 * Run the handlers bound to a command.  Every handler runs, in load order,
 * and each one sees the command as the ones before it left it, so the writes
 * to the operands chain.  A handler that replaces the command does not stop
 * the later handlers.  Returns 1 when a handler replaced the command.
 */
int bw_mod_script_run(u32 vm, u16 opcode, BwModScriptCommand *cmd);

/*
 * The hook: one command through the registry.  The game's own handler runs
 * unless a handler replaced the command; the call answers 1 when the command
 * was replaced and 0 when the game's own handler ran.
 */
int bw_mod_script_dispatch(u32 vm, BwModScriptCommand *cmd,
                           BwModScriptGameHandler game, void *ctx);

/* Drop every handler: the state at start-up and at a reset. */
void bw_mod_script_reset(void);

/* How many handlers are bound to a command, for the tests and the reports. */
u32 bw_mod_script_handler_count(u32 vm, u16 opcode);

/*
 * The operand width of a command, in bytes: how far the command reaches
 * after its opcode's own halfword.  A command the game's table does not
 * have reads BW_MOD_SCRIPT_WIDTH_NONE.
 */
u32 bw_mod_script_operand_width(u32 vm, u32 opcode);

/* How many commands the VM's table holds. */
u32 bw_mod_script_opcode_count(u32 vm);

/*
 * Which VM a command table of `cmd_count` entries belongs to:
 * BW_MOD_SCRIPT_* for the field VM and the battle effect VM, and -1 for any
 * other table (the trainer AI's and the station act script's, which the
 * port does not touch).
 */
int bw_mod_script_vm_of(u32 cmd_count);

/* The VM's name, for the reports. */
const char *bw_mod_script_vm_name(u32 vm);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_SCRIPT_H */
