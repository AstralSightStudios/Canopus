-- Canopus installer watchface.
--
-- Loads the exact-target supervisor, which registers /dev/canopus, then drives
-- its fixed status/command ABI. Band 10 uses stock insmod. Band 9 uses a
-- compare-before-write NSH mw/exec bootstrap that stages a custom ET_REL loader.
--
-- Reading ro.build.version is the only action performed while opening this
-- watchface. Loading and registration still require explicit button presses.
-- The supervisor module is boot-resident: never run `rmmod`, never insert a
-- second copy, reboot for complete recovery.

local lvgl = require("lvgl")

local BAND10_TARGET_ID_PREFIX = "xiaomi-band-10-pro-"
local FIRMWARE_VERSION_PROPERTY = "ro.build.version"
local FIRMWARE_VERSION_OUTPUT = "/data/canopus-installer-firmware-version.tmp"
local MODULE_PATH
local MODULE_NAME = "canopus_supervisor"
local MANAGER_ICON_RESOURCE = SCRIPT_PATH .. "manager_icon.bin"
local MANAGER_ICON_PATH = "/data/canopus/manager_icon.bin"
local BAND9_TARGET_BY_VERSION = {
    ["3.1.175"] = "xiaomi-band-9-pro-3.1.175",
    ["3.1.32"] = "xiaomi-band-9-3.1.32",
}
local BAND9_TARGET_ID
local BAND9_PROFILE
local BAND9_RESOURCE_DIR
local BAND9_STAGE1_RESOURCE
local BAND9_STAGE2_RESOURCE
local BAND9_SUPERVISOR_RESOURCE
local BAND9_STAGE2_PATH = "/data/canopus/stage2.bin"
local BAND9_SUPERVISOR_PATH = "/data/canopus/supervisor.elf"
local DEVICE_PATH = "/dev/canopus"
-- The 512-byte floor rejects truncated/empty resources while remaining below
-- every exact-target supervisor artifact.
local MODULE_MIN_SIZE = 512
local MODULE_MAX_SIZE = 262144
local EXPECTED_MAGIC = 0x43505331 -- "CPS1" supervisor status magic
local EXPECTED_CMD_MAGIC = 0x43504331 -- "CPC1" command magic
-- 128 header + 16 slots x 16 bytes
local STATUS_SIZE = 384
local MODULE_SLOT_STRIDE = 16
local MODULE_SLOTS = 16
local COMMAND_SIZE = 16
local MAX_MODULES = 16

-- Supervisor commands (see manager/service/canopus_supervisor.c).
local CMD_QUERY = 0x43510001
local CMD_INSTALL = 0x43510002
local CMD_ENABLE = 0x43510003
local CMD_DISABLE = 0x43510004
local CMD_REMOVE = 0x43510005
local CMD_UPDATE = 0x43510006
local CMD_ROLLBACK = 0x43510007
local CMD_ENTER_SAFE_MODE = 0x43510008
local CMD_ACTIVATE = 0x43510009
local CMD_RESTORE_AFTER_BOOT = 0x4351000A

-- Result states (CANOPUS_RESULT_* from canopus_abi.h).
local result_names = {
    [0] = "idle",
    [1] = "rejected", [2] = "accepted", [3] = "queued",
    [4] = "running", [5] = "completed", [6] = "failed",
    [7] = "disallowed", [8] = "reboot-required",
}

-- Module lifecycle classes (CANOPUS_LIFECYCLE_*).
local class_names = {
    [0] = "removable", [1] = "resident-after-activation",
    [2] = "always-resident", [3] = "patch-reboot",
}

local state_names = {
    [0] = "-", [1] = "discovered", [2] = "verified", [3] = "installed",
    [4] = "disabled", [5] = "enabled", [6] = "loading", [7] = "preparing",
    [8] = "ready", [9] = "active", [10] = "stopping", [11] = "draining",
    [12] = "unloaded", [13] = "boot-resident", [14] = "disabled-next-boot",
    [15] = "failed", [16] = "fail-stop", [17] = "quarantined-next-boot",
    [18] = "update-staged", [19] = "reboot-required", [20] = "remove-pending",
}

local function shell_quote(value)
    return "'" .. tostring(value):gsub("'", "'\\''") .. "'"
end

local function run(command)
    print("[canopus-installer] exec: " .. command)
    local ok, why, code = os.execute(command)
    return ok == true or ok == 0
end

