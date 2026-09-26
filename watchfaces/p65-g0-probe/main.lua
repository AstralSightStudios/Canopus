-- P65 G0 probe watchface. No device command runs until an explicit tap.
-- This is only a 3.100.043 loader smoke test; it does not install Canopus.
local lvgl = require("lvgl")

local PROBE_PATH = SCRIPT_PATH .. "p65_g0_probe.bin"
local MODULE_NAME = "canopus_g0_probe"
local LSMOD_PATH = "/tmp/canopus-g0-lsmod.txt"
local FIRMWARE_VERSION_PROPERTY = "ro.build.version"
local FIRMWARE_VERSION_OUTPUT = "/tmp/p65-g0-firmware-version.txt"
local EXPECTED_FIRMWARE_VERSION = "3.100.043"
local MAX_PROBE_SIZE = 65536
local MIN_PROBE_SIZE = 128
local ELF_ET_REL = 1
local ELF_MACHINE_ARM = 40
local EXPECTED_ENTRY = 1

local state = "needs_check"
local loaded_by_face = false
local load_attempted = false
local status
local check_button
local load_button
local unload_button

local function set_status(text, color)
    status:set { text = tostring(text), text_color = color or 0xBFD9FF }
end

local function shell_quote(value)
    return "'" .. tostring(value):gsub("'", "'\\''") .. "'"
end

local function execute(command)
    if type(os) ~= "table" or type(os.execute) ~= "function" then
        return false, "os.execute unavailable"
    end
    local called, result, reason, code = pcall(os.execute, command)
    if not called then return false, tostring(result) end
    if result == true or result == 0 then return true end
    if result == nil and reason == "exit" and code == 0 then return true end
    return false, tostring(code or reason or result or "command failed")
end

local function read_all(path)
    if type(io) ~= "table" or type(io.open) ~= "function" then
        return nil, "io.open unavailable"
    end
    local opened, file = pcall(io.open, path, "rb")
    if not opened or not file then return nil, "cannot open " .. path end
    local read_ok, content = pcall(file.read, file, "*a")
    pcall(file.close, file)
    if not read_ok or type(content) ~= "string" then
        return nil, "cannot read " .. path
    end
    return content
end

local function read_firmware_version()
    local ok, message = execute("getprop " .. shell_quote(FIRMWARE_VERSION_PROPERTY)
        .. " > " .. shell_quote(FIRMWARE_VERSION_OUTPUT))
    if not ok then return nil, "getprop failed: " .. tostring(message) end
    local raw, read_error = read_all(FIRMWARE_VERSION_OUTPUT)
    if type(raw) ~= "string" then
        return nil, read_error or "cannot read firmware version"
    end
    local version = raw:match("^%s*(.-)%s*$")
    if not version or not version:match("^%d+%.%d+%.%d+$") then
        return nil, "invalid firmware version"
    end
    return version
end

local function le_u16(data, offset)
    local lo, hi = data:byte(offset + 1, offset + 2)
    if not hi then return nil end
    return lo + hi * 0x100
end

local function le_u32(data, offset)
    local b1, b2, b3, b4 = data:byte(offset + 1, offset + 4)
    if not b4 then return nil end
    return b1 + b2 * 0x100 + b3 * 0x10000 + b4 * 0x1000000
end

local function verify_probe_resource()
    if type(io) ~= "table" or type(io.open) ~= "function" then
        return false, "io.open unavailable"
    end
    local opened, file = pcall(io.open, PROBE_PATH, "rb")
    if not opened or not file then return false, "probe ELF is missing" end
    local read_ok, header = pcall(file.read, file, 52)
    local seek_ok, size = pcall(file.seek, file, "end")
    pcall(file.close, file)
    if not read_ok or not seek_ok then return false, "probe ELF read failed" end
    if type(header) ~= "string" or #header ~= 52
        or header:sub(1, 7) ~= "\127ELF\1\1\1" then
        return false, "resource is not ELF32 little-endian"
    end
    if le_u16(header, 16) ~= ELF_ET_REL
        or le_u16(header, 18) ~= ELF_MACHINE_ARM then
        return false, "resource is not ARM ET_REL"
    end
    if le_u32(header, 24) ~= EXPECTED_ENTRY then
        return false, "P65 module_initialize entry mismatch"
    end
    if type(size) ~= "number" or size < MIN_PROBE_SIZE
        or size > MAX_PROBE_SIZE then
        return false, "unexpected probe size"
    end
    return true
end

