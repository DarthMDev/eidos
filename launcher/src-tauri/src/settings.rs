//! The launcher's own settings: `launcher/settings.json` in the user folder.

use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

pub const DEFAULT_REPO: &str = "Project-Holon/eidos";

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    /// The GitHub repository the eidos releases come from.
    pub repo: String,
    /// A GitHub token, only for a private repository.
    pub token: String,
    /// The address of the mods index (an `index.json`). Empty: none.
    pub index_url: String,
    /// A folder holding a build of eidos, offered as the version "Local build".
    pub local_build: Option<PathBuf>,
    /// The player's Pokemon Black ROM, given to the game (src/rom.rs).
    pub rom_black: Option<PathBuf>,
    /// The player's Pokemon White ROM.
    pub rom_white: Option<PathBuf>,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            repo: DEFAULT_REPO.to_string(),
            token: String::new(),
            index_url: String::new(),
            local_build: None,
            rom_black: None,
            rom_white: None,
        }
    }
}

impl Settings {
    pub fn load(path: &Path) -> Self {
        fs::read_to_string(path)
            .ok()
            .and_then(|t| serde_json::from_str(&t).ok())
            .unwrap_or_default()
    }

    pub fn save(&self, path: &Path) -> io::Result<()> {
        if let Some(dir) = path.parent() {
            fs::create_dir_all(dir)?;
        }
        let text = serde_json::to_string_pretty(self).map_err(io::Error::other)?;
        let tmp = path.with_extension("tmp");
        fs::write(&tmp, text + "\n")?;
        fs::rename(tmp, path)
    }

    pub fn token(&self) -> Option<&str> {
        Some(self.token.as_str()).filter(|t| !t.is_empty())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip_and_defaults() {
        let t = tempfile::tempdir().unwrap();
        let p = t.path().join("launcher/settings.json");
        assert_eq!(Settings::load(&p), Settings::default());
        let s = Settings {
            token: "abc".into(),
            local_build: Some(PathBuf::from("/x")),
            ..Settings::default()
        };
        s.save(&p).unwrap();
        assert_eq!(Settings::load(&p), s);
        assert_eq!(s.token(), Some("abc"));
        assert_eq!(Settings::default().token(), None);
        fs::write(&p, "{ not json").unwrap();
        assert_eq!(Settings::load(&p), Settings::default());
    }
}
