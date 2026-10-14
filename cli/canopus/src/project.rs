//! Module projects: `canopus new`, `canopus build`, `canopus package`
//! (refactor-plan P3.1–P3.3). A project is a `Canopus.toml` plus C sources.
//! Portable modules (the default) are built once; target modules once per
//! exact target. Packages carry an unsigned CMI2 receipt per target.

use anyhow::{Context, bail};
use canopus_core::model::{ModuleManifest, PackageArtifact, PackageManifest, TargetPack};
use canopus_core::registry::list_target_packs;
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::Command;

const PORTABLE_DIR: &str = "abi-1-thumbv8m";

/// The Canopus checkout providing `sdk/c` and `targets/`: `$CANOPUS_ROOT`,
/// else the nearest ancestor of the working directory or the executable.
pub fn canopus_root() -> anyhow::Result<PathBuf> {
    if let Some(root) = std::env::var_os("CANOPUS_ROOT") {
        return Ok(PathBuf::from(root));
    }
    let mut starts: Vec<PathBuf> = Vec::new();
    if let Ok(cwd) = std::env::current_dir() {
        starts.push(cwd);
    }
    if let Ok(exe) = std::env::current_exe() {
        starts.push(exe);
    }
    for start in starts {
        for dir in start.ancestors() {
            if dir.join("sdk/c/canopus_abi.h").is_file() {
                return Ok(dir.to_path_buf());
            }
        }
    }
    bail!("cannot find the Canopus checkout (sdk/c/canopus_abi.h); set CANOPUS_ROOT")
}

struct Project {
    dir: PathBuf,
    manifest: ModuleManifest,
}

impl Project {
    fn load(manifest_path: &Path) -> anyhow::Result<Self> {
        let text = std::fs::read_to_string(manifest_path)
            .with_context(|| format!("cannot read {}", manifest_path.display()))?;
        let value = serde_json::to_value(toml::from_str::<toml::Value>(&text)?)?;
        canopus_core::schema::validate(canopus_core::schema::SchemaKind::Module, &value)?;
        let manifest: ModuleManifest = serde_json::from_value(value)?;
        if manifest.module.language != "c" {
            bail!("`canopus build` supports language = \"c\" only");
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

fn tool(var: &str, default: &str) -> String {
    std::env::var(var).unwrap_or_else(|_| default.to_string())
}

fn run(cmd: &mut Command) -> anyhow::Result<()> {
    let status = cmd.status().with_context(|| format!("cannot run {cmd:?}"))?;
    if !status.success() {
        bail!("{cmd:?} failed with {status}");
    }
    Ok(())
}

/// Compiles the sources and links one relocatable ELF.
fn compile(project: &Project, root: &Path, target: Option<&TargetPack>) -> anyhow::Result<PathBuf> {
    let out = project.elf_path(target.map(|t| t.target_id.as_str()));
    let obj_dir = out.parent().unwrap().join(format!("{}.obj", project.id()));
    std::fs::create_dir_all(&obj_dir)?;
    let build = &project.manifest.build;
    let mut objects = Vec::new();
    for (i, source) in build.sources.iter().enumerate() {
        let src = PathBuf::from(expand(source, root));
        let src = if src.is_absolute() { src } else { project.dir.join(src) };
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
                root.join("targets").join(&t.target_id).join("generated").display()
            ));
        }
        for dir in build.include_dirs.iter().flatten() {
            let dir = PathBuf::from(expand(dir, root));
            cc.arg(format!(
                "-I{}",
                if dir.is_absolute() { dir } else { project.dir.join(dir) }.display()
            ));
        }
        for define in build.defines.iter().flatten() {
            cc.arg(format!("-D{define}"));
        }
        cc.arg("-c").arg(&src).arg("-o").arg(&obj);
        run(&mut cc)?;
        objects.push(obj);
    }
    run(Command::new(tool("LD", "ld.lld")).arg("-r").arg("-o").arg(&out).args(&objects))?;
    Ok(out)
}

