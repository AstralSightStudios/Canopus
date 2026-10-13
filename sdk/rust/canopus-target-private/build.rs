use std::env;

fn main() {
    const PREFIX: &str = "CARGO_FEATURE_TARGET_";
    let mut selected = env::vars_os()
        .filter_map(|(key, _)| key.into_string().ok())
        .filter(|key| key.starts_with(PREFIX))
        .collect::<Vec<_>>();
    selected.sort();

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
