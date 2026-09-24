# P65 firmware analysis — 3.100.043

## Input identity

- OTA bundle: `~/develop/temp/miwear.watch.p65_v3.100.043_full_662aa7db.bin`
- OTA SHA-256: `662aa7db5dd7ec98c4b7da0f7bec3066478ff45563c57b4cf3f16a2558cbc987`
- Extracted AP payload: `~/develop/temp/p65_v3.100.043_vela_ap.bin`
- AP SHA-256 (the target pack identity): `a19b601477569765103eaf468b937bd8026b4d68ffa5c896bde222a79e475858`
- AP size: `11,793,540` bytes
- IDA 9.4 database: `~/develop/temp/p65_v3.100.043_vela_ap.bin.i64`
- IDA database SHA-256: `4d2eb1c18f4a14dd27c07482105f76c06d5ab9c057745aef132da34516bcc6e5`

The OTA is a ZIP container despite its `.bin` suffix. It contains `vela_ap.bin`; the custom loader was applied to that extracted payload, not the outer archive.

## Loader adjustment

The supplied `best1503_vela.py` rejected the AP because the first header word is `0xBE57EC1C`, while the original probe required `0xFFFFFFFF`. The remaining header fields, startup stub, odd Thumb entry, footer, boot-info pointer, and derived flash aliases all validate. I adjusted the probe to accept a non-legacy first word only when the footer independently validates both flash aliases and the build-info configuration, then copied the updated loader to `~/.idapro/loaders/best1503_vela.py`.

The file was loaded by IDA as `BES BEST15xx Vela image`; IDA discovered `42,835` functions. The custom loader validated:

| Mapping | Base | File-backed span |
|---|---:|---:|
| XIP/execute | `0x0C0C0000` | `0x00B3F484` |
| Cached flash | `0x2C0C0000` | `0x00B3F484` |
| Non-cached flash | `0x280C0000` | `0x00B3F484` |

Build-info is at file offset `0x00B3F30C` / XIP `0x0CBFF30C`. The startup literals do not pass the supplied loader's `.data`/BSS validation, so the IDA loader still maps only XIP aliases. Follow-up analysis of the AP reset-stage code independently recovered a copy/zero range at 0x3C; see `EVID-P65-MEMORY-002.json`.

## Exact P65 findings

- Boot metadata: `CHIP=best1502p`, `KERNEL=NUTTX`.
- Flash config: `FLASH_BASE=0x2C000000`, `FLASH_NC_BASE=0x28000000`, `OTA_CODE_OFFSET=0xC0000`.
- Build metadata: `BUILD_DATE=Sep 12 2026 07:22:16`, `REV_INFO=d516af0:nx_best1502p_ap`.
- Build properties at XIP `0x0C3BF9EC` include `ro.build.version=3.100.043`, `ro.build.id=3.100.043`, `ro.product.device.devicetype=watch`, `ro.product.device.screenshape=rect`, and LCD density `320`.
- Internal product ID `miwear.watch.p65` is at XIP `0x0C2A7B14`. No marketing model name was inferred.
- `_start` is at XIP `0x0C0C0010`; the mapped Thumb startup target is `0x0C0C0014`. IDA pseudocode shows the startup routine configuring MSP/MSPLIM and branching into the main firmware path.
- Static NuttX loader leads are present: `insmod` at `0x0C2848C0`, `rmmod` at `0x0C284B90`, `modlib/modlib_load.c` at `0x0C27017C`, and `modlib/modlib_symtab.c` at `0x0C2701CC`.

The existing `extract_corpus.py` pass produced a temporary schema-2 corpus at `~/develop/temp/p65-3.100.043-corpus.json` (42,835 functions, 89 referenced data objects). The bounded corpus is useful for navigation only; its data-object coverage is insufficient for ABI/global promotion. No cross-target matcher result has been promoted into P65 symbols.

## Canopus status

