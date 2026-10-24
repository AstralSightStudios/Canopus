# `canopus new` templates

One directory per template (`canopus new <id> --template <name>`, list them with
`canopus new --list`). `template.toml` gives the description, the language and the
`[build] sources`; every other file is copied into the project with these
placeholders replaced, in paths and contents:

- `{{id}}`: the module id.
- `{{descriptor_target}}`: the C descriptor's target field, `CANOPUS_TARGET_ID`
  for target-bound modules or `"portable-thumbv8m"`.
- `{{sdk}}`: the path of `sdk/rust` (Rust templates' path dependencies).

`canopus new` writes `Canopus.toml` and `.gitignore` itself. Templates are built
for every active target by `scripts/ci.sh` (gate 5).
