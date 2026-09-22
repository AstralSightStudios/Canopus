# Resource Hook: user-reported settings icon smoke test

The operator reported successful physical-device validation of a custom settings
launcher icon on Xiaomi Band 11 firmware 4.100.155. This records that report; no
independent device log, screenshot or exhaustive acceptance checklist was supplied.

## Tested delivery

- Module: resource_hook 0.3.0, resident lifecycle, signed ELF/CMI1.
- ELF SHA-256: `3b43d674390459457d4b302c811d516a4518cd4fae599ae8f85949a05abc27fe`.
- Source resource: `/resource/app/settings/launcher.bin`.
- Replacement: `/data/canopus/themes/current/app/settings/launcher.bin`, 112x112 I8.
- Configuration: `/data/canopus/themes/mappings.tsv`.
- Mapping: `/resource/app/settings/` to `/data/canopus/themes/current/app/settings/`.

The local experimental installer used the existing framework's exact .155 Lua
execute-recovery implementation and profile to create the theme directory chain,
staged theme files and submitted the signed module via `/canopus/install`.
The operator confirmed the flow worked on device. The module was built with the
four exact restricted symbol records described in `evidence/EVID-RESOURCE-4155-002.json`.

## Scope

This is a user-reported end-to-end installation/configuration/visible-icon smoke
test, not proof of every adapter branch, owner lifetime or native ABI assumption.
It does not establish font replacement, animations, offscreen adoption, GPU drain,
memory-pressure behavior, rollback/recovery, or compatibility with .139. Existing
symbol policy/approval states and static evidence are unchanged by this report.

At the operator's request, the experimental theme installer is NOT integrated
into this repository. Future resource transfer will use another mechanism.
Generic module-installer templates remain unchanged and do not create theme
directories. Generated installer bundles are local build artifacts, not sources.

See `../Canopus-Module-Resource-Hook/docs/INSTALL.md` (relative to this repository root)
for module lifecycle, configuration, recovery and remaining acceptance gates.