`targets/xiaomi-p65-3.100.043/` is an analysis-only exact target pack. It records the exact AP SHA, build identity, and three recovered identity strings; generated identity checks remain `PENDING`. Capabilities are empty. The NSH `insmod`/`rmmod` command names and usage are now recovered, while the loader profile remains non-deployable; no target-private backend, callable firmware symbol, relocation allowlist, or device capability has been added.

The stock NSH `insmod`/`rmmod` path, ELF relocation/symbol resolution, AP 0x3C data/BSS initialization, and machine-level ET_REL initializer/unload-callback contract are now statically traced (see `EVID-P65-MODLIB-001.json`, `EVID-P65-MEMORY-002.json`, and `EVID-P65-CANABI-001.json`). The static symbol-table count is zero at startup; any later writer, usable Canopus imports, target-private backend, failure recovery, and device behavior remain unverified. Do not treat this static pack or its generated identity guard as device support.

## Follow-up static loader review

The active IDA database was verified against the exact AP SHA-256 and size above. The XIP strings include an NSH command/help pool near `nsh_command.c` (0x0C2844F0): `insmod` (0x0C2848C0) with `<file-path> <module-name>` usage, `lsmod` (0x0C284988), and `rmmod` (0x0C284B90), adjacent to ordinary shell commands. NuttX source-path strings also identify `module/mod_insmod.c`, `module/mod_rmmod.c`, `module/mod_modhandle.c`, `module/mod_procfs.c`, `modlib/modlib_{load,loadhdrs,registry,symtab}.c`, and `binfmt_execmodule.c`/`binfmt_execsymtab.c`. The strings alone did not prove reachability; the exact handler-to-loader path is now recovered separately below. IDA still records zero data xrefs to the tested strings, and only 13 of 46,916 XIP strings have any recorded xrefs. Generic `module` errors nearby are Lua 5.4 package-loader strings, not NuttX `modlib` evidence.

The `_start` routine copies one word from the boot-info alias at `0x2CBFF308` to `0x2019E724`; its following small zero-fill loop has an empty range, which is why the custom IDA loader did not infer `.data`/BSS. A later reset-stage routine, `0x0C0C0314`, is called from `0x0C0C0394` during startup: it copies cached-XIP `[0x2C0EFDC4, 0x2C4C1E44)` to writable VMA `[0x3C000000, 0x3C3D2080)` and clears BSS `[0x3C3D2080, 0x3C4DF960)`. The module-list head `0x3C3D67C8` is in that zeroed range. The active static-symbol count at `0x3C3B8FF0` is initialized from source `0x2C4A8DB4` as zero. The physical RAM type is not identified. See `EVID-P65-MEMORY-002.json`.

The earlier same-offset AP-flash alias hypothesis remains false, but the AP reset-stage copy/zero code now explains the lower 0x3C addresses as writable initialized-data/BSS VMAs. Runtime NSH metadata at `0x3C194F2C` maps back to source `0x2C284CF0`; its descriptor name pointer for `insmod` at `0x3C194AFC` maps to the literal `insmod` string at XIP `0x0C2848C0`. The physical memory type (PSRAM or other RAM) remains unproven. The OTA's separate ET_EXEC images at `0x3C800000`/`0x3C900000` are unrelated to this lower VMA range. See `EVID-P65-MEMORY-001.json` for the false-alias check and `EVID-P65-MEMORY-002.json` for the recovered startup mapping.

## Static module path recovered

The XIP command descriptor table source at `0x0C284CF0` spans 72 records and is copied to runtime VMA `0x3C194F2C`. The `insmod` row at `0x0C284EF0` points to handler `0x0C5B15E4` and has argument spec `0x303`; its runtime name pointer `0x3C194AFC` maps through the startup copy to source string `insmod` at `0x0C2848C0`, and its usage pointer resolves to `<file-path> <module-name>` at `0x0C2848C8`. The handler forwards `argv[1]` and `argv[2]` to `0x0C4C49AC`, matching runtime `help -v insmod`; the command-name mapping is now directly recovered.

