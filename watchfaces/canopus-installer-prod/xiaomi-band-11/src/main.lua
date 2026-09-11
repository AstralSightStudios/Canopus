-- Production Canopus installer for Xiaomi Band 11 (212x520, LuaLVGL v9).
--
-- Run performs, in order:
--   NSH mw/exec stage-1 -> stage-2 -> Supervisor constructor
--   -> apply restored boot intents -> INSTALL 0 -> 1 -> 2.
-- Loading happens only after the explicit Run click. After the Supervisor is
-- available, registration runs on the same UI thread through LuaLVGL timers.
-- Each checkpoint paints before the next operation. The native cache/exec
-- sequence remains synchronous and has no yield inside it.

-- @CANOPUS_RECOVERY@
-- @CANOPUS_PROFILE@
-- @CANOPUS_NATIVE_LOADER@
-- @CANOPUS_PROGRESS@

-- pmain's root argument is live only during entry-script evaluation.
local recovery_ok, recovery_result, recovery_message = pcall(recovery.recover, PROFILE)
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

local TARGET_ID = "xiaomi-band-11-4.100.139"
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
local run_button, clear_button
local resources_available = false

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
    local file = control_open(DEVICE_PATH, "rb")
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
    local file = control_open(DEVICE_PATH, "wb")
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

-- Supervisor error codes worth explaining in place. -21 is the one an
-- operator can act on: no heap domain could hold the module image.
local ERROR_HINTS = {
    [-21] = "内存不足：模块镜像放不进内核堆，请缩小模块",
}

local function execute_step(command, arg0, description)
    checkpoint(description)
    local ok, message = write_command(command, arg0)
    if not ok then return false, message end
    -- Keep request + status read together: /dev/canopus has a shared result
    -- mailbox, so yielding here would allow another client to replace it.
    local current, status_error = read_status()
    if not current then return false, status_error end
    if current.pending_op ~= command or current.pending_state ~= RESULT_COMPLETED then
        local hint = ERROR_HINTS[current.error_code]
        return false, string.format("result=%d error=%d%s",
            current.pending_state, current.error_code,
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
    for _, button in ipairs({run_button, clear_button}) do
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
    lvgl.Label(button, {
        text_font = lvgl.Font("MiSans-Regular", 16),
        text = text,
        text_color = '#eee',
        align = lvgl.ALIGN.CENTER,
    })
    button:onevent(lvgl.EVENT.CLICKED, function(obj, code)
        local ok, message = pcall(on_clicked)
        if not ok then set_status("Error: " .. tostring(message), '#ff9a9a') end
    end)
    return button
end

run_button = make_button("Run", '#14508a', function()
    if operation or not resources_available then return end
    clear_armed = false
    if profile.device_status == "DEVICE_REJECTED" then
        set_status("LOAD blocked: stage-0 candidate rejected\n"
            .. "Select a replacement candidate before testing.", '#ff9a9a')
        return
    end
    if run_attempted then
        set_status("Run can only be used once; reboot before retrying")
        return
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
end)

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

start_operation("startup", function()
    -- os.execute recovery must finish synchronously while initial pmain is
    -- live. Everything below may yield after the entry script returns.
    checkpoint("读取设备安装配置")
    check(select_target(), "Installer profile unavailable")
    check(resources_ready(), "Installer resources unavailable")
    return "Ready\n启动校验完成，点击 Run 开始"
end)
