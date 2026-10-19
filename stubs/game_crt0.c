/*
 * stubs/game_crt0.c -- what the port keeps of src/game/arm9/crt0.c.
 *
 * With EIDOS_SOURCE_TREE=game the boot leaves crt0.c out whole (the port
 * starts the game itself, cmake/boot.cmake).  The rest of the game still
 * calls one of its functions:
 *
 *   OSi_ReferSymbol (0x02004EB0)  an empty function card_backup.c calls
 *                                 with a symbol so the linker keeps it.
 */

void OSi_ReferSymbol(const void *symbol)
{
    (void)symbol;
}
