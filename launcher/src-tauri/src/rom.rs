//! The player's ROMs (Settings, "ROMs"): which file is Pokemon Black and
//! which is Pokemon White.  The launcher reads the cartridge header's game
//! code to tell them apart, and gives the Black ROM to the game the way the
//! game remembers its own (`rom=` in its `config.txt`): when the game data
//! is missing or made by another version, the game makes it again from that
//! file without asking.

use std::fs;
use std::io::{self, Read};
use std::path::Path;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Game {
    Black,
    White,
}

impl Game {
    pub fn parse(text: &str) -> Option<Game> {
        match text {
            "black" => Some(Game::Black),
            "white" => Some(Game::White),
            _ => None,
        }
    }

    fn name(self) -> &'static str {
        match self {
            Game::Black => "Pokemon Black",
            Game::White => "Pokemon White",
        }
    }

    /// The first three letters of the game code (the fourth is the region).
    fn code(self) -> &'static [u8; 3] {
        match self {
            Game::Black => b"IRB",
            Game::White => b"IRA",
        }
    }
}

/// The four-letter game code at 0x0C of a DS cartridge header.
fn game_code(path: &Path) -> io::Result<[u8; 4]> {
    let mut head = [0u8; 16];
    fs::File::open(path)?.read_exact(&mut head)?;
    Ok([head[12], head[13], head[14], head[15]])
}

/// Checks that `path` is a ROM of `game`.
pub fn check(path: &Path, game: Game) -> Result<(), String> {
    if !path.is_file() {
        return Err(format!("{} is not a file.", path.display()));
    }
    let code = game_code(path).map_err(|e| format!("Could not read {}: {e}", path.display()))?;
    if &code[..3] != game.code() {
        let other = [Game::Black, Game::White].into_iter().find(|g| &code[..3] == g.code());
        return Err(match other {
            Some(g) => format!("That is a {} ROM, not {}.", g.name(), game.name()),
            None => format!("That file is not a {} ROM.", game.name()),
        });
    }
    Ok(())
}

/// Writes `rom=` into the game's `config.txt` in `home`, keeping its other
/// lines; None removes the line.
pub fn remember(home: &Path, rom: Option<&Path>) -> io::Result<()> {
    let file = home.join("config.txt");
    let old = fs::read_to_string(&file).unwrap_or_default();
    let mut text: String = old
        .lines()
        .filter(|l| !l.trim_start().starts_with("rom="))
        .map(|l| format!("{l}\n"))
        .collect();
    if let Some(p) = rom {
        if text.is_empty() {
            text.push_str("# eidos: where the game data is and which ROM it came from.\n");
        }
        text.push_str(&format!("rom={}\n", p.display()));
    }
    if text == old {
        return Ok(());
    }
    fs::create_dir_all(home)?;
    let tmp = file.with_extension("tmp");
    fs::write(&tmp, text)?;
    fs::rename(tmp, file)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rom(dir: &Path, name: &str, code: &[u8; 4]) -> std::path::PathBuf {
        let mut head = vec![0u8; 512];
        head[12..16].copy_from_slice(code);
        let p = dir.join(name);
        fs::write(&p, head).unwrap();
        p
    }

    #[test]
    fn tells_black_from_white() {
        let t = tempfile::tempdir().unwrap();
        let b = rom(t.path(), "b.nds", b"IRBO");
        let w = rom(t.path(), "w.nds", b"IRAO");
        let x = rom(t.path(), "x.nds", b"ADAE");
        assert!(check(&b, Game::Black).is_ok());
        assert!(check(&w, Game::White).is_ok());
        assert_eq!(check(&w, Game::Black).unwrap_err(), "That is a Pokemon White ROM, not Pokemon Black.");
        assert!(check(&x, Game::White).is_err());
        assert!(check(&t.path().join("none.nds"), Game::Black).is_err());
    }

    #[test]
    fn remember_keeps_the_other_lines() {
        let t = tempfile::tempdir().unwrap();
        let cfg = t.path().join("config.txt");
        fs::write(&cfg, "# x\ndata_root=/d\nrom=/old.nds\n").unwrap();
        remember(t.path(), Some(Path::new("/new.nds"))).unwrap();
        assert_eq!(fs::read_to_string(&cfg).unwrap(), "# x\ndata_root=/d\nrom=/new.nds\n");
        remember(t.path(), None).unwrap();
        assert_eq!(fs::read_to_string(&cfg).unwrap(), "# x\ndata_root=/d\n");
        let fresh = t.path().join("fresh");
        remember(&fresh, Some(Path::new("/b.nds"))).unwrap();
        assert!(fs::read_to_string(fresh.join("config.txt")).unwrap().ends_with("rom=/b.nds\n"));
    }
}
