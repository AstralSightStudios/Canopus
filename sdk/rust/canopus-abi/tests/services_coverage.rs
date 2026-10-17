//! Source call-site gate for the first-class service objects.
//!
//! This checks direct `ctx->group->method(...)` syntax, not execution, assertions,
//! or behavioral coverage. Comments, string/character literals, preprocessor
//! directives and definitely inactive literal `#if 0` branches cannot supply a
//! call site. Other preprocessor conditions are treated as potentially active;
//! this intentionally is not a C frontend or a configuration-specific proof.
//!
//! Only `system.reboot` is exempt from calling: the smoke test must check its
//! pointer for availability instead, because invoking it would reboot the device.

use std::collections::{BTreeMap, BTreeSet};

type Method = (String, String);

const HEADER: &str = include_str!("../../../c/canopus_services.h");
const TEST_SOURCE: &str =
    include_str!("../../../../modules/examples/all-services-test/src/all_services_test.c");
const AVAILABILITY_ONLY: (&str, &str) = ("system", "reboot");

#[derive(Clone, Copy, Debug)]
struct Token<'a> {
    text: &'a str,
    start: usize,
    end: usize,
}

fn identifier(text: &str) -> bool {
    text.as_bytes()
        .first()
        .is_some_and(|c| c.is_ascii_alphabetic() || *c == b'_')
}

fn lex(src: &str) -> Result<Vec<Token<'_>>, String> {
    let bytes = src.as_bytes();
    let mut tokens = Vec::new();
    let mut i = 0;
    while i < bytes.len() {
        let start = i;
        match bytes[i] {
            b'\\' if bytes.get(i + 1) == Some(&b'\n') => {
                i += 2; // C line continuation, including preprocessor directives.
                continue;
            }
            b'/' if bytes.get(i + 1) == Some(&b'/') => {
                i += 2;
                while i < bytes.len() && bytes[i] != b'\n' {
                    if bytes[i] == b'\\' && bytes.get(i + 1) == Some(&b'\n') {
                        i += 2;
                    } else {
                        i += 1;
                    }
                }
                continue;
            }
            b'/' if bytes.get(i + 1) == Some(&b'*') => {
                i += 2;
                while i + 1 < bytes.len() && &bytes[i..i + 2] != b"*/" {
                    if bytes[i] == b'\n' {
                        tokens.push(Token {
                            text: "\n",
                            start: i,
                            end: i + 1,
                        });
                    }
                    i += 1;
                }
                if i + 1 >= bytes.len() {
                    return Err("unterminated C block comment".into());
                }
                i += 2;
                continue;
            }
            b'"' | b'\'' => {
                let quote = bytes[i];
                i += 1;
                loop {
                    if i >= bytes.len() {
                        return Err("unterminated C literal".into());
                    }
                    if bytes[i] == b'\\' {
                        i += 2;
                    } else if bytes[i] == quote {
                        i += 1;
                        break;
                    } else {
                        i += 1;
                    }
                }
                // Keep the literal as one opaque token, never its contents.
            }
            b'\n' => i += 1,
            c if c.is_ascii_whitespace() => {
                i += 1;
                continue;
            }
            c if c.is_ascii_alphanumeric() || c == b'_' => {
                i += 1;
                while i < bytes.len() && (bytes[i].is_ascii_alphanumeric() || bytes[i] == b'_') {
                    i += 1;
                }
            }
            _ => {
                let pair = bytes.get(i..i + 2);
                i += if matches!(pair, Some(b"->" | b"==" | b"!=" | b"&&" | b"||")) {
                    2
                } else {
                    src[i..].chars().next().unwrap().len_utf8()
                };
            }
        }
        tokens.push(Token {
            text: &src[start..i],
            start,
            end: i,
        });
    }
    Ok(tokens)
}

