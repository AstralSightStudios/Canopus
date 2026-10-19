//! canopus CLI — host tooling entry point.

mod commands;
mod doctor;
mod env;
mod project;

use clap::{Parser, Subcommand};
use std::path::PathBuf;

#[derive(Parser)]
#[command(
    name = "canopus",
    version,
    about = "Canopus firmware module framework host tooling",
    after_help = "Quick start:\n  canopus doctor                      check the toolchain\n  canopus new hello --target 155      create a module project\n  cd hello && canopus build           compile and verify for every target\n  canopus package                     write dist/hello-0.1.0.canopus\n\nTargets can be named by a unique version suffix (155) or by their full id."
)]
struct Cli {
    /// Print every external tool invocation.
    #[arg(short, long, global = true)]
    verbose: bool,
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Create a module project: Canopus.toml + a C source (portable by default).
    ///
    /// Examples:
    ///   canopus new hello                       portable C module
    ///   canopus new hello --target 155          bound to Band 11 4.100.155
    ///   canopus new hello --target 036 --target 043 --ui
    ///   canopus new hello --lang rust --target 155
    New {
        /// Module id (ADR-CAN-003: ^[a-z][a-z0-9_]{0,15}$).
        id: String,
        /// Directory to create the project in.
        #[arg(long, default_value = ".")]
        dir: PathBuf,
        /// Make a target-bound module for this target (repeatable): an exact
        /// target id or a unique version suffix such as `155`.
        #[arg(long = "target", action = clap::ArgAction::Append)]
        targets: Vec<String>,
        /// Source language: `c` (default) or `rust` (target-bound only).
        #[arg(long, default_value = "c")]
        lang: String,
        /// Start from a module with a UI page (C only).
        #[arg(long)]
        ui: bool,
    },
    /// Build a module project (portable: once; target: per exact target) and verify it.
    Build {
        #[arg(long, default_value = "Canopus.toml")]
        manifest: PathBuf,
        /// Build only this target (target modules): an id or a unique version suffix.
        #[arg(long)]
        target: Option<String>,
    },
    /// Package a built project into dist/<id>-<version>.canopus (or, with a
    /// subcommand, work with package manifests and archives).
    #[command(args_conflicts_with_subcommands = true)]
    Package {
        #[command(subcommand)]
        cmd: Option<PackageCmd>,
        #[arg(long, default_value = "Canopus.toml")]
        manifest: PathBuf,
        /// Output file (default: dist/<id>-<version>.canopus).
        #[arg(long)]
        output: Option<PathBuf>,
        /// Also write installer watchface folders (one per device family) to
        /// dist/installer/ for the legacy /dev/canopus or ordinary-IO flow.
        #[arg(long)]
        installer: bool,
        /// Build first, so the package never carries a stale ELF.
        #[arg(long)]
        build: bool,
    },
    /// Remove a project's build/ and dist/.
    Clean {
        #[arg(long, default_value = "Canopus.toml")]
        manifest: PathBuf,
    },
    /// Check the toolchain, the checkout and the registered targets.
    Doctor,
    /// Target pack commands.
    #[command(subcommand)]
    Target(TargetCmd),
    /// Verify a compiled module ELF against a target pack.
    Verify {
        /// Module ELF (ET_REL) to verify.
        elf: PathBuf,
        /// Target id to verify against, e.g. xiaomi-band-10-pro-3.101.036.
        #[arg(
            long,
            required_unless_present = "portable",
            conflicts_with = "portable"
        )]
        target: Option<String>,
        /// Portable module: no firmware address of any registered target may appear.
        #[arg(long)]
        portable: bool,
        /// Directory containing target packs.
        #[arg(long)]
        targets_dir: Option<PathBuf>,
        #[arg(long)]
        json: bool,
        /// Fail when the sections the loader allocates (.bss included) exceed this many bytes.
        #[arg(long)]
        max_resident: Option<u64>,
    },
    /// Expand a module across its target matrix.
    BuildPlan {
        /// Path to Canopus.toml (module manifest).
        #[arg(value_name = "Canopus.toml")]
        manifest: PathBuf,
        /// Directory containing target packs.
        #[arg(long)]
        targets_dir: Option<PathBuf>,
        #[arg(long)]
        json: bool,
    },
    /// Module manifest (Canopus.toml) commands.
    #[command(subcommand)]
    Module(ModuleCmd),
    /// Symbol record commands.
    #[command(subcommand)]
    Symbol(SymbolCmd),
    /// Type/layout record commands.
    #[command(subcommand)]
    Type(TypeCmd),
    /// Evidence bundle commands.
    #[command(subcommand)]
    Evidence(EvidenceCmd),
    /// RE orchestrator commands (Phase 9).
    #[command(subcommand)]
    Re(ReCmd),
    /// Mirage emulator workflows (end-to-end install/update/rollback smoke).
    #[command(subcommand)]
    Emu(EmuCmd),
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
        #[arg(long)]
        targets_dir: Option<PathBuf>,
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
        #[arg(long)]
        targets_dir: Option<PathBuf>,
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
        #[arg(long)]
        targets_dir: Option<PathBuf>,
    },
    /// Print target + family build data (`--shell` for sh-eval, `--json`).
    Info {
        target: String,
        #[arg(long)]
        targets_dir: Option<PathBuf>,
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
        #[arg(long)]
        targets_dir: Option<PathBuf>,
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

/// `canopus ... | head` closes our stdout early; that is not a failure to report.
fn quiet_on_broken_pipe() {
    let default = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        let message = info
            .payload()
            .downcast_ref::<String>()
            .map(String::as_str)
            .or_else(|| info.payload().downcast_ref::<&str>().copied())
            .unwrap_or("");
        if message.contains("Broken pipe") {
            std::process::exit(0);
        }
        default(info);
    }));
}

