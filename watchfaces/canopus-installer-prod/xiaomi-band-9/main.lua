-- Production Canopus installer for Xiaomi Band 9 (192x490, LuaLVGL v8).
--
-- Run performs, in order:
--   NSH mw/exec stage-1 -> stage-2 -> Supervisor constructor
--   -> apply restored boot intents -> INSTALL 0 -> 1 -> 2.
-- Loading happens only after the explicit Run click. After the Supervisor is
-- available, the fixed registration sequence is issued directly from that click;
-- this page has no external scheduling dependency.

local lvgl = require("lvgl")

local TARGET_ID = "xiaomi-band-9-3.1.32"
local MANAGER_ICON_RESOURCE = SCRIPT_PATH .. "manager_icon.bin"
local MANAGER_ICON_PATH = "/data/canopus/manager_icon.bin"
local STAGE2_PATH = "/data/canopus/stage2.bin"
local SUPERVISOR_PATH = "/data/canopus/supervisor.elf"
local DEVICE_PATH = "/dev/canopus"
local MODULE_MIN_SIZE = 512
local MODULE_MAX_SIZE = 262144
local STATUS_SIZE = 384
local EXPECTED_MAGIC = 0x43505331 -- "CPS1"
local EXPECTED_CMD_MAGIC = 0x43504331 -- "CPC1"
local CMD_INSTALL = 0x43510002
local CMD_RESTORE_AFTER_BOOT = 0x4351000A
local RESULT_COMPLETED = 5
local STAGE0_ENTRY_MARKER = 0xA5A5A5A5
local STAGE0_ENTRY_PROBE_MARKER = 0x5A5A5A5A
local STAGE0_DIAGNOSTIC = "stage-fixed-continuation-v1"

local target_id
local profile
local stage1_resource
local stage2_resource
local supervisor_resource
local status
local run_phase = 1
local run_active = false
local run_attempted = false
local clear_armed = false

local function run(command)
    print("[canopus-installer-prod:band9] exec: " .. command)
    local ok = os.execute(command)
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

local function mpu_rbar_attr(access_attr)
    if access_attr == 0 then return 0x02 end
    if access_attr == 1 or access_attr == 2 then return 0x06 end
    if access_attr == 3 then return 0x03 end
    if access_attr == 4 then return 0x07 end
    return nil
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
        or value.loader_family ~= "nsh-mw-stage1-stage2" then
        return nil
    end
    for _, key in ipairs({
        "memalign", "free", "kmem_malloc", "kmem_free", "mpu_alloc", "mpu_configure", "mpu_release",
        "mpu_sync", "mpu_rnr", "mpu_rbar", "mpu_rlar", "mpu_region_count",
        "exec_access_attr", "exec_mem_attr", "rw_access_attr", "cave",
        "cave_result", "stage0_size", "stage0_region", "stage0_exec_size",
    }) do
        if type(value[key]) ~= "number" then return nil end
    end
    if (value.exec_access_attr ~= 1 and value.exec_access_attr ~= 2)
        or value.rw_access_attr ~= 0
        or value.exec_mem_attr < 0 or value.exec_mem_attr > 7
        or value.exec_mem_attr ~= math.floor(value.exec_mem_attr)
        or type(value.cave_original) ~= "table"
        or #value.cave_original ~= 8 then
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
    if not file then return false, "missing exact Band 9 Supervisor" end
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
        { stage1_resource, "r" },
        { stage2_resource, "rb" },
        { supervisor_resource, "rb" },
    }) do
        local file = io.open(item[1], item[2])
        if not file then return false end
        file:close()
    end
    return verify_module_file(supervisor_resource)
end

local function supervisor_present()
    local file = io.open(DEVICE_PATH, "rb")
    if not file then return false end
    file:close()
    return true
end

local function shell_word(address)
    local output = "/data/canopus/bootstrap-word.txt"
    if not run(string.format("mw %08x 4 > %s", address, output)) then return nil end
    local text = read_all(output, "r")
    if type(text) ~= "string" then return nil end
    local value = text:match("=%s*0[xX]([0-9a-fA-F]+)")
        or text:match("%-%>%s*0[xX]([0-9a-fA-F]+)")
    return value and tonumber(value, 16) or nil
end

local function run_sequence(commands, separator)
    return run(table.concat(commands, separator or "; "))
end

local function check_cave()
    if profile.cave_original_known == false then return true end
    for index, expected in ipairs(profile.cave_original) do
        if shell_word(profile.cave + (index - 1) * 4) ~= expected then
            return false
        end
    end
    return true
end

