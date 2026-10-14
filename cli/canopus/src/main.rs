//! canopus CLI — host tooling entry point.

mod commands;
mod project;

use clap::{Parser, Subcommand};
use std::path::PathBuf;

#[derive(Parser)]
#[command(
    name = "canopus",
    version,
    about = "Canopus firmware module framework host tooling"
)]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Target pack commands.
    #[command(subcommand)]
    Target(TargetCmd),
    /// Symbol record commands.
    #[command(subcommand)]
    Symbol(SymbolCmd),
    /// Type/layout record commands.
    #[command(subcommand)]
    Type(TypeCmd),
    /// Evidence bundle commands.
    #[command(subcommand)]
    Evidence(EvidenceCmd),
    /// Module manifest (Canopus.toml) commands.
    #[command(subcommand)]
    Module(ModuleCmd),
    /// Package a built project (no subcommand: Canopus.toml -> .canopus with
    /// per-target receipts) or work with package manifests/archives.
    #[command(args_conflicts_with_subcommands = true)]
    Package {
        #[command(subcommand)]
        cmd: Option<PackageCmd>,
        #[arg(long, default_value = "Canopus.toml")]
        manifest: PathBuf,
        /// Output file (default: dist/<id>-<version>.canopus).
        #[arg(long)]
        output: Option<PathBuf>,
        /// Also write a signed quick-app container (.rpk) next to it, for
        /// installing through the phone's stock quick-app flow.
        #[arg(long)]
        rpk: bool,
        /// Directory holding the rpk signing key (default ~/.canopus/rpk).
        #[arg(long)]
        rpk_key_dir: Option<PathBuf>,
        /// Also write installer watchface folders (one per device family) to
        /// dist/installer/ for the legacy /dev/canopus or ordinary-IO flow.
        #[arg(long)]
        installer: bool,
    },
    /// Mirage emulator workflows (end-to-end install/update/rollback smoke).
    #[command(subcommand)]
    Emu(EmuCmd),
    /// Create a module project: Canopus.toml + a C source (portable by default).
    New {
        /// Module id (ADR-CAN-003: ^[a-z][a-z0-9_]{0,15}$).
        id: String,
        /// Directory to create the project in.
        #[arg(long, default_value = ".")]
        dir: PathBuf,
        /// Make a target-bound module for these exact target ids (repeatable).
        #[arg(long = "target", action = clap::ArgAction::Append)]
        targets: Vec<String>,
    },
    /// Build a module project (portable: once; target: per exact target) and verify it.
    Build {
        #[arg(long, default_value = "Canopus.toml")]
        manifest: PathBuf,
        /// Build only this target (target modules).
        #[arg(long)]
        target: Option<String>,
    },
    /// RE orchestrator commands (Phase 9).
    #[command(subcommand)]
    Re(ReCmd),
    /// Expand a module across its target matrix.
    BuildPlan {
        /// Path to Canopus.toml (module manifest).
        #[arg(value_name = "Canopus.toml")]
        manifest: PathBuf,
        /// Directory containing target packs.
        #[arg(long, default_value = "targets")]
        targets_dir: PathBuf,
        #[arg(long)]
        json: bool,
    },
    /// Verify a compiled module ELF against a target pack.
    Verify {
        /// Module ELF (ET_REL) to verify.
        elf: PathBuf,
        /// Target id to verify against, e.g. xiaomi-band-10-pro-3.101.036.
        #[arg(long, required_unless_present = "portable", conflicts_with = "portable")]
        target: Option<String>,
        /// Portable module: no firmware address of any registered target may appear.
        #[arg(long)]
        portable: bool,
        /// Directory containing target packs.
        #[arg(long, default_value = "targets")]
        targets_dir: PathBuf,
        #[arg(long)]
        json: bool,
    },
}

#[derive(Subcommand)]
enum ReCmd {
    /// Create a new RE task.
    NewTask {
        /// Task id, e.g. T-007.
        id: String,
        /// Target id the task operates on.
        target: String,
        /// Short description.
        desc: String,
    },
    /// Transition a task (forward only; reject/withdraw keep history).
    TransitionTask {
        id: String,
        /// One of analyzing, evidence-gathered, verifying, promoted, rejected, withdrawn.
        state: String,
    },
    /// Add an evidence record to a task.
    AddEvidence {
        task: String,
        id: String,
        /// function | type | signature | layout
        kind: String,
        summary: String,
    },
    /// Transition an evidence record.
    TransitionEvidence {
        id: String,
        /// One of candidate, verified, promoted, refuted, withdrawn.
        state: String,
    },
    /// Evaluate the human promotion gate for evidence.
    Gate {
        id: String,
        #[arg(long, default_value_t = 1)]
        needed: usize,
    },
    /// Emit a minimal, safe C probe module for a callable symbol (RE-008).
    Probe {
        target: String,
        symbol: String,
        #[arg(long, default_value = "targets")]
        targets_dir: PathBuf,
        #[arg(long)]
        output: Option<PathBuf>,
    },
}

