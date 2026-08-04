//! Error types for canopus-core.

use thiserror::Error;

/// Result alias used across canopus-core.
pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug, Error)]
pub enum Error {
    #[error("schema validation failed for {kind}: {message}")]
    SchemaValidation { kind: String, message: String },

    #[error("JSON parse error: {0}")]
    Json(#[from] serde_json::Error),

    #[error("TOML parse error: {0}")]
    Toml(#[from] toml::de::Error),

    #[error("schema '{0}' not found")]
    SchemaNotFound(String),

    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),

    #[error("policy violation: {0}")]
    Policy(String),

    #[error("identity mismatch: {0}")]
    Identity(String),

    #[error("{0}")]
    Other(String),
}

impl Error {
    pub fn policy(msg: impl Into<String>) -> Self {
        Error::Policy(msg.into())
    }

    pub fn other(msg: impl Into<String>) -> Self {
        Error::Other(msg.into())
    }
}
