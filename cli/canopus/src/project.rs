//! Module projects: `canopus new`, `canopus build`, `canopus package`
//! (refactor-plan P3.1–P3.3). A project is a `Canopus.toml` plus C sources.
//! Portable modules (the default) are built once; target modules once per
//! exact target. Packages carry an unsigned CMI2 receipt per target.

use crate::env::{canopus_root, display_rel, newest_input, read_text, resolve_in, run, tool};
use anyhow::{Context, bail};
use canopus_core::model::{ModuleManifest, PackageArtifact, PackageManifest, TargetPack};
use canopus_core::registry::list_target_packs;
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::Command;

const PORTABLE_DIR: &str = "abi-1-thumbv8m";

struct Project {
    dir: PathBuf,
    manifest: ModuleManifest,
}

impl Project {
    fn load(manifest_path: &Path) -> anyhow::Result<Self> {
        let text = read_text(
            manifest_path,
            "run this in a module project (create one with `canopus new <id>`) or pass --manifest",
        )?;
        let invalid = || format!("{} is not a valid module manifest", manifest_path.display());
        let value =
            serde_json::to_value(toml::from_str::<toml::Value>(&text).with_context(invalid)?)?;
        canopus_core::schema::validate(canopus_core::schema::SchemaKind::Module, &value)
            .with_context(invalid)?;
        let manifest: ModuleManifest = serde_json::from_value(value).with_context(invalid)?;
        if manifest.module.language == "rust" && manifest.is_portable() {
            bail!(
                "Rust modules are target-bound: there are no Rust bindings for the Supervisor service table yet"
            );
        }
        let dir = manifest_path
            .parent()
            .filter(|p| !p.as_os_str().is_empty())
            .unwrap_or(Path::new("."))
            .to_path_buf();
        Ok(Self { dir, manifest })
    }

    fn id(&self) -> &str {
        &self.manifest.module.id
    }

    fn build_dir(&self) -> PathBuf {
        self.dir.join("build")
    }

    fn elf_path(&self, target: Option<&str>) -> PathBuf {
        match target {
            Some(t) => self.build_dir().join(t).join(format!("{}.elf", self.id())),
            None => self.build_dir().join(format!("{}.elf", self.id())),
        }
    }
}

fn expand(s: &str, root: &Path) -> String {
    s.replace("${CANOPUS_ROOT}", &root.to_string_lossy())
}

const RUST_TRIPLE: &str = "thumbv8m.main-none-eabi";

