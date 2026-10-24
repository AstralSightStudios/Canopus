//! `canopus sdk`: the SDK, templates and target packs an installed `canopus`
//! builds against when it is not run inside a checkout. A snapshot is the whole
//! repository at one commit, kept at `~/.canopus/sdk/<commit>` with `current`
//! pointing at the one in use; `update` follows the latest `main` commit.

use crate::env::{RootSource, locate_root};
use anyhow::{Context, bail};
use canopus_core::registry::list_target_packs;
use std::path::{Component, Path, PathBuf};
use std::process::Command;
use std::time::Duration;

const REPO: &str = "AstralSightStudios/Canopus";
const BRANCH: &str = "main";
/// Written into each snapshot: the commit sha and its date, one per line.
const COMMIT_FILE: &str = ".canopus-commit";
/// The oldest CLI that understands a snapshot (`min_cli = "x.y.z"`).
const COMPAT_FILE: &str = "sdk/sdk.toml";
const CLI_VERSION: &str = env!("CARGO_PKG_VERSION");
const CHECK_INTERVAL: Duration = Duration::from_secs(24 * 60 * 60);

/// `$CANOPUS_HOME`, else `~/.canopus`.
fn home() -> PathBuf {
    if let Some(dir) = std::env::var_os("CANOPUS_HOME") {
        return PathBuf::from(dir);
    }
    PathBuf::from(std::env::var_os("HOME").unwrap_or_default()).join(".canopus")
}

fn sdk_dir() -> PathBuf {
    home().join("sdk")
}

/// The snapshot in use (a symlink to `<commit>/`).
pub fn current() -> PathBuf {
    sdk_dir().join("current")
}

struct Commit {
    sha: String,
    date: String,
}

impl Commit {
    fn short(&self) -> &str {
        &self.sha[..self.sha.len().min(10)]
    }
}

fn curl(url: &str, timeout: Option<u64>) -> anyhow::Result<Vec<u8>> {
    let mut cmd = Command::new("curl");
    cmd.args([
        "-fsSL",
        "-A",
        concat!("canopus-cli/", env!("CARGO_PKG_VERSION")),
    ]);
    if let Some(secs) = timeout {
        cmd.arg("--max-time").arg(secs.to_string());
    }
    let out = cmd.arg(url).output().map_err(|e| {
        if e.kind() == std::io::ErrorKind::NotFound {
            anyhow::anyhow!("`curl` not found: install it to download the SDK")
        } else {
            anyhow::anyhow!("cannot run curl: {e}")
        }
    })?;
    if !out.status.success() {
        bail!(
            "cannot fetch {url}: {}",
            String::from_utf8_lossy(&out.stderr).trim()
        );
    }
    Ok(out.stdout)
}

/// The commit `rev` (a branch, tag or sha) names on GitHub.
fn remote_commit(rev: &str, timeout: Option<u64>) -> anyhow::Result<Commit> {
    let body = curl(
        &format!("https://api.github.com/repos/{REPO}/commits/{rev}"),
        timeout,
    )?;
    let v: serde_json::Value = serde_json::from_slice(&body)?;
    Ok(Commit {
        sha: v["sha"]
            .as_str()
            .with_context(|| format!("GitHub returned no commit for '{rev}'"))?
            .to_string(),
        date: v["commit"]["committer"]["date"]
            .as_str()
            .unwrap_or("")
            .to_string(),
    })
}

/// How many commits `head` is ahead of `base`.
fn commits_between(base: &str, head: &str) -> Option<u64> {
    let body = curl(
        &format!("https://api.github.com/repos/{REPO}/compare/{base}...{head}"),
        Some(5),
    )
    .ok()?;
    serde_json::from_slice::<serde_json::Value>(&body).ok()?["ahead_by"].as_u64()
}

fn latest_cli(timeout: u64) -> Option<String> {
    let body = curl("https://crates.io/api/v1/crates/canopus-cli", Some(timeout)).ok()?;
    let v: serde_json::Value = serde_json::from_slice(&body).ok()?;
    v["crate"]["max_stable_version"]
        .as_str()
        .map(str::to_string)
}

fn version_parts(v: &str) -> Vec<u64> {
    v.split('.').map(|p| p.parse().unwrap_or(0)).collect()
}

