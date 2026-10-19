-- Production Canopus installer for Xiaomi Band 10.
--
-- Run performs, in order:
--   supervisor LOAD -> INSTALL 0 (Manager app) -> INSTALL 2 (Manager Launcher)
--   -> apply restored boot intents (third-party modules) -> INSTALL 1 -> INSTALL 2.
-- Prioritizing Manager ensures the Canopus Manager UI is always accessible,
-- even if an individual third-party module encounters an activation error.
--
-- Auto-run (opt-in, off by default) performs the same Run by itself a few seconds
-- after this watchface starts. /data/canopus/autorun enables it;
-- /data/canopus/autorun.pending exists while an automatic Run is in progress, so
-- a Run that never finished (the device reset in the middle of it) is found at
-- the next start and turns Auto-run off instead of repeating.

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
local CPC2_MAGIC = 0x43504332
local CPC2_HEADER = 36
local CMD_QUERY_DEVICE = 9
local CMD_INSTALL = 13           -- NATIVE_STAGE: payload-free INSTALL stages 0..2
local CMD_RESTORE_AFTER_BOOT = 14
local RESULT_COMPLETED = 5
local request_id = 0

local status
local run_timer
local run_phase = 1
local run_attempted = false
local clear_armed = false
local module_warning = nil
local AUTORUN_FLAG = "/data/canopus/autorun"
local AUTORUN_PENDING = "/data/canopus/autorun.pending"
local AUTORUN_DELAY_MS = 10000
local autorun_active = false  -- the Run in progress was started by Auto-run
local autorun_timer
local refresh_autorun_button

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

local function path_exists(path)
    local file = io.open(path, "rb")
    if not file then return false end
    file:close()
    return true
end

local function remove_file(path)
    pcall(os.remove, path)
end

local function write_marker(path)
    run("mkdir /data/canopus")
    local file = io.open(path, "wb")
    if not file then return false end
    local ok, written = pcall(file.write, file, "1")
    file:close()
    return ok and written ~= nil
end

-- An automatic Run that ended, however it ended, is no longer pending. One that
-- failed also switches Auto-run off so it cannot repeat at every boot.
local function settle_autorun(success)
    if not autorun_active then return end
    autorun_active = false
    remove_file(AUTORUN_PENDING)
    if not success then
        remove_file(AUTORUN_FLAG)
        refresh_autorun_button()
    end
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
    run("mkdir /data/canopus/inbox")
    return true
end

local function signed32(value)
    if value >= 0x80000000 then return value - 0x100000000 end
    return value
end

local function u32le(raw, offset)
    return (string.unpack("<I4", raw, offset + 1))
end

