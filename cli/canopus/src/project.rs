//! Module projects: `canopus new`, `canopus build`, `canopus package`
//! (refactor-plan P3.1–P3.3). A project is a `Canopus.toml` plus C or Rust sources.
//! Portable modules (the default) are built once and packaged for every target
//! whose loader can host them; target modules are built once per exact target. Packages carry an unsigned CMI2 receipt per target.

use crate::env::{canopus_root, display_rel, newest_input, read_text, resolve_in, run, tool};
use anyhow::{Context, bail};
use canopus_core::model::{ModuleManifest, PackageArtifact, PackageManifest, TargetPack};
use canopus_core::registry::list_target_packs;
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::Command;

const PORTABLE_DIR: &str = "abi-2-thumbv8m";

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
/// The descriptor target id of a portable module.
const PORTABLE_TARGET_ID: &str = "portable-thumbv8m";

/// `-Werror` only when asked (`CANOPUS_WERROR=1`, as scripts/ci.sh does): a
/// newer clang than the SDK was tested with must not fail a module over a
/// warning it just learned.
fn warning_flags() -> &'static [&'static str] {
    if std::env::var_os("CANOPUS_WERROR").is_some_and(|v| v == "1") {
        &["-Wall", "-Wextra", "-Werror"]
    } else {
        &["-Wall", "-Wextra"]
    }
}

/// Rust modules: `[build] sources = ["Cargo.toml"]` names the crate (a
/// `staticlib`, `#![no_std]`, panic=abort). Pipeline: cargo build for the
/// device triple (target modules: with the exact target's bindings feature) ->
/// C ctor shim -> `ld.lld -r --gc-sections` -> zero-import ELF.
fn compile_rust(
    project: &Project,
    root: &Path,
    target: Option<&TargetPack>,
) -> anyhow::Result<PathBuf> {
    let out = project.elf_path(target.map(|t| t.target_id.as_str()));
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
    let cargo_toml: toml::Value = toml::from_str(&std::fs::read_to_string(&manifest)?)
        .with_context(|| format!("{} is not valid TOML", manifest.display()))?;
    let declares =
        |table: &str, key: &str| cargo_toml.get(table).and_then(|t| t.get(key)).is_some();
    let mut feat_list = vec!["device".to_string()];
    if let Some(target) = target {
        let feature = format!("target-{}", target.target_id.replace('.', "-"));
        for dep in ["canopus-target-generated", "canopus-target-private"] {
            if declares("dependencies", dep) {
                feat_list.push(format!("{dep}/{feature}"));
            }
        }
        if declares("features", &feature) {
            feat_list.push(feature);
        }
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
            .env(
                "CANOPUS_TARGET_ID",
                target.map_or(PORTABLE_TARGET_ID, |t| t.target_id.as_str()),
            )
            // Same policy as the workspace's `device` profile.
            .env("CC_thumbv8m_main_none_eabi", tool("CC", "clang"))
            .env("AR_thumbv8m_main_none_eabi", tool("AR", "llvm-ar"))
            .env(
                "CFLAGS_thumbv8m_main_none_eabi",
                "--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft",
            )
            .env(
                "RUSTFLAGS",
                match std::env::var("RUSTFLAGS") {
                    Ok(f) if !f.is_empty() => format!("{f} -C target-cpu=cortex-m33"),
                    _ => "-C target-cpu=cortex-m33".to_string(),
                },
            )
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
    let mut shim = module_cc(root, target, &[], &None);
    shim.arg("-c").arg(&shim_c).arg("-o").arg(&shim_o);
    run(&mut shim, "compiling the constructor shim")?;
    let build_dir = release.join("build");
    let mut extra_archives = Vec::new();
    if build_dir.is_dir() {
        for entry in std::fs::read_dir(&build_dir)?.flatten() {
            let out_dir = entry.path().join("out");
            if out_dir.is_dir() {
                for f in std::fs::read_dir(&out_dir)?.flatten() {
                    if f.path().extension().is_some_and(|x| x == "a") {
                        extra_archives.push(f.path());
                    }
                }
            }
        }
    }
    let mut ld_cmd = Command::new(tool("LD", "ld.lld"));
    // A staticlib includes Rust runtime archive members with many independent
    // sections. Relocatable linking otherwise retains all their unused tables
    // and functions, increasing resident memory and making random table words
    // look like firmware addresses to the verifier. Keep the module ABI roots;
    // their relocations retain every reachable callback, while LLD also retains
    // .init_array/.fini_array and the functions referenced by those arrays.
    ld_cmd
        .args([
            "-r",
            "--gc-sections",
            "-u",
            "canopus_module_descriptor",
            "-u",
            "canopus_mod_stop",
            "-T",
        ])
        .arg(root.join("sdk/rust/c_shim/module.ld"))
        .arg(&shim_o)
        .arg(archive);
    for a in &extra_archives {
        ld_cmd.arg(a);
    }
    ld_cmd.arg("-o").arg(&out);
    run(&mut ld_cmd, "linking the module")?;
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

/// Shared `clang` invocation for one module translation unit.
fn module_cc(
    root: &Path,
    target: Option<&TargetPack>,
    include_dirs: &[PathBuf],
    defines: &Option<Vec<String>>,
) -> Command {
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
    ]);
    cc.args(warning_flags());
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
    for dir in include_dirs {
        cc.arg(format!("-I{}", dir.display()));
    }
    for define in defines.iter().flatten() {
        cc.arg(format!("-D{define}"));
    }
    cc
}

