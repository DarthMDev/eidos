# The console around the game: settings, save chip, firmware flash

A DS cartridge does not boot on a bare CPU. Before the game's first
instruction, the console's ARM7 has copied the owner's settings into main
RAM, and during play it answers the game's requests for the cartridge's
save chip, the console's firmware flash, the microphone and the wireless
hardware. This document is what the port puts in their place, what each
answer is based on, and the files that hold the player's data. Every
protocol here was read off the matched ARM9 side of the SDK (aletheia
`src/matched/arm9_sdk/`) and its DWARF; nothing is taken from ARM7 code.

| Piece | File | PXI tag | Host file |
|---|---|---|---|
| User settings in RAM at power-on | `sdk/firmware.c` | none | `firmware.ini` |
| Save chip (cartridge backup) | `sdk/arm7/fs.c` | 11 | `pokeblack.sav` |
| Firmware flash | `sdk/arm7/nvram.c` | 4 | `firmware.bin` |
| Microphone | `sdk/arm7/mic.c` | 9 | none |
| Wireless | none, on purpose | 10 | none |

The files live in the save folder (`sdk/save.h`; per platform in
`docs/installer.md`, "Where the saves go") and are always replaced whole
and atomically: written to a sibling `.tmp`, synced, then renamed over the
old one (`MoveFileEx` with `REPLACE_EXISTING | WRITE_THROUGH` on Windows,
`rename` elsewhere; `sdk/plat_save.c`).

## User settings (`sdk/firmware.c`, `firmware.ini`)

