# P65 firmware analysis — 3.100.043

**Current safety status: deployment withdrawn.** A user-reported Manager
failure followed by a Logo boot loop required REC data wipe. The former write
binding was pwrite with an unsupplied 64-bit stack offset; its correction does
not prove the cold-boot cause or safety. Native initialization, installation
and packaging are disabled. See [bootloop-incident.md](bootloop-incident.md)
and [offline build only](experimental.md). Historical statements below
about the absent backend, rejected build and mandatory pre-device gates are
superseded for experimental artifacts only; no hardware or production approval
is implied. The experimental corrections section records newer exact-AP ABI
findings, including the integer unload result, list factory and page +100 slot.

## Input identity

- OTA bundle: `~/develop/temp/miwear.watch.p65_v3.100.043_full_662aa7db.bin`
- OTA SHA-256: `662aa7db5dd7ec98c4b7da0f7bec3066478ff45563c57b4cf3f16a2558cbc987`
- Extracted AP payload: `~/develop/temp/p65_v3.100.043_vela_ap.bin`
- AP SHA-256 (the target pack identity): `a19b601477569765103eaf468b937bd8026b4d68ffa5c896bde222a79e475858`
- AP size: `11,793,540` bytes
- IDA 9.4 database: `~/develop/temp/p65_v3.100.043_vela_ap.bin.i64`
- Original IDA database SHA-256: `4d2eb1c18f4a14dd27c07482105f76c06d5ab9c057745aef132da34516bcc6e5`

The on-disk IDB is a mutable analysis artifact and has since changed to SHA-256 `6c2c3ac059a1b32f243573fda1aa16050e8b0c50332fdd6389203c3db852fd59`; the AP payload SHA remains the authoritative firmware identity. The later unregister-driver negative scan used a temporary copy of that current database; its AP bytes match the pinned payload hash. The OTA is a ZIP container despite its `.bin` suffix. It contains `vela_ap.bin`; the custom loader was applied to that extracted payload, not the outer archive.

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

`targets/xiaomi-p65-3.100.043/` is an analysis-only exact target pack. It records the exact AP SHA, build identity, and three recovered identity strings; generated identity checks remain `PENDING`. Capabilities are empty. The NSH `insmod`/`rmmod` command names and usage are now recovered, while the loader profile remains non-deployable; no target-private backend, callable firmware symbol, or device capability has been promoted. The verifier's relocation allowlist now mirrors the exact AP modlib switch but remains static-only.

The stock NSH `insmod`/`rmmod` path, ELF relocation/symbol resolution, AP 0x3C data/BSS initialization, and machine-level ET_REL initializer/unload-callback contract are now statically traced (see `EVID-P65-MODLIB-001.json`, `EVID-P65-MEMORY-002.json`, and `EVID-P65-CANABI-001.json`). The static symbol-table count is zero at startup; any later writer, usable Canopus imports, target-private backend, failure recovery, and device behavior remain unverified. Do not treat this static pack or its generated identity guard as device support.

## Follow-up static loader review

The active IDA database was verified against the exact AP SHA-256 and size above. The XIP strings include an NSH command/help pool near `nsh_command.c` (0x0C2844F0): `insmod` (0x0C2848C0) with `<file-path> <module-name>` usage, `lsmod` (0x0C284988), and `rmmod` (0x0C284B90), adjacent to ordinary shell commands. NuttX source-path strings also identify `module/mod_insmod.c`, `module/mod_rmmod.c`, `module/mod_modhandle.c`, `module/mod_procfs.c`, `modlib/modlib_{load,loadhdrs,registry,symtab}.c`, and `binfmt_execmodule.c`/`binfmt_execsymtab.c`. The strings alone did not prove reachability; the exact handler-to-loader path is now recovered separately below. IDA still records zero data xrefs to the tested strings, and only 13 of 46,916 XIP strings have any recorded xrefs. Generic `module` errors nearby are Lua 5.4 package-loader strings, not NuttX `modlib` evidence.