/// Compiles the sources and links one relocatable ELF.
fn compile(project: &Project, root: &Path, target: Option<&TargetPack>) -> anyhow::Result<PathBuf> {
    if project.manifest.module.language == "rust" {
        return compile_rust(project, root, target);
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
        let mut cc = module_cc(root, target, &include_dirs, &build.defines);
        cc.arg("-c").arg(&src).arg("-o").arg(&obj);
        run(
            &mut cc,
            &format!("compiling {}", display_rel(&src, &project.dir)),
        )?;
        objects.push(obj);
    }
    // Aggregate initialization and struct assignment lower to __aeabi_mem*
    // calls; without this definition the zero-import verifier rejects the ELF.
    {
        let src = root.join("sdk/c/canopus_aeabi_mem.c");
        let obj = obj_dir.join("canopus_aeabi_mem.o");
        let mut cc = module_cc(root, target, &include_dirs, &build.defines);
        cc.arg("-c").arg(&src).arg("-o").arg(&obj);
        run(&mut cc, "compiling sdk/c/canopus_aeabi_mem.c")?;
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
    let extra_allowed: Vec<u64> = project
        .manifest
        .build
        .allowed_constants
        .as_deref()
        .unwrap_or(&[])
        .iter()
        .filter_map(|c| {
            let s = c.trim().to_ascii_lowercase();
            u64::from_str_radix(s.strip_prefix("0x").unwrap_or(&s), 16).ok()
        })
        .collect();
    if project.manifest.is_portable() {
        if target.is_some() {
            bail!("a portable module is built once; omit --target");
        }
        let elf = compile(&project, &root, None)?;
        println!("built {}", elf.display());
        crate::commands::verify(&elf, None, true, &targets_dir, false, &extra_allowed, None)?;
        println!("ok: {} (portable) built and verified", project.id());
    } else {
        let packs = selected_targets(&project, &root, target)?;
        for pack in &packs {
            let elf = compile(&project, &root, Some(pack))?;
            println!("built {}", elf.display());
            crate::commands::verify(
                &elf,
                Some(&pack.target_id),
                false,
                &targets_dir,
                false,
                &extra_allowed,
                None,
            )?;
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
    let mut package_resources = Vec::new();
    let mut resource_files: HashMap<String, PathBuf> = HashMap::new();
    if let Some(res) = &m.resources {
        if let Some(files) = &res.files {
            for rel in files {
                let full = project.dir.join(rel);
                let bytes = std::fs::read(&full)
                    .with_context(|| format!("cannot read resource {}", full.display()))?;
                let filename = Path::new(rel).file_name().unwrap().to_str().unwrap();
                let archive_path = format!("resources/common/{filename}");
                package_resources.push(canopus_core::model::PackageResource {
                    path: archive_path.clone(),
                    sha256: canopus_package::hex_sha256(&bytes),
                    media_type: "application/octet-stream".into(),
                    target_backend: None,
                });
                resource_files.insert(archive_path, full);
            }
        }
        if let Some(target_map) = &res.targets {
            for (target_id, files) in target_map {
                for rel in files {
                    let full = project.dir.join(rel);
                    let bytes = std::fs::read(&full)
                        .with_context(|| format!("cannot read resource {}", full.display()))?;
                    let filename = Path::new(rel).file_name().unwrap().to_str().unwrap();
                    let archive_path = format!("resources/{target_id}/{filename}");
                    package_resources.push(canopus_core::model::PackageResource {
                        path: archive_path.clone(),
                        sha256: canopus_package::hex_sha256(&bytes),
                        media_type: "application/octet-stream".into(),
                        target_backend: Some(target_id.clone()),
                    });
                    resource_files.insert(archive_path, full);
                }
            }
        }
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
        resources: if package_resources.is_empty() {
            None
        } else {
            Some(package_resources)
        },
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
        reproducibility: Some(canopus_core::model::Reproducibility {
            build_environment: Some(format!(
                "canopus {}, sdk {}",
                env!("CARGO_PKG_VERSION"),
                crate::sdk::commit_of(&root).unwrap_or_else(|| "unknown".into())
            )),
            toolchain: None,
            clean_rebuild_identical: None,
        }),
    };
    let archive = canopus_package::build_archive_with_receipts(
        &package_manifest,
        &files,
        &resource_files,
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
    pub template: &'a str,
}

/// A `templates/<name>/` directory of the SDK (templates/README.md).
struct Template {
    name: String,
    dir: PathBuf,
    meta: TemplateMeta,
}

#[derive(serde::Deserialize)]
struct TemplateMeta {
    description: String,
    language: String,
    sources: Vec<String>,
}

fn templates(root: &Path) -> anyhow::Result<Vec<Template>> {
    let dir = root.join("templates");
    let mut out = Vec::new();
    for entry in std::fs::read_dir(&dir)
        .with_context(|| format!("cannot read {} (run `canopus sdk update`)", dir.display()))?
    {
        let path = entry?.path();
        let manifest = path.join("template.toml");
        if !manifest.is_file() {
            continue;
        }
        let meta: TemplateMeta = toml::from_str(&std::fs::read_to_string(&manifest)?)
            .with_context(|| format!("{} is not a valid template", manifest.display()))?;
        out.push(Template {
            name: path.file_name().unwrap().to_string_lossy().into_owned(),
            dir: path,
            meta,
        });
    }
    out.sort_by(|a, b| a.name.cmp(&b.name));
    Ok(out)
}

/// `canopus new --list`.
pub fn list_templates() -> anyhow::Result<()> {
    for t in templates(&canopus_root()?)? {
        println!("{:<10} {}", t.name, t.meta.description);
    }
    Ok(())
}

/// Creates a module project. Everything is validated first, so a bad id,
/// template or target leaves nothing behind; a failure while writing removes
/// the half-made directory.
pub fn new(opts: &NewOptions) -> anyhow::Result<()> {
    let NewOptions {
        id,
        dir,
        targets,
        template,
    } = *opts;
    if !canopus_core::receipt::module_id_valid(id) {
        bail!("module id '{id}' must match ^[a-z][a-z0-9_]{{0,15}}$ (ADR-CAN-003)");
    }
    let root = canopus_root()?;
    let all = templates(&root)?;
    let Some(template) = all.iter().find(|t| t.name == template) else {
        bail!(
            "unknown template '{template}'; available: {}",
            all.iter()
                .map(|t| t.name.as_str())
                .collect::<Vec<_>>()
                .join(", ")
        );
    };
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

    let result = write_project(id, &project, &root, &resolved, template);
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
        "created {} ({kind} module, template {})",
        project.display(),
        template.name
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
            "        (portable: built once for every Band 11 firmware; Band 10 Pro needs --target <id> to bind exact firmware)"
        );
    }
    Ok(())
}

fn write_project(
    id: &str,
    project: &Path,
    root: &Path,
    targets: &[String],
    template: &Template,
) -> anyhow::Result<()> {
    let kind = if targets.is_empty() {
        "portable"
    } else {
        "target"
    };
    let descriptor_target = if targets.is_empty() {
        format!("\"{PORTABLE_TARGET_ID}\"")
    } else {
        "CANOPUS_TARGET_ID".to_string()
    };
    let sdk = sdk_path(project, root)?;
    let fill = |text: &str| {
        text.replace("{{id}}", id)
            .replace("{{descriptor_target}}", &descriptor_target)
            .replace("{{sdk}}", &sdk)
    };
    let mut stack = vec![template.dir.clone()];
    while let Some(dir) = stack.pop() {
        for entry in std::fs::read_dir(&dir)? {
            let path = entry?.path();
            let rel = path.strip_prefix(&template.dir)?;
            if path.is_dir() {
                stack.push(path);
            } else if rel != Path::new("template.toml") {
                let out = project.join(fill(&rel.to_string_lossy()));
                std::fs::create_dir_all(out.parent().unwrap())?;
                std::fs::write(&out, fill(&std::fs::read_to_string(&path)?))?;
            }
        }
    }
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
    let sources = template
        .meta
        .sources
        .iter()
        .map(|s| format!("\"{}\"", fill(s)))
        .collect::<Vec<_>>()
        .join(", ");
    std::fs::write(
        project.join("Canopus.toml"),
        format!(
            "schema = 1\n\n[module]\nid = \"{id}\"\nversion = \"0.1.0\"\nversion_code = 1\nkind = \"{kind}\"\nlanguage = \"{}\"\nlifecycle = \"removable\"\ncanopus_abi = \"2\"\n\n[build]\nsources = [{sources}]\n{target_section}",
            template.meta.language
        ),
    )?;
    std::fs::write(project.join(".gitignore"), "build/\ndist/\nCargo.lock\n")?;
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
        // The Band 9 Pro (3.1.190) runs the same staged loader and
        // /dev/canopus transport as the Band 9 (3.1.32); only the version
        // source differs, and the shared hook covers both.
        let hook_family = match family.as_str() {
            "xiaomi-band-9-pro" => "xiaomi-band-9",
            other => other,
        };
        let device_file = template.join(format!("device-{hook_family}.lua"));
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
