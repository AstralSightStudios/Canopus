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

Build-info is at file offset `0x00B3F30C` / XIP `0x0CBFF30C`. The startup literals do not pass the supplied loader's `.data`/BSS validation, so no initialized-data or BSS mapping was inferred from those literals.

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

`targets/xiaomi-p65-3.100.043/` is an analysis-only exact target pack. It records the exact AP SHA, build identity, and three recovered identity strings; generated identity checks remain `PENDING`. Capabilities are empty. The loader profile and NSH command names are explicitly candidate-only; no target-private backend, callable firmware symbol, relocation allowlist, or device capability has been added.

Before P65 can load or call a Canopus module, independently trace the stock NSH/modlib call chain and relocation/constructor behavior, recover exact P65 subsystem ABIs, and run the identity/loader probes on physical hardware. Do not treat this static pack or its generated identity guard as device support.

## Follow-up static loader review

The active IDA database was verified against the exact AP SHA-256 and size above. The XIP strings include an NSH command/help pool near `nsh_command.c` (0x0C2844F0): `insmod` (0x0C2848C0) with `<file-path> <module-name>` usage, `lsmod` (0x0C284988), and `rmmod` (0x0C284B90), adjacent to ordinary shell commands. NuttX source-path strings also identify `module/mod_insmod.c`, `module/mod_rmmod.c`, `module/mod_modhandle.c`, `module/mod_procfs.c`, `modlib/modlib_{load,loadhdrs,registry,symtab}.c`, and `binfmt_execmodule.c`/`binfmt_execsymtab.c`. These strengthen the stock module-loader hypothesis, but do not prove reachable code: IDA records zero data xrefs to the tested command/source strings, full-width pointer searches and an ADR-target scan found no direct references, and only 13 of 46,916 XIP strings have any recorded xrefs. Generic `module` errors nearby are Lua 5.4 package-loader strings, not NuttX `modlib` evidence.

The current startup entry `_start` visibly copies one word from the boot-info alias at `0x2CBFF308` to `0x2019E724`; its following zero-fill loop has an empty range. No general `.data`/BSS copy region has been validated, so RAM-resident command or module tables may not be represented in the current IDA mapping. The next static milestone is to recover the NSH registration table and trace its handler into `modlib`, then identify relocation and symbol-table behavior. Until then, loader commands and profile fields remain candidate-only.

At the Canopus integration boundary, `canopus-target-generated` can select the P65 identity pack, but `sdk/rust/canopus-target-private` has no P65 feature or backend. There is therefore still no selectable P65 manager/runtime binding.