The `_start` routine copies one word from the boot-info alias at `0x2CBFF308` to `0x2019E724`; its following small zero-fill loop has an empty range, which is why the custom IDA loader did not infer `.data`/BSS. A later reset-stage routine, `0x0C0C0314`, is called from `0x0C0C0394` during startup: it copies cached-XIP `[0x2C0EFDC4, 0x2C4C1E44)` to writable VMA `[0x3C000000, 0x3C3D2080)` and clears BSS `[0x3C3D2080, 0x3C4DF960)`. The module-list head `0x3C3D67C8` is in that zeroed range. The active static-symbol count at `0x3C3B8FF0` is initialized from source `0x2C4A8DB4` as zero. The physical RAM type is not identified. See `EVID-P65-MEMORY-002.json`.

The earlier same-offset AP-flash alias hypothesis remains false, but the AP reset-stage copy/zero code now explains the lower 0x3C addresses as writable initialized-data/BSS VMAs. Runtime NSH metadata at `0x3C194F2C` maps back to source `0x2C284CF0`; its descriptor name pointer for `insmod` at `0x3C194AFC` maps to the literal `insmod` string at XIP `0x0C2848C0`. The physical memory type (PSRAM or other RAM) remains unproven. The OTA's separate ET_EXEC images at `0x3C800000`/`0x3C900000` are unrelated to this lower VMA range. See `EVID-P65-MEMORY-001.json` for the false-alias check and `EVID-P65-MEMORY-002.json` for the recovered startup mapping.

## Static module path recovered

The XIP command descriptor table source at `0x0C284CF0` spans 72 records and is copied to runtime VMA `0x3C194F2C`. The `insmod` row at `0x0C284EF0` points to handler `0x0C5B15E4` and has argument spec `0x303`; its runtime name pointer `0x3C194AFC` maps through the startup copy to source string `insmod` at `0x0C2848C0`, and its usage pointer resolves to `<file-path> <module-name>` at `0x0C2848C8`. The handler forwards `argv[1]` and `argv[2]` to `0x0C4C49AC`, matching runtime `help -v insmod`; the command-name mapping is now directly recovered.

`0x0C4C49AC` opens and validates ELF, allocates/loads sections, calls `0x0C4F901C`, then initializes and registers the module. The validator checks ELF32, little-endian ARM and accepts `ET_REL`/`ET_DYN`; the binder calls `0x0C4F84C4`, whose recovered ARM relocation cases are recorded in `EVID-P65-MODLIB-001`. Undefined names are searched in a fixed 8-byte table at `0x3C3B9000` using the count at `0x3C3B8FF0`, then in the loaded-module list at `0x3C3D67C8`; the count is zero and list head is null at startup. A later count writer is not yet found, so do not assume system exports are available. `rmmod` resolves by name and reaches the module removal/free path. These are static findings, not a successful module-load test.

## Canopus artifact compatibility

The generic `init_array` C scaffold still produces `ET_REL e_entry=0`; the legacy verifier experiment showed that shape is unsafe on P65. The CLI now reads `loader_profile.constructor_discovery`: `canopus module new --lang c --target xiaomi-p65-3.100.043` emits a `canopus_module_initialize` entry shim and unload-pair outputs at `+104/+108`, omits init/fini attributes, and links with `-e` plus `scripts/canopus_module_sections.ld`. A generated host artifact is ELF32 ARM `ET_REL`, has e_entry `0x1` matching the Thumb function symbol, zero undefined symbols, 107 relocations and no init/fini arrays; it passes the strengthened P65 verifier. SHA-256: `25e863f882f11dc65518d2c84bb5ab8ee19de20902f12db66430eca4de8a6056`. This is scaffolding verification, not a device load or Manager artifact. The Supervisor module glue also links under the P65 entry profile with fake platform stubs and passes the verifier (e_entry `0x1`, zero undefined, 38 relocations); the stubs deliberately have no driver hooks, so this does not demonstrate working `/dev/canopus`. The Rust template fails closed for `module_initialize` until it gains a C shim pipeline.

