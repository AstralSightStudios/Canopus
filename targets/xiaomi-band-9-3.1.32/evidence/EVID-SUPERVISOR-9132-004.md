# Band 9 3.1.32 Supervisor Identity Guard Rejection

**Target:** `xiaomi-band-9-3.1.32` (`vela_ap.bin`, SHA-256 `9c02dab4020b2cc9666ee7d34cf27d311b76aadcec519a38361bbcbd94c53264`)

**Device observation:** `2026-08-31`, installer status `LOAD failed: no /dev/canopus`

## Result

The corrected RWX-first/RX-final stage-0 publication returned without the prior
`CFSR=0xB2` exception-stacking fault. The installer completed its stage-1 command
and cleanup path, then failed the post-load `/dev/canopus` presence check. This
moves the observed failure beyond the rejected ROX-first publication, but does
not by itself prove which stage-2 operation completed because the old stage-1
entry discarded its return value.

Static review found a deterministic Supervisor constructor rejection before
`register_driver`:

```text
canopus_sup_ctor()
  -> canopus_identity_guard()
  -> mismatch
  -> return before canopus_supervisor_init/register_device
```

The approved firmware-version address `0x0c5fc5c1` points into a build.prop
property line, not to an isolated NUL-terminated value. Exact IDB memory reads:

```text
0x0c5fc5c1: "3.1.32\nro.product.device.screenshape=3\n..."
```

The generated guard previously compared that address with `"3.1.32"` using
full C-string equality. After matching the six expected characters it compared
LF with NUL, returned `-1`, and caused the constructor to exit. Therefore
`/dev/canopus` could not be registered even when the ELF loader successfully
invoked the constructor table.

## Source correction

The target pack now declares:

```toml
identity_terminator = "line"
```

The generated guard compares the complete expected token and accepts only NUL,
LF, or CR as its following boundary. It still rejects truncated values, wrong
versions, and longer prefixes such as `3.1.32evil`.

Stage 1 now also stores every return code at `0x200cb440`. A follow-up device
run displayed `stage-1 execution failed`; review showed that Lua inspected the
compound shell boolean before reading that result word, so any non-zero stage
return could still be misclassified. The result word is now authoritative even
when `os.execute` reports false, and an entry marker distinguishes failure before
entry from failure to return.

The same review removed the remaining use of Umem `memalign` from the bootstrap.
That exact allocator had already returned zero in an earlier device probe, yet it
was still stage 1's first internal operation and was also used by stage 2. Stage
1 and stage 2 now use the device-proven Kmem malloc/free pair throughout, retain
raw allocation owners while aligning interior pointers, and separate portable
ELF-loader failures as `-101..-106` for precise Lua diagnostics.

## Status

```text
ROX-first stage-0 publication:              DEVICE_REJECTED
RWX-first/RX-final stage-0 publication:     DEVICE_RETEST_REACHED_LOAD_CHECK
old full-string Supervisor identity guard:  STATIC_REJECTED
line-bounded Supervisor identity guard:     STATIC_CANDIDATE
full /dev/canopus registration path:         STATIC_CANDIDATE
```

A subsequent all-Kmem/result-first run returned stage result zero and still
reported `LOAD failed: no /dev/canopus`. This proves stage 1 returned, stage 2
completed ELF load/finalize, and the loader invoked the constructor table. It
does not distinguish a constructor early return from a character-driver
registration failure because the old constructor callback returned `void`.

The next build therefore publishes constructor status through the same result
word before stage 2 returns:

```text
-200  constructor not observed
-201  firmware identity rejected
-202  supervisor initialization failed
-203  register-device hook missing
-204  /dev/canopus registration failed
```

`register_device` also opens and closes `/dev/canopus` immediately after the
firmware registration call, so a false-success return that did not publish an
inode is converted to `-204`. A new device run of these instrumented resources
is required before promoting the full loader and Supervisor path to
`DEVICE_PROVEN`.

## Exact register-driver failure binding

The constructor-instrumented device run returned `-204`, proving that firmware
identity, Supervisor initialization, and the register-device hook all succeeded,
but `sup_register_device()` did not verify `/dev/canopus`.

Exact IDB review found that `register_driver@0x0c4e38ce` is not a faithful error
wrapper. It takes the global inode lock, calls `inode_reserve`, populates the
inode only when reserve succeeds, then returns `inode_unlock()` regardless of a
reserve failure. Consequently `-EEXIST`, `-ENOMEM`, and `-EINVAL` are normally
collapsed to zero.

The corrected Band 9 path no longer calls that wrapper. It uses the same exact
firmware primitives directly:

```text
inode lock:     0x0c390059  int(void)
inode reserve:  0x0c4f7b7d  int(const char *, void **)
inode unlock:   0x0c3908c5  int(void)
```

While holding the inode lock it reproduces the recovered character-driver inode
layout exactly: file-operations pointer at `+0x10`, inode type low nibble `7` at
`+0x0e`, and private pointer at `+0x1c`. It preserves the real reserve return
instead of replacing it with the unlock result. Registration is idempotent: a
same-name inode left by an earlier experimental load is removed before reserve.
After population, the constructor still verifies the result with an immediate
open/close.

Register-call errno is encoded as `-(73728 + errno)` and post-register open
errno as `-(65536 + errno)`, so another failure is reported with the exact phase
and errno rather than the generic `-204`.
