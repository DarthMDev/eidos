# The launcher

The launcher is a separate app in `launcher/`. It keeps profiles, installs
eidos versions, manages mods and starts the game. The plan is in
`docs/distribution.md`.

Stack: Tauri 2, Svelte 5, TypeScript and Rust, as in kenon. The look and the
shared Rust code come from morphe (tag `v0.1.0`).

## Pages

- Play: pick a profile, see the version it runs, press Play. One game runs
  per profile.
- Profiles: create, rename, copy and delete (delete asks). Each profile has a
  version: Latest, the local build, or an installed version. A Default
  profile always exists and cannot be renamed or deleted.
- Versions: lists the releases of the eidos repository, installs the file for
  this system, checks its SHA-256 against the release's `SHA256SUMS`, and
  unpacks it. Installed versions can be removed when no profile uses them.
- Mods: the mods of one profile. A mod can be switched off, added from a
  `.bw` file or a folder, and removed. The index address is set in Settings;
  with none set the page says "No index yet."
- Settings: theme, the releases repository, a GitHub token (only for a
  private repository), a local build folder and the mods index address.

## Folders

The launcher uses the folders in `docs/distribution.md`:
`%APPDATA%\eidos` on Windows, `~/.local/share/eidos` on Linux,
`~/Library/Application Support/eidos` on macOS.

```
versions/<id>/                 an installed eidos
profiles/<name>/profile.json   {"version": "latest" | "local" | "<id>"}
profiles/<name>/mods/          enabled mods
profiles/<name>/mods-disabled/ switched-off mods (eidos never sees them)
profiles/<name>/saves/
launcher/settings.json         repository, token, index address, local build
```

## Starting the game

Play runs the version's program in its own folder with:

```
--profile-dir <profile folder>
```

Nothing else. eidos opens its window, asks for the ROM on its first run and
keeps the extracted data in its own folder. The launcher never reads ROMs.

The program looked for in a version folder (or one folder below it):
`eidos.exe`, `eidos`, `eidos.AppImage`, `eidos_boot.exe`, `eidos_boot`.

The release file names are `eidos-<version>-windows.zip`,
`eidos-<version>-macos.zip` and `eidos-<version>-linux-x86_64.AppImage`, with a
`SHA256SUMS` file.

## Local build

Set a folder that holds `eidos.exe` or `eidos_boot.exe` in Settings. It
appears as the version "Local build". Latest means the local build when no
version is installed.

## Invite links

The installer registers `eidos://`. A second click reaches the running
launcher. For `eidos://join/<code>` the launcher asks which profile to use,
then starts it with:

```
--profile-dir <profile folder> eidos://join/<code>
```

eidos opens Multiplayer and joins. The code is checked first: letters,
digits, `-` and `_`, 64 characters at most.

TODO: "Get and join". It would read the host's version and mods from the
invite and install what is missing. It needs that data in the invite and the
mods index. Until then the launcher joins with the profile as it is.

## Build and run

```
cd launcher
npm install
npm run tauri dev          # run
npm run tauri build        # installer
npm run check              # Svelte and TypeScript checks
cd src-tauri && cargo test # Rust tests
```

Installers: NSIS per user (no administrator rights) on Windows, AppImage on
Linux, an app bundle on macOS. morphe is a private repository, so Cargo uses
the system git (`src-tauri/.cargo/config.toml`).

The icon is eidos's own (`assets/icon.png`), converted with `tauri icon`.