The recovered stock loader invokes `base + e_entry` for `ET_REL` and walks two arrays for `ET_DYN`. For ET_REL, the initializer receives a pointer to module-record words +104/+108; rmmod later invokes `callback(context)`. The profile-aware C scaffold now emits this host-verified contract, and the verifier requires e_entry to match a defined executable function symbol and Thumb address. This does not prove NuttX runtime execution, callback return behavior, device imports, or recovery. `scripts/build_canopus_supervisor.sh` still rejects P65, `sdk/rust/canopus-target-private` still has no P65 backend, and the P65 C Supervisor entry wrapper is not yet wired into that production build. The stock startup import count remains zero and the actual Manager API set is incomplete; do not deploy or load this artifact. See `EVID-P65-CANABI-001.json`.

At the Canopus integration boundary, `canopus-target-generated` can select the P65 identity pack, but `sdk/rust/canopus-target-private` has no P65 feature/backend and the generated P65 bindings contain only the identity guard. `scripts/build_canopus_supervisor.sh` still rejects P65 and its generated C config sets `CANOPUS_SUP_PLATFORM_COMPLETE=0`. Supervisor C glue now has a conditional P65 entry/unload wrapper that requires register and unregister hooks, but the production build case does not define/link it. Exact AP analysis has found candidate `app_install`, launcher, and LVX functions, including a 64-byte app descriptor body with null-guarded entry candidate 0x0C2418A8/Thumb 0x0C2418A9 (the apparent `STRH` at 0x0C2418AC is the second halfword of a preceding `BEQ.W`); external callability and return semantics remain unresolved. That body calls activity-manager global slot +12 for each page descriptor and stores returned page-object pointers, but the ROM callback is unmapped. It also has a five-argument `lvx_page_title_create`; P65 file APIs now include exact AP call-boundary candidates for open/close/read/write, mkdir/stat/rename/unlink/rmdir, and opendir/readdir/closedir; the mutators are ROM trampolines whose bodies are unmapped, and no fsync callsite was found, so crash-durable store semantics remain unproven. The AP contains `/data`, `/tmp`, `/system`, and `/data/ota.zip` path literals, but no read-only analysis proves a dedicated Canopus directory is writable or persistent. A host-tested P65 dual-slot CRD1 registry envelope adds sequence/CRC/read-back fallback, but injected slot I/O does not prove P65 filesystem durability. Its LVX list path is callback-driven through a native list object: an internal 40-byte callback table is copied to object offsets +92..+128. The code reveals key/create/update/count callbacks at +96/+100/+104/+112 and optional scroll/layout hooks, but object construction, ownership, cleanup and thread rules remain unverified; this is not a supported external ABI or a direct match for the v9 row macros. P65's `app_launcher_add` is no-argument. A QuickApp removal candidate `0x0C7EC53C` takes an app id and removes records from list head `0x3C3BF9DC`, separate from `app_install`'s registry at `0x3C3CF91C`; it cannot yet serve as Manager teardown. `sub_C2415C0` is named `packagemanager_app_unregister`; its record-link offsets +0/+4, id +16, nested lists +48/+52, and registry 0x3C3CF91C overlap the `app_install` copy layout, and the unregister candidate iterates the same page_registry through global activity-manager slot +16 (ROM `0x1C132368`), matching the install body’s slot +12 page-object factory. The teardown pairing is structurally strong: `sub_C2415C0` uses the same package-name key to remove the app record, and `sub_C241420(package_name)` appears to recover that record from map `0x3C3CF92C`. Neither helper has a direct AP caller or an approved public-call contract. A raw static pointer scan also finds no reference to app_install/lookup/unregister entry candidates; the inspected app_install name literal at 0x0C241A68 feeds its cleanup log, not a demonstrated export table. The active-list scan short-circuits an already-active 16-bit app ID as a no-op. `sub_C2415C0` may defer the app record to 0x3C3CF920 and emit event 28; adjacent `sub_C241A78` re-registers pages and emits event 29, so it is a resume path, not a callback-drain proof. `0x0C2216B4` is an `activitymanager_page_register` candidate taking one descriptor pointer and inserting into the global page list at `0x3C3CF53C`; the paired `0x0C22212C` unregister candidate synchronously dispatches lifecycle callbacks and unlinks page links. Neither is directly called by AP code or linked to `app_install` yet. Its page word +36 is incremented/decremented as a reference count by `0x0C221C48`; if still nonzero, downstream `0x0C221388` queues final destruction at `0x3C4941A8`. `0x0C221E78` later drains the queue, but has no direct AP callers. The unload-time callback drain is therefore still unsafe to assume. Only a `register_driver` ROM-trampoline candidate is known; a Band 10 `unregister_driver` address was falsified at the same P65 address, no AP counterpart was found, and a ROM-side counterpart remains unknown. A P65-local `canopus_client_io_v1` now forwards full CPC2 records to the in-process Supervisor device handlers, including query payloads, without creating `/dev/canopus`; this makes an in-process P65 UI architecture plausible, but target bootstrap, app/page teardown, package/module I/O and external client transport remain open. A firmware-independent P65 row adapter owns copies of ephemeral snapshots and bridges P65 click events through generation-checked Canopus UI dispatch. Its 40-byte LVX callback-table bridge is host-tested with injected fake ops (+96/+100/+104/+112), but no P65 firmware create/refresh/row operations or target app are bound. The host-only Manager controller composes `canopus_manager_native`, local CPC2 client I/O, and the injected LVX adapter; a fake integration navigates overview→modules→detail and confirms ENABLE through the in-process Supervisor. It does not register a P65 app/page or bind firmware calls. No selectable or device-tested P65 Manager exists. See `EVID-P65-MANAGER-001.json` for the current backend/build gap and `EVID-P65-MANAGER-002.json` for candidate APIs.

