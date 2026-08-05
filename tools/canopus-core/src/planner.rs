//! Multi-target build planner (CAN-BLD-001, CAN-MULTI-003).
//!
//! Expands a module manifest across its target matrix, validating each target
//! is registered and satisfies the module's required capabilities. The same
//! logic drives the CLI `build-plan` command and the multi-target matrix test.

use crate::model::{ModuleManifest, TargetPack};

/// One build artifact row in the expanded matrix.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PlanRow {
    pub target_id: String,
    pub target_pack_revision: u32,
    pub firmware_sha256: String,
    pub module_id: String,
    pub module_version: String,
}

/// Expands `module.targets.include` across the registered `targets`.
///
/// Errors on an unregistered target or a missing required capability. The
/// `exclude` list is honored by removing those target ids from the include
/// set (matching the manifest semantics).
pub fn expand(
    module: &ModuleManifest,
    targets: &[TargetPack],
) -> Result<Vec<PlanRow>, String> {
    let include: Vec<&str> = module
        .targets
        .include
        .iter()
        .map(|s| s.as_str())
        .collect();
    let exclude_vec = module.targets.exclude.clone().unwrap_or_default();
    let exclude: Vec<&str> = exclude_vec.iter().map(|s| s.as_str()).collect();

    let mut rows = Vec::new();
    for tid in include {
        if exclude.contains(&tid) {
            continue;
        }
        let pack = targets
            .iter()
            .find(|p| p.target_id == tid)
            .ok_or_else(|| format!("target '{tid}' not registered"))?;
        let req = module
            .capabilities
            .as_ref()
            .map(|c| c.required.clone())
            .unwrap_or_default();
        let have = pack.capabilities.clone().unwrap_or_default();
        let missing: Vec<&String> = req.iter().filter(|c| !have.contains(c)).collect();
        if !missing.is_empty() {
            return Err(format!(
                "target {tid} lacks required capabilities: {missing:?}"
            ));
        }
        rows.push(PlanRow {
            target_id: pack.target_id.clone(),
            target_pack_revision: pack.revision,
            firmware_sha256: pack.firmware_sha256.clone(),
            module_id: module.module.id.clone(),
            module_version: module.module.version.clone(),
        });
    }
    Ok(rows)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::{ModuleCapabilities, ModuleInfo, ModuleTargets};

    fn pack(id: &str, caps: &[&str]) -> TargetPack {
        serde_json::from_value(serde_json::json!({
            "schema": 1,
            "target_id": id,
            "device_family": "test",
            "device_model": "synthetic",
            "os_family": "test",
            "architecture": "arm",
            "cpu": "cortex-m33",
            "instruction_set": "thumb",
            "endianness": "little",
            "float_abi": "soft",
            "loader": "modlib",
            "firmware_sha256": "0000",
            "firmware_version": "1.0.0",
            "firmware_build": "synthetic",
            "module_abi": 1,
            "relocation_profile": "test",
            "revision": 1,
            "capabilities": caps.iter().map(|c| c.to_string()).collect::<Vec<_>>(),
        }))
        .unwrap()
    }

    fn module(include: &[&str], required: &[&str]) -> ModuleManifest {
        ModuleManifest {
            schema: 1,
            module: ModuleInfo {
                id: "org.canopus.test".into(),
                version: "0.1.0".into(),
                language: "c".into(),
                lifecycle: "removable".into(),
                canopus_abi: "1".into(),
                description: None,
                author: None,
            },
            targets: ModuleTargets {
                include: include.iter().map(|s| s.to_string()).collect(),
                exclude: None,
            },
            features: None,
            capabilities: if required.is_empty() {
                None
            } else {
                Some(ModuleCapabilities {
                    required: required.iter().map(|s| s.to_string()).collect(),
                    optional: None,
                })
            },
            native_app: None,
        }
    }

    #[test]
    fn single_target_expands_to_one_row() {
        let m = module(&["tgt-a"], &[]);
        let rows = expand(&m, &[pack("tgt-a", &[])]).unwrap();
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].target_id, "tgt-a");
        assert_eq!(rows[0].module_id, "org.canopus.test");
    }

    #[test]
    fn two_targets_expand_to_two_rows() {
        let m = module(&["tgt-a", "tgt-b"], &[]);
        let rows = expand(&m, &[pack("tgt-a", &[]), pack("tgt-b", &[])]).unwrap();
        assert_eq!(rows.len(), 2);
        assert_eq!(rows[0].target_id, "tgt-a");
        assert_eq!(rows[1].target_id, "tgt-b");
    }

    #[test]
    fn unregistered_target_is_an_error() {
        let m = module(&["tgt-a", "ghost"], &[]);
        assert!(expand(&m, &[pack("tgt-a", &[])]).is_err());
    }

    #[test]
    fn excluded_target_skipped() {
        let m = module(&["tgt-a", "tgt-b"], &[]);
        let mut m = m;
        m.targets.exclude = Some(vec!["tgt-b".into()]);
        let rows = expand(&m, &[pack("tgt-a", &[]), pack("tgt-b", &[])]).unwrap();
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].target_id, "tgt-a");
    }

    #[test]
    fn missing_capability_is_an_error() {
        let m = module(&["tgt-a"], &["clock.gettime"]);
        let rows = expand(&m, &[pack("tgt-a", &["other.cap"])]);
        assert!(rows.is_err());
    }

    #[test]
    fn capability_satisfied_when_present() {
        let m = module(&["tgt-a"], &["clock.gettime"]);
        let rows = expand(&m, &[pack("tgt-a", &["clock.gettime"])]);
        assert!(rows.is_ok());
    }
}
