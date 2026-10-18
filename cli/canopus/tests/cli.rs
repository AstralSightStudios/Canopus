//! End-to-end checks of the `canopus` binary: the commands a module author
//! runs, from directories that are not the checkout.

use std::path::{Path, PathBuf};
use std::process::{Command, Output};
use std::time::{Duration, SystemTime};

fn canopus(dir: &Path, args: &[&str]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_canopus"))
        .args(args)
        .current_dir(dir)
        .env_remove("CANOPUS_ROOT")
        .output()
        .expect("canopus runs")
}

fn text(out: &[u8]) -> String {
    String::from_utf8_lossy(out).into_owned()
}

struct Scratch(PathBuf);

impl Scratch {
    fn new(name: &str) -> Self {
        let dir =
            std::env::temp_dir().join(format!("canopus-cli-test-{}-{name}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        Scratch(dir)
    }
}

impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

fn toolchain_present() -> bool {
    ["clang", "ld.lld"].iter().all(|tool| {
        Command::new(tool)
            .arg("--version")
            .output()
            .is_ok_and(|o| o.status.success())
    })
}

#[test]
fn target_commands_do_not_depend_on_the_working_directory() {
    let scratch = Scratch::new("anywhere");
    let out = canopus(&scratch.0, &["target", "list", "--names"]);
    assert!(out.status.success(), "{}", text(&out.stderr));
    assert!(text(&out.stdout).contains("xiaomi-band-11-4.100.155"));
    let out = canopus(&scratch.0, &["target", "info", "155"]);
    assert!(out.status.success(), "{}", text(&out.stderr));
    assert!(text(&out.stdout).starts_with("xiaomi-band-11-4.100.155"));
}

#[test]
fn new_validates_before_creating_anything() {
    let scratch = Scratch::new("invalid");
    for (args, needle) in [
        (
            &["new", "hello", "--target", "999"][..],
            "unknown target '999'",
        ),
        (&["new", "hello", "--target", "1"][..], "unknown target '1'"),
        (&["new", "Bad-Id"][..], "must match"),
        (
            &["new", "hello", "--lang", "rust"][..],
            "pass at least one --target",
        ),
        (
            &["new", "hello", "--lang", "rust", "--target", "155", "--ui"][..],
            "C template",
        ),
    ] {
        let out = canopus(&scratch.0, args);
        assert!(!out.status.success(), "{args:?} should fail");
        assert!(
            text(&out.stderr).contains(needle),
            "{args:?}: {}",
            text(&out.stderr)
        );
    }
    assert_eq!(
        std::fs::read_dir(&scratch.0).unwrap().count(),
        0,
        "a failed `new` leaves nothing behind"
    );
}

#[test]
fn project_flow_builds_packages_and_refuses_a_stale_elf() {
    if !toolchain_present() {
        eprintln!("skipped: clang and ld.lld are required");
        return;
    }
    let scratch = Scratch::new("flow");
    let ok = |dir: &Path, args: &[&str]| {
        let out = canopus(dir, args);
        assert!(
            out.status.success(),
            "{args:?}: {}{}",
            text(&out.stdout),
            text(&out.stderr)
        );
        text(&out.stdout)
    };
    ok(
        &scratch.0,
        &["new", "demo", "--ui", "--target", "155", "--target", "043"],
    );
    let project = scratch.0.join("demo");
    assert!(project.join(".gitignore").is_file());

    assert!(ok(&project, &["build"]).contains("built and verified for 2 target(s)"));
    ok(&project, &["package"]);
    let package = project.join("dist/demo-0.1.0.canopus");
    assert!(
        ok(
            &scratch.0,
            &["package", "verify", package.to_str().unwrap()]
        )
        .contains("2 artifact(s)")
    );

    // An edited source must not be packaged from the old ELF.
    let source = project.join("src/demo.c");
    let later = SystemTime::now() + Duration::from_secs(60);
    std::fs::File::options()
        .write(true)
        .open(&source)
        .unwrap()
        .set_modified(later)
        .unwrap();
    let out = canopus(&project, &["package"]);
    assert!(!out.status.success());
    assert!(
        text(&out.stderr).contains("older than src/demo.c"),
        "{}",
        text(&out.stderr)
    );

    ok(&project, &["clean"]);
    assert!(!project.join("build").exists() && !project.join("dist").exists());
}