The game reads the owner's settings from the shared area at `0x02FFFC80`
(0x70 bytes, the firmware's user-settings layout), the MAC address at
`0x02FFFCF4` and the allowed wireless channels at `0x02FFFCFA`. The
readers: `OS_GetOwnerInfo` (`func_02087C88.c`), `OS_GetOwnerInfoEx`
(`func_02087D28.c`; the port boots as a NITRO console, so its language
comes from the same block), `OS_GetOwnerRtcOffset`, `OS_GetMacAddress`,
`TP_GetUserInfo` (`func_0208A998.c`, the touch calibration),
`WM_GetAllowedChannel` (`func_0208EE60.c`). `bw_port_reset` writes the
area after clearing main RAM (`bw_firmware_apply`), as the ARM7's boot code
does before the ARM9 starts.

The settings come from `bw_firmware_set` (a front end's settings screen,
tests), else `<save dir>/firmware.ini`, else the defaults. The file is
`key = value` lines, `#` comments; a line that cannot be used is reported
on stderr and ignored, and a missing key keeps its default.
`bw_firmware_write_default` writes a fully commented file to edit.

| Key | Value | Default |
|---|---|---|
| `nickname` | up to 10 characters, UTF-8 (Basic Multilingual Plane) | `Player` |
| `message` | up to 26 characters | empty |
| `birthday` | `month/day` | `1/1` |
| `favorite_color` | 0-15, or `gray brown red pink orange yellow lime green dark_green sea_green light_blue blue dark_blue dark_purple light_purple dark_pink` | `gray` |
| `language` | `japanese english french german italian spanish chinese korean`, or 0-7 | `english` |
| `alarm` | `HH:MM on` or `HH:MM off` | `00:00 off` |
| `backlight` | 0-3 | 3 |
| `touch_calibration` | raw x, raw y, pixel x, pixel y of point 1, then of point 2 | `512 512 32 32 3584 2560 224 160` |
| `rtc_offset` | signed seconds | 0 |
| `mac` | `xx:xx:xx:xx:xx:xx` | `02:00:00:00:00:01` (locally administered) |
| `wireless_channels` | bit n = channel n | `0x3FFE` (1-13) |

The default calibration maps pixel (x, y) to the raw reading (16x, 16y),
which `TP_GetUserInfo` turns into an exact 16-to-1 scale: an input layer
that reports a touch at pixel (x, y) as raw (16x, 16y) reaches the game at
(x, y).

## The save chip (`sdk/arm7/fs.c`, `pokeblack.sav`)

libcard puts each request in a 0x60-byte command block (`cardi_arg`,
`CARDiCommandArg`: result, type, id, src, dst, len, then the chip's
spec) and sends the request number on tag 11 with the error bit set, which
libcard uses as its own "request" flag; the ARM7 answers with one flagged
word (`CARD_REQ_ACK`) after writing the result into the block, and
`CARDi_OnFifoRecv` wakes the requester. Request numbers are the SDK's
`CARDRequest` (2 identify, 6 read, 7 write, 8 program, 9 verify, 10-12 and
15 erase page, sector, chip, subsector, 13 and 14 status); bit n of the
spec's capability word says request n is available, which is how the
responder knows what a chip supports.

Pokemon Black identifies its chip as `0x00FF1302` (`arm9/func_02033534.c`):
flash, 512 KiB, 64 KiB sectors, 256-byte pages. It reads with request 6
and saves with request 7 followed by a verify (9) for every 256-byte piece
(`arm9/func_020336B8.c`). The chip follows flash rules: program clears
bits, page write replaces, erase sets 0xFF, an address past the end wraps.

The block and the ARM9's 256-byte bounce buffer
(`CARDi_backup_cache_page_buf`) are found by name, at their DS addresses
(`0x021501A0`, `0x02150740`, bound by the ARM9 data layer,
`docs/armdata.md`), not through the 26-bit address in the initialise word,
which cannot carry a 64-bit host pointer. Every transfer to or from memory
goes through the bounce buffer, so the save path works at both pointer
widths on the ARM7's side; the matched ARM9 code around it keeps host
addresses in `u32` fields, which makes the whole path a 32-bit-host path
today (`docs/pointer-width.md`).

`pokeblack.sav` is a raw image of the chip, the format emulators use. A
longer file (DeSmuME's `.dsv` adds a trailer) is read up to 512 KiB; a
shorter or missing one is padded with 0xFF, which the game reads as "no
save". Nothing is written while the game only reads. After a change the
whole image is written back once no request has changed it for a second
(`bw_arm7_backup_set_flush_delay`; checked at every ARM7 step), at
power-off (`bw_arm7_reset`), at exit and on `bw_arm7_backup_flush`. A crash
in that second loses the last second of writes, never the file: the game's
own two save slots exist for exactly that case.

## The firmware flash (`sdk/arm7/nvram.c`, `firmware.bin`)

libnvram sends SPI commands as words with a start bit (25), an end bit
(24), an index (bits 16-23) and 16 bits of argument: write enable and
disable, read status, read, page write and program, page and sector erase,
software reset. Every command is answered with one word carrying the
command byte with bit 7 set and a result byte. Writes obey the write-enable
latch; the status register always reads "ready", so `NVRAMi_Write`'s
alarm-driven polling ends at its first read.

The flash is 256 KiB. `firmware.bin`, when present, is used as is (a dump
of the player's own console works); otherwise the port builds one: a
header with the user-settings offset, the MAC address and the channels,
Wi-Fi settings format 0 (a NITRO console's three connection slots), and
erased Wi-Fi slots, which libncfg reads as "no connection set up". Either
way the two user-settings blocks at `0x3FE00` and `0x3FF00` are written
from the settings above, with their update counters and CRC-16, so the
flash and the RAM copy never disagree. The file is written back at the end
of every firmware write (the write-disable command that closes
`NVRAMi_Write`).

## Microphone (`sdk/arm7/mic.c`)

`MIC_Init` spins until tag 9 is served, and the game calls it at boot
(`arm9/func_02006CBC.c`). Until the port has an input path for a
microphone, the ARM7 answers: a one-shot sample is silence (the midpoint
for unsigned sample types, 0 for signed); starting auto or limited
sampling is refused with "illegal status", the SDK's result for sampling
that cannot start, so no buffer is written and no completion callback
fires; stopping succeeds. Every command is answered.

## Wireless (tag 10)

There is no wireless responder, and that is the answer. `WM_Init`'s core
(`func_0208E038.c`) asks once, without waiting, whether the ARM7 serves tag
10, and returns `WM_ERRCODE_WM_DISABLE` when it does not: the SDK's path
for a console whose wireless is unavailable. Serving the tag would commit
the port to the whole WM protocol, whose words are raw 32-bit buffer
addresses. A word sent on tag 10 anyway comes back with the error bit
(`sdk/pxi.c`).

## Tests

`tests/test_firmware.c`: the settings file (parse, format, errors, the
written default), the power-on area read back through the matched
`OS_GetOwnerInfo`, `OS_GetOwnerInfoEx` and `TP_GetUserInfo`, the flash
block's CRC, the atomic writes. `tests/test_backup.c`: every responder on
the wire at both widths, and on a 32-bit host the game's own CARD
(identify, write-and-verify, read, sector erase, an unsupported request),
NVRAM (init, read, a write across a page boundary with its status polling,
verify and a verify mismatch), MIC (init, start, stop) and WM (`WM_Init`'s
core) code compiled as written. With `BW_SAVE` set to a real 512 KiB save,
it is identified and read back byte for byte, and left untouched.
