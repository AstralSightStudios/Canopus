# P65 experimental resident integration

This path is separate from the registered identity-only SDK pack. It is not a
production approval and does not claim Band11 API parity. It builds without a
hardware-validation prerequisite. The exact AP hash, zero-import ELF checks,
entrypoint rules, and recovered relocation allowlist remain mandatory.

## Build

```sh
CANOPUS_P65_FIRMWARE=/path/to/p65_v3.100.043_vela_ap.bin \
  bash scripts/build_p65_supervisor.sh
```

Output: `watchfaces/canopus-installer/build/xiaomi-p65-3.100.043/canopus_supervisor.elf`.
This is an ARM ET_REL module, not a flashable image or a packaged watchface.
The standalone G0 watchface has been removed.

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

- No P65 installer watchface is packaged yet; this is a native build candidate.
- `/data/canopus`, its inbox and Manager icon must be staged by an installer.
- The stock child-module path expects modules to register their Canopus
  descriptor over `/dev/canopus`. The generic P65 C scaffold does not yet do
  that, so it is not an end-to-end Manager-installable child artifact.
- Firmware notifications return `-ENOSYS`; the P65 message/context ABI must be
  recovered rather than copied from Band11. Watchface cleanup is not bound.
- This build currently uses the shared registry file implementation, not the
  new injected P65 dual-slot provider. Close/rename is not an fsync guarantee.
- Native rendering, launcher visibility and callback ownership are experimental.
  No device execution, screenshot, notification or persistence result is claimed.

The standard Supervisor builder and general Rust target-private backend are
not promoted by this separate experimental build. The original analysis report
contains historical blockers; newer corrections are in its experimental
integration section and this document.