/// Rust modules: `[build] sources = ["Cargo.toml"]` names the crate (a
/// `staticlib`, `#![no_std]`, panic=abort). Pipeline: cargo build for the
/// device triple with the exact target's bindings feature -> C ctor shim ->
/// `ld.lld -r --whole-archive` -> zero-import ELF.
fn compile_rust(project: &Project, root: &Path, target: &TargetPack) -> anyhow::Result<PathBuf> {
    let out = project.elf_path(Some(&target.target_id));
    let out_dir = out.parent().unwrap().to_path_buf();
    std::fs::create_dir_all(&out_dir)?;
    let manifest = project
        .manifest
        .build
        .sources
        .first()
        .map(|s| project.dir.join(expand(s, root)))
        .filter(|p| p.file_name().is_some_and(|n| n == "Cargo.toml"))
        .context("a Rust module's [build] sources must be [\"Cargo.toml\"]")?;
    let cargo_target = project.build_dir().join("cargo");
    let feature = format!("target-{}", target.target_id.replace('.', "-"));
    let cargo_toml = std::fs::read_to_string(&manifest)?;
    let mut feat_list = vec!["device".to_string()];
    if cargo_toml.contains("canopus-target-generated") {
        feat_list.push(format!("canopus-target-generated/{feature}"));
    }
    if cargo_toml.contains("canopus-target-private") {
        feat_list.push(format!("canopus-target-private/{feature}"));
    }
    if cargo_toml.contains(&format!("{feature} ="))
        || cargo_toml.contains(&format!("\"{feature}\""))
    {
        feat_list.push(feature.clone());
    }
    let features_arg = feat_list.join(",");
    run(
        Command::new(tool("CARGO", "cargo"))
            .args([
                "build",
                "--release",
                "--target",
                RUST_TRIPLE,
                "--no-default-features",
            ])
            .arg("--features")
            .arg(features_arg)
            .arg("--manifest-path")
            .arg(&manifest)
            .arg("--target-dir")
            .arg(&cargo_target)
            .env("CANOPUS_TARGET_ID", &target.target_id)
            // Same policy as the workspace's `device` profile.
            .env("CARGO_PROFILE_RELEASE_PANIC", "abort")
            .env("CARGO_PROFILE_RELEASE_OPT_LEVEL", "z")
            .env("CARGO_PROFILE_RELEASE_LTO", "thin")
            .env("CARGO_PROFILE_RELEASE_CODEGEN_UNITS", "1"),
        "building the Rust crate",
    )?;
    let release = cargo_target.join(RUST_TRIPLE).join("release");
    let archives: Vec<PathBuf> = std::fs::read_dir(&release)?
        .filter_map(|e| e.ok().map(|e| e.path()))
        .filter(|p| p.extension().is_some_and(|x| x == "a"))
        .collect();
    let [archive] = archives.as_slice() else {
        bail!(
            "expected exactly one staticlib in {} (crate-type = [\"staticlib\"]), found {}",
            release.display(),
            archives.len()
        );
    };
    // The constructor is the shared C shim: it registers the crate's descriptor
    // through sdk/c/canopus_module_entry.h, which picks the path the target's
    // loader gives constructors.
    let shim_c = root.join("sdk/rust/c_shim/canopus_ctor.c");
    let shim_o = out_dir.join("canopus_ctor.o");
    run(
        Command::new(tool("CC", "clang"))
            .args([
                "--target=arm-none-eabi",
                "-mcpu=cortex-m33",
                "-mthumb",
                "-mfloat-abi=soft",
                "-ffreestanding",
                "-fno-common",
                "-fno-builtin",
                "-fno-stack-protector",
                "-fno-unwind-tables",
                "-fno-asynchronous-unwind-tables",
                "-fdata-sections",
                "-ffunction-sections",
                "-Os",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DCANOPUS_TARGET=1",
            ])
            .arg(format!("-I{}", root.join("sdk/c").display()))
            .arg(format!(
                "-I{}",
                root.join("targets")
                    .join(&target.target_id)
                    .join("generated")
                    .display()
            ))
            .arg("-c")
            .arg(&shim_c)
            .arg("-o")
            .arg(&shim_o),
        "compiling the constructor shim",
    )?;
    run(
        Command::new(tool("LD", "ld.lld"))
            .args(["-r", "-T"])
            .arg(root.join("sdk/rust/c_shim/module.ld"))
            .arg(&shim_o)
            .arg(archive)
            .arg("-o")
            .arg(&out),
        "linking the module",
    )?;
    Ok(out)
}

/// `app-sdk/` was merged into `sdk/c` and `runtime/`; a manifest that still names
/// one of its sources is pointed at the new place (with a warning).
fn moved_from_app_sdk(src: PathBuf, root: &Path) -> PathBuf {
    const MOVED: [(&str, &str); 2] = [
        ("app-sdk/ui/canopus_ui.c", "runtime/ui/canopus_ui.c"),
        ("app-sdk/c/canopus_app.c", "runtime/app/canopus_app.c"),
    ];
    for (old, new) in MOVED {
        if src == root.join(old) {
            eprintln!("warning: {old} moved to {new}; update [build] sources");
            return root.join(new);
        }
    }
    src
}