fn selected_targets(
    project: &Project,
    root: &Path,
    only: Option<&str>,
) -> anyhow::Result<Vec<TargetPack>> {
    let active: Vec<TargetPack> = list_target_packs(&root.join("targets"))?
        .into_iter()
        .filter(|p| p.status.as_deref() != Some("frozen"))
        .collect();
    let rows = canopus_core::planner::expand(&project.manifest, &active)
        .map_err(|e| anyhow::anyhow!("{e}"))?;
    let mut packs: Vec<TargetPack> = rows
        .iter()
        .filter_map(|r| active.iter().find(|p| p.target_id == r.target_id).cloned())
        .collect();
    if let Some(only) = only {
        packs.retain(|p| p.target_id == only);
        if packs.is_empty() {
            bail!("target '{only}' is not part of this module's plan");
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
    } else {
        for pack in selected_targets(&project, &root, target)? {
            let elf = compile(&project, &root, Some(&pack))?;
            println!("built {}", elf.display());
            crate::commands::verify(&elf, Some(&pack.target_id), false, &targets_dir, false)?;
        }
    }
    Ok(())
}

pub fn package(
    manifest: &Path,
    output: Option<&Path>,
    rpk: bool,
    key_dir: Option<&Path>,
    installer: bool,
) -> anyhow::Result<()> {
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
        let elf = project.elf_path(if portable { None } else { Some(&pack.target_id) });
        let bytes = std::fs::read(&elf)
            .with_context(|| format!("{} is missing; run `canopus build` first", elf.display()))?;
        let receipt = canopus_core::receipt::build(
            project.id(),
            m.module.version_code,
            lifecycle,
            &pack.target_id,
            &pack.firmware_sha256,
            &bytes,
        )?;
        let dir = if portable { PORTABLE_DIR } else { pack.target_id.as_str() };
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
        target_pack_revision: packs.iter().map(|p| u64::from(p.revision)).max().unwrap_or(1),
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
        None => project
            .dir
            .join("dist")
            .join(format!("{}-{}.canopus", project.id(), m.module.version)),
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
            .map(|p| (p, project.elf_path(if portable { None } else { Some(&p.target_id) })))
            .collect();
        write_installers(&project, &root, &elfs, &receipts, &out)?;
    }
    if rpk {
        // Quick-app container for the phone's stock install path: every
        // target's receipt and the matching ELF(s) under canopus/.
        let mut payload: Vec<(String, Vec<u8>)> = Vec::new();
        for pack in &packs {
            payload.push((
                format!("canopus/{}.cmi", pack.target_id),
                receipts[&pack.target_id].clone(),
            ));
            let elf = project.elf_path(if portable { None } else { Some(&pack.target_id) });
            let name = if portable {
                "canopus/portable.ko".to_string()
            } else {
                format!("canopus/{}.ko", pack.target_id)
            };
            if !payload.iter().any(|(n, _)| *n == name) {
                payload.push((name, std::fs::read(&elf)?));
            }
        }
        let (key_pem, cert_der) = rpk_key(key_dir)?;
        let rpk_bytes = canopus_package::rpk::build_rpk(
            &canopus_package::rpk::RpkInput {
                module_id: project.id(),
                version_name: &m.module.version,
                version_code: m.module.version_code,
                files: &payload,
            },
            &key_pem,
            &cert_der,
        )?;
        let rpk_out = out.with_extension("rpk");
        std::fs::write(&rpk_out, &rpk_bytes)?;
        println!(
            "wrote {} ({} bytes, package {})",
            rpk_out.display(),
            rpk_bytes.len(),
            canopus_package::rpk::package_name(project.id())
        );
    }
    Ok(())
}

/// Publisher signing key for .rpk files: `<dir>/key.pem` + `cert.der`
/// (default `~/.canopus/rpk`), created once with the openssl CLI. The app
/// fingerprint derives from the certificate, so keep the same key per publisher.
fn rpk_key(dir: Option<&Path>) -> anyhow::Result<(String, Vec<u8>)> {
    let dir = match dir {
        Some(d) => d.to_path_buf(),
        None => PathBuf::from(std::env::var_os("HOME").context("HOME is not set")?)
            .join(".canopus/rpk"),
    };
    let (key, cert) = (dir.join("key.pem"), dir.join("cert.der"));
    if !key.is_file() || !cert.is_file() {
        std::fs::create_dir_all(&dir)?;
        let raw = dir.join("key.raw.pem");
        run(Command::new("openssl").args(["genrsa", "-out"]).arg(&raw).arg("2048"))?;
        run(Command::new("openssl")
            .args(["pkcs8", "-topk8", "-nocrypt", "-in"])
            .arg(&raw)
            .arg("-out")
            .arg(&key))?;
        run(Command::new("openssl")
            .args(["req", "-new", "-x509", "-days", "36500", "-subj", "/CN=Canopus publisher", "-key"])
            .arg(&key)
            .args(["-outform", "DER", "-out"])
            .arg(&cert))?;
        let _ = std::fs::remove_file(raw);
        println!("created rpk signing key in {}", dir.display());
    }
    Ok((std::fs::read_to_string(key)?, std::fs::read(cert)?))
}

