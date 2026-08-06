//! Integration tests: every fixture under tests/fixtures must validate
//! (valid/) or fail (invalid/) against the corresponding schema.

use canopus_core::schema::{SchemaKind, validate};
use std::path::{Path, PathBuf};

const FIXTURES: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../../tests/fixtures");

fn fixture_dir(kind: SchemaKind) -> PathBuf {
    Path::new(FIXTURES).join(kind.as_str())
}

fn run(kind: SchemaKind) {
    let dir = fixture_dir(kind);
    for sub in ["valid", "invalid"] {
        let subdir = dir.join(sub);
        if !subdir.is_dir() {
            continue;
        }
        let mut count = 0;
        for entry in std::fs::read_dir(&subdir).expect("read fixture dir") {
            let p = entry.unwrap().path();
            if p.extension().and_then(|e| e.to_str()) != Some("json") {
                continue;
            }
            let text = std::fs::read_to_string(&p).unwrap();
            let value: serde_json::Value = serde_json::from_str(&text).unwrap();
            let result = validate(kind, &value);
            match sub {
                "valid" => {
                    assert!(
                        result.is_ok(),
                        "{}: expected VALID but got: {:?}",
                        p.display(),
                        result.err().unwrap()
                    );
                }
                "invalid" => {
                    assert!(
                        result.is_err(),
                        "{}: expected INVALID but passed",
                        p.display()
                    );
                }
                _ => unreachable!(),
            }
            count += 1;
        }
        assert!(
            count > 0,
            "no {} fixtures found under {}",
            sub,
            subdir.display()
        );
    }
}

#[test]
fn target_fixtures() {
    run(SchemaKind::Target);
}

#[test]
fn symbol_fixtures() {
    run(SchemaKind::Symbol);
}

#[test]
fn type_fixtures() {
    run(SchemaKind::Type);
}

#[test]
fn evidence_fixtures() {
    run(SchemaKind::Evidence);
}

#[test]
fn module_fixtures() {
    run(SchemaKind::Module);
}

#[test]
fn package_fixtures() {
    run(SchemaKind::Package);
}

#[test]
fn function_signature_catalog_fixtures() {
    run(SchemaKind::FunctionSignatureCatalog);
}

#[test]
fn current_target_signature_catalog_validates() {
    let path = Path::new(env!("CARGO_MANIFEST_DIR")).join(
        "../../targets/xiaomi-band-10-pro-3.101.030/function-signatures.json",
    );
    let value: serde_json::Value =
        serde_json::from_str(&std::fs::read_to_string(path).unwrap()).unwrap();
    validate(SchemaKind::FunctionSignatureCatalog, &value).unwrap();
    assert_eq!(value["signatures"].as_array().unwrap().len(), 60);
    assert_eq!(value["unresolved"].as_array().unwrap().len(), 2);
}