/// Compiles the sources and links one relocatable ELF.
fn compile(project: &Project, root: &Path, target: Option<&TargetPack>) -> anyhow::Result<PathBuf> {
    if project.manifest.module.language == "rust" {
        return compile_rust(
            project,
            root,
            target.context("a Rust module needs an exact target")?,
        );
    }
    let out = project.elf_path(target.map(|t| t.target_id.as_str()));
    let obj_dir = out.parent().unwrap().join(format!("{}.obj", project.id()));
    std::fs::create_dir_all(&obj_dir)?;
    let build = &project.manifest.build;
    let mut objects = Vec::new();
    let mut include_dirs: Vec<PathBuf> = Vec::new();
    for dir in build.include_dirs.iter().flatten() {
        let dir = PathBuf::from(expand(dir, root));
        if dir.starts_with(root.join("app-sdk")) && !dir.exists() {
            eprintln!(
                "warning: {} is gone: app-sdk was merged into sdk/c, which is always on the include path; remove it from [build] include_dirs",
                dir.display()
            );
        } else if dir.is_absolute() {
            include_dirs.push(dir);
        } else {
            include_dirs.push(project.dir.join(dir));
        }
    }
    for (i, source) in build.sources.iter().enumerate() {
        let src = moved_from_app_sdk(PathBuf::from(expand(source, root)), root);
        let src = if src.is_absolute() {
            src
        } else {
            project.dir.join(src)
        };
        let obj = obj_dir.join(format!("{i}.o"));
        let mut cc = Command::new(tool("CC", "clang"));
        cc.args([
            "--target=arm-none-eabi",
            "-mcpu=cortex-m33",
            "-mthumb",
            "-mfloat-abi=soft",
            "-ffreestanding",
            "-fno-common",
            "-fno-builtin",
            "-fno-stack-protector",
            "-fno-unwind-tables",
            "-fno-asynchronous-unwind-tables",
            "-fdata-sections",
            "-ffunction-sections",
            "-Os",
            "-Wall",
            "-Wextra",
            "-Werror",
        ]);
        cc.arg(format!("-I{}", root.join("sdk/c").display()));
        if let Some(t) = target {
            cc.arg("-DCANOPUS_TARGET=1");
            cc.arg(format!(
                "-I{}",
                root.join("targets")
                    .join(&t.target_id)
                    .join("generated")
                    .display()
            ));
        }
        for dir in &include_dirs {
            cc.arg(format!("-I{}", dir.display()));
        }
        for define in build.defines.iter().flatten() {
            cc.arg(format!("-D{define}"));
        }
        cc.arg("-c").arg(&src).arg("-o").arg(&obj);
        run(
            &mut cc,
            &format!("compiling {}", display_rel(&src, &project.dir)),
        )?;
        objects.push(obj);
    }
    run(
        Command::new(tool("LD", "ld.lld"))
            .arg("-r")
            .arg("-o")
            .arg(&out)
            .args(&objects),
        "linking the module",
    )?;
    Ok(out)
}

fn selected_targets(
    project: &Project,
    root: &Path,
    only: Option<&str>,
) -> anyhow::Result<Vec<TargetPack>> {
    let all = list_target_packs(&root.join("targets"))?;
    let active: Vec<TargetPack> = all
        .iter()
        .filter(|p| p.status.as_deref() != Some("frozen"))
        .cloned()
        .collect();
    let rows = canopus_core::planner::expand(&project.manifest, &active)
        .map_err(|e| anyhow::anyhow!("{e}"))?;
    let mut packs: Vec<TargetPack> = rows
        .iter()
        .filter_map(|r| active.iter().find(|p| p.target_id == r.target_id).cloned())
        .collect();
    if let Some(only) = only {
        let id = resolve_in(&all, only)?;
        packs.retain(|p| p.target_id == id);
        if packs.is_empty() {
            bail!(
                "target '{id}' is not part of this module's plan ({})",
                rows.iter()
                    .map(|r| r.target_id.as_str())
                    .collect::<Vec<_>>()
                    .join(", ")
            );
        }
    }
    Ok(packs)
}

pub fn build(manifest: &Path, target: Option<&str>) -> anyhow::Result<()> {
    let project = Project::load(manifest)?;
    let root = canopus_root()?;
    let targets_dir = root.join("targets");
    if project.manifest.is_portable() {
        if target.is_some() {
            bail!("a portable module is built once; omit --target");
        }
        let elf = compile(&project, &root, None)?;
        println!("built {}", elf.display());
        crate::commands::verify(&elf, None, true, &targets_dir, false)?;
        println!("ok: {} (portable) built and verified", project.id());
    } else {
        let packs = selected_targets(&project, &root, target)?;
        for pack in &packs {
            let elf = compile(&project, &root, Some(pack))?;
            println!("built {}", elf.display());
            crate::commands::verify(&elf, Some(&pack.target_id), false, &targets_dir, false)?;
        }
        println!(
            "ok: {} built and verified for {} target(s)",
            project.id(),
            packs.len()
        );
    }
    Ok(())
}

/// An ELF older than the project's sources would silently package stale code.
fn ensure_fresh(project: &Project, elf: &Path) -> anyhow::Result<()> {
    let built = match std::fs::metadata(elf) {
        Ok(m) => m.modified()?,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => bail!(
            "{} is missing; run `canopus build` first (or `canopus package --build`)",
            elf.display()
        ),
        Err(e) => return Err(e).with_context(|| format!("cannot read {}", elf.display())),
    };
    if let Some((path, changed)) = newest_input(&project.dir)?
        && changed > built
    {
        bail!(
            "{} is older than {}; run `canopus build` again (or `canopus package --build`)",
            elf.display(),
            display_rel(&path, &project.dir)
        );
    }
    Ok(())
}