local function read_all(path, mode)
    if type(io) ~= "table" or type(io.open) ~= "function" then return nil end
    local file = io.open(path, mode or "rb")
    if not file then return nil end
    local content = file:read("*a")
    file:close()
    return content
end

local function detect_firmware_version()
    local command = string.format("getprop %s > %s",
        shell_quote(FIRMWARE_VERSION_PROPERTY),
        shell_quote(FIRMWARE_VERSION_OUTPUT))
    local command_ok = run(command)
    local raw
    if command_ok then raw = read_all(FIRMWARE_VERSION_OUTPUT, "r") end
    if type(os.remove) == "function" then
        pcall(os.remove, FIRMWARE_VERSION_OUTPUT)
    end
    if not command_ok or type(raw) ~= "string" then return nil end
    local version = raw:match("^%s*(.-)%s*$")
    if not version or not version:match("^%d+%.%d+%.%d+$") then return nil end
    return version
end

local function load_band9_profile(target_id)
    local path = SCRIPT_PATH .. "targets/" .. target_id .. "/canopus_loader_profile.lua"
    local source = read_all(path, "r")
    if type(source) ~= "string" then return nil end
    local compiler = loadstring or load
    if type(compiler) ~= "function" then return nil end
    local compiled, chunk = pcall(compiler, source)
    if not compiled or type(chunk) ~= "function" then return nil end
    local loaded, profile = pcall(chunk)
    if not loaded or type(profile) ~= "table"
        or profile.target_id ~= target_id
        or type(profile.status) ~= "string"
        or type(profile.loader_family) ~= "string" then
        return nil
    end
    return profile
end

local function select_band9_target(version)
    local target_id = BAND9_TARGET_BY_VERSION[version]
    if not target_id then return nil end
    local profile = load_band9_profile(target_id)
    if not profile then return nil end
    BAND9_TARGET_ID = target_id
    BAND9_PROFILE = profile
    BAND9_RESOURCE_DIR = SCRIPT_PATH .. "targets/" .. target_id .. "/"
    BAND9_STAGE1_RESOURCE = BAND9_RESOURCE_DIR .. "canopus_stage1.lua"
    BAND9_STAGE2_RESOURCE = BAND9_RESOURCE_DIR .. "canopus_stage2.bin"
    BAND9_SUPERVISOR_RESOURCE = BAND9_RESOURCE_DIR .. "canopus_supervisor.bin"
    return target_id
end

local function band9_profile_ready()
    return type(BAND9_PROFILE) == "table"
        and BAND9_PROFILE.status == "STATIC_RECOVERED"
        and type(BAND9_PROFILE.cave) == "number"
        and BAND9_PROFILE.cave ~= 0
        and type(BAND9_PROFILE.cave_result) == "number"
        and type(BAND9_PROFILE.cave_original) == "table"
        and #BAND9_PROFILE.cave_original == 8
end

local function band10_module_path_for(version)
    return SCRIPT_PATH .. "canopus_supervisor-" .. BAND10_TARGET_ID_PREFIX
        .. version .. ".bin"
end

local function write_all(path, content)
    local file = io.open(path, "wb")
    if not file then return false end
    local ok, result = pcall(file.write, file, content)
    local close_ok, close_result = pcall(file.close, file)
    return ok and result ~= nil and close_ok and close_result ~= nil
end

local function band9_shell_word(address)
    local output = "/data/canopus/bootstrap-word.txt"
    if not run(string.format("mw %08x 4 > %s", address, output)) then return nil end
    local text = read_all(output, "r")
    if type(text) ~= "string" then return nil end
    local value = text:match("=%s*0[xX]([0-9a-fA-F]+)")
        or text:match("%-%>%s*0[xX]([0-9a-fA-F]+)")
    return value and tonumber(value, 16) or nil
end

local function band9_check_cave()
    if not band9_profile_ready() then return false end
    for i, expected in ipairs(BAND9_PROFILE.cave_original) do
        if band9_shell_word(BAND9_PROFILE.cave + (i - 1) * 4) ~= expected then
            return false
        end
    end
    return true
end

local function band9_write_words(address, words)
    for i, value in ipairs(words) do
        if not run(string.format("mw %08x=%08x", address + (i - 1) * 4, value)) then
            return false
        end
    end
    return true
end

local function band9_restore_cave()
    return band9_write_words(BAND9_PROFILE.cave, BAND9_PROFILE.cave_original)
end

