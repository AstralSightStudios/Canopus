//! CAN-P1-012: codegen promotion gate. A callable is generated only when a
//! symbol is explicitly APPROVED with at least one evidence id; PENDING/
//! REJECTED status, a missing approval, or an empty evidence list never
//! emits a callable, even when the symbol otherwise looks callable.

use canopus_core::model::Symbol;
use canopus_core::rustgen::RustGen;
use canopus_core::veneer::VeneerGen;

fn symbol_with(approval: Option<&str>, evidence: &[&str]) -> Symbol {
    let evidence_json: Vec<String> = evidence.iter().map(|e| format!("\"{e}\"")).collect();
    let approval_json = match approval {
        Some(a) => format!("\"{a}\""),
        None => "null".to_string(),
    };
    let json = format!(
        r#"{{
            "schema": 1,
            "symbol_id": "t.reg",
            "target_id": "test-target",
            "name": "register_driver",
            "kind": "function",
            "entry_address": "0x1000",
            "callable_address": "0x1001",
            "instruction_set": "thumb",
            "prototype": "int(const char *)",
            "proof": {{
                "static": "confirmed",
                "device": "probed",
                "evidence_ids": [{}]
            }},
            "policy": "managed",
            "status": "DEVICE_PROBED",
            "approval_state": {},
            "provenance": {{ "firmware_sha256": "{}", "source": "test" }}
        }}"#,
        evidence_json.join(","),
        approval_json,
        "f701a84ffcafa67f4d4603ad8cd66a11e5442f27140f5af0982e0975dccd225b"
    );
    serde_json::from_str(&json).unwrap()
}

fn generate_both(symbols: &[Symbol]) -> (String, String) {
    let pack = {
        let json = r#"{
            "schema": 1,
            "target_id": "test-target",
            "device_family": "test",
            "device_model": "test",
            "os_family": "test",
            "architecture": "armv8-m.main",
            "cpu": "cortex-m33",
            "instruction_set": "thumb2",
            "endianness": "little",
            "float_abi": "soft",
            "loader": "nuttx-modlib-elf32-rel",
            "firmware_sha256": "f701a84ffcafa67f4d4603ad8cd66a11e5442f27140f5af0982e0975dccd225b",
            "firmware_version": "3.101.030",
            "firmware_build": "CONBINE_LTALM078_T3.101.030_06011854",
            "module_abi": 1,
            "relocation_profile": "v1",
            "revision": 1
        }"#;
        serde_json::from_str(json).unwrap()
    };
    let v = VeneerGen {
        pack: &pack,
        symbols,
        types: &[],
    }
    .generate();
    let r = RustGen {
        pack: &pack,
        symbols,
        types: &[],
    }
    .generate();
    (v, r)
}

#[test]
fn pending_never_generates_a_callable() {
    let symbols = [symbol_with(None, &["EVID-1"])]; // no approval_state
    let (veneer, bindings) = generate_both(&symbols);
    assert!(
        !veneer.contains("canopus_fw_register_driver"),
        "PENDING symbol leaked a veneer"
    );
    assert!(
        !bindings.contains("canopus_fw_register_driver"),
        "PENDING symbol leaked a binding"
    );
    assert!(veneer.contains("not APPROVED") || bindings.contains("not APPROVED"));
}

#[test]
fn rejected_never_generates_a_callable() {
    let symbols = [symbol_with(Some("REJECTED"), &["EVID-1"])];
    let (veneer, bindings) = generate_both(&symbols);
    assert!(!veneer.contains("canopus_fw_register_driver"));
    assert!(!bindings.contains("canopus_fw_register_driver"));
}

#[test]
fn approved_without_evidence_never_generates_a_callable() {
    let symbols = [symbol_with(Some("APPROVED"), &[])];
    let (veneer, bindings) = generate_both(&symbols);
    assert!(
        !veneer.contains("canopus_fw_register_driver"),
        "approved-but-evidence-free leaked"
    );
    assert!(!bindings.contains("canopus_fw_register_driver"));
    assert!(veneer.contains("not APPROVED") || bindings.contains("not APPROVED"));
}

#[test]
fn approved_with_evidence_generates_a_callable() {
    let symbols = [symbol_with(Some("APPROVED"), &["EVID-1"])];
    let (veneer, bindings) = generate_both(&symbols);
    assert!(
        veneer.contains("canopus_fw_register_driver"),
        "APPROVED symbol missing a veneer"
    );
    assert!(
        bindings.contains("canopus_fw_register_driver"),
        "APPROVED symbol missing a binding"
    );
}