`0x0C4C49AC` opens and validates ELF, allocates/loads sections, calls `0x0C4F901C`, then initializes and registers the module. The validator checks ELF32, little-endian ARM and accepts `ET_REL`/`ET_DYN`; the binder calls `0x0C4F84C4`, whose recovered ARM relocation cases are recorded in `EVID-P65-MODLIB-001`. Undefined names are searched in a fixed 8-byte table at `0x3C3B9000` using the count at `0x3C3B8FF0`, then in the loaded-module list at `0x3C3D67C8`; the count is zero and list head is null at startup. A later count writer is not yet found, so do not assume system exports are available. `rmmod` resolves by name and reaches the module removal/free path. These are static findings, not a successful module-load test.

## Canopus artifact compatibility

The current generic C template emits a descriptor plus `.init_array`/`.fini_array`, and its build script runs `ld.lld -r` without an entrypoint option. A host-only module generated for P65 with the current template is ELF32 ARM little-endian `ET_REL`, `e_entry=0`, with one constructor and one destructor. Initially the verifier passed it (zero undefined symbols, 107 relocations), exposing a packaging safety gap.

The recovered stock loader invokes `base + e_entry` for `ET_REL` and walks two arrays for `ET_DYN`. For ET_REL, the initializer receives a pointer to module-record words +104/+108; rmmod later treats them as an unload callback and its context, calling `callback(context)`. Thus the machine-level entry/unload contract is recovered, although the vendor C type and edge-case semantics are not. The generic Canopus artifact is still **not safe to load on P65 as built**: its `ET_REL` entrypoint is zero, and its constructors may not run on this branch. The sample relocation set is within the recovered P65 relocation switch, but that alone does not establish compatibility. The verifier now fails closed on zero `e_entry` for targets whose profile selects `module_initialize`; the same temporary artifact fails with `ELF entrypoint is zero`. This guard prevents packaging but does not provide a P65 entry shim. See `EVID-P65-CANABI-001.json`.

At the Canopus integration boundary, `canopus-target-generated` can select the P65 identity pack, but `sdk/rust/canopus-target-private` has no P65 feature/backend and the generated P65 bindings contain only the identity guard. `scripts/build_canopus_supervisor.sh` also rejects P65 and its generated C config sets `CANOPUS_SUP_PLATFORM_COMPLETE=0`. Exact AP analysis has found candidate `app_install`, launcher, and LVX functions, including a 64-byte app descriptor copy and a five-argument `lvx_page_title_create`; P65 also has POSIX-shaped open/close/read/write wrapper candidates. Its LVX list path appears callback-driven through an internal list object (+96/+100/+112 callbacks), but the external struct/callback ABI is still unknown and does not match the v9 row macros directly. P65's `app_launcher_add` is no-argument, and only a `register_driver` ROM-trampoline candidate is known; the unregister counterpart and lifecycle/ownership remain unreviewed. No selectable or device-tested P65 Manager exists. See `EVID-P65-MANAGER-001.json` for the current backend/build gap and `EVID-P65-MANAGER-002.json` for candidate APIs.

## Read-only NSH runtime probe

The user supplied read-only console output from the previously firmware-confirmed device. The visible NSH `help` list includes `rmmod` and `lsmod` but omits `insmod`; nevertheless, `help -v insmod` returns `insmod <file-path> <module-name>`, and `help -v rmmod` returns `rmmod <module-name>`, both with exit code 0 and empty error output. The reason `insmod` is omitted from the default listing is unknown. `lsmod` prints the columns `NAME INIT UNINIT ARG NEXPORTS TEXT SIZE DATA SIZE` with no module rows. This confirms runtime help descriptors and that no module was listed at that observation, but does not prove successful loading, Canopus ABI compatibility, relocations, symbol imports, initialization, or unload behavior. The transcript was not accompanied by a raw capture or a same-session identity dump; see `EVID-P65-NSH-001.json` for that limitation. The static handler chain is now documented; do not execute `insmod` or `rmmod` until the entrypoint/import ABI and recovery path are established.
