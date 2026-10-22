//! Profiles: one folder each under `profiles/`, holding `profile.json`,
//! `mods/`, `saves/` and `settings.ini`. The folder is the whole profile.

use std::fs;
use std::io;
use std::path::Path;

use serde::{Deserialize, Serialize};

use crate::layout::Layout;

pub const DEFAULT: &str = "Default";
/// The version value that follows the newest installed version.
pub const LATEST: &str = "latest";
const FILE: &str = "profile.json";
const NAME_MAX: usize = 40;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct ProfileFile {
    /// `latest`, `local`, or an installed version id.
    pub version: String,
}

impl Default for ProfileFile {
    fn default() -> Self {
        ProfileFile {
            version: LATEST.to_string(),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Profile {
    pub name: String,
    pub version: String,
}

/// Checks a profile name: it becomes a folder name on every platform.
pub fn validate_name(name: &str) -> Result<String, String> {
    let name = name.trim();
    if name.is_empty() {
        return Err("The name is empty.".into());
    }
    if name.chars().count() > NAME_MAX {
        return Err(format!("The name is longer than {NAME_MAX} characters."));
    }
    if name
        .chars()
        .any(|c| c.is_control() || "\\/:*?\"<>|".contains(c))
    {
        return Err("The name has a character a folder name cannot have.".into());
    }
    if name.starts_with('.') || name.ends_with('.') {
        return Err("The name cannot start or end with a dot.".into());
    }
    let stem = name.split('.').next().unwrap_or("").to_ascii_uppercase();
    let reserved = ["CON", "PRN", "AUX", "NUL"];
    let numbered = |p: &str| {
        stem.strip_prefix(p)
            .is_some_and(|n| n.len() == 1 && n.as_bytes()[0].is_ascii_digit())
    };
    if reserved.contains(&stem.as_str()) || numbered("COM") || numbered("LPT") {
        return Err("The system reserves that name.".into());
    }
    Ok(name.to_string())
}

fn exists_ci(layout: &Layout, name: &str) -> bool {
    list_names(layout)
        .iter()
        .any(|n| n.eq_ignore_ascii_case(name))
}

fn list_names(layout: &Layout) -> Vec<String> {
    let Ok(rd) = fs::read_dir(layout.profiles()) else {
        return Vec::new();
    };
    rd.flatten()
        .filter(|e| e.file_type().is_ok_and(|t| t.is_dir()))
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .collect()
}

fn write_file(dir: &Path, file: &ProfileFile) -> io::Result<()> {
    fs::create_dir_all(dir)?;
    let text = serde_json::to_string_pretty(file).map_err(io::Error::other)?;
    fs::write(dir.join(FILE), text + "\n")
}

fn read_file(dir: &Path) -> ProfileFile {
    fs::read_to_string(dir.join(FILE))
        .ok()
        .and_then(|t| serde_json::from_str(&t).ok())
        .unwrap_or_default()
}

/// Makes the Default profile when it is missing.
pub fn ensure_default(layout: &Layout) -> io::Result<()> {
    let dir = layout.profile(DEFAULT);
    if !dir.join(FILE).exists() {
        write_file(&dir, &ProfileFile::default())?;
        for sub in ["mods", "saves"] {
            fs::create_dir_all(dir.join(sub))?;
        }
    }
    Ok(())
}

/// Every profile, Default first, the rest by name.
pub fn list(layout: &Layout) -> io::Result<Vec<Profile>> {
    ensure_default(layout)?;
    let mut names = list_names(layout);
    names.sort_by_key(|n| (n != DEFAULT, n.to_lowercase()));
    Ok(names
        .into_iter()
        .map(|name| {
            let version = read_file(&layout.profile(&name)).version;
            Profile { name, version }
        })
        .collect())
}

pub fn get(layout: &Layout, name: &str) -> Result<Profile, String> {
    list(layout)
        .map_err(|e| e.to_string())?
        .into_iter()
        .find(|p| p.name == name)
        .ok_or_else(|| format!("There is no profile named {name}."))
}

pub fn create(layout: &Layout, name: &str, version: &str) -> Result<Profile, String> {
    let name = validate_name(name)?;
    ensure_default(layout).map_err(|e| e.to_string())?;
    if exists_ci(layout, &name) {
        return Err(format!("A profile named {name} exists."));
    }
    let file = ProfileFile {
        version: version.to_string(),
    };
    let dir = layout.profile(&name);
    write_file(&dir, &file).map_err(|e| e.to_string())?;
    for sub in ["mods", "saves"] {
        fs::create_dir_all(dir.join(sub)).map_err(|e| e.to_string())?;
    }
    Ok(Profile {
        name,
        version: file.version,
    })
}

pub fn set_version(layout: &Layout, name: &str, version: &str) -> Result<(), String> {
    get(layout, name)?;
    write_file(
        &layout.profile(name),
        &ProfileFile {
            version: version.to_string(),
        },
    )
    .map_err(|e| e.to_string())
}

pub fn rename(layout: &Layout, from: &str, to: &str) -> Result<(), String> {
    if from == DEFAULT {
        return Err("The Default profile keeps its name.".into());
    }
    get(layout, from)?;
    let to = validate_name(to)?;
    if !to.eq_ignore_ascii_case(from) && exists_ci(layout, &to) {
        return Err(format!("A profile named {to} exists."));
    }
    fs::rename(layout.profile(from), layout.profile(&to)).map_err(|e| e.to_string())
}

/// Copies a folder tree. Links are neither followed nor copied.
pub fn copy_dir(from: &Path, to: &Path) -> io::Result<()> {
    fs::create_dir_all(to)?;
    for entry in fs::read_dir(from)? {
        let entry = entry?;
        let kind = entry.file_type()?;
        let dest = to.join(entry.file_name());
        if kind.is_dir() {
            copy_dir(&entry.path(), &dest)?;
        } else if kind.is_file() {
            fs::copy(entry.path(), dest)?;
        }
    }
    Ok(())
}

/// Copies a profile with its mods, saves and settings.
pub fn duplicate(layout: &Layout, name: &str) -> Result<Profile, String> {
    get(layout, name)?;
    let mut n = 1;
    let target = loop {
        let candidate = if n == 1 {
            format!("{name} copy")
        } else {
            format!("{name} copy {n}")
        };
        if !exists_ci(layout, &candidate) {
            break candidate;
        }
        n += 1;
    };
    let target = validate_name(&target)?;
    copy_dir(&layout.profile(name), &layout.profile(&target)).map_err(|e| e.to_string())?;
    get(layout, &target)
}

pub fn delete(layout: &Layout, name: &str) -> Result<(), String> {
    if name == DEFAULT {
        return Err("The Default profile cannot be deleted.".into());
    }
    get(layout, name)?;
    fs::remove_dir_all(layout.profile(name)).map_err(|e| e.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn layout() -> (tempfile::TempDir, Layout) {
        let t = tempfile::tempdir().unwrap();
        let l = Layout::new(t.path());
        (t, l)
    }

    #[test]
    fn default_always_exists_and_comes_first() {
        let (_t, l) = layout();
        let first = list(&l).unwrap();
        assert_eq!(first.len(), 1);
        assert_eq!(first[0].name, DEFAULT);
        assert_eq!(first[0].version, LATEST);
        create(&l, "alpha", "1.0.0").unwrap();
        create(&l, "Beta", LATEST).unwrap();
        let names: Vec<_> = list(&l).unwrap().into_iter().map(|p| p.name).collect();
        assert_eq!(names, ["Default", "alpha", "Beta"]);
    }

    #[test]
    fn create_makes_the_folders_and_refuses_duplicates() {
        let (_t, l) = layout();
        let p = create(&l, "  Friends ", "0.2.0").unwrap();
        assert_eq!(p.name, "Friends");
        let dir = l.profile("Friends");
        assert!(dir.join("profile.json").is_file());
        assert!(dir.join("mods").is_dir() && dir.join("saves").is_dir());
        assert!(create(&l, "friends", LATEST).is_err());
    }

    #[test]
    fn names_are_checked() {
        for bad in ["", "  ", "a/b", "a:b", ".hidden", "x.", "CON", "com1", "nul.txt"] {
            assert!(validate_name(bad).is_err(), "{bad}");
        }
        assert!(validate_name(&"x".repeat(41)).is_err());
        assert_eq!(validate_name(" My game ").unwrap(), "My game");
        assert!(validate_name("com10").is_ok());
    }

    #[test]
    fn rename_duplicate_delete() {
        let (_t, l) = layout();
        create(&l, "one", LATEST).unwrap();
        fs::write(l.profile("one").join("saves/slot1.sav"), b"x").unwrap();
        rename(&l, "one", "two").unwrap();
        assert!(!l.profile("one").exists());
        assert!(rename(&l, "two", "Default").is_err());
        assert!(rename(&l, DEFAULT, "x").is_err());
        let copy = duplicate(&l, "two").unwrap();
        assert_eq!(copy.name, "two copy");
        assert!(l.profile("two copy").join("saves/slot1.sav").is_file());
        assert_eq!(duplicate(&l, "two").unwrap().name, "two copy 2");
        delete(&l, "two copy").unwrap();
        assert!(!l.profile("two copy").exists());
        assert!(delete(&l, DEFAULT).is_err());
        assert!(delete(&l, "missing").is_err());
    }

    #[test]
    fn version_is_kept() {
        let (_t, l) = layout();
        create(&l, "p", LATEST).unwrap();
        set_version(&l, "p", "0.9.0").unwrap();
        assert_eq!(get(&l, "p").unwrap().version, "0.9.0");
    }
}
