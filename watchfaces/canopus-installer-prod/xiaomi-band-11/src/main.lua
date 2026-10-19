-- Production Canopus installer for Xiaomi Band 11 (212x520, LuaLVGL v9).
--
-- Run performs, in order:
--   NSH mw/exec stage-1 -> stage-2 -> Supervisor constructor
--   -> apply restored boot intents -> INSTALL 0 -> 1 -> 2.
-- Loading happens only after the explicit Run click, or, when the user switched
-- Auto-run on (off by default), by itself a few seconds after this page starts.
-- /data/canopus/autorun enables it and /data/canopus/autorun.pending exists
-- while an automatic Run is in progress: a Run that never finished (the device
-- reset in the middle of it) is found at the next start and turns Auto-run off
-- instead of repeating. After the Supervisor is
-- available, registration runs on the same UI thread through LuaLVGL timers.
-- Each checkpoint paints before the next operation. The native cache/exec
-- sequence remains synchronous and has no yield inside it.

-- @CANOPUS_RECOVERY@
-- @CANOPUS_PROFILE@
-- @CANOPUS_NATIVE_LOADER@
-- @CANOPUS_PROGRESS@

-- pmain's root argument is live only during entry-script evaluation.
local PROFILE, profile_error = recovery.select_profile(PROFILES)
local recovery_ok, recovery_result, recovery_message = false, profile_error, nil
if PROFILE then
    recovery_ok, recovery_result, recovery_message = pcall(recovery.recover, PROFILE)
end
local recovered = recovery_ok and type(recovery_result) == "function"
local execute = recovered and recovery_result or nil
local recovery_error
if not recovered then recovery_error = tostring(recovery_ok and recovery_message or recovery_result) end
local control_open
if recovered and type(debug.getupvalue) == "function" then
    local _, original = debug.getupvalue(io.open, 1)
    local address = type(original) == "function" and (tostring(original):match("0[xX](%x+)$") or tostring(original):match(":%s*(%x+)$"))
    if address and (tonumber(address, 16) & ~1) == PROFILE.io_open then
        control_open = original
    end
end
local lvgl = require("lvgl")

local TARGET_ID = PROFILE and PROFILE.target_id
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

local target_id
local profile
local stage1_resource
local stage2_resource
local supervisor_resource
local status
local run_attempted = false
local clear_armed = false
local runner, operation, current_step
local progress_count = 0
local run_button, clear_button, autorun_button, autorun_label
local resources_available = false
local AUTORUN_FLAG = "/data/canopus/autorun"
local AUTORUN_PENDING = "/data/canopus/autorun.pending"
local AUTORUN_DELAY_MS = 10000
local autorun_active = false  -- the Run in progress was started by Auto-run
local autorun_timer
local autorun_notice

local function checkpoint(text) runner.checkpoint(text) end
local function check(ok, message) if not ok then error(message, 0) end end

local function run(command)
    print("[canopus-installer-prod:band11] exec: " .. command)
    if type(execute) ~= "function" then return false end
    local ok = execute(command)
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

local function write_all(path, content)
    local file = io.open(path, "wb")
    if not file then return false end
    local write_ok, write_result = pcall(file.write, file, content)
    local close_ok, close_result = pcall(file.close, file)
    return write_ok and write_result ~= nil and close_ok and close_result ~= nil
end

local function path_exists(path)
    local file = io.open(path, "rb")
    if not file then return false end
    file:close()
    return true
end

local function remove_file(path)
    return run("rm " .. path)
end

local function write_marker(path)
    run("mkdir /data/canopus")
    return write_all(path, "1")
end

local function refresh_autorun_button()
    autorun_label:set { text = path_exists(AUTORUN_FLAG) and "Auto-run: ON" or "Auto-run: OFF" }
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

local function load_profile(selected_target)
    local path = SCRIPT_PATH .. "canopus_loader_profile-" .. selected_target .. ".bin"
    local source = read_all(path, "r")
    if type(source) ~= "string" then return nil end
    local compiler = loadstring or load
    if type(compiler) ~= "function" then return nil end
    local compiled, chunk = pcall(compiler, source)
    if not compiled or type(chunk) ~= "function" then return nil end
    local loaded, value = pcall(chunk)
    if not loaded or type(value) ~= "table"
        or value.target_id ~= selected_target
        or value.status ~= "STATIC_RECOVERED"
        or type(value.device_status) ~= "string"
        or (value.cave_original_known ~= nil
            and type(value.cave_original_known) ~= "boolean")
        or value.loader_family ~= "lua-owned-stage1-stage2" then
        return nil
    end
    return value