local function band9_call_mailbox(callable, r0, r1)
    -- ldr r0/r1/r3/r2 from the trailing literal pool, blx r3, store r0.
    local trampoline = {
        0x49044803,
        0x47984B04,
        0x60104A04,
        0xBF004770,
        r0,
        r1,
        callable,
        BAND9_PROFILE.cave_result,
    }
    if not band9_check_cave() then return nil end
    if not band9_write_words(BAND9_PROFILE.cave, trampoline) then
        band9_restore_cave()
        return nil
    end
    run(string.format("exec %08x", BAND9_PROFILE.cave + 1))
    local result = band9_shell_word(BAND9_PROFILE.cave_result)
    if not band9_restore_cave() or not band9_check_cave() then return nil end
    if result == BAND9_PROFILE.cave_result then return nil end
    return result
end

local function band9_barrier_and_exec(address)
    local trampoline = {
        0x8F4FF3BF, 0x8F6FF3BF, 0xBF004770,
        0, 0, 0, 0, 0,
    }
    if not band9_check_cave() then return false end
    if not band9_write_words(BAND9_PROFILE.cave, trampoline) then
        band9_restore_cave()
        return false
    end
    run(string.format("exec %08x", BAND9_PROFILE.cave + 1))
    if not band9_restore_cave() or not band9_check_cave() then return false end
    return run(string.format("exec %08x", address + 1))
end

local function band9_release_allocation(address)
    return band9_call_mailbox(BAND9_PROFILE.free, address, 0) ~= nil
end

local function band9_release_region(region)
    if not run(string.format("mw %08x=%08x", BAND9_PROFILE.mpu_rnr, region))
        or not run(string.format("mw %08x=00000000", BAND9_PROFILE.mpu_rlar)) then return false end
    return band9_call_mailbox(BAND9_PROFILE.mpu_release, region, 0) ~= nil
end

local function band9_cleanup_stage1(address, region)
    if not band9_release_region(region) then return false end
    return band9_release_allocation(address)
end

local function load_band9_stage1()
    local stage1_source = read_all(BAND9_STAGE1_RESOURCE, "r")
    local stage2 = read_all(BAND9_STAGE2_RESOURCE, "rb")
    local supervisor = read_all(BAND9_SUPERVISOR_RESOURCE, "rb")
    if type(stage1_source) ~= "string" or type(stage2) ~= "string"
        or type(supervisor) ~= "string" then
        return false, "Missing Band-9 bootstrap resources"
    end
    local compiler = loadstring or load
    if type(compiler) ~= "function" then return false, "Lua compiler unavailable" end
    local compiled, chunk, compile_error = pcall(compiler, stage1_source)
    if not compiled then return false, tostring(chunk) end
    if type(chunk) ~= "function" then return false, tostring(compile_error) end
    local loaded, payload = pcall(chunk)
    if not loaded then return false, tostring(payload) end
    if type(payload) ~= "table" or type(payload.words) ~= "table" then
        return false, "Invalid Band-9 stage-1 table"
    end
    if not write_all(BAND9_STAGE2_PATH, stage2)
        or not write_all(BAND9_SUPERVISOR_PATH, supervisor) then
        return false, "Cannot stage Band-9 loader files"
    end
    local allocation = band9_call_mailbox(BAND9_PROFILE.memalign, 32, payload.size)
    if not allocation or allocation == 0 or allocation % 32 ~= 0 then
        return false, "Band-9 stage-1 allocation failed"
    end
    if not band9_write_words(allocation, payload.words) then
        if not band9_release_allocation(allocation) then
            return false, "Band-9 stage-1 write cleanup failed; reboot before retrying"
        end
        return false, "Band-9 stage-1 write failed"
    end
    local region = band9_call_mailbox(BAND9_PROFILE.mpu_alloc, 0, 0)
    if not region or region >= BAND9_PROFILE.mpu_region_count then
        if not band9_release_allocation(allocation) then
            return false, "Band-9 MPU failure cleanup failed; reboot before retrying"
        end
        return false, "Band-9 MPU region unavailable"
    end
    local size = math.floor((payload.size + 31) / 32) * 32
    local rbar = allocation + 6
    local rlar = allocation + size - 32 + 5
    if not run(string.format("mw %08x=%08x", BAND9_PROFILE.mpu_rnr, region))
        or not run(string.format("mw %08x=%08x", BAND9_PROFILE.mpu_rbar, rbar))
        or not run(string.format("mw %08x=%08x", BAND9_PROFILE.mpu_rlar, rlar)) then
        if not band9_cleanup_stage1(allocation, region) then
            return false, "Band-9 MPU cleanup failed; reboot before retrying"
        end
        return false, "Band-9 MPU configuration failed"
    end
    local executed = band9_barrier_and_exec(allocation)
    local cleaned = band9_cleanup_stage1(allocation, region)
    if not cleaned then
        return false, "Band-9 stage-1 cleanup failed; reboot before retrying"
    end
    if not executed then
        return false, "Band-9 stage-1 execution failed"
    end
    return true