fn main() {
    quiet_on_broken_pipe();
    let cli = Cli::parse();
    env::set_verbose(cli.verbose);
    if let Err(e) = run(cli) {
        eprintln!("error: {e:#}");
        std::process::exit(1);
    }
}

fn run(cli: Cli) -> anyhow::Result<()> {
    match cli.command {
        Command::Target(cmd) => commands::target(cmd),
        Command::New {
            id,
            dir,
            targets,
            lang,
            ui,
        } => project::new(&project::NewOptions {
            id: &id,
            dir: &dir,
            targets: &targets,
            lang: &lang,
            ui,
        }),
        Command::Build { manifest, target } => project::build(&manifest, target.as_deref()),
        Command::Clean { manifest } => project::clean(&manifest),
        Command::Doctor => doctor::run(),
        Command::Symbol(cmd) => commands::symbol(cmd),
        Command::Type(cmd) => commands::type_cmd(cmd),
        Command::Evidence(cmd) => commands::evidence(cmd),
        Command::Module(cmd) => commands::module(cmd),
        Command::Package { cmd: Some(cmd), .. } => commands::package(cmd),
        Command::Package {
            cmd: None,
            manifest,
            output,
            installer,
            build,
        } => project::package(&manifest, output.as_deref(), installer, build),
        Command::Emu(EmuCmd::Smoke) => {
            let root = env::canopus_root()?;
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
        } => commands::build_plan(&manifest, &env::targets_dir(targets_dir)?, json),
        Command::Verify {
            elf,
            target,
            portable,
            targets_dir,
            json,
            max_resident,
        } => {
            let targets_dir = env::targets_dir(targets_dir)?;
            let target = target
                .map(|t| env::resolve_target(&targets_dir, &t))
                .transpose()?;
            commands::verify(
                &elf,
                target.as_deref(),
                portable,
                &targets_dir,
                json,
                &[],
                max_resident,
            )
        }
    }
}