Address-map note: the supplied full OTA archive includes `vela_ap.bin`, `vela_audio.bin`, `vela_ota.bin`, `vela_resource.bin`, and `vela_sensor.bin`. `best1503_vela.py` maps `vela_ap.bin` to XIP `0x0C0C0000` with cached/non-cached aliases `0x2C0C0000`/`0x280C0000`; `vela_ota.bin` is a separate ARM ELF whose PT_LOAD mappings include `0x3C800000` and `0x00200200`, not `0x1C...`. AP trampolines target `0x1C...` addresses outside the AP mapping. In this report, “ROM target” is shorthand for that address-map inference; whether `0x1C...` is silicon ROM or another resident system mapping is not independently verified. This is not a claim that the provided full OTA package is incomplete.

## Experimental integration corrections

A fresh decompilation of exact-AP `0x0C4C4AE4` shows that the module unload
callback at record+104 returns a signed integer. A negative result branches to
errno handling **before** callback clearing, registry unlink, or allocation
release; nonnegative results permit removal. The C scaffold now propagates its
stop result instead of publishing a void callback. The experimental resident
Supervisor returns `-16` to keep firmware-held callbacks alive until reboot;
this follows static code evidence, not a device test.

Additional exact-AP review corrects an earlier LVX interpretation: all-list
caller `0x0C764170` obtains the list from `0x0C8BC758(parent)`, stores that
object at `0x3C4509D8`, then calls `0x0C8BE008(list, text)`. Other callers
`0x0C7524A8` and `0x0C752F84` pass localized string results to the second
function, identifying a title setter rather than a factory with an opaque
class descriptor. Experimental bindings must not use `0x0C8BE008` as create.
Page UI destruction at `0x0C22126C` dispatches page word 25 (+100), not the
Band10 +96 slot. Errno helper `0x0C4F793C` returns current-task+40 or a fallback
address; `0x0C25823C` is its AP trampoline used by fd wrappers.

## Host file-I/O adapter for the P65 registry

`manager/target/p65/canopus_manager_p65_registry.{h,c}` now includes an
injected file adapter between the existing dual-slot store and normalized
open/size/read/write/close operations. It handles short transfers, rejects
zero-progress and invalid transfer counts, closes every successfully opened
handle on failure, propagates close errors, and reports oversized slot lengths
without overflowing the supplied buffer. Only an exact-sized registry record
may be written. Missing read-only files are distinguished from I/O failures;
invalid configuration clears the adapter rather than retaining old hooks.

