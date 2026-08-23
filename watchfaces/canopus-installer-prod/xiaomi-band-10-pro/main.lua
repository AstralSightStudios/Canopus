-- Production Canopus installer for Xiaomi Band 10.
--
-- Run performs, in order:
--   supervisor LOAD -> apply restored boot intents -> INSTALL 0 -> 1 -> 2.
-- Each supervisor command is issued from a separate LuaLVGL timer callback so
-- miwear receives an event-loop turn between native registration stages.

local lvgl = require("lvgl")

local TARGET_ID_PREFIX = "xiaomi-band-10-pro-"
local FIRMWARE_VERSION_PROPERTY = "ro.build.version"
local FIRMWARE_VERSION_OUTPUT = "/data/canopus-installer-firmware-version.tmp"
local MODULE_PATH
local MODULE_NAME = "canopus_supervisor"
local MANAGER_ICON_RESOURCE = SCRIPT_PATH .. "manager_icon.bin"
local MANAGER_ICON_PATH = "/data/canopus/manager_icon.bin"
local DEVICE_PATH = "/dev/canopus"
local MODULE_MIN_SIZE = 512
local MODULE_MAX_SIZE = 262144
local STATUS_SIZE = 384
local EXPECTED_MAGIC = 0x43505331 -- "CPS1"
local EXPECTED_CMD_MAGIC = 0x43504331 -- "CPC1"
local CMD_INSTALL = 0x43510002
local CMD_RESTORE_AFTER_BOOT = 0x4351000A
local RESULT_COMPLETED = 5

local status
local run_timer
local run_phase = 1
local run_attempted = false
local clear_armed = false

local function shell_quote(value)
    return "'" .. tostring(value):gsub("'", "'\\''") .. "'"
end

local function run(command)
    print("[canopus-installer-prod] exec: " .. command)
    local ok = os.execute(command)
    return ok == true or ok == 0
end

local function set_status(text, color)
    status:set { text = tostring(text), text_color = color or 0xBFD9FF }
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

local function module_path_for(version)
    return SCRIPT_PATH .. "canopus_supervisor-" .. TARGET_ID_PREFIX
        .. version .. ".bin"
end

local function supervisor_present()
    local file = io.open(DEVICE_PATH, "rb")
    if not file then return false end
    file:close()
    return true
end

local function verify_module_file()
    if type(io) ~= "table" or type(io.open) ~= "function" then
        return false, "io.open unavailable"
    end
    local file = io.open(MODULE_PATH, "rb")
    if not file then return false, "missing matching Supervisor" end
    local header = file:read(20)
    local size = file:seek("end")
    file:close()
    if type(header) ~= "string" or #header ~= 20
        or header:sub(1, 4) ~= "\127ELF"
        or header:byte(5) ~= 1 or header:byte(6) ~= 1
        or header:byte(7) ~= 1 then
        return false, "resource is not ELF32 little-endian"
    end
    local elf_type = header:byte(17) + header:byte(18) * 0x100
    local machine = header:byte(19) + header:byte(20) * 0x100
    if elf_type ~= 1 or machine ~= 40 then
        return false, "resource is not relocatable ARM ELF"
    end
    if type(size) ~= "number" or size < MODULE_MIN_SIZE
        or size > MODULE_MAX_SIZE then
        return false, "unexpected supervisor size"
    end
    return true
end

local function stage_manager_icon()
    local content = read_all(MANAGER_ICON_RESOURCE, "rb")
    if type(content) ~= "string" or #content < 13
        or content:byte(1) ~= 0x19 then
        return false, "missing or invalid manager_icon.bin"
    end
    local width = content:byte(5) + content:byte(6) * 0x100
    local height = content:byte(7) + content:byte(8) * 0x100
    if width < 1 or height < 1 or #content ~= 12 + width * height * 4 then
        return false, "manager_icon.bin size mismatch"
    end
    local output = io.open(MANAGER_ICON_PATH, "wb")
    if not output then
        if not run("mkdir /data/canopus") then
            return false, "cannot create /data/canopus"
        end
        output = io.open(MANAGER_ICON_PATH, "wb")
    end
    if not output then return false, "cannot stage Manager icon" end
    local write_ok, write_result = pcall(output.write, output, content)
    local close_ok, close_result = pcall(output.close, output)
    if not write_ok or write_result == nil
        or not close_ok or close_result == nil then
        return false, "Manager icon write failed"
    end
    if read_all(MANAGER_ICON_PATH, "rb") ~= content then
        return false, "Manager icon verification failed"
    end
    return true