fn newer(a: &str, b: &str) -> bool {
    version_parts(a) > version_parts(b)
}

fn read_commit(snapshot: &Path) -> Option<Commit> {
    let text = std::fs::read_to_string(snapshot.join(COMMIT_FILE)).ok()?;
    let mut lines = text.lines();
    Some(Commit {
        sha: lines.next()?.to_string(),
        date: lines.next().unwrap_or("").to_string(),
    })
}

/// The commit a checkout or snapshot is at, for package provenance.
pub fn commit_of(root: &Path) -> Option<String> {
    if let Some(c) = read_commit(root) {
        return Some(c.sha);
    }
    let out = Command::new("git")
        .arg("-C")
        .arg(root)
        .args(["rev-parse", "HEAD"])
        .output()
        .ok()?;
    out.status
        .success()
        .then(|| String::from_utf8_lossy(&out.stdout).trim().to_string())
}

/// Unpacks a GitHub tarball (one top-level `<repo>-<sha>/` directory) into `dest`.
fn unpack(tarball: &[u8], dest: &Path) -> anyhow::Result<()> {
    let mut archive = tar::Archive::new(flate2::read::GzDecoder::new(tarball));
    for entry in archive.entries()? {
        let mut entry = entry?;
        let rel: PathBuf = entry.path()?.components().skip(1).collect();
        if rel.as_os_str().is_empty()
            || rel.components().any(|c| !matches!(c, Component::Normal(_)))
        {
            continue;
        }
        let out = dest.join(&rel);
        match entry.header().entry_type() {
            tar::EntryType::Directory => std::fs::create_dir_all(&out)?,
            tar::EntryType::Regular | tar::EntryType::Symlink => {
                std::fs::create_dir_all(out.parent().unwrap())?;
                entry.unpack(&out)?;
            }
            // The pax global header (GitHub's commit comment) and the like.
            _ => {}
        }
    }
    Ok(())
}

fn min_cli(snapshot: &Path) -> anyhow::Result<String> {
    let path = snapshot.join(COMPAT_FILE);
    let text = std::fs::read_to_string(&path).with_context(|| {
        format!("this commit predates `canopus sdk` ({COMPAT_FILE} is missing)")
    })?;
    let value: toml::Value = toml::from_str(&text)?;
    Ok(value["min_cli"]
        .as_str()
        .with_context(|| format!("{COMPAT_FILE} has no min_cli"))?
        .to_string())
}

fn targets(root: &Path) -> Vec<(String, u32)> {
    list_target_packs(&root.join("targets"))
        .map(|packs| {
            packs
                .into_iter()
                .map(|p| (p.target_id, p.revision))
                .collect()
        })
        .unwrap_or_default()
}

fn print_target_changes(old: &[(String, u32)], new: &[(String, u32)]) {
    for (id, rev) in new {
        match old.iter().find(|(o, _)| o == id) {
            None => println!("  + {id} (revision {rev})"),
            Some((_, o)) if o != rev => println!("  ~ {id} (revision {o} -> {rev})"),
            Some(_) => {}
        }
    }
    for (id, _) in old {
        if !new.iter().any(|(n, _)| n == id) {
            println!("  - {id}");
        }
    }
}

