-- Canopus installer watchface.
--
-- Drives the Canopus supervisor native module (which registers /dev/canopus)
-- exactly the way btpatch_phase5_watchface drives its module through
-- /dev/btpatch: verify the bundled ELF, insmod it, then read a fixed status
-- ABI and write a fixed command ABI over the char device.
--
-- Opening this watchface performs no native operation; every action requires
-- an explicit button press. The supervisor module is boot-resident: never
-- run `rmmod`, never insert a second copy, reboot for complete recovery.

local lvgl = require("lvgl")

local MODULE_PATH = SCRIPT_PATH .. "canopus_supervisor.bin"
local MODULE_NAME = "canopus_supervisor"
local DEVICE_PATH = "/dev/canopus"
-- The supervisor is a small module (~3-4 KB with -Os); the 4096 floor copied
-- from btpatch was sized for that project's 33 KB A2DP amalgamation. 512 still
-- rejects truncated/empty resources while allowing a minimal valid module.
local MODULE_MIN_SIZE = 512
local MODULE_MAX_SIZE = 131072
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

local function find_module()
    local content = read_all("/proc/modules", "r")
    if type(content) ~= "string" then return nil end
    for line in content:gmatch("[^\r\n]+") do
        if line:match("^" .. MODULE_NAME .. ",") then return line end
    end
    return nil
end

local function verify_module_file()
    if type(io) ~= "table" or type(io.open) ~= "function" then
        return false, "io.open unavailable"
    end
    local file = io.open(MODULE_PATH, "rb")
    if not file then return false, "Missing canopus_supervisor.bin" end
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
--   36..127 reserved
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
        string.format("last op=%s", result),
    }
    if st.safe_mode ~= 0 then lines[#lines + 1] = "** SAFE MODE **" end
    if st.error_code ~= 0 then
        lines[#lines + 1] = string.format("error=%d", signed32(st.error_code))
    end
    for i, mod in ipairs(st.modules) do
        if mod.state ~= 0 and mod.state ~= 12 then
            lines[#lines + 1] = format_module(mod, i - 1)
        end
    end
    return table.concat(lines, "\n")
end

-- Forward-declared UI globals: `refresh_status` (defined below) references
-- `status` and is called from button handlers, so the label must be declared
-- before that function even though it is created later in the LVGL section.
local status
local busy = false

local function refresh_status(text)
    local st, message = read_status()
    if not st then
        -- Distinguish "module not loaded" from "module loaded but device
        -- missing" (the current stub-platform boundary, G0/G4).
        local module = find_module()
        local dev = io.open(DEVICE_PATH, "rb")
        if dev then dev:close() end
        if module and not dev then
            status:set { text = "Supervisor loaded (insmod OK).\n"
                .. "/dev/canopus missing — device platform pending (G0/G4).\n"
                .. "Reboot before retrying." }
        else
            status:set { text = tostring(message) }
        end
        return nil, message
    end
    status:set { text = text and (text .. "\n" .. format_status(st))
        or format_status(st) }
    return st
end

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
    if find_module() then
        status:set { text = "Already loaded. Never load twice or rmmod." }
        return
    end
    local valid, message = verify_module_file()
    if not valid then status:set { text = tostring(message) }; return end
    status:set { text = "Loading supervisor..." }
    local inserted = run(string.format("insmod %s %s",
        shell_quote(MODULE_PATH), MODULE_NAME))
    if not inserted or not find_module() then
        status:set { text = find_module() and
            "Supervisor appeared after error. Reboot; do not retry." or
            "Load failed. Save log; do not retry." }
    else
        refresh_status("Supervisor loaded")
    end
end)

local refresh_button = make_button("REFRESH", 96, 74, 0x14355A, 134, function()
    if not find_module() then status:set { text = "Load supervisor first." }
        return end
    refresh_status()
end)

-- Row 2: install
local install_button = make_button("INSTALL", -96, 118, 0x1E6B2E, 134, function()
    if not find_module() then status:set { text = "Load supervisor first." }
        return end
    local ok, message = write_command(CMD_INSTALL, 0, 0)
    if not ok then
        status:set { text = "Install rejected: " .. tostring(message) }
    else
        refresh_status("Install staged")
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

-- Row 3: per-module enable / disable (arg0 = module index)
local enable_button = make_button("ENABLE 0", -96, 162, 0x1E6B2E, 134, function()
    if not find_module() then status:set { text = "Load supervisor first." }
        return end
    local ok, message = write_command(CMD_ENABLE, 0, 0)
    if not ok then status:set { text = "Enable rejected: " .. tostring(message) }
    else
        refresh_status("Enable 0 sent")
    end
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
else
    status:set { text = "Supervisor not present. Press LOAD."
        .. (initial_err and ("\n" .. tostring(initial_err)) or "") }
end