local function probe_is_loaded()
    local ok, message = execute("lsmod > " .. shell_quote(LSMOD_PATH))
    if not ok then return nil, "lsmod failed: " .. tostring(message) end
    local listing, read_error = read_all(LSMOD_PATH)
    if type(listing) ~= "string" then
        return nil, read_error or "cannot read lsmod output"
    end
    for line in (listing .. "\n"):gmatch("([^\r\n]*)[\r\n]") do
        local first = line:match("^%s*(%S+)")
        if first == MODULE_NAME then return true end
    end
    return false
end

local function set_button(button, enabled, color)
    if not button then return end
    if enabled then
        button:add_flag(lvgl.FLAG.CLICKABLE)
        button:set { bg_opa = lvgl.OPA(100), bg_color = color }
    else
        button:clear_flag(lvgl.FLAG.CLICKABLE)
        button:set { bg_opa = lvgl.OPA(45), bg_color = color }
    end
end

local function refresh_controls()
    set_button(check_button,
        state == "needs_check",
        0x34465C)
    set_button(load_button, state == "ready", 0x14508A)
    set_button(unload_button, state == "loaded" or state == "preexisting",
               0x8A1F14)
end

local function check_clicked()
    if state ~= "needs_check" then return end
    state = "busy"
    refresh_controls()
    set_status("Checking P65 firmware...")
    local version, version_error = read_firmware_version()
    if not version then
        state = "needs_check"
        set_status("CHECK FAILED\n" .. tostring(version_error), 0xFF9A9A)
        refresh_controls()
        return
    end
    if version ~= EXPECTED_FIRMWARE_VERSION then
        state = "unavailable"
        set_status("Wrong firmware: " .. tostring(version)
            .. "\nExpected P65 " .. EXPECTED_FIRMWARE_VERSION, 0xFF9A9A)
        refresh_controls()
        return
    end
    set_status("Checking module list...")
    local present, message = probe_is_loaded()
    if present == nil then
        state = "needs_check"
        set_status("CHECK FAILED\n" .. tostring(message), 0xFF9A9A)
    elseif present then
        state = "preexisting"
        set_status("Probe is already loaded.\nUnload, reboot, then start a fresh G0.",
                   0xFFD27A)
    else
        state = "ready"
        set_status("Ready. Confirm this is P65 3.100.043, then tap LOAD.")
    end
    refresh_controls()
end

local function load_clicked()
    if state ~= "ready" or load_attempted then return end
    load_attempted = true
    state = "busy"
    refresh_controls()
    set_status("Loading zero-import probe...")

    local valid, validation_error = verify_probe_resource()
    if not valid then
        state = "fault"
        set_status("Probe check failed\n" .. tostring(validation_error)
            .. "\nDo not retry before reboot.", 0xFF9A9A)
        refresh_controls()
        return
    end

    local ok, message = execute("insmod " .. shell_quote(PROBE_PATH)
        .. " " .. shell_quote(MODULE_NAME))
    if not ok then
        state = "fault"
        set_status("insmod failed\n" .. tostring(message)
            .. "\nReboot before retrying.", 0xFF9A9A)
        refresh_controls()
        return
    end

    local present, check_error = probe_is_loaded()
    if present ~= true then
        state = "fault"
        set_status("Load result is uncertain\n" .. tostring(check_error or
            "probe not listed") .. "\nReboot before retrying.", 0xFF9A9A)
        refresh_controls()
        return
    end

    loaded_by_face = true
    state = "loaded"
    set_status("Probe loaded. Tap UNLOAD to finish G0.", 0x8FF0A4)
    refresh_controls()
end

local function unload_clicked()
    if state ~= "loaded" and state ~= "preexisting" then return end
    local was_loaded_by_face = loaded_by_face
    state = "busy"
    refresh_controls()
    set_status("Unloading probe...")

    local ok, message = execute("rmmod " .. shell_quote(MODULE_NAME))
    if not ok then
        state = "fault"
        set_status("rmmod failed\n" .. tostring(message)
            .. "\nReboot; do not retry.", 0xFF9A9A)
        refresh_controls()
        return
    end

    local present, check_error = probe_is_loaded()
    if present ~= false then
        state = "fault"
        set_status("Unload result is uncertain\n" .. tostring(check_error or
            "probe still listed") .. "\nReboot; do not retry.", 0xFF9A9A)
        refresh_controls()
        return
    end

    if was_loaded_by_face then
        state = "passed"
        set_status("G0 PASS\nLoaded and unloaded; probe is absent from lsmod.",
                   0x8FF0A4)
    else
        state = "needs_reboot"
        set_status("Pre-existing probe unloaded.\nReboot before a fresh G0 test.",
                   0xFFD27A)
    end
    refresh_controls()