pub fn new(id: &str, dir: &Path, targets: &[String]) -> anyhow::Result<()> {
    if !canopus_core::receipt::module_id_valid(id) {
        bail!("module id '{id}' must match ^[a-z][a-z0-9_]{{0,15}}$ (ADR-CAN-003)");
    }
    let project = dir.join(id);
    if project.exists() {
        bail!("{} already exists", project.display());
    }
    std::fs::create_dir_all(project.join("src"))?;
    let kind = if targets.is_empty() { "portable" } else { "target" };
    let target_section = if targets.is_empty() {
        String::new()
    } else {
        format!(
            "\n[targets]\ninclude = [{}]\n",
            targets.iter().map(|t| format!("\"{t}\"")).collect::<Vec<_>>().join(", ")
        )
    };
    std::fs::write(
        project.join("Canopus.toml"),
        format!(
            "schema = 1\n\n[module]\nid = \"{id}\"\nversion = \"0.1.0\"\nversion_code = 1\nkind = \"{kind}\"\nlanguage = \"c\"\nlifecycle = \"removable\"\ncanopus_abi = \"1\"\n\n[build]\nsources = [\"src/{id}.c\"]\n{target_section}"
        ),
    )?;
    std::fs::write(project.join("src").join(format!("{id}.c")), source_template(id, kind))?;
    println!("created {} ({kind} module)", project.display());
    println!("next: cd {} && canopus build && canopus package", project.display());
    Ok(())
}

fn source_template(id: &str, kind: &str) -> String {
    if kind == "target" {
        format!(
            "/* {id} — target module. Include the exact target's generated veneer. */\n#include \"canopus_abi.h\"\n#include \"canopus_veneer.h\"\n\n/* TODO: descriptor, lifecycle callbacks and a constructor that checks\n * canopus_identity_guard() before touching firmware state. */\n"
        )
    } else {
        format!(
            "/* {id} — portable module: uses only the Supervisor service table. */\n#include \"canopus_services.h\"\n\nstatic const struct canopus_context_v1 *s_ctx;\n\nstatic int32_t on_prepare(const struct canopus_context_v1 *ctx) {{ s_ctx = ctx; return 0; }}\n\nstatic int32_t on_activate(const struct canopus_context_v1 *ctx)\n{{\n    if (canopus_services_has(ctx, CANOPUS_SVC_LOG, 1)) {{\n        ctx->services->log_write(ctx, CANOPUS_LOG_INFO, \"{id} active\");\n    }}\n    return 0;\n}}\n\nstatic int32_t on_deactivate(const struct canopus_context_v1 *ctx) {{ (void)ctx; return 0; }}\n\nstatic int32_t on_stop(const struct canopus_context_v1 *ctx) {{ (void)ctx; s_ctx = 0; return 0; }}\n\nstatic int32_t on_query(struct canopus_status_writer_v1 *writer) {{ (void)writer; return 0; }}\n\nstatic const struct canopus_module_descriptor_v1 s_descriptor = {{\n    sizeof(struct canopus_module_descriptor_v1), CANOPUS_ABI_MAJOR, CANOPUS_ABI_MINOR, 0u,\n    \"{id}\", \"0.1.0\", \"\", \"portable-thumbv8m\",\n    on_prepare, on_activate, on_deactivate, on_stop, on_query, 0, 0,\n}};\n\n__attribute__((constructor)) static void init(const struct canopus_context_v1 *ctx)\n{{\n    if (!canopus_services_has(ctx, CANOPUS_SVC_MODULE, 1)) return;\n    s_ctx = ctx;\n    (void)ctx->services->module_register(ctx, &s_descriptor);\n}}\n"
        )
    }
}

const INSTALLER_MAX_SIZE: usize = 393_216;

fn lua_value(v: &serde_json::Value) -> String {
    use serde_json::Value::*;
    match v {
        Null => "nil".into(),
        Bool(b) => b.to_string(),
        Number(n) => n.to_string(),
        String(s) => serde_json::to_string(s).unwrap(),
        Array(a) => format!("{{{}}}", a.iter().map(lua_value).collect::<Vec<_>>().join(",")),
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
    let base = package_out.parent().unwrap_or(Path::new(".")).join("installer");
    let mut families: std::collections::BTreeMap<String, Vec<(&TargetPack, &PathBuf)>> =
        Default::default();
    for (pack, elf) in elfs {
        let family = pack.target_id.rsplit_once('-').map(|x| x.0).unwrap_or("").to_string();
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
            std::fs::write(dir.join(format!("{stem}.cmi.bin")), &receipts[&pack.target_id])?;
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
            ("-- @CONFIG@", format!("local CONFIG = {}", lua_value(&config))),
            (
                "-- @PROGRESS@",
                format!("local progress = (function()\n{progress}\nend)()"),
            ),
            ("-- @DEVICE@", std::fs::read_to_string(&device_file)?.trim_end_matches('\n').to_string()),
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