// Only literal zero/one conditions are evaluated; unknown expressions preserve
// all potentially active branches rather than pretending to know target macros.
fn literal_condition(tokens: &[Token<'_>]) -> Option<bool> {
    let values: Vec<_> = tokens
        .iter()
        .filter(|t| t.text != "(" && t.text != ")")
        .collect();
    if values.len() != 1 {
        return None;
    }
    match values[0].text.trim_end_matches(['u', 'U', 'l', 'L']) {
        "0" => Some(false),
        "1" => Some(true),
        _ => None,
    }
}

fn active_tokens(src: &str) -> Result<Vec<Token<'_>>, String> {
    struct Conditional {
        parent_active: bool,
        remaining_possible: bool,
    }
    let raw = lex(src)?;
    let mut out = Vec::new();
    let mut stack: Vec<Conditional> = Vec::new();
    let mut active = true;
    for line in raw.split(|t| t.text == "\n") {
        if line.first().map(|t| t.text) != Some("#") {
            if active {
                out.extend_from_slice(line);
            }
            continue;
        }
        let directive = line.get(1).map(|t| t.text).unwrap_or("");
        match directive {
            "if" | "ifdef" | "ifndef" => {
                let condition = if directive == "if" {
                    literal_condition(&line[2..])
                } else {
                    None
                };
                stack.push(Conditional {
                    parent_active: active,
                    remaining_possible: condition != Some(true),
                });
                active &= condition != Some(false);
            }
            "elif" | "else" => {
                let frame = stack.last_mut().ok_or("unmatched C #elif/#else")?;
                let condition = if directive == "else" {
                    Some(true)
                } else {
                    literal_condition(&line[2..])
                };
                active =
                    frame.parent_active && frame.remaining_possible && condition != Some(false);
                frame.remaining_possible &= condition != Some(true);
            }
            "endif" => active = stack.pop().ok_or("unmatched C #endif")?.parent_active,
            _ => {} // Includes macro definitions: their bodies are not call sites.
        }
    }
    if !stack.is_empty() {
        return Err("unterminated C preprocessor conditional".into());
    }
    Ok(out)
}

fn closing(tokens: &[Token<'_>], open: usize, left: &str, right: &str) -> Result<usize, String> {
    let mut depth = 0;
    for (i, token) in tokens.iter().enumerate().skip(open) {
        if token.text == left {
            depth += 1;
        } else if token.text == right {
            depth -= 1;
            if depth == 0 {
                return Ok(i);
            }
        }
    }
    Err(format!("unclosed C delimiter {left}"))
}

fn declared_methods(header: &str) -> Result<BTreeSet<Method>, String> {
    let tokens = active_tokens(header)?;
    let mut structs = BTreeMap::new();
    let mut i = 0;
    while i + 2 < tokens.len() {
        if tokens[i].text == "struct" && identifier(tokens[i + 1].text) && tokens[i + 2].text == "{"
        {
            let end = closing(&tokens, i + 2, "{", "}")?;
            let name = tokens[i + 1].text;
            if structs.insert(name, &tokens[i + 3..end]).is_some() {
                return Err(format!("duplicate C struct definition: {name}"));
            }
            i = end + 1;
        } else {
            i += 1;
        }
    }
    let context = structs
        .get("canopus_context_v1")
        .ok_or("missing context definition")?;
    let mut methods = BTreeSet::new();
    let mut bound_objects = BTreeSet::new();
    // Derive actual group field names, including aliases such as bluetooth -> bt.
    for field in context.windows(4) {
        let object = field[1].text;
        if field[0].text != "struct"
            || !object.starts_with("canopus_")
            || !object.ends_with("_obj")
            || field[2].text != "*"
            || !identifier(field[3].text)
        {
            continue;
        }
        let group = field[3].text;
        let body = structs
            .get(object)
            .ok_or_else(|| format!("missing service object definition: {object}"))?;
        bound_objects.insert(object);
        for declaration in body.split(|t| t.text == ";").filter(|d| !d.is_empty()) {
            let method = declaration
                .windows(5)
                .find(|w| {
                    w[0].text == "("
                        && w[1].text == "*"
                        && identifier(w[2].text)
                        && w[3].text == ")"
                        && w[4].text == "("
                })
                .ok_or_else(|| format!("unsupported service field declaration in {object}"))?;
            methods.insert((group.to_string(), method[2].text.to_string()));
        }
    }
    // New object definitions must not disappear just because a hardcoded group
    // mapping was never updated (or the context field has not been exposed yet).
    for object in structs.keys() {
        if object.starts_with("canopus_")
            && object.ends_with("_obj")
            && !bound_objects.contains(object)
        {
            return Err(format!(
                "service object has no context group field: {object}"
            ));
        }
    }
    if methods.is_empty() {
        return Err("no service object methods parsed".into());
    }
    Ok(methods)
}

fn service_access<'a>(tokens: &[Token<'a>], i: usize) -> Option<(&'a str, &'a str)> {
    let access = tokens.get(i..i + 5)?;
    (identifier(access[0].text)
        && access[1].text == "->"
        && identifier(access[2].text)
        && access[3].text == "->"
        && identifier(access[4].text))
    .then_some((access[2].text, access[4].text))
}

