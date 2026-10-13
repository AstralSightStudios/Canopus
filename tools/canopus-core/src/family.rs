//! Family layer: build and generation data shared by the targets of one
//! device family (`targets/_families/<family>.toml`).

use crate::error::{Error, Result};
use serde::{Deserialize, Serialize};
use std::path::Path;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Family {
    /// Manager UI backend source, relative to the repository root.
    pub manager_backend: String,
    /// Upper bound of the Supervisor ELF in bytes.
    pub supervisor_max_size: u32,
    #[serde(default)]
    pub loader_sources: Vec<String>,
    #[serde(default)]
    pub defines: Vec<String>,
    /// Directory under `watchfaces/canopus-installer-prod/`.
    pub prod_family: String,
    /// Emit `generated_symbols_<rust_module>.rs` for the SDK facade.
    pub private_symbols: bool,
    /// Emit `canopus_band11_addresses.h` from `loader/native-bindings.json`.
    pub native_config: bool,
}

pub fn load_family(targets_dir: &Path, name: &str) -> Result<Family> {
    let path = targets_dir.join("_families").join(format!("{name}.toml"));
    let text = std::fs::read_to_string(&path)
        .map_err(|e| Error::other(format!("cannot read {}: {e}", path.display())))?;
    Ok(toml::from_str(&text)?)
}
