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

/// The arguments for eidos: the profile folder, and for a join the invite
/// link (eidos opens Multiplayer and joins). Nothing else: eidos opens its
/// window when it gets only these.
pub fn eidos_args(profile_dir: &Path, join: Option<&str>) -> Vec<OsString> {
    let mut args = vec![OsString::from("--profile-dir"), profile_dir.into()];
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
        let a = eidos_args(&dir, None);
        assert_eq!(a, [OsString::from("--profile-dir"), dir.clone().into()]);
        let j = eidos_args(&dir, Some("ABC123"));
        assert_eq!(
            j,
            [
                OsString::from("--profile-dir"),
                dir.into(),
                OsString::from("eidos://join/ABC123")
            ]
        );
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
        let mut args = eidos_args(&dir, Some("TEST42"));
        args.extend(["--frames".into(), "300".into()]);
        std::env::set_var("EIDOS_WINDOW_HIDDEN", "1");
        let status = spawn(Path::new(&exe), &args).unwrap().wait().unwrap();
        println!("exit: {status}");
        assert!(dir.join("saves").is_dir() && dir.join("mods").is_dir());
    }
}
