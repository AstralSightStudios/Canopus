#!/usr/bin/env python3
"""Build and exercise the real Thumb Hook Engine in Unicorn, not Mirage.

Run with a Python containing unicorn, e.g.:
  /tmp/venv/bin/python -I tests/hook/test_thumb.py \
      --cc /opt/homebrew/opt/llvm/bin/clang
Clang's ARM backend and ld.lld are required. All build outputs use a temporary
folder; missing tools/dependencies explicitly skip with exit status 77.
This is host/emulator evidence only, not physical-device verification.
"""
import argparse
import ctypes
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

SRAM = 0x20000000
SRAM_SIZE = 0x400000
STOP = 0x203FF000
SP = 0x203F0000
ORIG = 0x203F1000
SLOT = 0x203F2000
FLASH_A = 0x0C001001
FLASH_B = 0x1C002001
ALIAS = 0x1C100000        # where aliased code runs
ALIAS_DELTA = 0x20000000  # the data view it is written through
ALIAS_TRAMPOLINES = ALIAS + 0x800
AUTO, FPB, INLINE, POINTER = range(4)
UNSUPPORTED, STATE = -38, -16


def load_elf(path):
    blob = path.read_bytes()
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", blob)
    assert header[0][:7] == b"\x7fELF\x01\x01\x01" and header[2] == 40
    segments = [struct.unpack_from("<8I", blob, header[5] + i * header[9])
                for i in range(header[10])]
    sections = [struct.unpack_from("<10I", blob, header[6] + i * header[11])
                for i in range(header[12])]
    symbols = {}
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        strtab = sections[section[6]]
        strings = blob[strtab[4]:strtab[4] + strtab[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, value, _, _, _, _ = struct.unpack_from("<IIIBBH", blob, offset)
            if name:
                end = strings.index(b"\0", name)
                symbols[strings[name:end].decode()] = value
    return blob, segments, symbols


def build(root, out, cc, linker):
    flags = ["--target=arm-none-eabi", "-mcpu=cortex-m33", "-mthumb",
             "-mfloat-abi=soft", "-ffreestanding", "-fno-builtin", "-fno-common",
             "-fno-stack-protector", "-fno-unwind-tables",
             "-fno-asynchronous-unwind-tables", "-Os", "-Wall", "-Wextra", "-Werror"]
    hook_obj = out / "hook.o"
    fixture_obj = out / "fixtures.o"
    view_obj = out / "alias_view.o"
    subprocess.run([cc, *flags, "-std=c11", "-I" + str(root / "sdk/c"),
                    "-I" + str(root / "runtime/hook"), "-c",
                    str(root / "runtime/hook/canopus_hook.c"), "-o", str(hook_obj)],
                   check=True)
    subprocess.run([cc, *flags[:4], "-Wall", "-Wextra", "-Werror", "-c",
                    str(root / "tests/hook/thumb_fixtures.s"), "-o", str(fixture_obj)],
                   check=True)
    subprocess.run([cc, *flags, "-std=c11", "-I" + str(root / "sdk/c"),
                    "-I" + str(root / "runtime/hook"), "-c",
                    str(root / "tests/hook/alias_view.c"), "-o", str(view_obj)],
                   check=True)
    elf = out / "hook-thumb.elf"
    subprocess.run([linker, "--image-base=0x20000000", "-Ttext=0x20010000",
                    "-Tbss=0x20020000",
                    "--entry=canopus_hook_engine_init", str(hook_obj),
                    str(fixture_obj), str(view_obj), "-o", str(elf)], check=True)
    return load_elf(elf)


def run_tests(image, uc, regs):
    blob, segments, symbols = image
    checks = 0

    def check(condition, description):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(description)

    def put(u, addr, value):
        u.mem_write(addr, struct.pack("<I", value))

    def get(u, addr):
        return struct.unpack("<I", u.mem_read(addr, 4))[0]

    def call(u, address, *args):
        if isinstance(address, str):
            address = symbols[address]
        u.reg_write(regs.UC_ARM_REG_XPSR, 0x01000000)
        u.reg_write(regs.UC_ARM_REG_SP, SP)
        u.reg_write(regs.UC_ARM_REG_LR, STOP | 1)
        for reg, value in zip([regs.UC_ARM_REG_R0, regs.UC_ARM_REG_R1,
                               regs.UC_ARM_REG_R2, regs.UC_ARM_REG_R3],
                              list(args) + [0] * 4):
            u.reg_write(reg, value)
        for i, value in enumerate(args[4:]):
            put(u, SP + 4 * i, value)
        u.emu_start(address | 1, STOP, count=200000)
        check(u.reg_read(regs.UC_ARM_REG_PC) == STOP, "Thumb call must return")
        value = u.reg_read(regs.UC_ARM_REG_R0)
        return value if value < 0x80000000 else value - 0x100000000

    def machine():
        u = uc.Uc(uc.UC_ARCH_ARM, uc.UC_MODE_THUMB | uc.UC_MODE_MCLASS)
        u.mem_map(SRAM, SRAM_SIZE)
        for segment in segments:
            if segment[0] == 1 and segment[4]:
                u.mem_write(segment[2], blob[segment[1]:segment[1] + segment[4]])
        # Deliberately leave FPB registers unmapped: even init must not touch them.
        call(u, "canopus_hook_engine_init")
        for flash in (FLASH_A, FLASH_B):
            page = flash & ~0xFFF
            u.mem_map(page, 0x1000)
            u.mem_write(flash & ~1, b"\x11\x20\x70\x47" + b"\0\xbf" * 6)
            u.mem_protect(page, 0x1000, uc.UC_PROT_READ | uc.UC_PROT_EXEC)
        return u

    def install(u, owner, target, hook_type):
        put(u, ORIG, 0x12345678)
        result = call(u, "canopus_hook_install", owner, target,
                      symbols["fixture_replacement"], ORIG, hook_type)
        # Flush Unicorn's translated blocks after intentional SRAM self-modification.
        u.ctl_remove_cache(SRAM, SRAM + SRAM_SIZE)
        return result, get(u, ORIG)

    def register(u, owner, target, ram_slot=SLOT):
        return call(u, "canopus_hook_register_slot_map", owner, target, ram_slot)

    for name, expected, copy_len in [("fixture_aligned", 14, 10),
                                      ("fixture_unaligned", 10, 12),
                                      ("fixture_wide_push", 7, 8)]:
        u = machine()
        target = symbols[name]
        even = target & ~1
        before = bytes(u.mem_read(even, 24))
        check(call(u, target, 4) == expected, name + " baseline")
        hook_id, orig = install(u, 0, target, INLINE)
        check(hook_id >= 0, name + " install")
        check(bytes(u.mem_read(orig & ~1, copy_len)) == before[:copy_len],
              name + " complete original instruction prefix")
        check(bytes(u.mem_read(even + copy_len, 24 - copy_len)) == before[copy_len:],
              name + " does not overwrite following instruction")
        check(call(u, target, 4) == 104, name + " redirects to replacement")
        check(call(u, orig, 4) == expected, name + " trampoline preserves execution")
        check(call(u, "canopus_hook_uninstall", 0, hook_id) == 0, name + " uninstall")
        u.ctl_remove_cache(SRAM, SRAM + SRAM_SIZE)
        check(bytes(u.mem_read(even, 24)) == before, name + " exact byte restoration")
        check(call(u, target, 4) == expected, name + " restored execution")
        print("PASS", name, "instruction-boundary/trampoline/restore")

    rejected = ["fixture_pc_read", "fixture_mov_pc_lr", "fixture_return16",
                "fixture_return32", "fixture_it_split", "fixture_it_unaligned",
                "fixture_literal_boundary", "fixture_unimplemented32"]
    for name in rejected:
        u = machine()
        target = symbols[name]
        before = bytes(u.mem_read(target & ~1, 24))
        result, orig = install(u, 0, target, INLINE)
        check(result == UNSUPPORTED, name + " must return unsupported")
        check(orig == 0x12345678, name + " leaves original output untouched")
        check(bytes(u.mem_read(target & ~1, 24)) == before, name + " must not patch")
        check(call(u, "canopus_hook_is_hooked", target) == 0, name + " not active")
        print("PASS", name, "fail-closed unsupported")

    u = machine()
    for target in (FLASH_A, FLASH_B):
        before = bytes(u.mem_read(target & ~1, 16))
        for hook_type in (AUTO, FPB, INLINE, POINTER):
            result, orig = install(u, 0, target, hook_type)
            check(result == UNSUPPORTED, "unregistered read-only Flash unsupported")
            check(orig == 0x12345678, "Flash failure leaves original output untouched")
            check(bytes(u.mem_read(target & ~1, 16)) == before, "no Flash writes")
    print("PASS read-only Flash: all request types, both address aliases")

    u = machine()
    put(u, SLOT, FLASH_A)
    check(register(u, 0, FLASH_A) == 0, "register owner 0 mapping")
    check(register(u, 1, FLASH_B) == 0, "register second alias")
    result, _ = install(u, 2, FLASH_A, AUTO)
    check(result == UNSUPPORTED, "another owner cannot borrow mapping")
    hook_id, orig = install(u, 0, FLASH_A, FPB)
    check(hook_id >= 0 and orig == FLASH_A, "mapped legacy request uses real RAM slot")
    check(call(u, get(u, SLOT), 4) == 104, "RAM dispatch redirected")
    check(call(u, FLASH_A, 4) == 17, "Flash function itself remains unchanged")
    check(call(u, orig, 4) == 17, "mapped original remains callable")
    result, _ = install(u, 1, FLASH_B, AUTO)
    check(result == STATE, "mapped alias cannot hook an occupied physical slot")
    result, _ = install(u, 1, SLOT, POINTER)
    check(result == STATE, "direct alias cannot hook an occupied physical slot")
    check(call(u, "canopus_hook_uninstall", 0, hook_id) == 0, "mapped uninstall")
    check(get(u, SLOT) == FLASH_A, "mapped restore exact original")
    # Reverse direction: a direct hook must also block a mapped alias.
    hook_id, _ = install(u, 0, SLOT, POINTER)
    check(hook_id >= 0, "direct pointer hook")
    result, _ = install(u, 1, FLASH_B, AUTO)
    check(result == STATE, "direct hook blocks mapped alias")
    call(u, "canopus_hook_release_slot", 0)
    check(get(u, SLOT) == FLASH_A, "release restores direct pointer")
    put(u, SLOT, 0)
    hook_id, orig = install(u, 1, FLASH_B, AUTO)
    check(hook_id >= 0 and orig == 0, "mapped NULL original is not synthesized")
    call(u, "canopus_hook_release_slot", 1)
    check(get(u, SLOT) == 0, "release restores NULL exactly")
    print("PASS registered mappings: real dispatch, aliases, owners, NULL restore")

    u = machine()
    put(u, SLOT, FLASH_A)
    check(register(u, 0, FLASH_A) == 0, "register active mapping")
    check(register(u, 0, FLASH_B, SLOT + 4) == 0, "register unused mapping")
    hook_id, _ = install(u, 0, FLASH_A, AUTO)
    check(hook_id >= 0, "owner installs active mapping")
    call(u, "canopus_hook_release_slot", 0)
    check(get(u, SLOT) == FLASH_A, "owner release restores before free")
    u.mem_unmap(SLOT, 0x1000)  # Simulate freeing module-owned dispatch storage.
    for target in (FLASH_A, FLASH_B):
        result, _ = install(u, 0, target, AUTO)
        check(result == UNSUPPORTED, "released mapping must not access freed storage")
    print("PASS released active and unused mappings never access freed SRAM")

    # A platform code view: the code runs at an alias and is written through
    # another address, as on Band 11. One host buffer is mapped at both.
    window = (ctypes.c_ubyte * 0x1000)()

    def alias_machine(owner=1):
        u = machine()
        pointer = ctypes.addressof(window)
        ctypes.memset(pointer, 0, 0x1000)
        # The code view cannot be written and the data view cannot be executed.
        u.mem_map_ptr(ALIAS, 0x1000, uc.UC_PROT_READ | uc.UC_PROT_EXEC, pointer)
        u.mem_map_ptr(ALIAS + ALIAS_DELTA, 0x1000,
                      uc.UC_PROT_READ | uc.UC_PROT_WRITE, pointer)
        for offset, name in ((0x000, "fixture_aligned"), (0x100, "fixture_unaligned")):
            start = symbols[name] & ~1
            u.mem_write(ALIAS + ALIAS_DELTA + offset, bytes(u.mem_read(start, 0x40)))
        put(u, symbols["view_owner"], owner)
        call(u, "canopus_hook_set_code_view", symbols["fixture_view"])
        return u

    def alias_install(u, owner, target):
        put(u, ORIG, 0x12345678)
        result = call(u, "canopus_hook_install", owner, target,
                      symbols["fixture_replacement"], ORIG, INLINE)
        u.ctl_remove_cache(ALIAS, ALIAS + 0x1000)
        return result, get(u, ORIG)

    for offset, expected in ((0x000, 14), (0x100, 10)):
        u = alias_machine()
        target = ALIAS + offset + 1
        before = bytes(u.mem_read(ALIAS + offset, 0x40))
        check(call(u, target, 4) == expected, "aliased baseline")
        hook_id, orig = alias_install(u, 1, target)
        check(hook_id >= 0, "inline hook installs on aliased module code")
        check(ALIAS_TRAMPOLINES <= orig < ALIAS_TRAMPOLINES + 0x200 and orig & 1,
              "trampoline runs at the executable alias, not the write address")
        check(bytes(u.mem_read(ALIAS + offset, 0x40)) != before, "code was patched")
        check(call(u, target, 4) == 104, "aliased target redirects to the replacement")
        check(call(u, orig, 4) == expected, "trampoline preserves aliased execution")
        check(get(u, symbols["view_publish_count"]) == 1, "install publishes once")
        check(get(u, symbols["view_trampoline_calls"]) == 1, "trampoline block requested once")
        check(call(u, "canopus_hook_uninstall", 1, hook_id) == 0, "aliased uninstall")
        u.ctl_remove_cache(ALIAS, ALIAS + 0x1000)
        check(bytes(u.mem_read(ALIAS + offset, 0x40)) == before, "exact byte restoration")
        check(call(u, target, 4) == expected, "restored aliased execution")
        check(get(u, symbols["view_publish_count"]) == 2, "uninstall publishes")
        hook_id, _ = alias_install(u, 1, target)
        check(hook_id >= 0 and get(u, symbols["view_trampoline_calls"]) == 1,
              "the trampoline block is reused")
    print("PASS aliased code: write view, executable trampolines, publish, restore")

    u = alias_machine()
    before = bytes(u.mem_read(ALIAS, 0x40))
    result, orig = alias_install(u, 2, ALIAS + 1)
    check(result == UNSUPPORTED and orig == 0x12345678, "another module's code is refused")
    result, _ = alias_install(u, 1, ALIAS + 0x7F8 + 1)
    check(result == UNSUPPORTED, "a patch that leaves the owner's region is refused")
    result, _ = install(u, 1, symbols["fixture_aligned"], INLINE)
    check(result == UNSUPPORTED, "with a view, SRAM is not patched in place")
    put(u, symbols["view_publish_result"], 0xFFFFFFFF)
    result, orig = alias_install(u, 1, ALIAS + 1)
    check(result == UNSUPPORTED and orig == 0x12345678, "failed publish fails the install")
    check(bytes(u.mem_read(ALIAS, 0x40)) == before, "failed publish restores the code")
    check(call(u, "canopus_hook_is_hooked", ALIAS + 1) == 0, "failed publish leaves no hook")
    put(u, symbols["view_publish_result"], 0)
    check(alias_install(u, 1, ALIAS + 1)[0] >= 0, "the slot is reusable after a failed publish")
    call(u, "canopus_hook_release_slot", 1)
    u.ctl_remove_cache(ALIAS, ALIAS + 0x1000)
    check(bytes(u.mem_read(ALIAS, 0x40)) == before, "release restores aliased code")
    call(u, "canopus_hook_set_code_view", 0)
    result, _ = alias_install(u, 1, ALIAS + 1)
    check(result == UNSUPPORTED, "without a view the alias is read-only again")
    check(install(u, 0, symbols["fixture_aligned"], INLINE)[0] >= 0,
          "without a view SRAM is patched in place again")
    print("PASS code view limits: owner only, region bound, publish failure, no view")

    print(f"PASS {checks} checks: 3 Thumb executions, 8 rejected prefixes, "
          "read-only Flash, mapping lifecycle and aliased-code regressions")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang", help="Clang with ARM backend")
    parser.add_argument("--ld", default="ld.lld", help="LLVM ELF linker")
    args = parser.parse_args()
    cc, linker = shutil.which(args.cc), shutil.which(args.ld)
    if cc is None or linker is None:
        print("SKIP: Clang and ld.lld are required")
        return 77
    try:
        import unicorn
        from unicorn import arm_const
    except ImportError:
        print("SKIP: Python unicorn is required")
        return 77
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="canopus-hook-thumb-") as out:
        image = build(root, Path(out), cc, linker)
        run_tests(image, unicorn, arm_const)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