pub fn package(
    manifest: &Path,
    output: Option<&Path>,
    installer: bool,
    build_first: bool,
) -> anyhow::Result<()> {
    if build_first {
        build(manifest, None)?;
    }
    let project = Project::load(manifest)?;
    let root = canopus_root()?;
    let m = &project.manifest;
    let lifecycle = canopus_core::receipt::lifecycle_code(&m.module.lifecycle)?;
    let portable = m.is_portable();
    let packs = selected_targets(&project, &root, None)?;

    let mut artifacts = Vec::new();
    let mut files: HashMap<String, PathBuf> = HashMap::new();
    let mut receipts: HashMap<String, Vec<u8>> = HashMap::new();
    for pack in &packs {
        let elf = project.elf_path(if portable {
            None
        } else {
            Some(&pack.target_id)
        });
        ensure_fresh(&project, &elf)?;
        let bytes =
            std::fs::read(&elf).with_context(|| format!("cannot read {}", elf.display()))?;
        let receipt = canopus_core::receipt::build(
            project.id(),
            m.module.version_code,
            lifecycle,
            &pack.target_id,
            &pack.firmware_sha256,
            &bytes,
        )?;
        let dir = if portable {
            PORTABLE_DIR
        } else {
            pack.target_id.as_str()
        };
        artifacts.push(PackageArtifact {
            target_id: pack.target_id.clone(),
            target_pack_revision: u64::from(pack.revision),
            firmware_sha256: pack.firmware_sha256.clone(),
            path: format!("artifacts/{dir}/module.elf"),
            sha256: canopus_package::hex_sha256(&bytes),
            receipt_path: Some(format!("receipts/{}.cmi", pack.target_id)),
            receipt_sha256: Some(canopus_package::hex_sha256(&receipt)),
            symbols_used: None,
        });
        files.insert(pack.target_id.clone(), elf);
        receipts.insert(pack.target_id.clone(), receipt);
    }
    let package_manifest = PackageManifest {
        schema: 1,
        package_id: project.id().to_string(),
        module_id: project.id().to_string(),
        version: m.module.version.clone(),
        build_generation: u64::from(m.module.version_code),
        version_code: Some(m.module.version_code),
        canopus_abi: m.module.canopus_abi.clone(),
        lifecycle: m.module.lifecycle.clone(),
        artifacts,
        capabilities: m.capabilities.clone(),
        native_app: None,
        update_semantics: Some("staged next generation; switch at next load".into()),
        remove_semantics: None,
        reboot_required: Some(true),
        target_pack_revision: packs
            .iter()
            .map(|p| u64::from(p.revision))
            .max()
            .unwrap_or(1),
        min_manager_version: "0.1.0".into(),
        sbom: None,
        reproducibility: None,
    };
    let archive = canopus_package::build_archive_with_receipts(
        &package_manifest,
        &files,
        &HashMap::new(),
        &receipts,
    )?;
    let out = match output {
        Some(p) => p.to_path_buf(),
        None => {
            project
                .dir
                .join("dist")
                .join(format!("{}-{}.canopus", project.id(), m.module.version))
        }
    };
    if let Some(parent) = out.parent() {
        std::fs::create_dir_all(parent)?;
    }
    std::fs::write(&out, &archive)?;
    println!(
        "wrote {} ({} bytes, {} target(s), versionCode {})",
        out.display(),
        archive.len(),
        packs.len(),
        m.module.version_code
    );
    if installer {
        let elfs: Vec<(&TargetPack, PathBuf)> = packs
            .iter()
            .map(|p| {
                (
                    p,
                    project.elf_path(if portable { None } else { Some(&p.target_id) }),
                )
            })
            .collect();
        write_installers(&project, &root, &elfs, &receipts, &out)?;
    }
    Ok(())
}

pub struct NewOptions<'a> {
    pub id: &'a str,
    pub dir: &'a Path,
    pub targets: &'a [String],
    pub lang: &'a str,
    pub ui: bool,
}

