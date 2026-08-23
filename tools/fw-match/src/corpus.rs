//! Corpus model: per-function feature records dumped from an IDB.
//!
//! The corpus is intentionally *position-independent*: features are stored as
//! relative CFG shapes, string *content*, small constants and callee/caller
//! addresses (which become comparable once anchors are established). Entry
//! bytes are stored raw; the mask is computed by [`crate::thumb`] at match
//! time so the same corpus works with any mask policy.

use serde::{Deserialize, Serialize};
use std::collections::BTreeSet;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Corpus {
    pub schema: u32,
    pub target_id: String,
    #[serde(default)]
    pub image_base: String,
    pub functions: Vec<FunctionRecord>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FunctionRecord {
    /// Absolute address in this firmware (XIP_TEXT_RO mapping).
    pub addr: String,
    #[serde(default)]
    pub name: String,
    #[serde(default)]
    pub size: u64,
    #[serde(default)]
    pub insn: u64,
    #[serde(default)]
    pub blocks: usize,
    /// CFG: (offset_from_start, size) per block, in block-id order.
    #[serde(default)]
    pub block_offs: Vec<BlockShape>,
    /// CFG edges: (from_block_id, to_block_id).
    #[serde(default)]
    pub succ: Vec<(usize, usize)>,
    /// Absolute callee addresses (direct calls to function starts).
    #[serde(default)]
    pub callees: Vec<String>,
    /// Absolute caller addresses.
    #[serde(default)]
    pub callers: Vec<String>,
    /// Referenced string literals (content only).
    #[serde(default)]
    pub strings: Vec<String>,
    /// Small immediates (values < 0x100000).
    #[serde(default)]
    pub constants: Vec<u64>,
    /// Raw entry bytes, hex.
    #[serde(default)]
    pub entry: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BlockShape {
    pub off: u64,
    pub size: u64,
}

impl FunctionRecord {
    pub fn addr_u64(&self) -> u64 {
        u64::from_str_radix(self.addr.trim_start_matches("0x"), 16).unwrap_or(0)
    }

    pub fn entry_bytes(&self) -> Vec<u8> {
        hex::decode(self.entry.trim()).unwrap_or_default()
    }

    /// Set of referenced strings for overlap scoring.
    pub fn string_set(&self) -> BTreeSet<&str> {
        self.strings.iter().map(|s| s.as_str()).collect()
    }

    /// Set of small constants (deduplicated) for overlap scoring.
    pub fn constant_set(&self) -> BTreeSet<u64> {
        self.constants.iter().copied().collect()
    }

    /// Normalized CFG fingerprint: sorted block sizes + edge count.
    /// Used for a coarse structural pre-filter before fine scoring.
    pub fn cfg_fingerprint(&self) -> (Vec<u64>, usize) {
        let mut sizes: Vec<u64> = self.block_offs.iter().map(|b| b.size).collect();
        sizes.sort_unstable();
        (sizes, self.succ.len())
    }
}

impl Corpus {
    pub fn function_at(&self, addr: u64) -> Option<&FunctionRecord> {
        self.functions.iter().find(|f| f.addr_u64() == addr)
    }

    /// Look up a function by exact address string.
    pub fn function_by_addr_str(&self, addr: &str) -> Option<&FunctionRecord> {
        self.functions.iter().find(|f| f.addr == addr)
    }
}

/// Load a corpus from a JSON file.
pub fn load_corpus(path: &std::path::Path) -> Result<Corpus, String> {
    let text = std::fs::read_to_string(path)
        .map_err(|e| format!("cannot read {}: {e}", path.display()))?;
    serde_json::from_str(&text).map_err(|e| format!("corpus parse error: {e}"))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> FunctionRecord {
        FunctionRecord {
            addr: "0x1234".into(),
            name: "foo".into(),
            size: 64,
            insn: 30,
            blocks: 3,
            block_offs: vec![
                BlockShape { off: 0, size: 16 },
                BlockShape { off: 16, size: 24 },
                BlockShape { off: 40, size: 24 },
            ],
            succ: vec![(0, 1), (1, 2)],
            callees: vec!["0x2000".into()],
            callers: vec![],
            strings: vec!["hello".into()],
            constants: vec![5, 0x100],
            entry: "00be".into(),
        }
    }

    #[test]
    fn addr_u64_parses() {
        assert_eq!(sample().addr_u64(), 0x1234);
    }

    #[test]
    fn fingerprint_sorted_sizes() {
        let (sizes, edges) = sample().cfg_fingerprint();
        assert_eq!(sizes, vec![16, 24, 24]);
        assert_eq!(edges, 2);
    }

    #[test]
    fn entry_hex_roundtrip() {
        let rec = sample();
        assert_eq!(rec.entry_bytes(), vec![0x00, 0xbe]);
    }

    #[test]
    fn corpus_json_roundtrip() {
        let c = Corpus {
            schema: 1,
            target_id: "t".into(),
            image_base: "0x0".into(),
            functions: vec![sample()],
        };
        let s = serde_json::to_string(&c).unwrap();
        let back: Corpus = serde_json::from_str(&s).unwrap();
        assert_eq!(back.functions[0].addr, "0x1234");
        assert_eq!(back.functions[0].constants, vec![5, 0x100]);
    }
}