local function write_words(address, words)
    for index, value in ipairs(words) do
        if not run(string.format("mw %08x=%08x",
            address + (index - 1) * 4, value)) then
            return false
        end
    end
    return true
end

local function restore_cave()
    if profile.cave_original_known == false then return true end
    return write_words(profile.cave, profile.cave_original)
end

local function mpu_rlar_attr(mem_attr)
    return (mem_attr * 2) % 16 + 1
end

local function stage0_mpu_values()
    local limit = profile.cave + profile.stage0_exec_size - 32
    -- Publish writable while the old RLAR can still cover a live task stack, then
    -- narrow to RX only after the new limit confines the region to the mailbox.
    return profile.cave + mpu_rbar_attr(profile.rw_access_attr),
        profile.cave + mpu_rbar_attr(profile.exec_access_attr),
        limit + mpu_rlar_attr(profile.exec_mem_attr)
end

local function mpu_sync_command()
    return string.format("exec 0x%08x", profile.mpu_sync)
end

local function stage0_mpu_release()
    return run_sequence({
        string.format("mw %08x=%08x", profile.mpu_rnr,
            profile.stage0_region),
        string.format("mw %08x=00000000", profile.mpu_rlar),
        mpu_sync_command(),
    })
end

local function stage0_exec_with_mpu(address, prefix, keep_region, postfix)
    local writable_rbar, executable_rbar, rlar = stage0_mpu_values()
    local commands = {}
    if prefix then
        for _, command in ipairs(prefix) do
            commands[#commands + 1] = command
        end
    end
    commands[#commands + 1] = string.format("mw %08x=%08x", profile.mpu_rnr,
        profile.stage0_region)
    commands[#commands + 1] = string.format("mw %08x=%08x",
        profile.mpu_rbar, writable_rbar)
    commands[#commands + 1] = string.format("mw %08x=%08x", profile.mpu_rlar, rlar)
    commands[#commands + 1] = string.format("mw %08x=%08x",
        profile.mpu_rbar, executable_rbar)
    commands[#commands + 1] = mpu_sync_command()
    commands[#commands + 1] = string.format("exec 0x%08x", profile.cave + 1)
    if address then
        commands[#commands + 1] = string.format("mw %08x=%08x", profile.mpu_rnr,
            profile.stage0_region)
        commands[#commands + 1] = string.format("mw %08x=00000000", profile.mpu_rlar)
        commands[#commands + 1] = mpu_sync_command()
        commands[#commands + 1] = string.format("exec 0x%08x", address)
        if postfix then
            for _, command in ipairs(postfix) do
                commands[#commands + 1] = command
            end
        end
    elseif not keep_region then
        commands[#commands + 1] = string.format("mw %08x=%08x", profile.mpu_rnr,
            profile.stage0_region)
        commands[#commands + 1] = string.format("mw %08x=00000000", profile.mpu_rlar)
        commands[#commands + 1] = mpu_sync_command()
    end
    return run(table.concat(commands, "; ")), nil
end

local function execute_stage0_trampoline()
    local exec_ok, mpu_error = stage0_exec_with_mpu(nil, nil, false)
    if mpu_error then return nil, mpu_error end
    local result = shell_word(profile.cave_result)
    if result == nil then return nil, "stage-0 result read failed" end
    if result == profile.cave_result and not exec_ok then
        return nil, "stage-0 exec parser rejected target ["
            .. STAGE0_DIAGNOSTIC .. "]"
    end
    return result
end

local function call_mailbox(callable, r0, r1)
    local probe = {
        0x49034802, 0x4A036008, 0xBF004710,
        STAGE0_ENTRY_PROBE_MARKER, profile.cave_result, 0x0c1c956d,
    }
    local trampoline = {
        0x8F4FF3BF, 0x8F6FF3BF,
        0x49064805, 0x4B074A06, 0x4B076013, 0x4A044798,
        0x4B066010, 0xBF004718,
        r0, r1, profile.cave_result, STAGE0_ENTRY_MARKER, callable,
        0x0c1c956d,
    }
    if not check_cave() then return nil, "stage-0 preflight mismatch" end
    if not run(string.format("mw %08x=%08x", profile.cave_result,
        profile.cave_result)) then
        return nil, "stage-0 result sentinel write failed"
    end
    if not write_words(profile.cave, probe) then
        restore_cave()
        return nil, "stage-0 entry probe write failed"
    end
    local probe_result, probe_error = execute_stage0_trampoline()
    if probe_error then
        restore_cave()
        return nil, probe_error
    end
    if probe_result == profile.cave_result then
        restore_cave()
        return nil, "stage-0 entry probe returned sentinel ["
            .. STAGE0_DIAGNOSTIC .. "]"
    end
    if probe_result ~= STAGE0_ENTRY_PROBE_MARKER then
        restore_cave()
        return nil, string.format("stage-0 entry probe result=0x%08x ["
            .. STAGE0_DIAGNOSTIC .. "]", probe_result)
    end
    if not run(string.format("mw %08x=%08x", profile.cave_result,
        profile.cave_result)) then
        restore_cave()
        return nil, "stage-0 result sentinel write failed"
    end
    if not write_words(profile.cave, trampoline) then
        restore_cave()
        return nil, "stage-0 mailbox write failed"
    end
    local result, callback_error = execute_stage0_trampoline()
    if callback_error then
        restore_cave()
        return nil, callback_error
    end
    if not restore_cave() or not check_cave() then
        return nil, "stage-0 mailbox restore failed"
    end
    if result == nil then return nil, "stage-0 result read failed" end
    if result == profile.cave_result then
        return nil, "stage-0 callback returned result sentinel ["
            .. STAGE0_DIAGNOSTIC .. "]"
    end
    if result == STAGE0_ENTRY_MARKER then
        return nil, "stage-0 callback did not return ["
            .. STAGE0_DIAGNOSTIC .. "]"
    end
    return result
end

local function barrier_and_exec(address, region, rbar, rlar)
    local trampoline = {
        0x8F4FF3BF, 0x8F6FF3BF, 0x47702001, 0, 0, 0, 0, 0,
    }
    if not check_cave() then return false end
    if not write_words(profile.cave, trampoline) then
        restore_cave()
        return false
    end
    local commands = {
        string.format("mw %08x=%08x", profile.mpu_rnr, region),
        string.format("mw %08x=%08x", profile.mpu_rbar,
            address + mpu_rbar_attr(profile.rw_access_attr)),
        string.format("mw %08x=%08x", profile.mpu_rlar, rlar),
        string.format("mw %08x=%08x", profile.mpu_rbar, rbar),
    }
    local release_stage1 = {
        string.format("mw %08x=%08x", profile.mpu_rnr, region),
        string.format("mw %08x=00000000", profile.mpu_rlar),
        mpu_sync_command(),
    }
    local stage0_ok, stage0_error = stage0_exec_with_mpu(
        address + 1, commands, false, release_stage1)
    if stage0_error then
        stage0_mpu_release()
        restore_cave()
        return false
    end
    if not stage0_ok then
        restore_cave()
        return false
    end
    if not restore_cave() or not check_cave() then return false end
    return true
end

local function release_allocation(address, free_callable)
    return call_mailbox(free_callable or profile.kmem_free, address, 0) ~= nil
end

local function release_region(region)
    if not run_sequence({
        string.format("mw %08x=%08x", profile.mpu_rnr, region),
        string.format("mw %08x=00000000", profile.mpu_rlar),
        mpu_sync_command(),
    }) then
        return false
    end
    return call_mailbox(profile.mpu_release, region, 0) ~= nil
end

local function cleanup_stage1(address, region)
    if not release_region(region) then return false end
    return release_allocation(address, profile.kmem_free)
end

local function load_supervisor()
    local stage1_source = read_all(stage1_resource, "r")
    local stage2 = read_all(stage2_resource, "rb")
    local supervisor = read_all(supervisor_resource, "rb")
    if type(stage1_source) ~= "string" or type(stage2) ~= "string"
        or type(supervisor) ~= "string" then
        return false, "missing Band 9 bootstrap resources"
    end
    local compiler = loadstring or load
    if type(compiler) ~= "function" then return false, "Lua compiler unavailable" end
    local compiled, chunk, compile_error = pcall(compiler, stage1_source)
    if not compiled then return false, tostring(chunk) end
    if type(chunk) ~= "function" then return false, tostring(compile_error) end
    local loaded, payload = pcall(chunk)
    if not loaded then return false, tostring(payload) end
    if type(payload) ~= "table" or type(payload.words) ~= "table"
        or type(payload.size) ~= "number" then
        return false, "invalid Band 9 stage-1 table"
    end
    -- `/data/canopus` does not exist on a clean device. Ignore mkdir's status:
    -- NSH reports failure when the directory already exists, but staging is still
    -- valid in that case and the following writes are authoritative.
    run("mkdir /data/canopus")
    if not write_all(STAGE2_PATH, stage2) then
        return false, "cannot stage Band 9 stage-2"
    end
    if not write_all(SUPERVISOR_PATH, supervisor) then
        return false, "cannot stage Band 9 Supervisor"
    end
    local exec_size = math.floor((payload.size + 31) / 32) * 32
    local raw_allocation, allocation_error = call_mailbox(
        profile.kmem_malloc, exec_size + 31, 0)
    if not raw_allocation or raw_allocation == 0 then
        return false, "Band 9 stage-1 Kmem allocation failed: "
            .. tostring(allocation_error or ("result=" .. tostring(raw_allocation)))
    end
    local allocation = raw_allocation + 31
        - ((raw_allocation + 31) % 32)
    if allocation < raw_allocation or allocation % 32 ~= 0 then
        release_allocation(raw_allocation)
        return false, "Band 9 stage-1 alignment failed"
    end
    if not write_words(allocation, payload.words) then
        if not release_allocation(raw_allocation) then
            return false, "stage-1 write cleanup failed; reboot before retrying"
        end
        return false, "Band 9 stage-1 write failed"
    end
    local region = call_mailbox(profile.mpu_alloc, 0, 0)
    if not region or region >= profile.mpu_region_count then
        if not release_allocation(raw_allocation) then
            return false, "MPU failure cleanup failed; reboot before retrying"
        end
        return false, "Band 9 MPU region unavailable"
    end
    if region == profile.stage0_region then
        if not release_allocation(raw_allocation) then
            return false, "MPU collision cleanup failed; reboot before retrying"
        end
        return false, "Band 9 MPU region collides with stage-0 region"
    end
    local rbar = allocation + mpu_rbar_attr(profile.exec_access_attr)
    local rlar = allocation + exec_size - 32
        + mpu_rlar_attr(profile.exec_mem_attr)
    local executed = barrier_and_exec(allocation, region, rbar, rlar)
    local cleaned = cleanup_stage1(raw_allocation, region)
    if not cleaned then
        return false, "stage-1 cleanup failed; reboot before retrying"
    end
    if not executed then return false, "Band 9 stage-1 execution failed" end
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
    if not write_ok or write_result == nil or not close_ok or close_result == nil then
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

local function set_status(text, color)
    status:set { text = tostring(text), text_color = color or '#bfd9ff' }
end

local function finish_run(success, message)
    run_active = false
    set_status(message, success and '#8ff0a4' or '#ff9a9a')
end

local function run_all_steps()
    run_active = true
    for index, step in ipairs(steps) do
        run_phase = index
        set_status(step.progress)
        local ok, message = execute_step(step.command, step.arg0)
        if not ok then
            finish_run(false, "Run failed: " .. tostring(message)
                .. "\nReboot before retrying.")
            return false
        end
    end
    run_phase = #steps + 1
    finish_run(true, "Run completed")
    return true
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
    text = "Loading installer...",
    text_color = '#bfd9ff',
    bg_color = 0,
    bg_opa = 0,
    pad_all = 4,
    w = 176,
    h = 246,
})
if lvgl.FLAG.EVENT_BUBBLE then status:add_flag(lvgl.FLAG.EVENT_BUBBLE) end

local resources_available = select_target() and resources_ready()
if not resources_available then
    set_status("Installer resources unavailable", '#ff9a9a')
    return
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

local run_button
run_button = make_button("Run", '#14508a', function()
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
    run_button:clear_flag(lvgl.FLAG.CLICKABLE)
    local valid, validation_error = verify_module_file(supervisor_resource)
    if not valid then
        set_status("LOAD failed: " .. tostring(validation_error), '#ff9a9a')
        return
    end
    if not supervisor_present() then
        set_status("Loading supervisor with mw/exec...")
        local loaded, load_error = load_supervisor()
        if not loaded or not supervisor_present() then
            set_status("LOAD failed: " .. tostring(load_error or "no /dev/canopus")
                .. "\nReboot before retrying.", '#ff9a9a')
            return
        end
    end
    local icon_ok, icon_error = stage_manager_icon()
    if not icon_ok then
        set_status("Run failed: " .. tostring(icon_error), '#ff9a9a')
        return
    end
    run_phase = 1
    set_status("Supervisor loaded; installing...")
    run_all_steps()
end)

make_button("Clear Env", '#8a1f14', function()
    if run_active then
        clear_armed = false
        set_status("Run is in progress; reboot before clearing")
        return
    end
    if not clear_armed then
        clear_armed = true
        set_status("Click again to clear", '#ffd27a')
        return
    end
    clear_armed = false
    if run("rm -rf /data/canopus") then
        set_status("Environment cleared; reboot before Run", '#8ff0a4')
    else
        set_status("Clear Env failed", '#ff9a9a')
    end
end)

set_status("Ready")
