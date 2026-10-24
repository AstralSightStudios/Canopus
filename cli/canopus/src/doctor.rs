//! `canopus doctor`: is everything needed to build, verify and package modules there?

use crate::env::{locate_root, tool};
use canopus_core::registry::list_target_packs;
use std::path::Path;
use std::process::{Command, Stdio};

enum Level {
    Ok,
    Warn,
    Fail,
}

struct Report {
    failed: bool,
}

impl Report {
    fn line(&mut self, level: Level, what: &str, detail: &str) {
        let tag = match level {
            Level::Ok => "ok  ",
            Level::Warn => "warn",
            Level::Fail => {
                self.failed = true;
                "FAIL"
            }
        };
        println!("{tag}  {what:<12} {detail}");
    }
}

/// The first line a tool prints for `--version`, or why it could not run.
fn version(program: &str) -> Result<String, String> {
    let out = Command::new(program)
        .arg("--version")
        .output()
        .map_err(|e| format!("`{program}` cannot run ({e})"))?;
    let text = String::from_utf8_lossy(&out.stdout);
    Ok(text.lines().next().unwrap_or("").trim().to_string())
}

/// Compiles an empty translation unit the way `canopus build` does.
fn compiles_for_device(cc: &str) -> Result<(), String> {
    let obj = std::env::temp_dir().join(format!("canopus-doctor-{}.o", std::process::id()));
    let mut child = Command::new(cc)
        .args([
            "--target=arm-none-eabi",
            "-mcpu=cortex-m33",
            "-mthumb",
            "-mfloat-abi=soft",
            "-ffreestanding",
            "-c",
            "-x",
            "c",
            "-",
            "-o",
        ])
        .arg(&obj)
        .stdin(Stdio::piped())
        .stdout(Stdio::null())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|e| e.to_string())?;
    {
        use std::io::Write;
        let _ = child
            .stdin
            .take()
            .map(|mut i| i.write_all(b"int canopus_doctor;\n"));
    }
    let out = child.wait_with_output().map_err(|e| e.to_string())?;
    let _ = std::fs::remove_file(&obj);
    if out.status.success() {
        Ok(())
    } else {
        Err(String::from_utf8_lossy(&out.stderr)
            .lines()
            .next()
            .unwrap_or("compile failed")
            .to_string())
    }
}

/// The LLVM packages that provide clang, ld.lld and llvm-ar.
fn llvm_hint() -> &'static str {
    if cfg!(target_os = "macos") {
        "brew install llvm lld"
    } else {
        "apt install clang lld llvm (dnf/pacman: clang lld llvm)"
    }
}

fn rust_device_target_installed(cargo: &str) -> bool {
    // `rustc` next to `cargo` (a bare `cargo` stays a PATH lookup).
    let rustc = Path::new(cargo).with_file_name("rustc");
    Command::new(rustc)
        .args([
            "--print",
            "target-libdir",
            "--target",
            "thumbv8m.main-none-eabi",
        ])
        .output()
        .ok()
        .and_then(|o| String::from_utf8(o.stdout).ok())
        .is_some_and(|dir| Path::new(dir.trim()).is_dir())
}

pub fn run() -> anyhow::Result<()> {
    let mut r = Report { failed: false };

    match locate_root() {
        Ok((root, source)) => {
            r.line(
                Level::Ok,
                "SDK",
                &format!("{} ({})", root.display(), source.label()),
            );
            match list_target_packs(&root.join("targets")) {
                Ok(packs) if !packs.is_empty() => {
                    let active = packs
                        .iter()
                        .filter(|p| p.status.as_deref() != Some("frozen"))
                        .count();
                    r.line(
                        Level::Ok,
                        "targets",
                        &format!(
                            "{} registered, {active} active: {}",
                            packs.len(),
                            packs
                                .iter()
                                .map(|p| p.target_id.rsplit('-').next().unwrap_or(&p.target_id))
                                .collect::<Vec<_>>()
                                .join(" ")
                        ),
                    );
                }
                Ok(_) => r.line(
                    Level::Fail,
                    "targets",
                    &format!("no target packs under {}", root.join("targets").display()),
                ),
                Err(e) => r.line(Level::Fail, "targets", &e.to_string()),
            }
        }
        Err(e) => r.line(Level::Fail, "SDK", &e.to_string()),
    }

    let cc = tool("CC", "clang");
    match version(&cc) {
        Ok(v) => match compiles_for_device(&cc) {
            Ok(()) => r.line(Level::Ok, "C compiler", &format!("{v}; targets Cortex-M33")),
            Err(e) => r.line(
                Level::Fail,
                "C compiler",
                &format!("{v}; cannot build for arm-none-eabi: {e}"),
            ),
        },
        Err(e) => r.line(
            Level::Fail,
            "C compiler",
            &format!("{e}; install it (`{}`) or set CC", llvm_hint()),
        ),
    }

    let ld = tool("LD", "ld.lld");
    match version(&ld) {
        Ok(v) => r.line(Level::Ok, "linker", &v),
        Err(e) => r.line(
            Level::Fail,
            "linker",
            &format!("{e}; install it (`{}`) or set LD", llvm_hint()),
        ),
    }

    let cargo = tool("CARGO", "cargo");
    match version(&cargo) {
        Ok(v) if rust_device_target_installed(&cargo) => r.line(
            Level::Ok,
            "Rust",
            &format!("{v}; thumbv8m.main-none-eabi installed"),
        ),
        Ok(v) => r.line(
            Level::Warn,
            "Rust",
            &format!("{v}; Rust modules need `rustup target add thumbv8m.main-none-eabi`"),
        ),
        Err(e) => r.line(
            Level::Warn,
            "Rust",
            &format!("{e}; only needed for --lang rust"),
        ),
    }

    let ar = tool("AR", "llvm-ar");
    match version(&ar) {
        Ok(v) => r.line(Level::Ok, "archiver", &v),
        Err(e) => r.line(
            Level::Warn,
            "archiver",
            &format!(
                "{e}; Rust modules with C dependencies need it (`{}`)",
                llvm_hint()
            ),
        ),
    }

    match version("curl") {
        Ok(v) => r.line(Level::Ok, "curl", &v),
        Err(e) => r.line(
            Level::Warn,
            "curl",
            &format!("{e}; `canopus sdk update` downloads with it"),
        ),
    }

    match version("python3") {
        Ok(v) => r.line(Level::Ok, "python3", &v),
        Err(e) => r.line(
            Level::Warn,
            "python3",
            &format!("{e}; only needed for the emulator scripts"),
        ),
    }

    if r.failed {
        anyhow::bail!("the toolchain is incomplete");
    }
    Ok(())
}