/// Creates a module project. Everything is validated first, so a bad id,
/// language or target leaves nothing behind; a failure while writing removes
/// the half-made directory.
pub fn new(opts: &NewOptions) -> anyhow::Result<()> {
    let NewOptions {
        id,
        dir,
        targets,
        lang,
        ui,
    } = *opts;
    if !canopus_core::receipt::module_id_valid(id) {
        bail!("module id '{id}' must match ^[a-z][a-z0-9_]{{0,15}}$ (ADR-CAN-003)");
    }
    if lang != "c" && lang != "rust" {
        bail!("--lang must be c or rust");
    }
    if lang == "rust" && ui {
        bail!("--ui is a C template; there are no Rust bindings for the UI tree yet");
    }
    if lang == "rust" && targets.is_empty() {
        bail!("Rust modules are target-bound: pass at least one --target (e.g. --target 155)");
    }
    let root = canopus_root()?;
    let registered = list_target_packs(&root.join("targets"))?;
    let mut resolved: Vec<String> = Vec::new();
    for name in targets {
        let id = resolve_in(&registered, name)?;
        if !resolved.contains(&id) {
            resolved.push(id);
        }
    }
    let project = dir.join(id);
    if project.exists() {
        bail!("{} already exists", project.display());
    }

    let result = write_project(id, &project, &root, &resolved, lang, ui);
    if let Err(e) = result {
        let _ = std::fs::remove_dir_all(&project);
        return Err(e);
    }
    let kind = if resolved.is_empty() {
        "portable"
    } else {
        "target"
    };
    println!(
        "created {} ({kind} {}module{})",
        project.display(),
        if lang == "rust" { "Rust " } else { "" },
        if ui { ", UI page" } else { "" }
    );
    if !resolved.is_empty() {
        println!("targets: {}", resolved.join(", "));
    }
    println!(
        "next:   cd {} && canopus build && canopus package",
        project.display()
    );
    if resolved.is_empty() {
        println!(
            "        (portable: built once for every target; add --target <id> to bind exact firmware)"
        );
    }
    Ok(())
}

fn write_project(
    id: &str,
    project: &Path,
    root: &Path,
    targets: &[String],
    lang: &str,
    ui: bool,
) -> anyhow::Result<()> {
    std::fs::create_dir_all(project.join("src"))?;
    let kind = if targets.is_empty() {
        "portable"
    } else {
        "target"
    };
    let target_section = if targets.is_empty() {
        String::new()
    } else {
        format!(
            "\n[targets]\ninclude = [{}]\n",
            targets
                .iter()
                .map(|t| format!("\"{t}\""))
                .collect::<Vec<_>>()
                .join(", ")
        )
    };
    let sources = if lang == "rust" {
        "\"Cargo.toml\"".to_string()
    } else {
        format!("\"src/{id}.c\"")
    };
    std::fs::write(
        project.join("Canopus.toml"),
        format!(
            "schema = 1\n\n[module]\nid = \"{id}\"\nversion = \"0.1.0\"\nversion_code = 1\nkind = \"{kind}\"\nlanguage = \"{lang}\"\nlifecycle = \"removable\"\ncanopus_abi = \"1\"\n\n[build]\nsources = [{sources}]\n{target_section}"
        ),
    )?;
    std::fs::write(project.join(".gitignore"), "build/\ndist/\nCargo.lock\n")?;
    if lang == "rust" {
        let sdk = sdk_path(project, root)?;
        std::fs::write(
            project.join("Cargo.toml"),
            format!(
                "[package]\nname = \"{id}\"\nversion = \"0.1.0\"\nedition = \"2021\"\n\n[lib]\ncrate-type = [\"staticlib\"]\n\n[features]\ndefault = []\ndevice = [\"dep:canopus-target-generated\"]\n\n[dependencies]\ncanopus-abi = {{ path = \"{sdk}/canopus-abi\" }}\ncanopus-runtime = {{ path = \"{sdk}/canopus-runtime\" }}\ncanopus-target-generated = {{ path = \"{sdk}/canopus-target-generated\", optional = true, default-features = false }}\n"
            ),
        )?;
        std::fs::write(project.join("src/lib.rs"), rust_lib_template(id))?;
    } else {
        let source = if ui {
            ui_source_template(id, kind)
        } else {
            source_template(id, kind)
        };
        std::fs::write(project.join("src").join(format!("{id}.c")), source)?;
    }
    Ok(())
}

/// `<checkout>/sdk/rust` as a Cargo path: relative when the project lives inside
/// the checkout (so the project can move with it), else absolute.
fn sdk_path(project: &Path, root: &Path) -> anyhow::Result<String> {
    let project = std::path::absolute(project)?;
    let sdk = std::path::absolute(root)?.join("sdk/rust");
    if !project.starts_with(std::path::absolute(root)?) {
        return Ok(sdk.display().to_string());
    }
    let common = project
        .ancestors()
        .find(|a| sdk.starts_with(a))
        .context("no common ancestor")?;
    let mut rel = PathBuf::new();
    for _ in project.strip_prefix(common)?.components() {
        rel.push("..");
    }
    Ok(rel.join(sdk.strip_prefix(common)?).display().to_string())
}