fn direct_calls(tokens: &[Token<'_>]) -> BTreeSet<Method> {
    (0..tokens.len())
        .filter_map(|i| {
            let (group, method) = service_access(tokens, i)?;
            (tokens.get(i + 5)?.text == "(").then(|| (group.to_string(), method.to_string()))
        })
        .collect()
}

fn has_availability_check(tokens: &[Token<'_>], wanted: (&str, &str)) -> Result<bool, String> {
    for i in 0..tokens.len().saturating_sub(1) {
        if tokens[i].text != "if" || tokens[i + 1].text != "(" {
            continue;
        }
        let end = closing(tokens, i + 1, "(", ")")?;
        let condition = &tokens[i + 2..end];
        for j in 0..condition.len() {
            if service_access(condition, j) != Some(wanted)
                || condition.get(j + 5).map(|t| t.text) == Some("(")
            {
                continue;
            }
            let next = condition.get(j + 5).map(|t| t.text);
            let value = condition.get(j + 6).map(|t| t.text);
            let null_comparison =
                matches!(next, Some("==" | "!=")) && matches!(value, Some("0" | "NULL"));
            let negated = j > 0 && condition[j - 1].text == "!";
            if null_comparison || negated {
                return Ok(true);
            }
        }
    }
    Ok(false)
}

fn source_call_site_gate(header: &str, source: &str) -> Result<usize, String> {
    let methods = declared_methods(header)?;
    let tokens = active_tokens(source)?;
    let calls = direct_calls(&tokens);
    let mut missing = Vec::new();
    let mut call_count = 0;
    for (group, method) in &methods {
        if (group.as_str(), method.as_str()) == AVAILABILITY_ONLY {
            if !has_availability_check(&tokens, AVAILABILITY_ONLY)? {
                missing.push("ctx->system->reboot (explicit null availability check)".to_string());
            }
        } else if calls.contains(&(group.clone(), method.clone())) {
            call_count += 1;
        } else {
            missing.push(format!("ctx->{group}->{method}(...)"));
        }
    }
    if !methods.contains(&(AVAILABILITY_ONLY.0.into(), AVAILABILITY_ONLY.1.into())) {
        return Err(
            "availability exception no longer names a declared system.reboot method".into(),
        );
    }
    if calls.contains(&(AVAILABILITY_ONLY.0.into(), AVAILABILITY_ONLY.1.into())) {
        return Err(
            "system.reboot must only be checked for availability, not called by the smoke test"
                .into(),
        );
    }
    if !missing.is_empty() {
        return Err(format!(
            "SOURCE CALL-SITE GATE FAILED (not behavioral coverage):\n{}",
            missing.join("\n")
        ));
    }
    Ok(call_count)
}

#[test]
fn all_service_object_methods_have_source_call_sites_or_explicit_reboot_check() {
    let called = source_call_site_gate(HEADER, TEST_SOURCE).unwrap_or_else(|err| panic!("{err}"));
    println!(
        "Source call-site gate: {called} service methods have direct calls; system.reboot has an explicit availability check. This is not behavioral coverage."
    );
}

// Mutations operate only on in-memory copies; no source file is edited.
fn remove_calls(source: &str, wanted: (&str, &str)) -> String {
    let tokens = active_tokens(source).unwrap();
    let mut ranges = Vec::new();
    for i in 0..tokens.len() {
        if service_access(&tokens, i) == Some(wanted)
            && tokens.get(i + 5).map(|t| t.text) == Some("(")
        {
            let end = closing(&tokens, i + 5, "(", ")").unwrap();
            if ranges
                .last()
                .is_none_or(|&(_, previous_end)| tokens[i].start >= previous_end)
            {
                ranges.push((tokens[i].start, tokens[end].end));
            }
        }
    }
    assert!(
        !ranges.is_empty(),
        "mutation needs an existing {}.{} call",
        wanted.0,
        wanted.1
    );
    let mut mutated = source.to_string();
    for (start, end) in ranges.into_iter().rev() {
        mutated.replace_range(start..end, "0");
    }
    mutated
}

#[test]
fn mutation_read_all_and_non_call_references_cannot_replace_read_call() {
    let source = remove_calls(TEST_SOURCE, ("fs", "read"));
    assert!(
        direct_calls(&active_tokens(&source).unwrap()).contains(&("fs".into(), "read_all".into()))
    );
    let mut source = source
        + r#"
        if (ctx->fs->read != 0) { }
        // ctx->fs->read(ctx, 0, 0, 0);
        /* ctx->fs->read(ctx, 0, 0, 0); */
        const char *fake = "escaped quote: \" ctx->fs->read(ctx, 0, 0, 0)";
        int fake_char = 'ctx->fs->read(ctx)';
        #define FAKE_READ ctx->fs->read(ctx, 0, 0, 0)
        #if 0
        #if UNKNOWN_TARGET
        ctx->fs->read(ctx, 0, 0, 0);
        #else
        ctx->fs->read(ctx, 0, 0, 0);
        #endif
        #elif (0)
        ctx->fs->read(ctx, 0, 0, 0);
        #endif
    "#;
    assert!(
        source_call_site_gate(HEADER, &source)
            .unwrap_err()
            .contains("ctx->fs->read(")
    );
    source.push_str(
        "\n#if 0\nignored();\n#else\nctx -> fs -> read /* exact call */ (ctx, 0, 0, 0);\n#endif\n",
    );
    assert!(source_call_site_gate(HEADER, &source).is_ok());
}

#[test]
fn mutation_registration_pointer_check_cannot_replace_call() {
    let source = remove_calls(TEST_SOURCE, ("hook", "register_slot_map"))
        + "\nif (ctx->hook->register_slot_map != 0) { }\n";
    assert!(
        source_call_site_gate(HEADER, &source)
            .unwrap_err()
            .contains("ctx->hook->register_slot_map(")
    );
}

#[test]
fn mutation_new_methods_and_context_groups_require_calls() {
    let header = HEADER.replacen(
        "struct canopus_hook_obj {",
        "struct canopus_hook_obj {\nint32_t (*future_method)(void);",
        1,
    );
    let source = TEST_SOURCE.to_string() + "\nif (ctx->hook->future_method != 0) { }\n";
    assert!(
        source_call_site_gate(&header, &source)
            .unwrap_err()
            .contains("ctx->hook->future_method(")
    );

    let header = HEADER.replacen(
        "struct canopus_context_v1 {",
        "struct canopus_context_v1 {\nconst struct canopus_future_obj *new_group;",
        1,
    ) + "\nstruct canopus_future_obj { int32_t (*ping)(void); };\n";
    assert!(
        source_call_site_gate(&header, TEST_SOURCE)
            .unwrap_err()
            .contains("ctx->new_group->ping(")
    );
    assert!(
        source_call_site_gate(
            &header,
            &(TEST_SOURCE.to_string() + "\nctx->new_group->ping();\n")
        )
        .is_ok()
    );
}

#[test]
fn mutation_reboot_exception_is_scoped_and_requires_availability_check() {
    let tokens = active_tokens(TEST_SOURCE).unwrap();
    let mut source = TEST_SOURCE.to_string();
    let mut references = Vec::new();
    for i in 0..tokens.len() {
        if service_access(&tokens, i) == Some(AVAILABILITY_ONLY) {
            references.push((tokens[i + 4].start, tokens[i + 4].end));
        }
    }
    assert!(!references.is_empty());
    for (start, end) in references.into_iter().rev() {
        source.replace_range(start..end, "not_reboot");
    }
    assert!(
        source_call_site_gate(HEADER, &source)
            .unwrap_err()
            .contains("ctx->system->reboot (explicit null")
    );
    let header = HEADER.replacen(
        "struct canopus_fs_obj {",
        "struct canopus_fs_obj {\nint32_t (*reboot)(void);",
        1,
    );
    let source = TEST_SOURCE.to_string() + "\nif (!ctx->fs->reboot) { }\n";
    assert!(
        source_call_site_gate(&header, &source)
            .unwrap_err()
            .contains("ctx->fs->reboot(")
    );
}