end

local function select_target()
    local loaded_profile = load_profile(TARGET_ID)
    if not loaded_profile then return false end
    target_id = TARGET_ID
    profile = loaded_profile
    stage1_resource = SCRIPT_PATH .. "canopus_stage1-" .. target_id .. ".bin"
    stage2_resource = SCRIPT_PATH .. "canopus_stage2-" .. target_id .. ".bin"
    supervisor_resource = SCRIPT_PATH .. "canopus_supervisor-" .. target_id .. ".bin"
    return true
end

local function verify_module_file(path)
    if type(io) ~= "table" or type(io.open) ~= "function" then
        return false, "io.open unavailable"
    end
    local file = io.open(path, "rb")
    if not file then return false, "missing exact Band 11 Supervisor" end
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

local function resources_ready()
    if type(profile) ~= "table" then return false end
    for _, item in ipairs({
        { stage1_resource, "r", "检查第一阶段加载资源" },
        { stage2_resource, "rb", "检查第二阶段加载资源" },
        { supervisor_resource, "rb", "检查 Supervisor 资源" },
    }) do
        checkpoint(item[3])
        local file = io.open(item[1], item[2])
        if not file then return false end
        file:close()
    end
    checkpoint("检查 Supervisor 格式与大小")
    return verify_module_file(supervisor_resource)
end

local function supervisor_present()
    local file = control_open(DEVICE_PATH, "rb")
    if not file then return false end
    file:close()
    return true
end

local function load_supervisor()
    return native_loader.load(profile, stage1_resource, stage2_resource,
        supervisor_resource, run, read_all, write_all, checkpoint)
end

local function stage_manager_icon()
    checkpoint("读取并校验管理器图标")
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
    checkpoint("写入管理器图标")
    local output = io.open(MANAGER_ICON_PATH, "wb")
    if not output then
        checkpoint("创建管理器资源目录")
        if not run("mkdir /data/canopus") then
            return false, "cannot create /data/canopus"
        end
        checkpoint("重新写入管理器图标")
        output = io.open(MANAGER_ICON_PATH, "wb")
    end
    if not output then return false, "cannot stage Manager icon" end
    local write_ok, write_result = pcall(output.write, output, content)
    local close_ok, close_result = pcall(output.close, output)
    if not write_ok or write_result == nil or not close_ok or close_result == nil then
        return false, "Manager icon write failed"
    end
    checkpoint("回读校验管理器图标")
    if read_all(MANAGER_ICON_PATH, "rb") ~= content then
        return false, "Manager icon verification failed"
    end
    checkpoint("准备外部模块安装目录")
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
    local file = control_open(DEVICE_PATH, "wb")
    if not file then return nil, "cannot open /dev/canopus" end
    local write_ok, write_result, write_error = pcall(file.write, file, request)
    local close_ok, close_result = pcall(file.close, file)
    if not write_ok or write_result == nil or not close_ok or close_result == nil then
        return nil, tostring(write_error or write_result or "write failed")
    end
    file = control_open(DEVICE_PATH, "rb")
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

-- Supervisor error codes worth explaining in place. -21 is the one an
-- operator can act on: no heap domain could hold the module image.
local ERROR_HINTS = {
    [-21] = "内存不足：模块镜像放不进内核堆，请缩小模块",
}

local function execute_step(command, arg0, description)
    checkpoint(description)
    local reply, message = exchange(command,
        command == CMD_INSTALL and string.pack("<I4", arg0 or 0) or nil, CPC2_HEADER)
    if not reply then return false, message end
    local result = u32le(reply, 28)
    if result ~= RESULT_COMPLETED then
        local code = device_error()
        local hint = ERROR_HINTS[code]
        return false, string.format("result=%d error=%d%s", result, code,
            hint and ("\n" .. hint) or "")
    end
    return true
end

local steps = {
    { command = CMD_RESTORE_AFTER_BOOT, arg0 = 0,
      progress = "恢复已启用模块" },
    { command = CMD_INSTALL, arg0 = 0,
      progress = "注册管理器" },
    { command = CMD_INSTALL, arg0 = 1,
      progress = "注册模块应用" },
    { command = CMD_INSTALL, arg0 = 2,
      progress = "发布应用列表入口" },
}

