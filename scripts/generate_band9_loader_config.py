#!/usr/bin/env python3
"""Validate and materialize a target-local Band 9 bootstrap profile."""
from __future__ import annotations

import argparse
import pathlib
import tomllib


def load(path: pathlib.Path) -> dict:
    with path.open("rb") as stream:
        return tomllib.load(stream)


def number(value: object, name: str) -> int:
    if not isinstance(value, int) or value < 0:
        raise SystemExit(f"{name} must be a non-negative integer")
    return value


def validate(profile: dict, target: dict) -> tuple[dict, dict, dict, dict, dict]:
    target_id = profile.get("target_id")
    if not isinstance(target_id, str) or target.get("target_id") != target_id:
        raise SystemExit("loader profile target_id does not match target.toml")
    if profile.get("firmware_sha256") != target.get("firmware_sha256"):
        raise SystemExit("loader profile firmware_sha256 does not match target.toml")
    loader_kind = target.get("loader")
    if not isinstance(loader_kind, str) or not loader_kind.startswith(
        "nsh-mw-stage1-stage2"
    ):
        raise SystemExit("target.toml does not select the NSH/mw loader family")
    if profile.get("loader_family") != "nsh-mw-stage1-stage2":
        raise SystemExit("unsupported Band 9 loader family")

    stage = profile.get("stage")
    stage0 = profile.get("stage0")
    firmware = profile.get("firmware")
    architecture = profile.get("architecture")
    sram = profile.get("sram_text")
    if not all(isinstance(item, dict) for item in (stage, stage0, firmware, architecture, sram)):
        raise SystemExit("loader profile is missing a required table")

    stage0_base = number(stage0.get("base"), "stage0.base")
    stage0_size = number(stage0.get("size"), "stage0.size")
    stage0_exec_size = number(stage0.get("exec_size", stage0_size), "stage0.exec_size")
    stage0_region = number(stage0.get("stage0_region"), "stage0.stage0_region")
    stage0_result = number(stage0.get("result_word"), "stage0.result_word")
    original_words_known = stage0.get("original_words_known", True)
    requires_mpu_sync = stage0.get("requires_mpu_sync", False)
    if not isinstance(original_words_known, bool):
        raise SystemExit("stage0.original_words_known must be boolean")
    if not isinstance(requires_mpu_sync, bool):
        raise SystemExit("stage0.requires_mpu_sync must be boolean")
    if stage0_region >= number(architecture.get("mpu_region_count"),
                              "architecture.mpu_region_count"):
        raise SystemExit("stage0.stage0_region is outside the MPU region count")
    if stage0_base & 31 or stage0_size < 32 or stage0_exec_size < 32 \
            or stage0_exec_size > stage0_size or stage0_exec_size & 31 \
            or stage0_result < stage0_base or stage0_result + 4 > stage0_base + stage0_size \
            or stage0_base <= stage0_result < stage0_base + stage0_exec_size:
        raise SystemExit("stage0 must be an aligned region containing result_word")

    firmware_entries = [
        "exec_handler", "mw_handler", "open", "close", "read", "memalign", "free",
        "kmem_malloc", "kmem_free", "mpu_alloc", "mpu_configure", "mpu_release",
    ]
    if requires_mpu_sync and "mpu_sync" not in firmware:
        raise SystemExit("stage0.requires_mpu_sync needs an exact firmware.mpu_sync entry")
    if "mpu_sync" in firmware:
        firmware_entries.append("mpu_sync")
    for key in firmware_entries:
        entry = number(firmware[key], f"firmware.{key}")
        if entry == 0 and profile.get("status") != "STATIC_RECOVERED":
            continue
        if entry == 0:
            raise SystemExit(f"firmware.{key} is unresolved")
        if entry & 1:
            raise SystemExit(f"firmware.{key} must be an even IDA entry address")
        callable_address = entry | 1
        if callable_address & 1 == 0:
            raise SystemExit(f"firmware.{key} is not a Thumb entry")

    for key in ("mpu_rnr", "mpu_rbar", "mpu_rlar", "mpu_region_count"):
        number(architecture[key], f"architecture.{key}")
    for key in (
        "exec_access_attr", "exec_mem_attr", "ro_access_attr", "rw_access_attr",
    ):
        number(architecture[key], f"architecture.{key}")
    if requires_mpu_sync:
        if architecture["exec_access_attr"] not in (1, 2):
            raise SystemExit(
                "synchronized publication requires executable read-only access")
        if architecture["rw_access_attr"] != 0:
            raise SystemExit(
                "synchronized publication requires executable read-write access")
        if architecture["exec_mem_attr"] not in range(8):
            raise SystemExit(
                "synchronized publication requires a valid MPU memory attribute")
    number(sram["base"], "sram_text.base")
    number(sram["size"], "sram_text.size")
    cave = number(sram["cave"], "sram_text.cave")
    words = sram.get("original_words")
    if profile.get("status") == "STATIC_RECOVERED":
        if cave == 0 or not isinstance(words, list) or len(words) != 8:
            raise SystemExit("STATIC_RECOVERED profile needs an exact cave and eight original words")
        for index, word in enumerate(words):
            number(word, f"sram_text.original_words[{index}]")
    return stage, stage0, firmware, architecture, sram


