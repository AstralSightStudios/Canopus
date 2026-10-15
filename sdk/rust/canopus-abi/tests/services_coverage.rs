//! CI Gate: verify that every method declared in `canopus_services.h`
//! Service Objects is tested in `all_services_test.c`.
//!
//! If a new method is added to any `struct canopus_*_obj` in `sdk/c/canopus_services.h`
//! without corresponding test coverage in `all_services_test.c`, this test fails
//! immediately at Gate 0.

#[test]
fn all_service_object_methods_are_tested_in_all_services_test() {
    let header = include_str!("../../../c/canopus_services.h");
    let test_src = include_str!("../../../../modules/examples/all-services-test/src/all_services_test.c");

    // Strip comments from test source
    let clean_test_src = strip_c_comments(test_src);

    // Parse service object structs from header
    let mut methods: Vec<(String, String)> = Vec::new(); // (group_field, method_name)
    let mut current_struct: Option<String> = None;

    for line in header.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("struct canopus_") && trimmed.ends_with("_obj {") {
            let struct_name = trimmed
                .strip_prefix("struct ")
                .unwrap()
                .strip_suffix(" {")
                .unwrap()
                .trim();
            let group_field = match struct_name {
                "canopus_fs_obj" => "fs",
                "canopus_kv_obj" => "kv",
                "canopus_mem_obj" => "mem",
                "canopus_time_obj" => "time",
                "canopus_hook_obj" => "hook",
                "canopus_haptics_obj" => "haptics",
                "canopus_display_obj" => "display",
                "canopus_bus_obj" => "bus",
                "canopus_log_obj" => "log",
                "canopus_notify_obj" => "notify",
                "canopus_system_obj" => "system",
                "canopus_bluetooth_obj" => "bt",
                "canopus_ui_obj" => "ui",
                _ => continue,
            };
            current_struct = Some(group_field.to_string());
            continue;
        }

        if trimmed == "};" {
            current_struct = None;
            continue;
        }

        if let Some(ref group_field) = current_struct {
            // Match pattern: (*method_name)(
            if let Some(open_paren_star) = trimmed.find("(*") {
                let rest = &trimmed[open_paren_star + 2..];
                if let Some(close_paren) = rest.find(')') {
                    let method_name = rest[..close_paren].trim();
                    if !method_name.is_empty() {
                        methods.push((group_field.clone(), method_name.to_string()));
                    }
                }
            }
        }
    }

    assert!(
        methods.len() >= 93,
        "Expected at least 93 methods in canopus_services.h, parsed {}",
        methods.len()
    );

    let mut missing = Vec::new();
    let mut tested_count = 0;

    for (group, method) in &methods {
        let needle = format!("->{group}->{method}");
        if clean_test_src.contains(&needle) {
            tested_count += 1;
        } else {
            missing.push(format!("ctx->{}->{}", group, method));
        }
    }

    if !missing.is_empty() {
        panic!(
            "CI GATE FAILED: The following {} service object method(s) lack test coverage in all_services_test.c:\n{}",
            missing.len(),
            missing.join("\n")
        );
    }

    println!("All {} service object methods are verified in all_services_test.c!", tested_count);
}

fn strip_c_comments(src: &str) -> String {
    let mut out = String::with_capacity(src.len());
    let mut chars = src.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '/' {
            if let Some(&'/') = chars.peek() {
                chars.next();
                // Skip single-line comment
                for sc in chars.by_ref() {
                    if sc == '\n' {
                        out.push('\n');
                        break;
                    }
                }
                continue;
            } else if let Some(&'*') = chars.peek() {
                chars.next();
                // Skip block comment
                while let Some(bc) = chars.next() {
                    if bc == '*' {
                        if let Some(&'/') = chars.peek() {
                            chars.next();
                            break;
                        }
                    }
                }
                continue;
            }
        }
        out.push(c);
    }
    out
}