/// Removes `build/` and `dist/`.
pub fn clean(manifest: &Path) -> anyhow::Result<()> {
    let project = Project::load(manifest)?;
    let mut removed = 0;
    for dir in [project.build_dir(), project.dir.join("dist")] {
        if dir.is_dir() {
            std::fs::remove_dir_all(&dir)?;
            println!("removed {}", dir.display());
            removed += 1;
        }
    }
    if removed == 0 {
        println!("nothing to clean in {}", project.dir.display());
    }
    Ok(())
}

/// One C skeleton for both kinds (sdk/c/canopus_module_entry.h registers it on
/// whichever path the target's loader gives constructors); only the descriptor's
/// target differs.
fn source_template(id: &str, kind: &str) -> String {
    let target_id = if kind == "target" {
        "CANOPUS_TARGET_ID"
    } else {
        "\"portable-thumbv8m\""
    };
    r#"/* {id} - Canopus module. */
#include "canopus_module_entry.h"

static const struct canopus_context_v1 *s_ctx;

static int32_t on_prepare(const struct canopus_context_v1 *ctx) { s_ctx = ctx; return 0; }

static int32_t on_activate(const struct canopus_context_v1 *ctx)
{
    if (canopus_services_has(ctx, CANOPUS_SVC_LOG, 1)) {
        ctx->services->log_write(ctx, CANOPUS_LOG_INFO, "{id} active");
    }
    return 0;
}

static int32_t on_deactivate(const struct canopus_context_v1 *ctx) { (void)ctx; return 0; }

static int32_t on_stop(const struct canopus_context_v1 *ctx) { (void)ctx; s_ctx = 0; return 0; }

static int32_t on_query(struct canopus_status_writer_v1 *writer) { (void)writer; return 0; }

static const struct canopus_module_descriptor_v1 s_descriptor = {
    sizeof(struct canopus_module_descriptor_v1), CANOPUS_ABI_MAJOR, CANOPUS_ABI_MINOR, 0u,
    "{id}", "0.1.0", "", {target_id},
    on_prepare, on_activate, on_deactivate, on_stop, on_query, 0, 0,
};

CANOPUS_MODULE_ENTRY(s_descriptor)
"#
    .replace("{id}", id)
    .replace("{target_id}", target_id)
}

