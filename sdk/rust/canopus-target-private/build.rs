use std::env;

fn main() {
    const PREFIX: &str = "CARGO_FEATURE_TARGET_";
    let mut selected = env::vars_os()
        .filter_map(|(key, _)| key.into_string().ok())
        .filter(|key| key.starts_with(PREFIX))
        .collect::<Vec<_>>();
    selected.sort();

    if selected.iter().any(|feature| feature == "CARGO_FEATURE_TARGET_XIAOMI_P65_3_100_043") {
        let root = std::path::PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
            .join("../../..");
        let header = root.join("manager/target/p65/canopus_p65_abi.h");
        println!("cargo:rerun-if-changed={}", header.display());
        let text = std::fs::read_to_string(header).expect("exact P65 ABI header");
        let mut generated = String::from("// Generated from the private experimental C ABI.\n");
        for line in text.lines() {
            let words = line.split_whitespace().collect::<Vec<_>>();
            if words.len() == 3 && words[0] == "#define" {
                if let Some(address) = words[2].strip_prefix("UINT32_C(").and_then(|v| v.strip_suffix(')')) {
                    u32::from_str_radix(address.trim_start_matches("0x"), 16)
                        .expect("P65 ABI address must be a 32-bit hex integer");
                    generated.push_str(&format!("pub const {}: usize = {}usize;\n", words[1], address));
                }
            }
        }
        let destination = std::path::PathBuf::from(env::var("OUT_DIR").unwrap())
            .join("p65_addresses.rs");
        std::fs::write(destination, generated).expect("write P65 address constants");
    }

    if selected.len() != 1 {
        panic!(
            "canopus-target-private requires exactly one target-* feature; selected: {}",
            if selected.is_empty() {
                "none".to_string()
            } else {
                selected.join(", ")
            }
        );
    }
}