def hex32(value: int) -> str:
    return f"0x{value:08x}u"


def hex_lua(value: int) -> str:
    return f"0x{value:08x}"


def lua_bool(value: bool) -> str:
    return "true" if value else "false"


def write_header(path: pathlib.Path, profile: dict, stage0: dict, firmware: dict, architecture: dict) -> None:
    lines = [
        "#ifndef CANOPUS_BAND9_LOADER_CONFIG_H",
        "#define CANOPUS_BAND9_LOADER_CONFIG_H",
        "",
        "/* Generated from targets/<target-id>/loader/bootstrap.toml. */",
    ]
    mapping = {
        "CANOPUS_FW_OPEN": firmware["open"] | 1,
        "CANOPUS_FW_CLOSE": firmware["close"] | 1,
        "CANOPUS_FW_READ": firmware["read"] | 1,
        "CANOPUS_FW_MEMALIGN": firmware["memalign"] | 1,
        "CANOPUS_FW_FREE": firmware["free"] | 1,
        "CANOPUS_FW_KMEM_MALLOC": firmware["kmem_malloc"] | 1,
        "CANOPUS_FW_KMEM_FREE": firmware["kmem_free"] | 1,
        "CANOPUS_FW_MPU_ALLOC": firmware["mpu_alloc"] | 1,
        "CANOPUS_FW_MPU_CONFIGURE": firmware["mpu_configure"] | 1,
        "CANOPUS_FW_MPU_RELEASE": firmware["mpu_release"] | 1,
        "CANOPUS_MPU_RNR": architecture["mpu_rnr"],
        "CANOPUS_MPU_RBAR": architecture["mpu_rbar"],
        "CANOPUS_MPU_RLAR": architecture["mpu_rlar"],
        "CANOPUS_BAND9_CAVE": stage0["base"],
        "CANOPUS_BAND9_CAVE_RESULT": stage0["result_word"],
        "CANOPUS_BAND9_STAGE0_SIZE": stage0["size"],
        "CANOPUS_BAND9_STAGE0_REGION": stage0["stage0_region"],
        "CANOPUS_BAND9_STAGE0_EXEC_SIZE": stage0["exec_size"],
    }
    if "mpu_sync" in firmware:
        mapping["CANOPUS_FW_MPU_SYNC"] = firmware["mpu_sync"] | 1
    for key, value in mapping.items():
        lines.append(f"#define {key} UINT32_C(0x{number(value, key):08x})")
    lines.extend([
        f"#define CANOPUS_BAND9_MPU_REGION_COUNT {number(architecture['mpu_region_count'], 'architecture.mpu_region_count')}u",
        f"#define CANOPUS_BAND9_EXEC_ACCESS_ATTR {number(architecture['exec_access_attr'], 'architecture.exec_access_attr')}u",
        f"#define CANOPUS_BAND9_EXEC_MEM_ATTR {number(architecture['exec_mem_attr'], 'architecture.exec_mem_attr')}u",
        f"#define CANOPUS_BAND9_RO_ACCESS_ATTR {number(architecture['ro_access_attr'], 'architecture.ro_access_attr')}u",
        f"#define CANOPUS_BAND9_RW_ACCESS_ATTR {number(architecture['rw_access_attr'], 'architecture.rw_access_attr')}u",
        "",
        "#endif",
        "",
    ])
    path.write_text("\n".join(lines))