#[derive(Subcommand)]
enum TargetCmd {
    /// Validate a target.toml / target.json.
    Validate { path: PathBuf },
    /// List registered target packs.
    List {
        #[arg(long, default_value = "targets")]
        targets_dir: PathBuf,
        /// Only active (non-frozen) targets.
        #[arg(long)]
        active: bool,
        /// Print bare target ids, one per line.
        #[arg(long)]
        names: bool,
    },
    /// Generate every artifact derived from a target pack: C veneer + config
    /// header, Rust bindings, SDK private-symbol metadata, native address header.
    Generate {
        /// Target id (omit with --all).
        target: Option<String>,
        /// All active (non-frozen) targets.
        #[arg(long)]
        all: bool,
        /// Do not write; fail if any generated file is stale.
        #[arg(long)]
        check: bool,
        #[arg(long, default_value = "targets")]
        targets_dir: PathBuf,
    },
    /// Print target + family build data (`--shell` for sh-eval, `--json`).
    Info {
        target: String,
        #[arg(long, default_value = "targets")]
        targets_dir: PathBuf,
        /// Print VAR='value' lines for `eval` in shell scripts.
        #[arg(long)]
        shell: bool,
        /// Include Band 11 native bindings/addresses (JSON only).
        #[arg(long)]
        json: bool,
    },
}

#[derive(Subcommand)]
enum SymbolCmd {
    /// Validate a symbol record (schema + policy).
    Validate { path: PathBuf },
}

#[derive(Subcommand)]
enum TypeCmd {
    /// Validate a type/layout record.
    Validate { path: PathBuf },
}

#[derive(Subcommand)]
enum EvidenceCmd {
    /// Validate an evidence bundle.
    Validate { path: PathBuf },
}

#[derive(Subcommand)]
enum ModuleCmd {
    /// Validate a module manifest (Canopus.toml / .json).
    Validate { path: PathBuf },
    /// Legacy in-repo scaffold (target-bound C or Rust); prefer `canopus new`.
    New {
        /// Module name (reverse-dns-safe; sanitized for C identifiers).
        name: String,
        /// Language: c or rust.
        #[arg(long, default_value = "c")]
        lang: String,
        /// Target id to bake into the descriptor, e.g. xiaomi-band-10-pro-3.101.036.
        #[arg(long, default_value = "xiaomi-band-10-pro-3.101.036")]
        target: String,
        /// Directory containing target packs (default: targets/).
        #[arg(long, default_value = "targets")]
        targets_dir: PathBuf,
        /// Directory to create the module in (default: modules/).
        #[arg(long, default_value = "modules")]
        out_dir: PathBuf,
    },
}

#[derive(Subcommand)]
enum PackageCmd {
    /// Validate a package manifest.
    Validate { path: PathBuf },
    /// Build a .canopus package from a manifest + artifact files.
    Build {
        /// Package manifest JSON.
        manifest: PathBuf,
        /// Artifact source, target_id=path (repeatable).
        #[arg(long = "artifact", action = clap::ArgAction::Append)]
        artifact: Vec<String>,
        /// Native-app resource source, declared_path=file (repeatable).
        #[arg(long = "resource", action = clap::ArgAction::Append)]
        resource: Vec<String>,
        /// Output .canopus file.
        #[arg(long)]
        output: PathBuf,
    },
    /// Verify a package's structure and artifact SHA-256 hashes.
    Verify { pkg: PathBuf },
}

#[derive(Subcommand)]
enum EmuCmd {
    /// Run scripts/emu/smoke.py (exit 77 when Mirage or the firmware is missing).
    Smoke,
}

fn main() {
    let cli = Cli::parse();
    if let Err(e) = run(cli) {
        eprintln!("error: {e}");
        std::process::exit(1);
    }
}

fn run(cli: Cli) -> anyhow::Result<()> {
    match cli.command {
        Command::Target(cmd) => commands::target(cmd),
        Command::New { id, dir, targets } => project::new(&id, &dir, &targets),
        Command::Build { manifest, target } => project::build(&manifest, target.as_deref()),
        Command::Symbol(cmd) => commands::symbol(cmd),
        Command::Type(cmd) => commands::type_cmd(cmd),
        Command::Evidence(cmd) => commands::evidence(cmd),
        Command::Module(cmd) => commands::module(cmd),
        Command::Package { cmd: Some(cmd), .. } => commands::package(cmd),
        Command::Package {
            cmd: None,
            manifest,
            output,
            rpk,
            rpk_key_dir,
            installer,
        } => project::package(&manifest, output.as_deref(), rpk, rpk_key_dir.as_deref(), installer),
        Command::Emu(EmuCmd::Smoke) => {
            let root = project::canopus_root()?;
            let status = std::process::Command::new("python3")
                .args(["-u", "scripts/emu/smoke.py"])
                .current_dir(&root)
                .status()?;
            std::process::exit(status.code().unwrap_or(1));
        }
        Command::Re(cmd) => commands::re(cmd),
        Command::BuildPlan {
            manifest,
            targets_dir,
            json,
        } => commands::build_plan(&manifest, &targets_dir, json),
        Command::Verify {
            elf,
            target,
            portable,
            targets_dir,
            json,
        } => commands::verify(&elf, target.as_deref(), portable, &targets_dir, json),
    }
}