end

local width, height = lvgl.HOR_RES(), lvgl.VER_RES()
local button_width = math.min(width - 64, 260)
local button_height = math.min(48, math.floor(height * 0.105))
local button_gap = math.max(8, math.floor(height * 0.025))
local top = math.floor(height * 0.11)
local panel_top = math.floor(height * 0.27)
local panel_height = math.min(64, math.floor(height * 0.14))
local first_button_y = math.floor(height * 0.44)
local rootbase = lvgl.Object(nil, {
    w = width, h = height, bg_color = 0x07111F,
    bg_opa = lvgl.OPA(100), border_width = 0,
})
rootbase:clear_flag(lvgl.FLAG.SCROLLABLE)
local root = lvgl.Object(rootbase, {
    w = width, h = height, bg_color = 0x07111F,
    bg_opa = lvgl.OPA(100), border_width = 0, pad_all = 0,
    align = lvgl.ALIGN.CENTER,
})
root:clear_flag(lvgl.FLAG.SCROLLABLE)

lvgl.Label(root, {
    text = "P65 G0 Probe", text_color = 0xFFFFFF,
    w = width - 32, h = 32,
    align = { type = lvgl.ALIGN.TOP_MID, x_ofs = 0,
              y_ofs = top },
})
lvgl.Label(root, {
    text = "3.100.043 / zero-import load test", text_color = 0x91A6BF,
    w = width - 32, h = 26,
    align = { type = lvgl.ALIGN.TOP_MID, x_ofs = 0,
              y_ofs = top + 34 },
})

local panel = lvgl.Object(root, {
    w = math.min(width - 56, 300), h = panel_height, bg_color = 0x101D2D,
    bg_opa = lvgl.OPA(100), radius = 14, border_width = 0,
    align = { type = lvgl.ALIGN.TOP_MID, x_ofs = 0,
              y_ofs = panel_top },
})
panel:clear_flag(lvgl.FLAG.SCROLLABLE)
status = lvgl.Label(panel, {
    text = "No command runs automatically.\nTap CHECK before loading.",
    text_color = 0xBFD9FF, w = math.min(width - 76, 280), h = panel_height - 8,
    align = lvgl.ALIGN.CENTER,
})

local function make_button(text, y, color, callback)
    local button = lvgl.Object(root, {
        w = button_width, h = button_height, bg_color = color,
        bg_opa = lvgl.OPA(100), radius = 14, border_width = 0,
        align = { type = lvgl.ALIGN.TOP_MID, x_ofs = 0,
                  y_ofs = y },
    })
    button:clear_flag(lvgl.FLAG.SCROLLABLE)
    button:add_flag(lvgl.FLAG.CLICKABLE)
    lvgl.Label(button, {
        text = text, text_color = 0xFFFFFF, align = lvgl.ALIGN.CENTER,
    })
    button:onClicked(function()
        local ok, message = pcall(callback)
        if not ok then
            state = "fault"
            set_status("Unexpected error\n" .. tostring(message)
                .. "\nReboot before retrying.", 0xFF9A9A)
            refresh_controls()
        end
    end)
    return button
end

check_button = make_button("CHECK MODULES", first_button_y, 0x34465C,
                           check_clicked)
load_button = make_button("LOAD G0 PROBE", first_button_y + button_height
    + button_gap, 0x14508A, load_clicked)
unload_button = make_button("UNLOAD PROBE", first_button_y
    + 2 * (button_height + button_gap), 0x8A1F14, unload_clicked)

lvgl.Label(root, {
    text = "G0 only; reboot before retrying a failed load.",
    text_color = 0x91A6BF, w = width - 64, h = 24,
    align = { type = lvgl.ALIGN.BOTTOM_MID, x_ofs = 0, y_ofs = -22 },
})

local has_execute = type(os) == "table" and type(os.execute) == "function"
local resource_ok, resource_error = verify_probe_resource()
if not has_execute then
    state = "unavailable"
    set_status("This watchface runtime has no os.execute.", 0xFF9A9A)
elseif not resource_ok then
    state = "unavailable"
    set_status("Probe resource unavailable\n" .. tostring(resource_error), 0xFF9A9A)
else
    refresh_controls()
end