/// A module with one UI page: a counter and a switch. The Manager lists it as
/// "打开界面" on the module's detail page (ADR-CAN-010).
fn ui_source_template(id: &str, kind: &str) -> String {
    let target_id = if kind == "target" {
        "CANOPUS_TARGET_ID"
    } else {
        "\"portable-thumbv8m\""
    };
    r#"/* {id} - Canopus module with one UI page: the Manager's "打开界面" opens it. */
#include "canopus_module_entry.h"
#include "canopus_ui.h"

enum { EV_ADD = 1, EV_TOGGLE };

static struct { uint32_t clicks, checked; } s_state;

static uint32_t text_len(const char *text)
{
    uint32_t n = 0u;
    while (text[n] != '\0') n++;
    return n;
}

static void format_u32(char *out, uint32_t value)
{
    char digits[12];
    uint32_t n = 0u, i;
    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    for (i = 0u; i < n; i++) out[i] = digits[n - 1u - i];
    out[n] = '\0';
}

/* Builds the whole page again and commits it; the host redraws only what changed. */
static int32_t render(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui)
{
    struct canopus_ui_tree_v1 *tree = 0;
    struct canopus_ui_navigation_page_props_v1 page = { sizeof(page), "{id}", text_len("{id}") };
    struct canopus_ui_action_row_props_v1 add = {
        sizeof(add), "点击", text_len("点击"), "点击一次加一", text_len("点击一次加一"), EV_ADD, 1u };
    struct canopus_ui_switch_row_props_v1 toggle = {
        sizeof(toggle), "开关", text_len("开关"), "", 0u, EV_TOGGLE, s_state.checked, 1u };
    char count[12];
    struct canopus_ui_status_row_props_v1 status = { sizeof(status), "点击次数", text_len("点击次数"), count, 0u };
    int32_t rc = ctx->ui->tree_begin(ui, &tree);

    if (rc != CANOPUS_UI_OK) return rc;
    format_u32(count, s_state.clicks);
    status.value_len = text_len(count);
    rc = ctx->ui->navigation_page(tree, 1u, &page);
    if (rc == CANOPUS_UI_OK) rc = ctx->ui->status_row(tree, 2u, &status);
    if (rc == CANOPUS_UI_OK) rc = ctx->ui->action_row(tree, 3u, &add);
    if (rc == CANOPUS_UI_OK) rc = ctx->ui->switch_row(tree, 4u, &toggle);
    if (rc == CANOPUS_UI_OK) rc = ctx->ui->end(tree);   /* page */
    if (rc == CANOPUS_UI_OK) return ctx->ui->tree_commit(tree);
    ctx->ui->tree_abort(tree);
    return rc;
}

/* Runs on the UI task: the first tree must be committed before it returns. */
static int32_t on_open(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui,
                       void *user_data)
{
    (void)user_data;
    return render(ctx, ui);
}

static int32_t on_event(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui,
                        uint32_t key, uint32_t event_id, void *user_data)
{
    (void)key;
    (void)user_data;
    if (event_id == EV_ADD) s_state.clicks++;
    else if (event_id == EV_TOGGLE) s_state.checked = !s_state.checked;
    else return CANOPUS_UI_ERR_ARGUMENT;
    return render(ctx, ui);
}

/* No on_back: the title's back closes the page. Return 0 from one to handle it
 * yourself, e.g. to pop a sub-page. */
static const struct canopus_ui_entry_v1 s_entry = {
    sizeof(struct canopus_ui_entry_v1), "{id}", on_open, on_event, 0, 0, 0,
};

static int32_t on_prepare(const struct canopus_context_v1 *ctx) { (void)ctx; return 0; }

static int32_t on_activate(const struct canopus_context_v1 *ctx)
{
    if (ctx->struct_size < sizeof(struct canopus_context_v1) || ctx->ui_ext == 0) return -1;
    return ctx->ui_ext->set_entry(ctx, &s_entry);
}

static int32_t on_deactivate(const struct canopus_context_v1 *ctx)
{
    if (ctx->struct_size >= sizeof(struct canopus_context_v1) && ctx->ui_ext != 0) {
        (void)ctx->ui_ext->set_entry(ctx, 0);
    }
    return 0;
}

static int32_t on_stop(const struct canopus_context_v1 *ctx) { return on_deactivate(ctx); }

static int32_t on_query(struct canopus_status_writer_v1 *writer) { (void)writer; return 0; }

static const struct canopus_module_descriptor_v1 s_descriptor = {
    sizeof(struct canopus_module_descriptor_v1), CANOPUS_ABI_MAJOR, CANOPUS_ABI_MINOR, 0u,
    "{id}", "0.1.0", "", {target_id},
    on_prepare, on_activate, on_deactivate, on_stop, on_query, 0, 0,
};

CANOPUS_MODULE_ENTRY(s_descriptor)
"#
    .replace("{id}", id)
    .replace("{target_id}", target_id)
}

const INSTALLER_MAX_SIZE: usize = 393_216;

fn lua_value(v: &serde_json::Value) -> String {
    use serde_json::Value::*;
    match v {
        Null => "nil".into(),
        Bool(b) => b.to_string(),
        Number(n) => n.to_string(),
        String(s) => serde_json::to_string(s).unwrap(),
        Array(a) => format!(
            "{{{}}}",
            a.iter().map(lua_value).collect::<Vec<_>>().join(",")
        ),
        Object(o) => format!(
            "{{{}}}",
            o.iter()
                .map(|(k, v)| format!("[{}] = {}", serde_json::to_string(k).unwrap(), lua_value(v)))
                .collect::<Vec<_>>()
                .join(",\n")
        ),
    }
}

