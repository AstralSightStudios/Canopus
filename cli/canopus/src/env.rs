//! Where the Canopus checkout is, how external tools run, how target names resolve.

use anyhow::{Context, bail};
use canopus_core::model::TargetPack;
use canopus_core::registry::list_target_packs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};

static VERBOSE: AtomicBool = AtomicBool::new(false);

pub fn set_verbose(on: bool) {
    VERBOSE.store(on, Ordering::Relaxed);
}

const ROOT_MARKER: &str = "sdk/c/canopus_abi.h";

fn is_root(dir: &Path) -> bool {
    dir.join(ROOT_MARKER).is_file()
}

/// The Canopus checkout providing `sdk/c` and `targets/`, searched in order:
/// `$CANOPUS_ROOT`, the working directory and its ancestors, the executable and
/// its ancestors, and the checkout this binary was built from. The last one is
/// what lets a `cargo install`ed `canopus` work anywhere.
pub fn canopus_root() -> anyhow::Result<PathBuf> {
    if let Some(root) = std::env::var_os("CANOPUS_ROOT") {
        let root = std::path::absolute(PathBuf::from(&root))?;
        if !is_root(&root) {
            bail!(
                "CANOPUS_ROOT={} is not a Canopus checkout ({ROOT_MARKER} is missing)",
                root.display()
            );
        }
        return Ok(root);
    }
    let mut starts: Vec<PathBuf> = Vec::new();
    if let Ok(cwd) = std::env::current_dir() {
        starts.push(cwd);
    }
    if let Ok(exe) = std::env::current_exe() {
        starts.push(exe.canonicalize().unwrap_or(exe));
    }
    starts.push(PathBuf::from(env!("CARGO_MANIFEST_DIR")));
    for start in starts {
        if let Some(dir) = start.ancestors().find(|d| is_root(d)) {
            return Ok(dir.to_path_buf());
        }
    }
    bail!("cannot find the Canopus checkout ({ROOT_MARKER}); run inside it or set CANOPUS_ROOT")
}

/// `--targets-dir` when given, else `<checkout>/targets`.
pub fn targets_dir(arg: Option<PathBuf>) -> anyhow::Result<PathBuf> {
    match arg {
        Some(dir) => Ok(dir),
        None => Ok(canopus_root()?.join("targets")),
    }
}

/// Resolves a target name to a registered target id: the exact id, or a unique
/// version suffix (`155` for `xiaomi-band-11-4.100.155`).
pub fn resolve_target(targets_dir: &Path, name: &str) -> anyhow::Result<String> {
    let packs = list_target_packs(targets_dir)?;
    resolve_in(&packs, name)
}

pub fn resolve_in(packs: &[TargetPack], name: &str) -> anyhow::Result<String> {
    if let Some(p) = packs.iter().find(|p| p.target_id == name) {
        return Ok(p.target_id.clone());
    }
    let suffixed: Vec<&TargetPack> = packs
        .iter()
        .filter(|p| {
            p.target_id
                .strip_suffix(name)
                .is_some_and(|head| head.ends_with('.') || head.ends_with('-'))
        })
        .collect();
    match suffixed.as_slice() {
        [one] => Ok(one.target_id.clone()),
        [] => bail!("unknown target '{name}'; registered: {}", ids(packs)),
        many => bail!(
            "target '{name}' is ambiguous: {}",
            many.iter()
                .map(|p| p.target_id.as_str())
                .collect::<Vec<_>>()
                .join(", ")
        ),
    }
}

fn ids(packs: &[TargetPack]) -> String {
    if packs.is_empty() {
        return "none".into();
    }
    packs
        .iter()
        .map(|p| p.target_id.as_str())
        .collect::<Vec<_>>()
        .join(", ")
}

/// A tool path from `var` (`CC`, `LD`, `CARGO`) or its default name.
pub fn tool(var: &str, default: &str) -> String {
    if let Ok(v) = std::env::var(var) {
        return v;
    }
    for candidate in [
        "/opt/homebrew/opt/llvm/bin",
        "/opt/homebrew/bin",
        "/usr/local/opt/llvm/bin",
        "/usr/local/bin",
    ] {
        let p = std::path::Path::new(candidate).join(default);
        if p.is_file() {
            return p.to_string_lossy().into_owned();
        }
    }
    default.to_string()
}

/// Runs a build tool. Its own output stays visible; a failure names the step
/// instead of printing the whole command line (`-v` prints it up front).
pub fn run(cmd: &mut Command, what: &str) -> anyhow::Result<()> {
    if VERBOSE.load(Ordering::Relaxed) {
        let args: Vec<_> = cmd.get_args().map(|a| a.to_string_lossy()).collect();
        eprintln!(
            "+ {} {}",
            cmd.get_program().to_string_lossy(),
            args.join(" ")
        );
    }
    let program = cmd.get_program().to_string_lossy().into_owned();
    let status = cmd.status().map_err(|e| {
        if e.kind() == std::io::ErrorKind::NotFound {
            anyhow::anyhow!(
                "`{program}` not found while {what}: install it or point CC / LD / CARGO at it (`canopus doctor` checks the toolchain)"
            )
        } else {
            anyhow::anyhow!("cannot run `{program}` while {what}: {e}")
        }
    })?;
    if !status.success() {
        bail!("`{program}` failed ({status}) while {what}");
    }
    Ok(())
}

/// Reads a file; a missing one gets `hint` appended.
pub fn read_text(path: &Path, hint: &str) -> anyhow::Result<String> {
    std::fs::read_to_string(path).map_err(|e| {
        if e.kind() == std::io::ErrorKind::NotFound {
            anyhow::anyhow!("cannot read {}: not found; {hint}", path.display())
        } else {
            anyhow::anyhow!("cannot read {}: {e}", path.display())
        }
    })
}

/// `path` relative to `base` when it lies inside it, else as given.
pub fn display_rel(path: &Path, base: &Path) -> String {
    path.strip_prefix(base)
        .unwrap_or(path)
        .display()
        .to_string()
}

/// The newest modification time of a project's inputs (sources, headers,
/// manifests), skipping build output.
pub fn newest_input(dir: &Path) -> anyhow::Result<Option<(PathBuf, std::time::SystemTime)>> {
    const SKIP: [&str; 4] = ["build", "dist", ".git", "target"];
    const EXT: [&str; 7] = ["c", "h", "rs", "toml", "ld", "s", "S"];
    let mut newest: Option<(PathBuf, std::time::SystemTime)> = None;
    let mut stack = vec![dir.to_path_buf()];
    while let Some(d) = stack.pop() {
        for entry in
            std::fs::read_dir(&d).with_context(|| format!("cannot read {}", d.display()))?
        {
            let entry = entry?;
            let path = entry.path();
            let name = entry.file_name();
            if entry.file_type()?.is_dir() {
                if !SKIP.iter().any(|s| name == *s) {
                    stack.push(path);
                }
            } else if path
                .extension()
                .and_then(|e| e.to_str())
                .is_some_and(|e| EXT.contains(&e))
            {
                let modified = entry.metadata()?.modified()?;
                if newest.as_ref().is_none_or(|(_, t)| modified > *t) {
                    newest = Some((path, modified));
                }
            }
        }
    }
    Ok(newest)
}