local function set_status(text, color)
    status:set { text = tostring(text), text_color = color or '#bfd9ff' }
end

local function update_buttons()
    for _, button in ipairs({run_button, autorun_button, clear_button}) do
        if operation or not resources_available or (button == run_button and run_attempted) then
            button:clear_flag(lvgl.FLAG.CLICKABLE)
        else
            button:add_flag(lvgl.FLAG.CLICKABLE)
        end
    end
end

local function run_all_steps()
    for _, step in ipairs(steps) do
        check(execute_step(step.command, step.arg0, step.progress))
    end
    return "Run completed\n安装完成，请从应用列表打开管理器"
end

local rootbase = lvgl.Object(nil, {
    w = lvgl.HOR_RES(),
    h = lvgl.VER_RES(),
    bg_color = 0,
    bg_opa = lvgl.OPA(100),
    border_width = 0,
})
rootbase:clear_flag(lvgl.FLAG.SCROLLABLE)
if lvgl.FLAG.EVENT_BUBBLE then rootbase:add_flag(lvgl.FLAG.EVENT_BUBBLE) end

local root = lvgl.Object(rootbase, {
    outline_width = 0,
    border_width = 0,
    pad_all = 0,
    bg_opa = 0,
    bg_color = 0,
    align = lvgl.ALIGN.CENTER,
    w = lvgl.HOR_RES(),
    h = lvgl.VER_RES(),
    flex = {
        flex_direction = "row",
        flex_wrap = "wrap",
        justify_content = "center",
        align_items = "center",
        align_content = "center",
    },
})
root:clear_flag(lvgl.FLAG.SCROLLABLE)
if lvgl.FLAG.EVENT_BUBBLE then root:add_flag(lvgl.FLAG.EVENT_BUBBLE) end

local title = lvgl.Label(root, {
    text_font = lvgl.Font("MiSans-Regular", 18),
    text = "Canopus Installer",
    text_color = '#eee',
    w = 176,
    h = 40,
    align = lvgl.ALIGN.CENTER,
})
if lvgl.FLAG.EVENT_BUBBLE then title:add_flag(lvgl.FLAG.EVENT_BUBBLE) end

status = lvgl.Label(root, {
    text_font = lvgl.Font("MiSans-Regular", 14),
    text = "执行接口已就绪\n正在准备安装器...",
    text_color = '#bfd9ff',
    bg_color = 0,
    bg_opa = 0,
    pad_all = 4,
    w = 176,
    h = 246,
})
if lvgl.FLAG.EVENT_BUBBLE then status:add_flag(lvgl.FLAG.EVENT_BUBBLE) end

if not recovered or not control_open then
    set_status(recovery_error and ("Startup failed: " .. recovery_error)
        or "Installer resources unavailable", '#ff9a9a')
    return
end

runner = progress.new(lvgl, function(text)
    local previous = current_step
    current_step = tostring(text)
    progress_count = progress_count + 1
    set_status(string.format("步骤 %d\n%s", progress_count, current_step)
        .. (previous and ("\n\n已完成：" .. previous) or ""))
end, function(success, message)
    local completed = operation
    operation = nil
    if completed == "startup" then resources_available = success end
    if completed == "run" then settle_autorun(success) end
    if completed == "clear" then refresh_autorun_button() end
    if success then
        set_status(message, '#8ff0a4')
    else
        set_status("失败步骤：" .. tostring(current_step or "启动进度显示")
            .. "\n" .. tostring(message)
            .. (completed == "run" and "\n请重启后再试" or ""), '#ff9a9a')
    end
    update_buttons()
end)

local function start_operation(name, work)
    if operation then return end
    operation, current_step, progress_count = name, nil, 0
    update_buttons()
    local ok, message = runner.start(work)
    if not ok then
        operation = nil
        set_status("无法启动进度显示：" .. tostring(message), '#ff9a9a')
        update_buttons()
    end
end

-- Timers must not retain a task after its owning page is destroyed.
if lvgl.EVENT.DELETE then
    rootbase:onevent(lvgl.EVENT.DELETE, function()
        if autorun_timer then
            autorun_timer:delete()
            autorun_timer = nil
        end
        runner.cancel()
        native_loader.release()
        operation = nil
    end)
end