end

local function has_band9_bootstrap()
    local resources = {
        { BAND9_STAGE1_RESOURCE, "r" },
        { BAND9_STAGE2_RESOURCE, "rb" },
        { BAND9_SUPERVISOR_RESOURCE, "rb" },
    }
    for _, resource in ipairs(resources) do
        local file = io.open(resource[1], resource[2])
        if not file then return false end
        file:close()
    end
    return true
end

local function supervisor_present()
    local file = io.open(DEVICE_PATH, "rb")
    if file then file:close(); return true end
    return false
end

local function find_named_module(name)
    local content = read_all("/proc/modules", "r")
    if type(content) ~= "string" then return nil end
    for line in content:gmatch("[^\r\n]+") do
        if line:match("^" .. name .. ",") then return line end
    end
    return nil
end

local function find_module()
    return find_named_module(MODULE_NAME)
end

local function verify_module_file(path, missing_name)
    path = path or MODULE_PATH
    missing_name = missing_name or "matching versioned Supervisor"
    if type(io) ~= "table" or type(io.open) ~= "function" then
        return false, "io.open unavailable"
    end
    if type(path) ~= "string" then return false, "Missing " .. missing_name end
    local file = io.open(path, "rb")
    if not file then return false, "Missing " .. missing_name end
    local header = file:read(20)
    local size = file:seek("end")
    file:close()
    if type(header) ~= "string" or #header ~= 20
        or header:sub(1, 4) ~= "\127ELF" then
        return false, "Resource is not ELF"
    end
    if header:byte(5) ~= 1 or header:byte(6) ~= 1
        or header:byte(7) ~= 1 then
        return false, "Resource is not ELF32 little-endian"
    end
    local elf_type = header:byte(17) + header:byte(18) * 0x100
    local machine = header:byte(19) + header:byte(20) * 0x100
    if elf_type ~= 1 or machine ~= 40 then
        return false, "Resource is not relocatable ARM ELF"
    end
    if type(size) ~= "number" or size < MODULE_MIN_SIZE
        or size > MODULE_MAX_SIZE then
        return false, "Unexpected ELF size range"
    end
    return true
end

local function stage_manager_icon()
    local content = read_all(MANAGER_ICON_RESOURCE, "rb")
    -- LVGL v9 bin resource: 12-byte header (magic 0x19, cf, flags, w, h,
    -- stride, reserved) followed by ARGB8888 pixel data carrying the alpha
    -- channel. Validate the magic and that the size matches w*h*4.
    if type(content) ~= "string" or #content < 13
        or content:byte(1) ~= 0x19 then
        return false, "Missing or invalid manager_icon.bin LVGL resource"
    end
    local width = content:byte(5) + content:byte(6) * 0x100
    local height = content:byte(7) + content:byte(8) * 0x100
    if width < 1 or height < 1
        or #content ~= 12 + width * height * 4 then
        return false, "manager_icon.bin size mismatch"
    end
    -- The target mkdir command returns failure when the directory already
    -- exists, even with -p. Try the actual file first; create its parent only
    -- when that open proves the directory is absent.
    local output = io.open(MANAGER_ICON_PATH, "wb")
    if not output then
        if not run("mkdir /data/canopus") then
            return false, "Cannot create /data/canopus"
        end
        output = io.open(MANAGER_ICON_PATH, "wb")
    end
    if not output then return false, "Cannot stage Manager icon" end
    local call_ok, write_result, write_error = pcall(output.write, output, content)
    local close_ok, close_result, close_error = pcall(output.close, output)
    if not call_ok or write_result == nil then
        return false, tostring(write_error or write_result or "icon write failed")
    end
    if not close_ok or close_result == nil then
        return false, tostring(close_error or close_result or "icon close failed")
    end
    local staged = read_all(MANAGER_ICON_PATH, "rb")
    if staged ~= content then return false, "Staged Manager icon mismatch" end
    return true
