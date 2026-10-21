# Mod data tables

A mod can ship its own tables of numbers: level caps per badge, banned
species, gym trainer ids, a zone-to-area map. Rules and code read them by
key. The port only reads the files; what a table means is the mod's own
business.

## Declaring a table

One manifest line per table (`eidos_mod.txt`):

    table = caps tables/caps.txt

- The name is letters, digits and `_`, starts with a letter or `_`, and is
  at most 31 characters.
- The path is relative to the mod's folder.

The port holds up to 128 tables, counting every loaded mod. A
table that fails to load is reported on stderr (or in the `--mod-log`
file) with the mod's name, file and line, and the mod's other tables and
rules still load. A rule that reads a missing table is refused when it is
bound.

## The file

Text, one entry per line:

    # level cap per badge count
    0 = 12
    1 = 20        # after the first gym
    0x2 = 25
    -1 = 0
    default = 100

- A key and a value are signed 32-bit numbers, decimal or `0x` hex.
- `#` starts a comment, to the end of the line. Blank lines are skipped.
- `default = N` is the value of every key the table does not list. With no
  `default` line it is 0.
- When a key appears twice, the later line wins.
- At most 65536 entries per table, and the file is at most 16 MiB.

## Reading a table

**Rules** (`docs/mod-rules-language.md`):

    table.caps[badges]

The key is any number expression. A key the table does not have reads
the default.

**Code** (services table, interface version 4):

    s32 cap;
    int found = host->services->table_get("caps", badges, &cap);

`table_get` answers 0 when the key is in the table, 1 when it is not
(`*out` then holds the default), and -1 when the calling mod has no table
with that name (`*out` is then 0).

## Whose table

A table belongs to the mod that declares it. Two mods can each have a
table called `caps`, and each one reads its own. A mod cannot read another
mod's table.
