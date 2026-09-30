# P65 deployment withdrawn: Logo boot-loop incident

**Do not install, reload, or reboot to retry the P65 experimental installer.**
The device boot-loop cause is unresolved. An address correction and passing
host tests do not authorize another device attempt.

## User report

- Supervisor installation returned normally in the installer.
- Register Manager displayed `Operation failed: 4294967291` followed by the
  unsafe instruction `Reboot before retrying`.
- Following that instruction caused repeated restarts at the boot Logo,
  before reaching the watchface.
- The user recovered through REC by clearing data/factory reset.

These are user-reported observations, not a device trace. The displayed value
is signed `-5`. Neither a reset reason nor a pre-reset filesystem snapshot is
available. Data-wipe recovery indicates a data-dependent condition, but does
not identify a particular file or prove what wrote it. The exact packed
watchface/device artifact hash was not supplied.

The local candidate corresponding to the previous handoff was preserved, not
deleted, under ignored `build/p65-incident-0f62381/`:

- Original resource ZIP SHA-256:
  `1c1aa26693e26a737f76d0ec3b26abdcfce60176875a0c35dc6ea5c75b3de674`.
- Original Supervisor SHA-256:
  `403d942c0c60a0f4078393859cc1a608b9a9c1a5e6706833c4c3fcf1d7537f46`.

These local hashes do not prove that the device ran identical bytes.

## Confirmed ABI defect

Exact AP SHA-256:
`a19b601477569765103eaf468b937bd8026b4d68ffa5c896bde222a79e475858`.

The private header incorrectly bound `CANOPUS_SUP_NUTTX_WRITE` to Thumb
`0x0C248455` and called it as `write(fd, buffer, count)`. That body is **pwrite**:

1. It resolves the fd through `0x0C2444C4`.
2. It obtains the current position through internal `file_seek` at `0x0C2478D0`.
3. `LDRD R2,R3,[SP,#48]` at `0x0C248480` reads the offset at the caller's entry
   SP (the preceding frame is 48 bytes). AAPCS aligns the fourth, 64-bit
   argument to the stack after the three 32-bit arguments.
4. It seeks, calls file-object write at `0x0C249854`, and restores the position.

No offset was supplied by the registry provider or Rust facade. This can use
unrelated caller-stack bytes as a file offset, produce failures, or write at an
unintended position. Short writes also do not advance the caller's cursor as
ordinary write would.

Plain fd write is even entry `0x0C2497F0`, Thumb `0x0C2497F1`. Its body retains
R1/R2, resolves the fd, checks the write-permission bit, invokes inode fops +12,
releases the file reference, and converts negative returns to errno/-1. It does
not seek or read an extra stack argument. Exact AP callers at `0x0C11E5A8`,
`0x0C132E88`, and `0x0C14B682` pass fd/buffer/count. The shared private address
header is corrected; Rust constants are generated from it.

`test_p65_firmware_io.py` executes both real AP Thumb bodies in Unicorn 2.1.4.
Only fd lookup/release, errno and the downstream driver are modeled. A poisoned
caller stack reproduces a write at `0x0123456700001000` through the old binding;
the corrected write produces sequential offsets 0/3/6 on short transfers. Error
paths and callee-saved registers are checked. No real file/device I/O occurs.
This is evidence for the ABI error, **not filesystem or cold-boot safety**.

## Why the screenshot alone cannot locate the failure

`-5` had two meanings:

- A nonzero native stage return becomes generic Supervisor stage error `-5`.
  This skips the outer registry save, but cannot undo escaped stock callbacks.
- Native stage success is followed by a registry save, even if the module table
  is unchanged. Provider write failure was also `-5`, propagated unchanged.
  Native app registration/Launcher refresh may already have occurred.

The corrected platform maps provider errors into the Supervisor registry
error domain (`-13` for write, `-17` for readback). Module app publication
stages also preserve a registry-save error after callbacks have run, rather
than replacing it with generic -5. Lua displays signed errors and the existing
CNT1 identity/app fields, including in the withdrawn read-only status view. This cannot retroactively resolve
the original screenshot's ambiguity.

Known persistent paths before/during installation are the staged Manager icon
and `/data/canopus/registry0.bin` / `registry1.bin`. The source has no demonstrated
startup autoload registration. Stock app event 27/Launcher effects, boot settings,
filesystem behavior with a bogus pwrite offset, and possible memory corruption
remain open investigation paths. Do not assume that deleting Canopus registry
files is sufficient recovery; do not call guessed unregister or rmmod APIs.

## Containment and validation

- `canopus_p65_safety.h` disables deployment, without environment/CLI overrides.
- The native module entry clears the unload pair and returns `-130` **before**
  constructor, identity, file I/O or device registration. Stage/package and
  direct Manager registration also refuse while disabled.
- Both normal build entry points and the standalone packager refuse before
  touching outputs. The previous installable output path contains only a
  withdrawal notice; original artifacts are preserved separately.
- Lua mutation buttons are disabled. Read status does not issue the CPC2-to-CPC1
  handoff QUERY while withdrawn. No reboot/retry recovery promise remains.
- `sh scripts/build_p65_supervisor.sh --offline-audit` builds a guarded ELF under
  `build/p65-offline-audit/`, never a watchface/resource pack. Its native entry
  refuses initialization. **Do not install this ELF.**
- Host/ASan/UBSan, private Rust host/Thumb, six experimental-policy tests,
  six exact-Thumb write regressions and the offline ELF verifier pass.
  This does not claim the complete workspace suite or any device recovery gate.

Before deployment can resume, identify the cold-boot trigger with exact startup
and storage evidence, audit every remaining mutated firmware boundary, eliminate
failed-save commit ambiguity and re-entry hazards, and establish a recovery plan
that does not depend on another destructive user trial. No new device package is
provided by this correction.
