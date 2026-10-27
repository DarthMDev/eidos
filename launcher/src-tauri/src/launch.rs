//! Starting eidos: the arguments for a normal start and for an invite link.

use std::ffi::OsString;
use std::io;
use std::path::Path;
use std::process::{Child, Command};

const INVITE_PREFIX: &str = "eidos://join/";
const CODE_MAX: usize = 64;

/// The join code in an `eidos://join/<code>` link, or None when the text is
/// not such a link. The code is letters, digits, `-` and `_` only, because it
/// is handed to a program as an argument.
pub fn parse_invite(url: &str) -> Option<String> {
    let url = url.trim();
    let head = url.get(..INVITE_PREFIX.len())?;
    if !head.eq_ignore_ascii_case(INVITE_PREFIX) {
        return None;
    }
    let code = url[INVITE_PREFIX.len()..].trim_end_matches('/');
    let ok = !code.is_empty()
        && code.len() <= CODE_MAX
        && code
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_');
    ok.then(|| code.to_string())
}

/// The invite link for a code.
pub fn invite_url(code: &str) -> String {
    format!("{INVITE_PREFIX}{code}")
}

/// The screen layout a start asks for. Classic adds `--two-screens` (the
/// saved two-screen arrangement, never the one screen); One adds `--layout one`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum Layout {
    #[default]
    Classic,
    One,
}

impl Layout {
    /// "classic" or "one"; anything else is refused.
    pub fn parse(text: &str) -> Option<Layout> {
        match text {
            "classic" => Some(Layout::Classic),
            "one" => Some(Layout::One),
            _ => None,
        }
    }
}

/// The arguments for eidos: the profile folder, `--window` (eidos runs
/// headless when it gets arguments other than the profile and the invite),
/// the game data folder when the profile sets one, `--two-screens` or
/// `--layout one`, and for a join the invite link (eidos opens Multiplayer
/// and joins).
pub fn eidos_args(profile_dir: &Path, data_root: &str, join: Option<&str>, layout: Layout) -> Vec<OsString> {
    let mut args = vec![
        OsString::from("--profile-dir"),
        profile_dir.into(),
        OsString::from("--window"),
    ];
    if !data_root.is_empty() {
        args.push("--data-root".into());
        args.push(data_root.into());
    }
    match layout {
        Layout::Classic => args.push("--two-screens".into()),
        Layout::One => {
            args.push("--layout".into());
            args.push("one".into());
        }
    }
    if let Some(code) = join {
        args.push(invite_url(code).into());
    }
    args
}

/// Starts the program in its own folder (its libraries are beside it).
pub fn spawn(exe: &Path, args: &[OsString]) -> io::Result<Child> {
    let mut cmd = Command::new(exe);
    cmd.args(args);
    if let Some(dir) = exe.parent() {
        cmd.current_dir(dir);
    }
    cmd.spawn()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn invite_links() {
        assert_eq!(parse_invite("eidos://join/ABCD-12").as_deref(), Some("ABCD-12"));
        assert_eq!(parse_invite("EIDOS://join/xy9/").as_deref(), Some("xy9"));
        assert_eq!(parse_invite("  eidos://join/Q  ").as_deref(), Some("Q"));
        assert_eq!(parse_invite("eidos://join/"), None);
        assert_eq!(parse_invite("eidos://host/ABC"), None);
        assert_eq!(parse_invite("https://join/ABC"), None);
        assert_eq!(parse_invite("eidos://join/a b"), None);
        assert_eq!(parse_invite("eidos://join/a\"b"), None);
        assert_eq!(parse_invite("eidos://join/--x y"), None);
        assert_eq!(parse_invite(&format!("eidos://join/{}", "a".repeat(65))), None);
        assert_eq!(parse_invite("é"), None);
    }

    #[test]
    fn play_arguments() {
        let dir = PathBuf::from("/u/profiles/Default");
        let a = eidos_args(&dir, "", None, Layout::Classic);
        assert_eq!(
            a,
            [
                OsString::from("--profile-dir"),
                dir.clone().into(),
                OsString::from("--window"),
                OsString::from("--two-screens")
            ]
        );
        let j = eidos_args(&dir, "", Some("ABC123"), Layout::Classic);
        assert_eq!(
            j,
            [
                OsString::from("--profile-dir"),
                dir.into(),
                OsString::from("--window"),
                OsString::from("--two-screens"),
                OsString::from("eidos://join/ABC123")
            ]
        );
    }

    #[test]
    fn data_root_goes_before_the_invite() {
        let dir = PathBuf::from("/u/profiles/Default");
        let a = eidos_args(&dir, "/games/eidos", Some("ABC123"), Layout::Classic);
        assert_eq!(
            a,
            [
                OsString::from("--profile-dir"),
                dir.into(),
                OsString::from("--window"),
                OsString::from("--data-root"),
                OsString::from("/games/eidos"),
                OsString::from("--two-screens"),
                OsString::from("eidos://join/ABC123")
            ]
        );
    }

    #[test]
    fn one_screen_layout() {
        let dir = PathBuf::from("/u/profiles/Default");
        let a = eidos_args(&dir, "/games/eidos", Some("ABC123"), Layout::One);
        assert_eq!(
            a,
            [
                OsString::from("--profile-dir"),
                dir.clone().into(),
                OsString::from("--window"),
                OsString::from("--data-root"),
                OsString::from("/games/eidos"),
                OsString::from("--layout"),
                OsString::from("one"),
                OsString::from("eidos://join/ABC123")
            ]
        );
        let b = eidos_args(&dir, "", None, Layout::One);
        assert_eq!(b.len(), 5);
        assert!(!eidos_args(&dir, "", None, Layout::Classic).iter().any(|x| x == "--layout"));
    }

    #[test]
    fn layout_names() {
        assert_eq!(Layout::parse("classic"), Some(Layout::Classic));
        assert_eq!(Layout::parse("one"), Some(Layout::One));
        assert_eq!(Layout::parse("One"), None);
        assert_eq!(Layout::parse("--x"), None);
        assert_eq!(Layout::default(), Layout::Classic);
    }

    /// Runs a real eidos with a hidden window for a few frames. Needs
    /// `LAUNCHER_TEST_EXE`; run with `cargo test -- --ignored`.
    #[test]
    #[ignore]
    fn starts_a_real_eidos() {
        let Ok(exe) = std::env::var("LAUNCHER_TEST_EXE") else {
            return;
        };
        let scratch = tempfile::tempdir().unwrap();
        let dir = scratch.path().join("profile");
        let mut args = eidos_args(&dir, "", Some("TEST42"), Layout::Classic);
        args.extend(["--frames".into(), "300".into()]);
        std::env::set_var("EIDOS_WINDOW_HIDDEN", "1");
        let status = spawn(Path::new(&exe), &args).unwrap().wait().unwrap();
        println!("exit: {status}");
        assert!(dir.join("saves").is_dir() && dir.join("mods").is_dir());
    }
}