end

local function bytes_to_words(content, expected)
    if type(content) ~= "string" or #content ~= expected
        or expected % 4 ~= 0 then return nil end
    local words = {}
    for offset = 1, expected, 4 do
        local a, b, c, d = content:byte(offset, offset + 3)
        words[#words + 1] = a + b * 0x100 + c * 0x10000 + d * 0x1000000
    end
    return words
end

local function signed32(value)
    if value >= 0x80000000 then return value - 0x100000000 end
    return value
end

-- ---- status ABI -----------------------------------------------------
-- 384 bytes:
--   0..3   u32 magic "CPS1"
--   4..7   u32 abi (1)
--   8..11  u32 framework_revision
--   12..15 u32 safe_mode (0/1)
--   16..19 u32 module_count
--   20..23 u32 pending_op
--   24..27 u32 pending_state (CANOPUS_RESULT_*)
--   28..31 u32 flags
--   32..35 u32 error_code
--   36..39 u32 snapshot sequence begin
--   40..43 u32 snapshot sequence end
--   44..47 i32 first non-zero module callback error
--   48..51 u32 Manager backend build ID
--   52..55 u32 Manager row callback count
--   56..59 u32 last clicked row slot
--   60..63 u32 last clicked semantic generation
--   64..67 u32 last clicked semantic key
--   68..71 u32 last clicked semantic event ID
--   72..75 u32 selected module before dispatch
--   76..79 u32 selected module after dispatch
--   80..83 u32 selected module observed by detail on_create
--   84..127 reserved
--   128..383 16 module slots x 16 bytes:
--     slot+0  u32 state
--     slot+4  u32 lifecycle_class
--     slot+8  u32 version
--     slot+12 u32 flags (bit0 signature_ok)

local function read_status()
    local file = io.open(DEVICE_PATH, "rb")
    if not file then return nil, "Cannot open /dev/canopus" end
    local raw = file:read(STATUS_SIZE)
    file:close()
    if type(raw) ~= "string" or #raw ~= STATUS_SIZE then
        return nil, "Short status; supervisor ABI mismatch"
    end
    local words = bytes_to_words(raw, STATUS_SIZE)
    if not words or words[1] ~= EXPECTED_MAGIC then
        return nil, "Bad supervisor status magic"
    end
    -- CAN-P2-007: the legacy parser verifies ABI and the sequence snapshot,
    -- not just the magic, so a torn/mismatched status is never accepted.
    if words[2] ~= 1 then
        return nil, "Supervisor ABI mismatch (expected 1, got " .. tostring(words[2]) .. ")"
    end
    local seq_begin = words[10] -- bytes 36..39 (1-based words)
    local seq_end = words[11]   -- bytes 40..43
    if seq_begin ~= seq_end or seq_begin % 2 ~= 0 then
        return nil, "Supervisor status snapshot inconsistent"
    end
    local st = {
        abi = words[2], framework_revision = words[3],
        safe_mode = words[4], module_count = words[5],
        pending_op = words[6], pending_state = words[7],
        flags = words[8], error_code = words[9],
        module_error = words[12],
        manager_build = words[13],
        manager_clicks = words[14],
        manager_row = words[15],
        manager_generation = words[16],
        manager_key = words[17],
        manager_event = words[18],
        manager_selected_before = words[19],
        manager_selected_after = words[20],
        manager_detail_selected = words[21],
        registry_stage = words[8] % 0x100,
        registry_errno = math.floor(words[8] / 0x100) % 0x10000,
        registry_saves = math.floor(words[8] / 0x1000000) % 0x100,
        modules = {},
    }
    if st.pending_state < 0 or st.pending_state > 8 then
        return nil, "Supervisor result state out of range"
    end
    for i = 0, MODULE_SLOTS - 1 do
        local base = 32 + i * (MODULE_SLOT_STRIDE // 4)
        if base + 3 <= #words then
            local state = words[base + 1]
            if state < 0 or state > 20 then
                return nil, "Supervisor module state out of range"
            end
            st.modules[i + 1] = {
                state = state,
                lifecycle_class = words[base + 2],
                version = words[base + 3],
                flags = words[base + 4],
            }
        end
    end
    return st
end

local function write_command(command, arg0, arg1)
    local payload = string.char(
        EXPECTED_CMD_MAGIC % 0x100,
        math.floor(EXPECTED_CMD_MAGIC / 0x100) % 0x100,
        math.floor(EXPECTED_CMD_MAGIC / 0x10000) % 0x100,
        math.floor(EXPECTED_CMD_MAGIC / 0x1000000) % 0x100)
    local function word(v)
        v = math.floor(v)
        return string.char(v % 0x100, math.floor(v / 0x100) % 0x100,
            math.floor(v / 0x10000) % 0x100,
            math.floor(v / 0x1000000) % 0x100)
    end
    payload = payload .. word(command) .. word(arg0 or 0) .. word(arg1 or 0)
    local file = io.open(DEVICE_PATH, "wb")
    if not file then return false, "Cannot open /dev/canopus" end
    local call_ok, write_result, write_error = pcall(file.write, file, payload)
    local close_ok, close_result, close_error = pcall(file.close, file)
    if not call_ok then return false, tostring(write_result) end
    if write_result == nil then return false, tostring(write_error or "write failed") end
    if not close_ok then return false, tostring(close_result) end
    if close_result == nil then return false, tostring(close_error or "close failed") end
    return true
end

-- ---- format helpers ------------------------------------------------

local function format_module(mod, index)
    local class = class_names[mod.lifecycle_class] or ("class?" .. mod.lifecycle_class)
    local state = state_names[mod.state] or ("state?" .. mod.state)
    local sig = (mod.flags % 2 == 1) and "ok" or "unsigned"
    return string.format("  %d [%s] v%u %s sig=%s",
        index, state, mod.version, class, sig)
end

local function format_status(st)
    if not st then return "no status" end
    local result = result_names[st.pending_state] or ("res?" .. st.pending_state)
    local lines = {
        string.format("framework v%u  modules=%u", st.framework_revision, st.module_count),
        string.format("Manager build=%08X clicks=%u", st.manager_build,
            st.manager_clicks),
        string.format("row=%u gen=%u key=%08X event=%08X", st.manager_row,
            st.manager_generation, st.manager_key, st.manager_event),
        string.format("selected %u -> %u detail=%u", st.manager_selected_before,
            st.manager_selected_after, st.manager_detail_selected),
        string.format("last op=%s", result),
    }
    if st.safe_mode ~= 0 then lines[#lines + 1] = "** SAFE MODE **" end
    if st.error_code ~= 0 then
        lines[#lines + 1] = string.format("error=%d", signed32(st.error_code))
    end
    if st.module_error ~= 0 then
        lines[#lines + 1] = string.format("module callback=%d", signed32(st.module_error))
    end
    if st.registry_stage ~= 0 or st.registry_saves ~= 0 then
        lines[#lines + 1] = string.format("registry stage=%u errno=%u saves=%u",
            st.registry_stage, st.registry_errno, st.registry_saves)
    end
    for i, mod in ipairs(st.modules) do
        if mod.state ~= 0 and mod.state ~= 12 then
            lines[#lines + 1] = format_module(mod, i - 1)
        end
    end
    return table.concat(lines, "\n")
end

local function restore_enabled_modules()
    local ok, message = write_command(CMD_RESTORE_AFTER_BOOT, 0, 0)
    if not ok then return false, tostring(message) end
    local st, status_error = read_status()
    if not st then return false, tostring(status_error) end
    if st.pending_op ~= CMD_RESTORE_AFTER_BOOT or st.pending_state ~= 5 then
        return false, format_status(st)
    end
    return true, format_status(st)
end

-- Forward-declared UI globals: `refresh_status` (defined below) references
-- `status` and is called from button handlers, so the label must be declared
-- before that function even though it is created later in the LVGL section.
local status
local busy = false
local install_stage = 0

local function refresh_status(text)
    local st, message = read_status()
    if not st then
        -- A module registry entry only proves modlib accepted the ELF. If the
        -- device is absent, the constructor either rejected the exact firmware
        -- identity or register_driver failed.
        local module = find_module()
        local dev = io.open(DEVICE_PATH, "rb")
        if dev then dev:close() end
        if module and not dev then
            status:set { text = "Supervisor appears in modlib, but /dev/canopus is missing.\n"
                .. "Exact firmware identity guard rejected it or register_driver failed.\n"
                .. "Verify the target-specific artifact, then reboot before retrying." }
        else
            status:set { text = tostring(message) }
        end
        return nil, message
    end
    status:set { text = text and (text .. "\n" .. format_status(st))
        or format_status(st) }
    return st
end

local firmware_version = detect_firmware_version()
local band9_target = select_band9_target(firmware_version)
local band9_selected = band9_target ~= nil
    and band9_profile_ready() and has_band9_bootstrap()
if firmware_version == "3.101.036" or firmware_version == "3.101.043" then
    MODULE_PATH = band10_module_path_for(firmware_version)
end
local firmware_supported = band9_selected
if MODULE_PATH then firmware_supported = verify_module_file() end

-- ---- LVGL page (336x480) -------------------------------------------

local rootbase = lvgl.Object(nil, {
    w = lvgl.HOR_RES(), h = lvgl.VER_RES(), bg_color = 0x07111F,
    bg_opa = lvgl.OPA(100), border_width = 0,
})
rootbase:clear_flag(lvgl.FLAG.SCROLLABLE)
local root = lvgl.Object(rootbase, {
    w = 336, h = 480, bg_color = 0x07111F, bg_opa = lvgl.OPA(100),
    border_width = 0, pad_all = 0, align = lvgl.ALIGN.CENTER,
})
root:clear_flag(lvgl.FLAG.SCROLLABLE)

lvgl.Label(root, { text = "Canopus Installer", text_color = 0xFFFFFF,
    align = { type = lvgl.ALIGN.TOP_MID, x_ofs = 0, y_ofs = 8 } })

status = lvgl.Label(root, {
    text = "Load supervisor first.", text_color = 0xBFD9FF, bg_color = 0x0A1830,
    bg_opa = lvgl.OPA(100), pad_all = 4, width = 320, height = 208,
    align = { type = lvgl.ALIGN.TOP_MID, x_ofs = 0, y_ofs = 30 },
})

local function make_button(text, x, y, color, width, onClicked)
    local button = lvgl.Object(root, {
        w = width or 134, h = 34, bg_color = color, bg_opa = lvgl.OPA(100),
        align = { type = lvgl.ALIGN.CENTER, x_ofs = x, y_ofs = y },
    })
    button:clear_flag(lvgl.FLAG.SCROLLABLE)
    button:add_flag(lvgl.FLAG.CLICKABLE)
    lvgl.Label(button, { text = text, text_color = 0xFFFFFF,
        align = lvgl.ALIGN.CENTER })
    button:onClicked(function()
        if busy then return end
        busy = true
        local ok, err = pcall(onClicked)
        busy = false
        if not ok then status:set { text = "error: " .. tostring(err) } end
    end)
    return button
end

-- Row 1: supervisor lifecycle
local load_button = make_button("LOAD", -96, 74, 0x14508A, 134, function()
    if supervisor_present() then
        local restored, restore_message = restore_enabled_modules()
        status:set { text = restored and
            ("Already loaded; enabled modules restored.\n" .. restore_message) or
            ("Already loaded, but boot restore failed:\n" .. restore_message) }
        return
    end
    if not firmware_supported then
        status:set { text = "Firmware version not supported\n"
            .. tostring(firmware_version or "Unknown") }
        return
    end
    local valid, message = verify_module_file(
        band9_selected and BAND9_SUPERVISOR_RESOURCE or MODULE_PATH)
    if not valid then status:set { text = tostring(message) }; return end
    status:set { text = "Loading supervisor..." }
    local inserted
    if band9_selected then
        inserted, message = load_band9_stage1()
    else
        inserted = run(string.format("insmod %s %s",
            shell_quote(MODULE_PATH), MODULE_NAME))
    end
    if not inserted or not supervisor_present() then
        status:set { text = supervisor_present() and
            "Supervisor appeared after error. Reboot; do not retry." or
            ("Load failed: " .. tostring(message or "save log; do not retry")) }
    else
        local restored, restore_message = restore_enabled_modules()
        if restored then
            status:set { text = "Supervisor loaded; enabled modules restored.\n"
                .. restore_message }
        else
            status:set { text = "Supervisor loaded, but boot restore failed:\n"
                .. restore_message .. "\nReboot before retrying." }
        end
    end
end)

local refresh_button = make_button("REFRESH", 96, 74, 0x14355A, 134, function()
    if not supervisor_present() then status:set { text = "Load supervisor first." }
        return end
    refresh_status()
end)

-- Row 2: install
local install_button = make_button("INSTALL", -96, 118, 0x1E6B2E, 134, function()
    if not supervisor_present() then
        status:set { text = "Load supervisor first." }
        return
    end
    local icon_ok, icon_error = stage_manager_icon()
    if not icon_ok then
        status:set { text = "Manager icon staging failed: " .. tostring(icon_error) }
        return
    end
    local stage_text = {
        [0] = "Registering Manager from miwear context...",
        [1] = "Registering module apps and pages...",
        [2] = "Publishing module Launcher entries from a fresh event turn...",
    }
    status:set { text = stage_text[install_stage] or "Invalid install stage" }
    local requested_stage = install_stage
    local ok, message = write_command(CMD_INSTALL, requested_stage, 0)
    if not ok then
        local operation = ({
            [0] = "Manager install",
            [1] = "Module app registration",
            [2] = "Module Launcher publication",
        })[requested_stage] or "Install"
        status:set { text = operation .. " request failed: " .. tostring(message) }
        return
    end
    local st, status_error = read_status()
    if not st then
        status:set { text = "Install status unavailable: " .. tostring(status_error) }
    elseif st.pending_op ~= CMD_INSTALL or st.pending_state ~= 5 then
        local operation = ({
            [0] = "Manager registration",
            [1] = "Module app registration",
            [2] = "Module Launcher publication",
        })[requested_stage] or "Install"
        status:set { text = operation .. " did not complete.\n"
            .. format_status(st) .. "\nReboot before retrying." }
    elseif requested_stage == 0 then
        install_stage = 1
        status:set { text = "Manager registered; its event was returned to miwear.\n\n"
            .. "Press INSTALL again to register loaded module apps and pages.\n"
            .. format_status(st) }
    elseif requested_stage == 1 then
        install_stage = 2
        status:set { text = "Module apps/pages registered; their events returned to miwear.\n\n"
            .. "Press INSTALL a third time to add module Launcher entries.\n"
            .. format_status(st) }
    else
        install_stage = 0
        status:set { text = "Module Launcher entries published in a separate miwear transaction.\n"
            .. format_status(st) }
    end
end)

local query_button = make_button("QUERY", 96, 118, 0x14355A, 134, function()
    if not find_module() then status:set { text = "Load supervisor first." }
        return end
    local ok, message = write_command(CMD_QUERY, 0, 0)
    if not ok then status:set { text = "Query rejected: " .. tostring(message) }
    else
        refresh_status("Queried")
    end
end)

-- Row 3: persist the next-boot enable intent. LOAD applies it only after the
-- required reboot and only after the supervisor's own insmod has returned.
local enable_button = make_button("ENABLE 0", -96, 162, 0x1E6B2E, 134, function()
    if not find_module() then status:set { text = "Load supervisor first." }
        return end
    local ok, message = write_command(CMD_ENABLE, 0, 0)
    if not ok then
        status:set { text = "Enable rejected: " .. tostring(message) }
        return
    end
    refresh_status("Enabled slot 0 for next boot; reboot required")
end)

local disable_button = make_button("DISABLE 0", 96, 162, 0x8A4A14, 134, function()
    if not find_module() then status:set { text = "Load supervisor first." }
        return end
    local ok, message = write_command(CMD_DISABLE, 0, 0)
    if not ok then status:set { text = "Disable rejected: " .. tostring(message) }
    else
        refresh_status("Disable 0 sent")
    end
end)

-- Row 4: remove / rollback / safe mode
local remove_button = make_button("REMOVE 0", -96, 206, 0x8A1F14, 134, function()
    if not find_module() then status:set { text = "Load supervisor first." }
        return end
    local ok, message = write_command(CMD_REMOVE, 0, 0)
    if not ok then status:set { text = "Remove rejected: " .. tostring(message) }
    else
        refresh_status("Remove 0 sent")
    end
end)

local safe_button = make_button("SAFE MODE", 96, 206, 0x8A4A14, 134, function()
    if not find_module() then status:set { text = "Load supervisor first." }
        return end
    local ok, message = write_command(CMD_ENTER_SAFE_MODE, 0, 0)
    if not ok then status:set { text = "Safe mode rejected: " .. tostring(message) }
    else
        refresh_status("Safe mode sent")
    end
end)

-- Initial refresh on open (read-only; never loads anything).
local initial, initial_err = read_status()
if initial then
    status:set { text = format_status(initial) }
elseif not firmware_supported then
    status:set { text = "Firmware version not supported\n"
        .. tostring(firmware_version or "Unknown") }
else
    status:set { text = "Supervisor not present. Press LOAD.\nFirmware "
        .. firmware_version
        .. (initial_err and ("\n" .. tostring(initial_err)) or "") }
end
