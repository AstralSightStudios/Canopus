//! Typed view of the Canopus data model.
//!
//! These structs are the registry's working representation. Canonical
//! validation is done by JSON Schema (see `crate::schema`); the typed
//! structs exist for the operations the CLI performs (matching, identity,
//! policy checks). Fields that only appear in some families are `Option`.

use serde::{Deserialize, Serialize};

// ---------------------------------------------------------------- target

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TargetPack {
    #[serde(rename = "schema")]
    pub schema: u32,
    pub target_id: String,
    pub device_family: String,
    pub device_model: String,
    pub board_revision: Option<String>,
    pub os_family: String,
    pub architecture: String,
    pub cpu: String,
    pub instruction_set: String,
    pub endianness: String,
    pub float_abi: String,
    pub loader: String,
    pub firmware_sha256: String,
    pub firmware_version: String,
    pub firmware_build: String,
    pub module_abi: u32,
    pub relocation_profile: String,
    pub revision: u32,
    pub loader_profile: Option<LoaderProfile>,
    pub capabilities: Option<Vec<String>>,
    pub provenance: Option<TargetProvenance>,
    /* CAN-P1-011: the firmware's absolute-address space(s). The verifier
     * scans module instructions and data for 32-bit values inside these
     * ranges; every hit must be in the allowed-address allowlist. */
    pub firmware_address_ranges: Option<Vec<AddressRange>>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AddressRange {
    pub base: u64,
    pub size: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoaderProfile {
    pub elf_class: u32,
    pub elf_machine: String,
    pub elf_type: String,
    pub endianness: Option<String>,
    pub constructor_discovery: String,
    pub symbol_imports: String,
    pub max_size: u64,
    pub alignment: u64,
    pub relocations: Option<Vec<RelocationRule>>,
    pub module_name_rules: Option<String>,
    pub load_command: Option<String>,
    pub unload_command: Option<String>,
    pub persistent_autoload: Option<bool>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RelocationRule {
    pub name: String,
    pub arm_type: u32,
    pub notes: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TargetProvenance {
    pub source_repo: Option<String>,
    pub source_findings: Option<Vec<String>>,
    pub import_date: Option<String>,
    pub maintainer: Option<String>,
    pub reviewers: Option<Vec<String>>,
}

// ---------------------------------------------------------------- symbol

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Symbol {
    #[serde(rename = "schema")]
    pub schema: u32,
    pub symbol_id: String,
    pub target_id: String,
    pub name: String,
    pub kind: String,
    pub entry_address: Option<String>,
    pub callable_address: Option<String>,
    pub instruction_set: String,
    pub prototype_id: Option<String>,
    pub prototype: Option<String>,
    pub calling_convention: Option<String>,
    pub contexts: Option<SymbolContexts>,
    pub ownership: Option<Ownership>,
    pub side_effects: Option<Vec<String>>,
    pub proof: SymbolProof,
    pub policy: String,
    pub status: String,
    /* CAN-P1-012: explicit codegen approval. A callable is only generated
     * when approval_state == "APPROVED" AND proof.evidence_ids is non-empty;
     * absent/PENDING never promotes a symbol to callable. */
    pub approval_state: Option<String>,
    pub promotion: Option<SymbolPromotion>,
    pub provenance: SymbolProvenance,
    pub notes: Option<String>,
}

impl Symbol {
    /// CAN-P1-012: whether this symbol may be emitted as a callable binding.
    /// Restricted/forbidden policy and status are already excluded upstream;
    /// this is the promotion gate: explicit APPROVED plus at least one
    /// evidence id (a promotion record with no evidence never goes live).
    pub fn approved_for_codegen(&self) -> bool {
        self.approval_state.as_deref() == Some("APPROVED")
            && self
                .proof
                .evidence_ids
                .as_ref()
                .is_some_and(|e| !e.is_empty())
    }
}

/// Promotion record attached to an APPROVED symbol (CAN-P1-012). Required for
/// every callable emitted by codegen; documents who/why/when it was approved.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SymbolPromotion {
    pub reviewer: String,
    pub date: String,
    pub firmware_sha256: String,
    pub prototype: String,
    pub ownership: String,
    pub thread: String,
    pub device_probe: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SymbolContexts {
    pub allowed: Option<Vec<String>>,
    pub blocking: Option<bool>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Ownership {
    pub argument: Option<String>,
    pub callback_argument: Option<String>,
    pub return_value: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SymbolProof {
    #[serde(rename = "static")]
    pub static_level: String,
    pub device: Option<String>,
    pub host_tested: Option<bool>,
    pub evidence_ids: Option<Vec<String>>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SymbolProvenance {
    pub firmware_sha256: String,
    pub evidence_ids: Option<Vec<String>>,
    pub source: Option<String>,
    pub withdrawal_reason: Option<String>,
}

// ---------------------------------------------------------------- type

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TypeRecord {
    #[serde(rename = "schema")]
    pub schema: u32,
    pub type_id: String,
    pub target_id: String,
    pub kind: String,
    pub name: Option<String>,
    pub size: u64,
    pub alignment: u64,
    pub fields: Option<Vec<TypeField>>,
    pub layout_diffs: Option<Vec<LayoutDiff>>,
    pub provenance: TypeProvenance,
    /// For `kind == "typedef"` function pointers: the C signature without the
    /// name (e.g. `void(void *, int32_t, const X *, const char *)`). Emits as
    /// `typedef <ret> (*<name>)(<args>);` in the veneer.
    pub prototype: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TypeField {
    pub name: String,
    pub offset: u64,
    pub width: u64,
    pub signedness: String,
    pub pointer_target: Option<String>,
    pub array_length: Option<u64>,
    pub bitfield: Option<Bitfield>,
    pub ownership: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Bitfield {
    pub lsb: Option<u64>,
    pub width: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LayoutDiff {
    pub firmware_sha256: Option<String>,
    pub diff: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TypeProvenance {
    pub firmware_sha256: String,
    pub evidence_ids: Option<Vec<String>>,
    pub source: Option<String>,
}

// ---------------------------------------------------------------- evidence

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EvidenceBundle {
    #[serde(rename = "schema")]
    pub schema: u32,
    pub evidence_id: String,
    pub target_id: String,
    pub question: String,
    pub candidate_symbols: Option<Vec<String>>,
    pub prototype_hypothesis: Option<String>,
    pub callsite_evidence: Option<Vec<String>>,
    pub control_flow_evidence: Option<Vec<String>>,
    pub ownership_analysis: Option<String>,
    pub unsafe_assumptions: Option<Vec<String>>,
    pub recommended_probe: Option<String>,
    pub reviewers: Option<Vec<String>>,
    pub verdict: String,
    pub created: String,
    pub artifacts: Option<Vec<EvidenceArtifact>>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EvidenceArtifact {
    pub uri: Option<String>,
    pub sha256: Option<String>,
    pub role: Option<String>,
}

// ---------------------------------------------------------------- module

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ModuleManifest {
    #[serde(rename = "schema")]
    pub schema: u32,
    pub module: ModuleInfo,
    pub targets: ModuleTargets,
    pub features: Option<Vec<String>>,
    pub capabilities: Option<ModuleCapabilities>,
    pub native_app: Option<NativeApp>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ModuleInfo {
    pub id: String,
    pub version: String,
    pub language: String,
    pub lifecycle: String,
    pub canopus_abi: String,
    pub description: Option<String>,
    pub author: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ModuleTargets {
    pub include: Vec<String>,
    pub exclude: Option<Vec<String>>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ModuleCapabilities {
    pub required: Vec<String>,
    pub optional: Option<Vec<String>>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NativeApp {
    pub enabled: bool,
    pub app_id: String,
    pub name: String,
    pub icon: Option<String>,
    pub entry: Option<String>,
    pub role: Option<String>,
}

// ---------------------------------------------------------------- package

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PackageManifest {
    #[serde(rename = "schema")]
    pub schema: u32,
    pub package_id: String,
    pub module_id: String,
    pub version: String,
    pub build_generation: u64,
    pub canopus_abi: String,
    pub lifecycle: String,
    pub artifacts: Vec<PackageArtifact>,
    pub capabilities: Option<ModuleCapabilities>,
    pub native_app: Option<PackageNativeApp>,
    pub update_semantics: Option<String>,
    pub remove_semantics: Option<String>,
    pub reboot_required: Option<bool>,
    pub target_pack_revision: u64,
    pub signature: PackageSignature,
    pub min_manager_version: String,
    pub sbom: Option<String>,
    pub reproducibility: Option<Reproducibility>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PackageArtifact {
    pub target_id: String,
    pub target_pack_revision: u64,
    pub firmware_sha256: String,
    pub path: String,
    pub sha256: String,
    pub symbols_used: Option<Vec<String>>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PackageNativeApp {
    pub app_id: Option<String>,
    pub name: Option<String>,
    pub icon: Option<String>,
    pub entry: Option<String>,
    pub role: Option<String>,
    pub pages: Option<Vec<String>>,
    pub resource_hashes: Option<std::collections::HashMap<String, String>>,
    /* CAN-P1-013: structured native-app resources. Each is hash-verified and
     * embedded by the builder, becomes part of the signed digest, and must
     * be present in the archive when the manifest declares it. */
    pub resources: Option<Vec<PackageResource>>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PackageResource {
    pub path: String,
    pub sha256: String,
    pub media_type: String,
    pub target_backend: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PackageSignature {
    pub key_id: String,
    pub algorithm: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Reproducibility {
    pub build_environment: Option<String>,
    pub toolchain: Option<String>,
    pub clean_rebuild_identical: Option<bool>,
}