/// Downloads `rev` (default: the latest `main` commit) and makes it current.
pub fn update(rev: Option<&str>) -> anyhow::Result<()> {
    let commit = remote_commit(rev.unwrap_or(BRANCH), None)?;
    let sdk = sdk_dir();
    let old = read_commit(&current());
    if old.as_ref().is_some_and(|o| o.sha == commit.sha) {
        println!("SDK is up to date: {} ({})", commit.short(), commit.date);
        return Ok(());
    }
    println!("downloading {REPO}@{} ({})", commit.short(), commit.date);
    let tarball = curl(
        &format!("https://codeload.github.com/{REPO}/tar.gz/{}", commit.sha),
        None,
    )?;
    std::fs::create_dir_all(&sdk)?;
    let staging = sdk.join(format!(".download-{}", commit.sha));
    if staging.exists() {
        std::fs::remove_dir_all(&staging)?;
    }
    unpack(&tarball, &staging)?;
    let needed = min_cli(&staging).and_then(|min| {
        if newer(&min, CLI_VERSION) {
            bail!(
                "{} needs canopus {min} or newer (this is {CLI_VERSION}): run `cargo install canopus-cli`, then update again",
                commit.short()
            );
        }
        Ok(())
    });
    if let Err(e) = needed {
        std::fs::remove_dir_all(&staging)?;
        return Err(e);
    }
    std::fs::write(
        staging.join(COMMIT_FILE),
        format!("{}\n{}\n", commit.sha, commit.date),
    )?;
    let old_targets = targets(&current());
    let dest = sdk.join(&commit.sha);
    if dest.exists() {
        std::fs::remove_dir_all(&dest)?;
    }
    std::fs::rename(&staging, &dest)?;
    let link = sdk.join(".current");
    let _ = std::fs::remove_file(&link);
    std::os::unix::fs::symlink(&commit.sha, &link)?;
    std::fs::rename(&link, current())?;
    for entry in std::fs::read_dir(&sdk)?.flatten() {
        let name = entry.file_name();
        if entry.file_type()?.is_dir() && name != commit.sha.as_str() {
            std::fs::remove_dir_all(entry.path())?;
        }
    }
    match old {
        Some(o) => println!(
            "updated {} -> {} ({})",
            o.short(),
            commit.short(),
            commit.date
        ),
        None => println!(
            "installed {} ({}) at {}",
            commit.short(),
            commit.date,
            current().display()
        ),
    }
    print_target_changes(&old_targets, &targets(&current()));
    Ok(())
}

pub fn status() -> anyhow::Result<()> {
    match locate_root() {
        Ok((root, source)) => println!("in use:    {} ({})", root.display(), source.label()),
        Err(e) => println!("in use:    none ({e})"),
    }
    let installed = read_commit(&current());
    match &installed {
        Some(c) => println!("snapshot:  {} ({})", c.short(), c.date),
        None => println!("snapshot:  not installed (`canopus sdk update` downloads it)"),
    }
    match remote_commit(BRANCH, Some(10)) {
        Ok(latest) => {
            let behind = match &installed {
                Some(c) if c.sha == latest.sha => "up to date".to_string(),
                Some(c) => match commits_between(&c.sha, &latest.sha) {
                    Some(n) => {
                        format!("{n} commit(s) ahead of the snapshot: run `canopus sdk update`")
                    }
                    None => "newer than the snapshot: run `canopus sdk update`".to_string(),
                },
                None => String::new(),
            };
            println!(
                "{BRANCH}:      {} ({}) {behind}",
                latest.short(),
                latest.date
            );
        }
        Err(e) => println!("{BRANCH}:      unknown ({e})"),
    }
    match latest_cli(10) {
        Some(v) if newer(&v, CLI_VERSION) => {
            println!("cli:       {CLI_VERSION}; {v} is available: `cargo install canopus-cli`")
        }
        Some(_) => println!("cli:       {CLI_VERSION} (latest)"),
        None => println!("cli:       {CLI_VERSION}"),
    }
    Ok(())
}

/// At most once a day, while building against the snapshot: says when `main`
/// or the CLI on crates.io moved on. Never updates, never fails a command.
pub fn notify_if_outdated() {
    let Some(installed) = read_commit(&current()) else {
        return;
    };
    if !matches!(locate_root(), Ok((_, RootSource::Sdk))) {
        return;
    }
    let stamp = sdk_dir().join(".last-check");
    let checked_recently = std::fs::metadata(&stamp)
        .and_then(|m| m.modified())
        .is_ok_and(|t| t.elapsed().is_ok_and(|age| age < CHECK_INTERVAL));
    if checked_recently {
        return;
    }
    let _ = std::fs::write(&stamp, "");
    if let Ok(latest) = remote_commit(BRANCH, Some(3))
        && latest.sha != installed.sha
    {
        eprintln!(
            "note: the Canopus SDK has a newer commit ({} -> {}): run `canopus sdk update`",
            installed.short(),
            latest.short()
        );
    }
    if let Some(v) = latest_cli(3)
        && newer(&v, CLI_VERSION)
    {
        eprintln!("note: canopus {v} is available: run `cargo install canopus-cli`");
    }
}
