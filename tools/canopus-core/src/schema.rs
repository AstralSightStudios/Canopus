//! JSON Schema loading and instance validation.
//!
//! The six schemas live in the repo root `schemas/` and are the single
//! source of truth. They are embedded at compile time so validation works
//! from any working directory and no runtime discovery is needed.

use crate::error::{Error, Result};
use jsonschema::validator_for;
use serde_json::Value;

pub const SCHEMA_TARGET: &str = include_str!("../../../schemas/target.schema.json");
pub const SCHEMA_SYMBOL: &str = include_str!("../../../schemas/symbol.schema.json");
pub const SCHEMA_TYPE: &str = include_str!("../../../schemas/type.schema.json");
pub const SCHEMA_EVIDENCE: &str = include_str!("../../../schemas/evidence.schema.json");
pub const SCHEMA_MODULE: &str = include_str!("../../../schemas/module.schema.json");
pub const SCHEMA_PACKAGE: &str = include_str!("../../../schemas/package.schema.json");

/// Which schema family an instance belongs to.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SchemaKind {
    Target,
    Symbol,
    Type,
    Evidence,
    Module,
    Package,
}

impl SchemaKind {
    pub fn as_str(&self) -> &'static str {
        match self {
            SchemaKind::Target => "target",
            SchemaKind::Symbol => "symbol",
            SchemaKind::Type => "type",
            SchemaKind::Evidence => "evidence",
            SchemaKind::Module => "module",
            SchemaKind::Package => "package",
        }
    }

    pub fn schema_text(&self) -> &'static str {
        match self {
            SchemaKind::Target => SCHEMA_TARGET,
            SchemaKind::Symbol => SCHEMA_SYMBOL,
            SchemaKind::Type => SCHEMA_TYPE,
            SchemaKind::Evidence => SCHEMA_EVIDENCE,
            SchemaKind::Module => SCHEMA_MODULE,
            SchemaKind::Package => SCHEMA_PACKAGE,
        }
    }
}

/// Compile a validator for the given schema family.
pub fn validator(kind: SchemaKind) -> Result<jsonschema::Validator> {
    let schema: Value = serde_json::from_str(kind.schema_text())
        .map_err(|e| Error::SchemaNotFound(format!("{}: {e}", kind.as_str())))?;
    let v = validator_for(&schema)
        .map_err(|e| Error::Other(format!("failed to compile {} schema: {e}", kind.as_str())))?;
    Ok(v)
}

/// Validate `instance` against the schema family, returning all errors as a
/// single message. Returns Ok if the instance is valid.
pub fn validate(kind: SchemaKind, instance: &Value) -> Result<()> {
    let v = validator(kind)?;
    let mut lines: Vec<String> = Vec::new();
    for err in v.iter_errors(instance) {
        lines.push(format!("  {}: {}", err.instance_path, err.to_string()));
    }
    if lines.is_empty() {
        Ok(())
    } else {
        Err(Error::SchemaValidation {
            kind: kind.as_str().to_string(),
            message: lines.join("\n"),
        })
    }
}
