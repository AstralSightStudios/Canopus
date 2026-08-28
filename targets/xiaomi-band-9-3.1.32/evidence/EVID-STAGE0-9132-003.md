# Band 9 3.1.32 Stage-0 MPU Publication Fault

**Target:** `xiaomi-band-9-3.1.32` (`vela_ap.bin`, SHA-256 `9c02dab4020b2cc9666ee7d34cf27d311b76aadcec519a38361bbcbd94c53264`)

**Device log:** `crash20260826165012.log` (device timestamp `2026-08-27 00:50:04`)

## Result

The `0x200cb400` stage-0 RAM candidate is writable, but the first implementation published its MPU mapping unsafely. It selected live region 7 and wrote the new ROX RBAR before replacing that region's old RLAR. For the interval between those two `mw` stores, the cave base and the running shell task's old stack limit formed one enabled, read-only MPU range. IRQ 3 arrived in that interval, and exception stacking to the shell stack failed.

The ROX-first sequence is device-rejected. The corrected sequence publishes stage 0 as RWX while replacing the retained limit, narrows the confined cave range to RX, performs a firmware-resident DSB/ISB before the first cave fetch, and remains `STATIC_CANDIDATE` pending device retest.

## Fault binding

The failing task and architectural status are:

```text
task:     PID 2247, system -c mw
IRQ:      3
CFSR:     0x000000b2
MMFAR:    0x3c80874c
SP:       0x3c808738
CONTROL:  0x0000000c
```

`CFSR=0xb2` decodes to:

- `DACCVIOL`: data access violation;
- `MSTKERR`: MemManage fault during exception stacking;
- `MLSPERR`: floating-point lazy-state preservation fault;
- `MMARVALID`: `MMFAR` is valid.

`MMFAR` is `SP + 0x14` and lies inside PID 2247's user stack (base `0x3c807978`, size 3960 bytes / `0x0f78`). This is an exception-frame write failure, not a bad callback target.

The stack retains the interrupted `mw` call's arguments:

```text
0x0c1cc3b9  0x2c5fb81b  0xe000ed9c  ...
...         0x200cb406  ...
```

`0x2c5fb81b` is the `mw` formatter `"  %p = 0x%08lx"`; `0xe000ed9c` is MPU_RBAR; and `0x200cb406` is the requested ROX RBAR. The reported `PC=0x78302020` and `xPSR=0x30303065` are ASCII fragments from the incompletely written exception frame and are not an executed address.

## Triggering project path

```text
Run click
  -> load_supervisor()
  -> call_mailbox()
  -> execute_stage0_trampoline()
  -> stage0_exec_with_mpu()
  -> mw e000ed98=00000007
  -> mw e000ed9c=200cb406  [unsafe ROX-first publication]
  -> mw e000eda0=200cb423
  -> exec 0x200cb401
```

The triggering code was `watchfaces/canopus-installer-prod/xiaomi-band-9/main.lua`. Installation had not reached stage 1, stage 2, the Supervisor constructor, or `/dev/canopus` registration.

## Exact firmware comparison

The recovered helper at `0x0c52272c` maps access attributes as follows for this use:

```text
exec_access_attr = 1  -> RBAR low bits 0x06 (RO, executable)
rw_access_attr   = 0  -> RBAR low bits 0x02 (RW, executable)
exec_mem_attr    = 1  -> RLAR low bits 0x03 (AttrIndx 1, enabled)
```

Thus `0x200cb406` and `0x200cb423` were correctly encoded individually. The defect was their non-atomic transition against a live region, not an incorrect bit formula.

The firmware's direct publisher at `0x0c5226e8` disables IRQs, writes RNR/RBAR/RLAR contiguously, executes `DSB SY; ISB SY`, and restores IRQ state. Three independent NSH `mw` handlers cannot provide that atomic publication. A firmware-resident callable tail at `0x0c5226da` contains exactly:

```text
DSB.W SY
ISB.W
BX LR
```

Its Thumb callable address is `0x0c5226db`.

The NSH `exec` continuation used by the mailbox, `0x0c1c956d`, is also correct: the handler calls the target at `0x0c1c956a` and resumes at `0x0c1c956c`. No AAPCS/Thumb continuation mismatch was found.

## Source correction

The corrected loader applies these invariants:

1. Stage 0 first publishes `RBAR=0x200cb402` (RWX), so a retained old stack RLAR cannot make exception stacking read-only during the transition.
2. After publishing the confined `RLAR=0x200cb423`, stage 0 narrows RBAR to `0x200cb406` (RX).
3. Before `exec 0x200cb401`, the command invokes `exec 0x0c5226db` to execute DSB/ISB from already executable firmware.
4. Stage 1 uses the same RWX-first/RX-final ordering around its new RLAR.
5. MPU disable operations are followed by the same synchronization callable.
6. Stage-0 mapping, synchronization, cave execution, release, and continuation remain in one NSH command context.

The profile now exports `rw_access_attr` and the exact-firmware `mpu_sync` callable rather than hardcoding either in Lua.

## Status

```text
0x2006a9b0 SRAM-text cave:                 DEVICE_REJECTED
0x200cb400 with ROX-first publication:     DEVICE_REJECTED
0x200cb400 with RWX-first/RX-final + sync: STATIC_CANDIDATE
```

Host tests can verify command ordering, permissions, cleanup, and the pre-entry synchronization call. Only an on-device run can promote the corrected sequence to `DEVICE_PROVEN`.