Integration order (all objects, paths, and operation tables must remain alive):

```c
/* file_ops are caller-provided normalized wrappers, not raw firmware APIs. */
canopus_manager_p65_registry_files_init(
    &files, &file_ops, io_cookie, slot0_path, slot1_path);
canopus_manager_p65_registry_store_init(
    &store, &canopus_manager_p65_registry_file_io, &files);
/* Check both return codes before using store as the persist/restore cookie. */
```

The provider must create/truncate only the selected slot, supply an exact file
size, normalize errors, and ensure both paths refer to distinct files in an
existing directory. Access is serialized; external writers and path aliases are
not supported. No fsync, atomic-rename, power-loss durability, target path, or
firmware function address is assumed. This adapter does not install a P65
filesystem provider or make the Supervisor deployable.

Host regression tests exercise short-transfer round trips, fallback after a
failed inactive-slot write, read/write/size/open/close failures, invalid transfer
counts, missing files, rejected configurations, and bounded oversized reads.
The complete host suite, ASan/UBSan suite, and Cortex-M33 freestanding compile
passed. These are host results, not P65 device evidence.

## P65 LVX list callback ABI (internal only)

The P65 `lvx_list` path has a 40-byte configuration table copied by code at `0x0C2358C4` to list-object offsets `+92..+128`. `0x0C2401D4` supplies built-in ROM Thumb callback pointers; the callback bodies are outside the AP database. A concrete `sub_C764170` all-list caller creates a list widget through `0x0C8BE008(parent, 0x3C19D9EC)`, binds callbacks with `0x0C8BEF60(list, table)`, then finalizes via `0x0C8BEBC0(list)`. Its table binds key/type `sub_C763B84`, create `sub_C764638`, update `sub_C7644A0`, count `sub_C763B34`, and count-like helpers at `+116/+120`. The row callbacks use P65's all_list storage and class `listview`; their ABI is now concrete, but their data model is not a direct Canopus snapshot adapter. `sub_C764638` creates each row via ROM class-create trampoline `0x0C8BEB70(class=0x3C21B55C,parent)`, then creates label children through `0x0C78BCB4` using label-class descriptor `0x3C224844`. The listview class data at XIP `0x0C30B320` names `listview` and its packed `+32` size decodes to 76 bytes. These are row-building candidates for a target adapter; their ownership and event drain must still be proven.

| List offset | Observed role / ABI | Evidence note |
|---:|---|---|
| `+92` | User context pointer | Passed as the final argument to list callbacks |
| `+96` | `(list, index, ctx) -> key` | Return is consumed as an 8-bit row-cache key |
| `+100` | `(list, index, ctx) -> row` | Creates a row object when not cached |
| `+104` | `(list, row, index, ctx) -> void` | Refreshes/rebinds cached row objects |
| `+108` | Unknown / unused in examined list paths | Copied but no callsite found |
| `+112` | `(list, ctx) -> count` | Supplies virtual list item count |
| `+116`, `+120` | `(list, ctx) -> int` in one caller | All-list config binds count-like helpers here; list event code invokes these slots at scroll boundaries and ignores return; general role unresolved |
| `+124` | `(list, ctx) -> extent` | Replaces per-row total-extent calculation |
| `+128` | `(list, ctx) -> geometry value` | Feeds scroll geometry calculations |