end

local function bytes_to_words(content)
    if type(content) ~= "string" or #content ~= STATUS_SIZE then return nil end
    local words = {}
    for offset = 1, STATUS_SIZE, 4 do
        local a, b, c, d = content:byte(offset, offset + 3)
        words[#words + 1] = a + b * 0x100 + c * 0x10000 + d * 0x1000000
    end
    return words
end

local function signed32(value)
    if value >= 0x80000000 then return value - 0x100000000 end
    return value
end

local function read_status()
    local file = io.open(DEVICE_PATH, "rb")
    if not file then return nil, "cannot open /dev/canopus" end
    local raw = file:read(STATUS_SIZE)
    file:close()
    local words = bytes_to_words(raw)
    if not words or words[1] ~= EXPECTED_MAGIC then
        return nil, "supervisor status ABI mismatch"
    end
    if words[2] ~= 1 then return nil, "supervisor ABI is not version 1" end
    if words[10] ~= words[11] or words[10] % 2 ~= 0 then
        return nil, "supervisor status snapshot is inconsistent"
    end
    return {
        pending_op = words[6],
        pending_state = words[7],
        error_code = signed32(words[9]),
    }
end

local function word(value)
    value = math.floor(value)
    return string.char(value % 0x100, math.floor(value / 0x100) % 0x100,
        math.floor(value / 0x10000) % 0x100,
        math.floor(value / 0x1000000) % 0x100)
end

local function write_command(command, arg0)
    local payload = word(EXPECTED_CMD_MAGIC) .. word(command)
        .. word(arg0 or 0) .. word(0)
    local file = io.open(DEVICE_PATH, "wb")
    if not file then return false, "cannot open /dev/canopus" end
    local write_ok, write_result, write_error = pcall(file.write, file, payload)
    local close_ok, close_result, close_error = pcall(file.close, file)
    if not write_ok or write_result == nil then
        return false, tostring(write_error or write_result or "write failed")
    end
    if not close_ok or close_result == nil then
        return false, tostring(close_error or close_result or "close failed")
    end
    return true
end

local function execute_step(command, arg0)
    local ok, message = write_command(command, arg0)
    if not ok then return false, message end
    local current, status_error = read_status()
    if not current then return false, status_error end
    if current.pending_op ~= command or current.pending_state ~= RESULT_COMPLETED then
        return false, string.format("result=%d error=%d",
            current.pending_state, current.error_code)
    end
    return true
end

local steps = {
    { command = CMD_RESTORE_AFTER_BOOT, arg0 = 0,
      progress = "Loading enabled modules..." },
    { command = CMD_INSTALL, arg0 = 0,
      progress = "Registering Manager..." },
    { command = CMD_INSTALL, arg0 = 1,
      progress = "Registering module apps..." },
    { command = CMD_INSTALL, arg0 = 2,
      progress = "Publishing Launcher entries..." },
}

local function finish_run(timer, success, message)
    timer:delete()
    if run_timer == timer then run_timer = nil end
    set_status(message, success and 0x8FF0A4 or 0xFF9A9A)
end

local function run_next_step(timer)
    local step = steps[run_phase]
    if not step then
        finish_run(timer, true, "Run completed")
        return
    end
    set_status(step.progress)
    local ok, message = execute_step(step.command, step.arg0)
    if not ok then
        finish_run(timer, false, "Run failed: " .. tostring(message)
            .. "\nReboot before retrying.")
        return
    end
    run_phase = run_phase + 1
    if run_phase > #steps then
        finish_run(timer, true, "Run completed")
    else
        timer:ready()
    end
end

local function start_run_timer()
    run_phase = 1
    local created = lvgl.Timer {
        period = 1000,
        repeat_count = -1,
        paused = true,
        cb = function(timer)
            local ok, message = pcall(run_next_step, timer)
            if not ok then
                finish_run(timer, false, "Run failed: " .. tostring(message)
                    .. "\nReboot before retrying.")
            end
        end,
    }
    if not created then return false, "cannot create LuaLVGL timer" end
    run_timer = created
    created:resume()
    created:ready()
    return true
end

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

local function make_button(text, y, color, on_clicked)
    local button = lvgl.Object(root, {
        w = 220, h = 52, bg_color = color, bg_opa = lvgl.OPA(100),
        radius = 16,
        align = { type = lvgl.ALIGN.CENTER, x_ofs = 0, y_ofs = y },
    })
    button:clear_flag(lvgl.FLAG.SCROLLABLE)
    button:add_flag(lvgl.FLAG.CLICKABLE)
    lvgl.Label(button, {
        text = text, text_color = 0xFFFFFF, align = lvgl.ALIGN.CENTER,
    })
    button:onClicked(function()
        local ok, message = pcall(on_clicked)
        if not ok then set_status("Error: " .. tostring(message), 0xFF9A9A) end
    end)
    return button
end

local firmware_version = detect_firmware_version()
if firmware_version then MODULE_PATH = module_path_for(firmware_version) end
local module_supported = false
if MODULE_PATH then module_supported = verify_module_file() end
if not module_supported then
    status = lvgl.Label(root, {
        text = "Firmware version not supported\n"
            .. tostring(firmware_version or "Unknown"),
        text_color = 0xFF9A9A, width = 300, height = 96,
        align = lvgl.ALIGN.CENTER,
    })
    return
end

local run_button
run_button = make_button("Run", -36, 0x14508A, function()
    clear_armed = false
    if run_attempted then
        set_status("Run can only be used once; reboot before retrying")
        return
    end
    run_attempted = true
    run_button:clear_flag(lvgl.FLAG.CLICKABLE)
    local valid, validation_error = verify_module_file()
    if not valid then
        set_status("LOAD failed: " .. tostring(validation_error)
            .. "\nEnsure the firmware version matches this installer.", 0xFF9A9A)
        return
    end
    if not supervisor_present() then
        set_status("Loading supervisor...")
        local inserted = run(string.format("insmod %s %s",
            shell_quote(MODULE_PATH), MODULE_NAME))
        if not inserted or not supervisor_present() then
            set_status("LOAD failed. Ensure the firmware version matches this installer."
                .. "\nReboot before retrying.", 0xFF9A9A)
            return
        end
    end
    local icon_ok, icon_error = stage_manager_icon()
    if not icon_ok then
        set_status("Run failed: " .. tostring(icon_error), 0xFF9A9A)
        return
    end
    local started, timer_error = start_run_timer()
    if not started then
        set_status("Run failed: " .. tostring(timer_error), 0xFF9A9A)
    else
        set_status("Supervisor loaded; scheduling boot restore...")
    end
end)

make_button("Clear Env", 36, 0x8A1F14, function()
    if run_timer then
        clear_armed = false
        set_status("Run is in progress; reboot before clearing")
        return
    end
    if not clear_armed then
        clear_armed = true
        set_status("Click again to clear", 0xFFD27A)
        return
    end
    clear_armed = false
    if run("rm -rf /data/canopus") then
        set_status("Environment cleared; reboot before Run", 0x8FF0A4)
    else
        set_status("Clear Env failed", 0xFF9A9A)
    end
end)

status = lvgl.Label(root, {
    text = "Ready\nFirmware " .. firmware_version,
    text_color = 0xBFD9FF, width = 300, height = 96,
    align = { type = lvgl.ALIGN.CENTER, x_ofs = 0, y_ofs = 132 },
})
