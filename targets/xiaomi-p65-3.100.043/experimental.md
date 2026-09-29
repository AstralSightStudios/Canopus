# P65 experimental resident integration

This path is separate from the registered identity-only generated SDK pack.
The target-private Rust facade exposes the Band11-named API surface with
explicit unsupported fallbacks; it does **not** claim Band11 functional parity
or production approval. It builds without a
hardware-validation prerequisite. The exact AP hash, zero-import ELF checks,
entrypoint rules, and recovered relocation allowlist remain mandatory.

## Build

```sh
CANOPUS_P65_FIRMWARE=/path/to/p65_v3.100.043_vela_ap.bin \
  bash scripts/build_p65_supervisor.sh
```

The input may also be the original full OTA ZIP despite its `.bin` suffix;
its unique `vela_ap.bin` is hashed in memory without extracting arbitrary paths.

Outputs under `watchfaces/canopus-installer/build/xiaomi-p65-3.100.043/`:

- `canopus_supervisor.elf`: native ARM ET_REL resident module.
- `installer/canopus-p65-resources.zip`: deterministic flat watchface resource
  bundle containing `main.lua`, `supervisor.bin`, and `manager_icon.bin`.
- `installer/build-info.json`: exact target/AP identity, resource sizes/hashes,
  and archive hash (not included inside the resource ZIP).

Use the existing watchface packing/install workflow for the resource ZIP. It
is **not** a flashable watchface image. The standalone G0 watchface is removed;
this installer loads the actual Supervisor and registers the actual Manager.

Open the packed watchface, tap Install Supervisor, then Register Manager on a
separate event turn. Register/Publish module apps are distinct optional stages.
There is no auto-load or unload action. Native identity checking still precedes
firmware registration; the Lua version check is not a whole-image hash check.

The build verifies the exact AP SHA-256 and creates a separate verifier overlay
under the output's `targets/` directory. That overlay preserves target.toml
byte-for-byte and allows only individual addresses enumerated by the private
`manager/target/p65/canopus_p65_abi.h`; it does not enlarge address ranges,
disable relocation checks, or approve generated SDK callables. Generated
experimental symbol records deliberately remain PENDING. Use this overlay to
re-run verification of this experimental module, not the production pack.

## Implemented experimental bindings

- Stock P65 `module_initialize` entrypoint, exact identity guard, integer-return
  unload callback. Unload returns `-EBUSY`; reboot is the teardown path.
- Shared Supervisor CPC2 device and installer endpoint, signed receipt/artifact
  verification, module lifecycle intents, registry persistence and diagnostics.
- P65 fd open/close/read/write/errno, rename/unlink and four-argument driver
  registration. These are exact-AP static call boundaries, not hardware results.
- P65 64-byte app descriptor, app registry lookup, no-argument launcher refresh,
  page lifecycle with UI destruction at +100, callback-driven native list,
  stock listview rows/labels/events, and shared semantic Manager navigation.

The UI preserves the existing Manager flow, rather than introducing a probe UI:

```text
Canopus
  Overview -> Modules -> Module detail
                         Enable / Disable / Remove
                         Confirmation / result
```

Registration failure after a firmware callback escapes must not release the
module. The resident entry therefore retains the image even if registration
partially fails. `insmod` success alone is not proof that both endpoints or the
Manager exist. Never retry installation without checking current state; reboot
clears the resident image and any partially registered endpoints.

## Remaining integration work

- A flat resource bundle is packaged, but no device-specific flashable
  watchface container is generated. The installer stages `/data/canopus`, its
  inbox and Manager icon and checks icon readback before loading native code.
- The P65 C scaffold now publishes its descriptor over `/dev/canopus` with
  a single 40-byte CMR1 write. Registration is accepted only during a matching
  Supervisor load transaction; direct shell loading outside it fails. Once
  published, the child image refuses `rmmod` until reboot because the Supervisor
  retains its descriptor. A close failure after publication does not free the
  image. A generated ARM scaffold passed the experimental verifier with zero
  imports, 115 relocations and no constructor/destructor arrays. Signed receipt
  generation and end-to-end device installation remain separate from this host
  artifact test.
- Notification-center delivery now uses P65's own 88-byte layout, recovered
  from `0x0CA5FF3C..0x0CA60084` setters and `0x0CA5FCAC` deep-copy code, with
  title/source/body/icon strings and no callbacks or popup context. Insertion
  `0x0CA60098` is bound experimentally; its raw result is retained in diagnostics
  because flags82-dependent return semantics do not establish delivery success.
  Popup display and watchface cleanup are not bound.
- The experimental platform now binds the P65 dual-slot registry to recovered
  fd calls. Only the inactive slot is unlinked/recreated; short writes are
  completed and readback/CRC verifies a save. Reads count oversized records
  without overflowing scratch (bounded to 64 KiB). The actual provider is
  host-tested through fake exact-address dispatch, including failure fallback.
  Close/readback still is not an fsync/power-loss durability guarantee.
- Native rendering, launcher visibility and callback ownership are experimental.
  No device execution, screenshot, notification or persistence result is claimed.

The standard Supervisor builder now dispatches the P65 target to this separate
experimental build, without staging production approval. The Rust target-private
crate has an experimental P65 feature; its unsafe address constants are generated
from the same private C ABI header, not copied from Band11.

## Rust API coverage

```sh
cargo test --manifest-path sdk/rust/canopus-target-private/Cargo.toml \
  --no-default-features --features target-xiaomi-p65-3-100-043
cargo check --manifest-path sdk/rust/canopus-target-private/Cargo.toml \
  --no-default-features --features target-xiaomi-p65-3-100-043 \
  --target thumbv8m.main-none-eabi
```

| Area | Experimental P65 status |
|---|---|
| Exact identity | Generated exact-AP guard |
| CPC2, module intents, signed installer receipts | Shared native Supervisor implementation |
| VFS | open/create/close/read/write/errno/unlink/rename bound |
| Driver registration | P65 four-argument ABI; unregister unsupported, reboot-only |
| Native UI | labels, size/align, events, event accessors and list bridge bound |
| App registry | P65 64-byte app + package-name lookup; numeric-ID lookup unsupported |
| Page lifecycle | P65 +100 UI-destroy slot; generic activity_finish unsupported |
| Notification center | P65 88-byte deep-copy input, null callbacks; popup unsupported |
| Bluetooth/L2CAP/SDP | Unsupported fallback, **not implemented** |
| Interconnect, unrecovered LVGL helpers, VFS seek/ioctl/clock | Unsupported fallback |

All 95 public function names present in the Band11 backend are available through
the P65 facade or its existing unsupported fallback. This is source-surface
coverage, not 95 working firmware operations. About twenty common methods have
P65 overrides; the rest remain error/null/no-op fallbacks as specified by their
return types. Always check `capabilities()`. Compile-only inherited Bluetooth
structures do not approve P65 Bluetooth layouts. The P65-specific
`app_lookup_package` and `launcher_refresh` avoid pretending its ABI is Band11's.
The host facade tests, P65 Thumb compile and Band11 .155 Thumb regression compile
pass. The full unrelated Python suite is not green: two Band11 cases lack its
pinned host luac and another rejects errno_location metadata. Core generated
stability also detects stale Band10 .043 bindings. Those inputs were not changed
by this port; these failures are not waived or counted as P65 passes. Rust module scaffolding still requires a custom C entry pipeline; the
P65 CLI's verified scaffold is currently C only.

 The original analysis report
contains historical blockers; newer corrections are in its experimental
integration section and this document.