A concrete firmware caller (`sub_C764170`) creates a list with `0x0C8BE008(parent, 0x3C19D9EC)`, installs the 40-byte callback table via `0x0C8BEF60`, then finalizes with `0x0C8BEBC0`. It binds `sub_C763B84` for key/type, `sub_C764638` for row creation, `sub_C7644A0` for row update, and `sub_C763B34` for item count. The row factory creates a `listview` row through `0x0C8BEB70` with class descriptor `0x3C21B55C` (runtime VMA maps to XIP `0x0C30B320`; its `+20` name pointer resolves to `listview`, `+32` encodes a 76-byte instance, and `+36` embeds the calendar notification asset path), adds labels via `0x0C78BCB4`, sets label text via `0x0C78C878`, and adds click callback event 7 through `0x0C8BCE78`. These are concrete implementation candidates; the sample callbacks depend on P65's own `all_list` data model and do not directly consume a Canopus Manager snapshot. Internal `sub_C236E18(list,a2,a3)` reinitializes mode-1 lists by re-querying +112 count, rebuilding visible rows via `sub_C2340C8` (+100/+104), and recalculating extent; it is called by setup and `sub_C236FE8`, whose event trigger has no direct AP caller or pointer literal. This suggests an internal refresh path but does not establish a safe target-call contract. Cache reset helper `sub_C233200` moves row objects to the list+132 free pool through ROM trampoline 0x0037F61C; the AP path does not remove the separate event-7 click callback there. A ROM event callback is also registered for codes 1/2/3/8, but its body and pool/destructor behavior are unavailable, so row bindings must remain resident until teardown is proven.

The default table points to `0x1C15015D`, `0x1C15034D`, `0x1C150591`, `0x1C15016D`, `0x1C1505E5`, and `0x1C1505F5` for `+96`, `+100`, `+104`, `+112`, `+116`, and `+120` respectively. These are ROM-side defaults, not usable P65 AP exports. A separate scroll helper candidate is exposed by `lvx_list_refresh_to_bottom`: AP trampoline `0x0C8BD798` jumps to ROM `0x1C148888` with one list pointer, but it does not create/update rows. `sub_C23FF40` is a possible LVX class-constructor callback: raw code ignores R0, uses R1 as the object, clears state at `+1340/+1344/+1348/+1352`, sets `+1356`, and applies more list setup. That argument shape is consistent with a class constructor hook, but it has no direct AP caller; raw scans found no `0x0C23FF41`, `0x2C23FF41`, or `0x3C23FF41` pointer in the AP, so class registration/public creation is unproven. The LVGL core path confirms the callback convention: `sub_C0D15D4` allocates from class descriptor size at `+32`; `sub_C0D16C8` invokes class `+4` callbacks as `(class, object)`; and `sub_C0D17D4` invokes class `+8` destructors. The generic descriptor at VMA `0x3C1DE5C8` (source/XIP `0x0C2CE38C`) is named `obj`, not `lvx_list`; its `+32` word `0x34A` decodes to a 52-byte base object. No list descriptor linking `sub_C23FF40` or a matching list destructor has been found. List code reaches state at `+1364`, but exact instance size and allocator remain unknown. Row-item candidates also appear in the AP: `sub_C219454` (7 args) initializes a caller-provided item object, populating child slots including `+84/+88/+92/+96`, while `sub_C2196F8` (6 args) mutates existing child/style state. Neither has a direct code/data xref tying it to list callbacks `+100/+104`, and the constructor-like function does not allocate the outer item. Although the internal callback conventions are now recoverable, the native list object's construction/size contract, allocator, callback lifetime, thread affinity, row ownership, and teardown path are not; do not bind this internal ABI as a target API yet.

## Read-only NSH runtime probe

The user supplied read-only console output from the previously firmware-confirmed device. The visible NSH `help` list includes `rmmod` and `lsmod` but omits `insmod`; nevertheless, `help -v insmod` returns `insmod <file-path> <module-name>`, and `help -v rmmod` returns `rmmod <module-name>`, both with exit code 0 and empty error output. The reason `insmod` is omitted from the default listing is unknown. `lsmod` prints the columns `NAME INIT UNINIT ARG NEXPORTS TEXT SIZE DATA SIZE` with no module rows. This confirms runtime help descriptors and that no module was listed at that observation, but does not prove successful loading, Canopus ABI compatibility, relocations, symbol imports, initialization, or unload behavior. The transcript was not accompanied by a raw capture or a same-session identity dump; see `EVID-P65-NSH-001.json` for that limitation. The static handler chain is now documented; do not execute `insmod` or `rmmod` until the entrypoint/import ABI and recovery path are established.
