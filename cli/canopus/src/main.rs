//! canopus CLI — host tooling entry point.

mod commands;

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
    /// Package manifest commands.
    #[command(subcommand)]
    Package(PackageCmd),
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
        /// Target id to verify against, e.g. xiaomi-band-10-pro-3.101.030.
        #[arg(long)]
        target: String,
        /// Directory containing target packs.
        #[arg(long, default_value = "targets")]
        targets_dir: PathBuf,
        #[arg(long)]
        json: bool,
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
    },
    /// Generate the C veneer + identity guard header for a target pack.
    GenerateVeneer {
        /// Target id, e.g. xiaomi-band-10-pro-3.101.030.
        target: String,
        #[arg(long, default_value = "targets")]
        targets_dir: PathBuf,
        /// Write output to this path (default: <pack>/generated/canopus_veneer.h).
        #[arg(long)]
        output: Option<PathBuf>,
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
        /// Output .canopus file.
        #[arg(long)]
        output: PathBuf,
        /// Sign with this secret key (32 bytes hex).
        #[arg(long)]
        key: Option<String>,
    },
    /// Append an Ed25519 signature entry to a package.
    Sign {
        pkg: PathBuf,
        /// Secret key hex (32 bytes).
        #[arg(long)]
        key: String,
        #[arg(long)]
        output: Option<PathBuf>,
    },
    /// Verify a package's Ed25519 signature over its payload.
    Verify {
        pkg: PathBuf,
        /// Public key hex (32 bytes).
        #[arg(long)]
        pubkey: String,
    },
    /// Generate a fresh Ed25519 key pair.
    Keygen {
        /// Optional output file (writes secret\\npublic). Otherwise prints.
        #[arg(long)]
        output: Option<PathBuf>,
    },
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
        Command::Symbol(cmd) => commands::symbol(cmd),
        Command::Type(cmd) => commands::type_cmd(cmd),
        Command::Evidence(cmd) => commands::evidence(cmd),
        Command::Module(cmd) => commands::module(cmd),
        Command::Package(cmd) => commands::package(cmd),
        Command::BuildPlan {
            manifest,
            targets_dir,
            json,
        } => commands::build_plan(&manifest, &targets_dir, json),
        Command::Verify {
            elf,
            target,
            targets_dir,
            json,
        } => commands::verify(&elf, &target, &targets_dir, json),
    }
}