-- One CPC2 request and its reply, kept together: /dev/canopus has a shared
-- result mailbox, so nothing may yield between the write and the read.
local function exchange(command, payload, reply_size)
    payload = payload or ""
    request_id = request_id + 1
    local request = string.pack("<I4I2I2I2I2I4I4I4I4I4I4", CPC2_MAGIC, CPC2_HEADER,
        1, 1, 0, CPC2_HEADER + #payload, command, request_id, 0, 0, #payload) .. payload
    local file = io.open(DEVICE_PATH, "wb")
    if not file then return nil, "cannot open /dev/canopus" end
    local write_ok, write_result, write_error = pcall(file.write, file, request)
    local close_ok, close_result = pcall(file.close, file)
    if not write_ok or write_result == nil or not close_ok or close_result == nil then
        return nil, tostring(write_error or write_result or "write failed")
    end
    file = io.open(DEVICE_PATH, "rb")
    if not file then return nil, "cannot open /dev/canopus" end
    local reply = file:read(reply_size)
    file:close()
    if type(reply) ~= "string" or #reply ~= reply_size
        or u32le(reply, 0) ~= CPC2_MAGIC or u32le(reply, 16) ~= command
        or u32le(reply, 20) ~= request_id then
        return nil, "supervisor reply ABI mismatch"
    end
    return reply
end

local function device_error()
    local reply = exchange(CMD_QUERY_DEVICE, nil, CPC2_HEADER + 36)
    return reply and signed32(u32le(reply, CPC2_HEADER + 24)) or 0
end

local function execute_step(command, arg0)
    local reply, message = exchange(command,
        command == CMD_INSTALL and string.pack("<I4", arg0 or 0) or nil, CPC2_HEADER)
    if not reply then return false, message end
    local result = u32le(reply, 28)
    if result ~= RESULT_COMPLETED then
        return false, string.format("result=%d error=%d", result, device_error())
    end
    return true
end

-- 注册顺序：以注册管理器原生应用为最高优先级！
-- 1. 先注册 Manager (Stage 0) 并发布 Launcher 入口 (Stage 2)
-- 2. 随后再恢复与加载已启用的第三方模块 (即使模块故障，管理器也已就位，不发生死锁)
-- 3. 最后刷新各模块的原生应用页面与桌面图标
local steps = {
    { command = CMD_INSTALL, arg0 = 0,
      progress = "Registering Manager...", critical = true },
    { command = CMD_INSTALL, arg0 = 2,
      progress = "Publishing Launcher entries...", critical = true },
    { command = CMD_RESTORE_AFTER_BOOT, arg0 = 0,
      progress = "Loading enabled modules...", critical = false },
    { command = CMD_INSTALL, arg0 = 1,
      progress = "Registering module apps...", critical = false },
    { command = CMD_INSTALL, arg0 = 2,
      progress = "Updating Launcher entries...", critical = false },
}

local function finish_run(timer, success, message)
    timer:delete()
    if run_timer == timer then run_timer = nil end
    set_status(message, success and 0x8FF0A4 or 0xFF9A9A)
    settle_autorun(success)
end

local function run_next_step(timer)
    local step = steps[run_phase]
    if not step then
        if module_warning then
            finish_run(timer, true, "Manager registered!\nWarning: " .. tostring(module_warning)
                .. "\nCheck Manager app to fix module.")
        else
            finish_run(timer, true, "Run completed")
        end
        return
    end
    set_status(step.progress)
    local ok, message = execute_step(step.command, step.arg0)
    if not ok then
        if step.critical then
            finish_run(timer, false, "Run failed: " .. tostring(message)
                .. "\nReboot before retrying.")
            return
        else
            print("[canopus-installer-prod] non-critical step failed: " .. tostring(message))
            module_warning = message
        end
    end
    run_phase = run_phase + 1
    if run_phase > #steps then
        if module_warning then
            finish_run(timer, true, "Manager registered!\nWarning: " .. tostring(module_warning)
                .. "\nCheck Manager app to fix module.")
        else
            finish_run(timer, true, "Run completed")
        end
    else
        timer:ready()
    end
end

local function start_run_timer()
    run_phase = 1
    module_warning = nil
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
    local label = lvgl.Label(button, {
        text = text, text_color = 0xFFFFFF, align = lvgl.ALIGN.CENTER,
    })
    button:onClicked(function()
        local ok, message = pcall(on_clicked)
        if not ok then set_status("Error: " .. tostring(message), 0xFF9A9A) end
    end)
    return button, label
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
local function abort_run(message)
    set_status(message, 0xFF9A9A)
    settle_autorun(false)
end

local function do_run()
    clear_armed = false
    if run_attempted then
        set_status("Run can only be used once; reboot before retrying")
        return
    end
    run_attempted = true
    run_button:clear_flag(lvgl.FLAG.CLICKABLE)
    local valid, validation_error = verify_module_file()
    if not valid then
        abort_run("LOAD failed: " .. tostring(validation_error)
            .. "\nEnsure the firmware version matches this installer.")
        return
    end
    if not supervisor_present() then
        set_status("Loading supervisor...")
        local inserted = run(string.format("insmod %s %s",
            shell_quote(MODULE_PATH), MODULE_NAME))
        if not inserted or not supervisor_present() then
            abort_run("LOAD failed. Ensure the firmware version matches this installer."
                .. "\nReboot before retrying.")
            return
        end
    end
    local icon_ok, icon_error = stage_manager_icon()
    if not icon_ok then
        abort_run("Run failed: " .. tostring(icon_error))
        return
    end
    local started, timer_error = start_run_timer()
    if not started then
        abort_run("Run failed: " .. tostring(timer_error))
    else
        set_status("Supervisor loaded; scheduling Manager registration...")
    end
end

run_button = make_button("Run", -72, 0x14508A, do_run)

local autorun_button, autorun_label
autorun_button, autorun_label = make_button("Auto-run", 0, 0x2B5D3A, function()
    clear_armed = false
    if path_exists(AUTORUN_FLAG) then
        remove_file(AUTORUN_FLAG)
        set_status("Auto-run off")
    elseif write_marker(AUTORUN_FLAG) then
        set_status("Auto-run on: Canopus loads itself after the next boot.\n"
            .. "It turns itself off if a run does not finish.", 0x8FF0A4)
    else
        set_status("Cannot enable Auto-run", 0xFF9A9A)
    end
    refresh_autorun_button()
end)

refresh_autorun_button = function()
    autorun_label:set { text = path_exists(AUTORUN_FLAG) and "Auto-run: ON" or "Auto-run: OFF" }
end

make_button("Clear Env", 72, 0x8A1F14, function()
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
    refresh_autorun_button()
end)

status = lvgl.Label(root, {
    text = "Ready\nFirmware " .. firmware_version,
    text_color = 0xBFD9FF, width = 300, height = 96,
    align = { type = lvgl.ALIGN.CENTER, x_ofs = 0, y_ofs = 170 },
})

local function start_autorun(timer)
    timer:delete()
    autorun_timer = nil
    if run_attempted or supervisor_present() then return end
    if not write_marker(AUTORUN_PENDING) then
        set_status("Auto-run skipped: cannot write its marker", 0xFF9A9A)
        return
    end
    autorun_active = true
    set_status("Auto-run: starting...")
    local ok, message = pcall(do_run)
    if not ok then abort_run("Auto-run failed: " .. tostring(message)) end
end

-- A pending marker at start means the automatic Run of the last boot never
-- finished: leave Auto-run off until it is switched on again.
local autorun_unfinished = path_exists(AUTORUN_PENDING)
if autorun_unfinished then
    remove_file(AUTORUN_PENDING)
    remove_file(AUTORUN_FLAG)
end
refresh_autorun_button()
if autorun_unfinished then
    set_status("Auto-run turned off: the last automatic run did not finish", 0xFFD27A)
elseif path_exists(AUTORUN_FLAG) and not supervisor_present() then
    set_status("Auto-run: starting in " .. tostring(AUTORUN_DELAY_MS // 1000) .. " s")
    autorun_timer = lvgl.Timer {
        period = AUTORUN_DELAY_MS,
        repeat_count = -1,
        cb = function(timer)
            local ok, message = pcall(start_autorun, timer)
            if not ok then set_status("Auto-run failed: " .. tostring(message), 0xFF9A9A) end
        end,
    }
end

-- The timer must not outlive the page it would report to.
if lvgl.EVENT and lvgl.EVENT.DELETE then
    rootbase:onevent(lvgl.EVENT.DELETE, function()
        if autorun_timer then
            autorun_timer:delete()
            autorun_timer = nil
        end
    end)
end