/// Installer watchface folders, one per device family, next to the package:
/// `<out dir>/installer/<family>/{main.lua, <id>-<target>.bin, <id>-<target>.cmi.bin}`.
fn write_installers(
    project: &Project,
    root: &Path,
    elfs: &[(&TargetPack, PathBuf)],
    receipts: &HashMap<String, Vec<u8>>,
    package_out: &Path,
) -> anyhow::Result<()> {
    let template = root.join("watchfaces/module-installer-prod/src");
    let progress = std::fs::read_to_string(
        root.join("watchfaces/canopus-installer-prod/xiaomi-band-11/src/progress.lua"),
    )?;
    let base = package_out
        .parent()
        .unwrap_or(Path::new("."))
        .join("installer");
    let mut families: std::collections::BTreeMap<String, Vec<(&TargetPack, &PathBuf)>> =
        Default::default();
    for (pack, elf) in elfs {
        let family = pack
            .target_id
            .rsplit_once('-')
            .map(|x| x.0)
            .unwrap_or("")
            .to_string();
        families.entry(family).or_default().push((pack, elf));
    }
    for (family, members) in families {
        let device_file = template.join(format!("device-{family}.lua"));
        if !device_file.is_file() {
            println!("installer: skipped {family} (no installer transport)");
            continue;
        }
        let dir = base.join(&family);
        std::fs::create_dir_all(&dir)?;
        let mut targets = serde_json::Map::new();
        for (pack, elf) in &members {
            let bytes = std::fs::read(elf)?;
            if bytes.len() > INSTALLER_MAX_SIZE {
                bail!("{} exceeds the installer size limit", elf.display());
            }
            let stem = format!("{}-{}", project.id(), pack.target_id);
            std::fs::write(dir.join(format!("{stem}.bin")), &bytes)?;
            std::fs::write(
                dir.join(format!("{stem}.cmi.bin")),
                &receipts[&pack.target_id],
            )?;
            let version = pack.firmware_version.clone();
            targets.insert(
                version,
                serde_json::json!({
                    "id": pack.target_id,
                    "firmware": pack.firmware_sha256,
                    "runtime_pending": false,
                    "build": pack.firmware_build,
                }),
            );
        }
        let config = serde_json::json!({
            "token": project.id(),
            "title": project.id(),
            "max_size": INSTALLER_MAX_SIZE,
            "stem": project.id(),
            "assets": [],
            "targets": targets,
        });
        let mut source = std::fs::read_to_string(template.join("main.lua"))?;
        let replacements = [
            (
                "-- @CONFIG@",
                format!("local CONFIG = {}", lua_value(&config)),
            ),
            (
                "-- @PROGRESS@",
                format!("local progress = (function()\n{progress}\nend)()"),
            ),
            (
                "-- @DEVICE@",
                std::fs::read_to_string(&device_file)?
                    .trim_end_matches('\n')
                    .to_string(),
            ),
        ];
        for (marker, text) in replacements {
            if source.matches(marker).count() != 1 {
                bail!("installer template is missing marker {marker}");
            }
            source = source.replace(marker, &text);
        }
        std::fs::write(
            dir.join("main.lua"),
            format!("-- Generated by `canopus package --installer`.\n{source}"),
        )?;
        println!("wrote installer {}", dir.display());
    }
    Ok(())
}

fn rust_lib_template(id: &str) -> String {
    format!(
        r#"//! {id} - Canopus no_std Rust module. The `canopus build` C shim registers
//! `canopus_module_descriptor` with the Supervisor (sdk/c/canopus_module_entry.h)
//! from the loader's constructor and calls `canopus_mod_stop` from the destructor.
#![no_std]

use canopus_abi::*;
use canopus_runtime::*;

const MAGIC: u32 = 0x4D4F4455; // "MODU"

#[no_mangle]
pub extern "C" fn canopus_mod_prepare(_ctx: *const ContextV1) -> i32 {{
    0
}}

#[no_mangle]
pub extern "C" fn canopus_mod_stop(_ctx: *const ContextV1) -> i32 {{
    0
}}

extern "C" fn activate(_ctx: *const ContextV1) -> i32 {{
    // Fail closed on the wrong firmware before touching anything.
    if canopus_target_generated::canopus_identity_guard() != 0 {{
        return -1;
    }}
    0
}}

extern "C" fn deactivate(_ctx: *const ContextV1) -> i32 {{
    0
}}

extern "C" fn query(w: *mut StatusWriterV1) -> i32 {{
    if w.is_null() {{
        return -1;
    }}
    let w = unsafe {{ &mut *w }};
    unsafe {{
        if !status_put_u32(w, MAGIC) {{
            return -1;
        }}
    }}
    status_writer_publish(w);
    0
}}

const fn pack<const N: usize>(s: &[u8]) -> [u8; N] {{
    let mut out = [0u8; N];
    let mut i = 0;
    while i < s.len() && i < N {{
        out[i] = s[i];
        i += 1;
    }}
    out
}}

#[no_mangle]
pub static canopus_module_descriptor: ModuleDescriptorV1 = ModuleDescriptorV1 {{
    struct_size: core::mem::size_of::<ModuleDescriptorV1>() as u32,
    abi_major: ABI_MAJOR,
    abi_minor: ABI_MINOR,
    flags: 0,
    module_id: pack(b"{id}"),
    module_version: pack(b"0.1.0"),
    build_id: pack(b"{id}-0.1.0"),
    target_id: pack(env!("CANOPUS_TARGET_ID").as_bytes()), // set by `canopus build`
    prepare: Some(canopus_mod_prepare),
    activate: Some(activate),
    deactivate: Some(deactivate),
    stop: Some(canopus_mod_stop),
    query: Some(query),
    publish_native_app: None,
    publish_native_app_stage: None,
}};

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {{
    loop {{}}
}}
"#
    )
}