def write_lua(path: pathlib.Path, profile: dict, firmware: dict, architecture: dict, stage0: dict) -> None:
    words = [0] * 8
    def field(name: str, value: int) -> str:
        return f"    {name} = {hex_lua(value)},"
    lines = [
        "-- Generated from the target-local bootstrap profile; do not edit by hand.",
        "return {",
        f"    target_id = {profile['target_id']!r},",
        f"    firmware_sha256 = {profile['firmware_sha256']!r},",
        f"    status = {profile['status']!r},",
        f"    device_status = {profile.get('device_status', 'NOT_PROBED')!r},",
        f"    loader_family = {profile['loader_family']!r},",
        field("exec_handler", firmware["exec_handler"] | 1),
        field("mw_handler", firmware["mw_handler"] | 1),
        field("open", firmware["open"] | 1),
        field("close", firmware["close"] | 1),
        field("read", firmware["read"] | 1),
        field("memalign", firmware["memalign"] | 1),
        field("free", firmware["free"] | 1),
        field("kmem_malloc", firmware["kmem_malloc"] | 1),
        field("kmem_free", firmware["kmem_free"] | 1),
        field("mpu_alloc", firmware["mpu_alloc"] | 1),
        field("mpu_configure", firmware["mpu_configure"] | 1),
        field("mpu_release", firmware["mpu_release"] | 1),
        *(
            [field("mpu_sync", firmware["mpu_sync"] | 1)]
            if "mpu_sync" in firmware else []
        ),
        field("mpu_rnr", architecture["mpu_rnr"]),
        field("mpu_rbar", architecture["mpu_rbar"]),
        field("mpu_rlar", architecture["mpu_rlar"]),
        field("mpu_region_count", architecture["mpu_region_count"]),
        field("exec_access_attr", architecture["exec_access_attr"]),
        field("exec_mem_attr", architecture["exec_mem_attr"]),
        field("rw_access_attr", architecture["rw_access_attr"]),
        field("cave", stage0["base"]),
        field("cave_result", stage0["result_word"]),
        field("stage0_size", stage0["size"]),
        field("stage0_region", stage0["stage0_region"]),
        field("stage0_exec_size", stage0["exec_size"]),
        f"    cave_original_known = {lua_bool(stage0.get('original_words_known', True))},",
        "    cave_original = {",
    ]
    lines.extend(f"        {hex_lua(word)}," for word in words)
    lines.extend(["    },", "}", ""])
    path.write_text("\n".join(lines))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", required=True, type=pathlib.Path)
    parser.add_argument("--target-toml", required=True, type=pathlib.Path)
    parser.add_argument("--header", type=pathlib.Path)
    parser.add_argument("--lua", type=pathlib.Path)
    args = parser.parse_args()
    profile = load(args.profile)
    target = load(args.target_toml)
    stage, stage0, firmware, architecture, sram = validate(profile, target)
    del stage, sram
    if profile.get("status") != "STATIC_RECOVERED":
        raise SystemExit(
            f"loader profile is {profile.get('status')}; refusing to emit executable bootstrap config"
        )
    if args.header:
        args.header.parent.mkdir(parents=True, exist_ok=True)
        write_header(args.header, profile, stage0, firmware, architecture)
    if args.lua:
        args.lua.parent.mkdir(parents=True, exist_ok=True)
        write_lua(args.lua, profile, firmware, architecture, stage0)


if __name__ == "__main__":
    main()