local function make_button(text, color, on_clicked)
    local button = lvgl.Object(root, {
        w = 164,
        h = 48,
        bg_color = color,
        bg_opa = lvgl.OPA(100),
    })
    button:clear_flag(lvgl.FLAG.SCROLLABLE)
    button:add_flag(lvgl.FLAG.CLICKABLE)
    if lvgl.FLAG.EVENT_BUBBLE then button:add_flag(lvgl.FLAG.EVENT_BUBBLE) end
    local label = lvgl.Label(button, {
        text_font = lvgl.Font("MiSans-Regular", 16),
        text = text,
        text_color = '#eee',
        align = lvgl.ALIGN.CENTER,
    })
    button:onevent(lvgl.EVENT.CLICKED, function(obj, code)
        local ok, message = pcall(on_clicked)
        if not ok then set_status("Error: " .. tostring(message), '#ff9a9a') end
    end)
    return button, label
end

-- Returns true once the Run operation has started.
local function click_run()
    if operation or not resources_available then return false end
    clear_armed = false
    if profile.device_status == "DEVICE_REJECTED" then
        set_status("LOAD blocked: stage-0 candidate rejected\n"
            .. "Select a replacement candidate before testing.", '#ff9a9a')
        return false
    end
    if run_attempted then
        set_status("Run can only be used once; reboot before retrying")
        return false
    end
    run_attempted = true
    start_operation("run", function()
        checkpoint("复核 Supervisor 文件")
        check(verify_module_file(supervisor_resource))
        checkpoint("检查运行环境是否已加载")
        if not supervisor_present() then
            check(load_supervisor())
            checkpoint("确认运行环境已就绪")
            check(supervisor_present(), "no /dev/canopus")
        end
        check(stage_manager_icon())
        return run_all_steps()
    end)
    return operation == "run"
end

run_button = make_button("Run", '#14508a', click_run)

autorun_button, autorun_label = make_button("Auto-run", '#2b5d3a', function()
    if operation or not resources_available then return end
    clear_armed = false
    if path_exists(AUTORUN_FLAG) then
        remove_file(AUTORUN_FLAG)
        set_status("Auto-run off")
    elseif write_marker(AUTORUN_FLAG) then
        set_status("Auto-run on: Canopus loads itself after the next boot.\n"
            .. "It turns itself off if a run does not finish.", '#8ff0a4')
    else
        set_status("Cannot enable Auto-run", '#ff9a9a')
    end
    refresh_autorun_button()
end)
refresh_autorun_button()

clear_button = make_button("Clear Env", '#8a1f14', function()
    if operation or not resources_available then return end
    if not clear_armed then
        clear_armed = true
        set_status("Click again to clear", '#ffd27a')
        return
    end
    clear_armed = false
    start_operation("clear", function()
        checkpoint("清理安装环境")
        check(run("rm -rf /data/canopus"), "Clear Env failed")
        return "Environment cleared; reboot before Run"
    end)
end)

local function start_autorun(timer)
    timer:delete()
    autorun_timer = nil
    if operation or run_attempted or not resources_available or supervisor_present() then return end
    if not write_marker(AUTORUN_PENDING) then
        set_status("Auto-run skipped: cannot write its marker", '#ff9a9a')
        return
    end
    autorun_active = true
    if not click_run() then settle_autorun(false) end
end

-- A pending marker at start means the automatic Run of the last boot never
-- finished: leave Auto-run off until it is switched on again.
if path_exists(AUTORUN_PENDING) then
    remove_file(AUTORUN_PENDING)
    remove_file(AUTORUN_FLAG)
    refresh_autorun_button()
    autorun_notice = "Auto-run 已关闭：上次自动运行没有完成"
end

start_operation("startup", function()
    -- os.execute recovery must finish synchronously while initial pmain is
    -- live. Everything below may yield after the entry script returns.
    checkpoint("读取设备安装配置")
    check(select_target(), "Installer profile unavailable")
    check(resources_ready(), "Installer resources unavailable")
    if not autorun_notice and path_exists(AUTORUN_FLAG) and not supervisor_present() then
        autorun_timer = lvgl.Timer {
            period = AUTORUN_DELAY_MS,
            repeat_count = -1,
            cb = function(timer)
                local ok, message = pcall(start_autorun, timer)
                if not ok then set_status("Auto-run failed: " .. tostring(message), '#ff9a9a') end
            end,
        }
        return "Ready\n" .. tostring(AUTORUN_DELAY_MS // 1000) .. " 秒后自动运行"
    end
    return "Ready\n启动校验完成，点击 Run 开始" .. (autorun_notice and ("\n" .. autorun_notice) or "")
end)
