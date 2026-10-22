//! Where everything lives in the user folder (docs/distribution.md, "Folders").

use std::path::{Path, PathBuf};

/// The user folder and the paths below it.
#[derive(Debug, Clone)]
pub struct Layout {
    pub root: PathBuf,
}

/// The user folder for an operating system, from an environment lookup.
/// Windows: `%APPDATA%\eidos`; macOS: `~/Library/Application Support/eidos`;
/// elsewhere: `$XDG_DATA_HOME/eidos` or `~/.local/share/eidos`.
pub fn root_for(os: &str, env: &dyn Fn(&str) -> Option<String>) -> Option<PathBuf> {
    let get = |k: &str| env(k).filter(|v| !v.is_empty());
    match os {
        "windows" => get("APPDATA").map(|p| Path::new(&p).join("eidos")),
        "macos" => get("HOME").map(|p| {
            Path::new(&p)
                .join("Library")
                .join("Application Support")
                .join("eidos")
        }),
        _ => get("XDG_DATA_HOME")
            .map(|p| Path::new(&p).join("eidos"))
            .or_else(|| get("HOME").map(|p| Path::new(&p).join(".local/share/eidos"))),
    }
}

impl Layout {
    pub fn new(root: impl Into<PathBuf>) -> Self {
        Layout { root: root.into() }
    }

    /// The user folder of this computer.
    pub fn system() -> Option<Self> {
        root_for(std::env::consts::OS, &|k| std::env::var(k).ok()).map(Layout::new)
    }

    pub fn profiles(&self) -> PathBuf {
        self.root.join("profiles")
    }
    pub fn profile(&self, name: &str) -> PathBuf {
        self.profiles().join(name)
    }
    pub fn versions(&self) -> PathBuf {
        self.root.join("versions")
    }
    pub fn version(&self, id: &str) -> PathBuf {
        self.versions().join(id)
    }
    pub fn launcher(&self) -> PathBuf {
        self.root.join("launcher")
    }
    pub fn settings_file(&self) -> PathBuf {
        self.launcher().join("settings.json")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn env<'a>(pairs: &'a [(&'a str, &'a str)]) -> impl Fn(&str) -> Option<String> + 'a {
        move |k| {
            pairs
                .iter()
                .find(|(n, _)| *n == k)
                .map(|(_, v)| v.to_string())
        }
    }

    #[test]
    fn roots_per_platform() {
        let e = env(&[("APPDATA", "/ad"), ("HOME", "/h")]);
        assert_eq!(root_for("windows", &e), Some(PathBuf::from("/ad/eidos")));
        assert_eq!(
            root_for("macos", &e),
            Some(PathBuf::from("/h/Library/Application Support/eidos"))
        );
        assert_eq!(
            root_for("linux", &e),
            Some(PathBuf::from("/h/.local/share/eidos"))
        );
        let x = env(&[("XDG_DATA_HOME", "/x"), ("HOME", "/h")]);
        assert_eq!(root_for("linux", &x), Some(PathBuf::from("/x/eidos")));
        assert_eq!(root_for("windows", &env(&[])), None);
    }

    #[test]
    fn folder_names() {
        let l = Layout::new("/r");
        assert_eq!(l.profile("A"), PathBuf::from("/r/profiles/A"));
        assert_eq!(l.version("1.0.0"), PathBuf::from("/r/versions/1.0.0"));
        assert_eq!(
            l.settings_file(),
            PathBuf::from("/r/launcher/settings.json")
        );
    }
}
