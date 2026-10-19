/*
 * sdk/asm/data_0272EB9C.c -- 0x0272EB9C data_0272EB9C.
 *
 * aletheia's src/game/twl/card_rom.c files the 64-byte block at 0x0272EB9C
 * (the HMAC key CARD_InitRomHashContext reads, src/game/twl/card_hash.c) as
 * an `asm` body of `dcd` words, so the copy the port compiles has it cut.
 * It belongs to the DSi-mode card hash, which this NITRO port never runs:
 * the block is here as zeroed storage so the link has the name.  The bytes
 * are the game's own and stay out of the repository; a DSi-mode port would
 * read them from the cartridge's TWL static.
 */

unsigned char data_0272EB9C[0x40];
