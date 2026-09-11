local fixture = assert(package.loadlib(assert(arg[1]), "luaopen_band11_fixture"))()
local recovery = assert(loadfile(
    "watchfaces/canopus-installer-prod/xiaomi-band-11/src/recover_execute.lua"))()
local native_os, native_io, native_debug = os, io, debug
local native_execute, native_version = os.execute, _VERSION
local registry = debug.getregistry()
local loaded = registry._LOADED
local original_loaded_os = loaded.os
local state_key = "canopus.band11.execute_recovery.v1"
local build = "user-4.100.139-cn-202608280000"
local identity = "ro.build.version=4.100.139\nro.build.id=" .. build .. "\n"
local profile

local function setup(fail, retain_root)
    fixture.reset(fail, retain_root)
    os, io, debug, _VERSION = native_os, {}, native_debug, "Lua 5.4"
    os.execute = nil
    loaded.os = os
    loaded.lvgl = loaded.lvgl or {}
    debug.setmetatable(loaded, nil)
    registry[state_key] = nil
    profile = {
        identity_path = "/etc/build.prop", firmware_version = "4.100.139",
        firmware_build = build, lua_version = "Lua 5.4",
        pmain = fixture.pmain_address - fixture.pmain_address % 2,
        pmain_error_handler = fixture.error_handler_address - fixture.error_handler_address % 2,
        os_execute = fixture.execute_address - fixture.execute_address % 2,
    }
    io.open = function(path, mode)
        assert(path == "/etc/build.prop" and mode == "rb")
        return {read = function(_, size) assert(size == 4097); return identity end,
                close = function() return true end}
    end
end

local function enter(body, root)
    __band11_test_entry = body
    if root == false then
        fixture.pmain("fixture.lua", fixture.loop, nil)
    else
        fixture.pmain("fixture.lua", fixture.loop, fixture.root)
    end
    __band11_test_entry = nil
end

setup()
enter(function()
    local execute, message = recovery.recover(profile)
    assert(execute == fixture.execute, tostring(message))
    assert(os.execute == fixture.execute and loaded.os == os)
    assert(debug.getmetatable(loaded) == nil)
    local root_ok, dataman_ok = fixture.context_ok()
    assert(root_ok and dataman_ok, "recovery corrupted native context")
    local entries, commands = fixture.counts()
    assert(entries == 2 and commands == 0, "recovery executed a command or reentered repeatedly")
    assert(recovery.recover(profile) == execute)
    os.execute = nil
    local again, error = recovery.recover(profile)
    assert(not again and error:match("already attempted"))
    assert(fixture.counts() == 2)
end)

setup(false, true)
enter(function()
    local execute, message = recovery.recover(profile)
    assert(not execute and message:match("^Unexpected pmain arguments"))
    assert(fixture.counts() == 1)
    assert(registry[state_key] == nil)
end, false)

-- A previously initialized loop keeps all three original arguments.
setup(false, true)
enter(function()
    local execute, message = recovery.recover(profile)
    assert(execute == fixture.execute, message)
    local root_ok, dataman_ok = fixture.context_ok()
    assert(root_ok and dataman_ok)
end)

-- Cached-root fallback is restricted to the exact firmware stack signature.
for _, fault in ipairs({"handler", "root", "light_root", "uservalue", "lvgl"}) do
    setup()
    enter(function()
        if fault == "handler" then profile.pmain_error_handler = profile.pmain_error_handler + 2 end
        if fault == "root" then registry.luavgl_key = nil end
        if fault == "light_root" then registry.luavgl_key = fixture.root end
        if fault == "uservalue" then debug.setuservalue(registry.luavgl_key, "unexpected", 1) end
        if fault == "lvgl" then loaded.lvgl = nil end
        local dataman = registry["dataman.meta"]
        local execute, message = recovery.recover(profile)
        assert(not execute and type(message) == "string", fault)
        assert(fixture.counts() == 1 and registry[state_key] == nil, fault)
        assert(registry["dataman.meta"] == dataman, fault)
    end)
end

-- The temporary dataman entry must also disappear if none existed originally.
setup()
enter(function()
    registry["dataman.meta"] = nil
    local execute, message = recovery.recover(profile)
    assert(execute == fixture.execute, message)
    assert(registry["dataman.meta"] == nil)
end)

setup()
local native_tostring = tostring
tostring = function(value)
    return native_tostring(value):gsub("^function:%s*0x", "function: ")
end
enter(function()
    local execute, message = recovery.recover(profile)
    assert(execute == fixture.execute, message)
end)
tostring = native_tostring

for _, fault in ipairs({"identity", "duplicate", "large", "read", "close",
    "version", "debug", "frame", "metatable", "registry", "address", "init"}) do
    setup(fault == "init")
    local saved_identity = identity
    if fault == "identity" then identity = identity:gsub("4.100.139", "4.100.138") end
    if fault == "duplicate" then identity = identity .. "ro.build.version=4.100.139\n" end
    if fault == "large" then identity = string.rep("x", 4097) end
    if fault == "read" then io.open = function() error("denied") end end
    if fault == "close" then io.open = function()
        return {read = function() return identity end, close = function() return nil end}
    end end
    if fault == "version" then _VERSION = "Lua 5.3" end
    if fault == "debug" then debug = nil end
    if fault == "frame" then profile.pmain = profile.pmain + 2 end
    if fault == "address" then profile.os_execute = profile.os_execute + 2 end
    enter(function()
        local meta = fault == "metatable" and {} or nil
        if meta then native_debug.setmetatable(loaded, meta) end
        if fault == "registry" then loaded.os = {} end
        local before = loaded.os
        local execute, message = recovery.recover(profile)
        assert(execute == nil and type(message) == "string", fault)
        assert(os.execute == nil and loaded.os == before, fault .. " changed os")
        assert(native_debug.getmetatable(loaded) == meta, fault .. " changed metatable")
        local entries, commands = fixture.counts()
        assert(entries == ((fault == "address" or fault == "init") and 2 or 1), fault)
        assert(commands == 0, fault .. " executed a command")
        local root_ok, dataman_ok = fixture.context_ok()
        assert(root_ok and dataman_ok, fault .. " changed native context")
    end)
    identity = saved_identity
end

-- Exercise the generated production UI in a real C frame. Firmware VFS is
-- substituted, but recovery, control-open unwrapping and Run sequencing are real.
setup()
local objects, clicks, device_commands, files = {}, {}, {}, {}
local timers, deletions, painted, history = {}, {}, nil, {}
local reject_command = false
local lvgl = {
    HOR_RES = function() return 212 end, VER_RES = function() return 520 end,
    OPA = function(n) return n end, ALIGN = {CENTER = 0}, EVENT = {CLICKED = 7, DELETE = 33},
    FLAG = {SCROLLABLE = 16, CLICKABLE = 2}, Font = function(name, size) return size end,
}
local methods = {
    clear_flag = function() end, add_flag = function() end,
    onevent = function(self, code, callback)
        if code == 33 then deletions[#deletions + 1] = callback
        else assert(code == 7); clicks[#clicks + 1] = callback end
    end,
    set = function(self, props) for k,v in pairs(props) do self[k] = v end end,
}
local function object(parent, props)
    props.parent = parent; objects[#objects + 1] = props
    return setmetatable(props, {__index = methods})
end
lvgl.Object, lvgl.Label = object, object
lvgl.Timer = function(props)
    assert(props.period >= 100 and props.paused and props.repeat_count == -1)
    function props:resume() self.paused = false end
    function props:delete() self.deleted = true end
    function props:ready() error("must allow the UI to paint between steps") end
    timers[#timers + 1] = props
    return props
end
local function tick()
    -- The previous text reaches the display before the next scheduled action.
    painted = objects[4].text
    history[#history + 1] = painted
    local count = #timers
    for i = 1,count do
        local timer = timers[i]
        if not timer.deleted and not timer.paused then timer.cb(timer) end
    end
end
local function drain()
    for i = 1,200 do
        local active = false
        for _,timer in ipairs(timers) do if not timer.deleted then active = true end end
        if not active then return end
        tick()
    end
    error("progress timer did not finish")
end
loaded.lvgl = lvgl
SCRIPT_PATH = "watchfaces/canopus-installer-prod/xiaomi-band-11/"
__band11_fixture_open = function(path, mode)
    if path == "/etc/build.prop" then
        return {read = function() return identity end, close = function() return true end}
    end
    if path == "/dev/canopus" then
        return {
            write = function(self, payload)
                assert(painted == objects[4].text, "command ran before its status could paint")
                local magic, op, arg0 = string.unpack("<I4I4I4", payload)
                assert(magic == 0x43504331 and #payload == 16)
                if reject_command then return nil, "fixture command rejected" end
                device_commands[#device_commands + 1] = {op, arg0}
                return self
            end,
            read = function()
                local w = {}; for i = 1,96 do w[i] = 0 end
                w[1],w[2],w[6],w[7] = 0x43505331,1,device_commands[#device_commands][1],5
                return string.pack("<" .. string.rep("I4",96), table.unpack(w))
            end,
            close = function() return true end,
        }
    end
    if path == "/data/canopus/manager_icon.bin" then
        return {read = function() return files[path] end,
                write = function(self,data) files[path]=data; return self end,
                close = function() return true end}
    end
    local file=arg[2] ~= "fresh" and native_io.open(path, mode) or nil
    if file then return file end
    -- Fresh checkouts do not contain ignored binary resources. The UI fixture
    -- supplies inert files; native execution is covered by the ARM test.
    local data
    if path:match('canopus_loader_profile%-') then
        data='return {target_id="xiaomi-band-11-4.100.139",status="STATIC_RECOVERED",device_status="NOT_PROBED",loader_family="lua-owned-stage1-stage2"}'
    elseif path:match('canopus_supervisor%-') then
        data="\127ELF" .. string.char(1,1,1) .. string.rep("\0",9) .. string.pack("<I2I2",1,40) .. string.rep("\0",492)
    elseif path:match('manager_icon.bin$') then
        data=string.char(0x19,0,0,0) .. string.pack("<I2I2I4I4",1,1,0,0)
    elseif path:match('canopus_stage[12]%-') then data=string.rep('x',64) end
    if data then return {read=function(_,n) return type(n)=='number' and data:sub(1,n) or data end,
        seek=function() return #data end, close=function() return true end} end
end
local original = fixture.open
io.open = function(...) return original(...) end
local file = assert(native_io.open(SCRIPT_PATH .. "main.lua", "rb"))
local source = file:read("*a"); file:close()
source = source:gsub("pmain = 0x%x+", "pmain = " .. profile.pmain)
source = source:gsub("pmain_error_handler = 0x%x+", "pmain_error_handler = " .. profile.pmain_error_handler)
source = source:gsub("os_execute = 0x%x+", "os_execute = " .. profile.os_execute)
source = source:gsub("io_open = 0x%x+", "io_open = " .. (fixture.open_address & ~1))
enter(function() assert(load(source, "band11-generated-entry", "t"))() end)
assert(#clicks == 2 and #objects == 8, "Run/Clear Env production UI missing")
assert(objects[1].w == 212 and objects[1].h == 520)
assert(objects[3].text == "Canopus Installer" and objects[3].h == 40)
assert(objects[4].text:match("读取设备安装配置") and objects[4].h == 246)
assert(objects[5].w == 164 and objects[5].h == 48 and objects[6].text == "Run")
assert(objects[8].text == "Clear Env")
local _, commands = fixture.counts(); assert(commands == 0 and #device_commands == 0)
clicks[1]()
assert(#device_commands == 0, "Run started during startup validation")
drain()
assert(objects[4].text:match("^Ready"))
clicks[1]()
assert(#device_commands == 0, "Run must publish progress before executing")
local in_progress = objects[4].text
clicks[1](); clicks[2]()
assert(objects[4].text == in_progress, "busy clicks obscured the current step")
drain()
assert(#device_commands == 4 and device_commands[1][1] == 0x4351000a)
for i=2,4 do assert(device_commands[i][1] == 0x43510002 and device_commands[i][2] == i-2) end
assert(objects[4].text:match("^Run completed"))
for _, step in ipairs({"复核 Supervisor", "检查运行环境", "读取并校验管理器图标",
    "写入管理器图标", "回读校验管理器图标", "恢复已启用模块", "注册管理器",
    "注册模块应用", "发布应用列表入口"}) do
    local found = false
    for _,text in ipairs(history) do if text:find(step,1,true) then found = true end end
    assert(found, "missing painted phase: " .. step)
end
clicks[1](); assert(#device_commands == 4, "Run must be one-shot")
local _, before_clear = fixture.counts()
clicks[2](); _,commands=fixture.counts(); assert(commands==before_clear, "clear needs second click")
clicks[2](); _,commands=fixture.counts(); assert(commands==before_clear, "clear ran before painting progress")
assert(objects[4].text:match("清理安装环境"))
drain(); _,commands=fixture.counts(); assert(commands==before_clear+1)

-- Destroying the owning page cancels queued work before another write.
clicks[2](); clicks[2]()
local before = #device_commands
deletions[1]()
drain()
_,commands=fixture.counts(); assert(commands==before_clear+1 and #device_commands==before)

-- A failed command keeps its phase on screen and stops all later commands.
setup()
io.open = function(...) return original(...) end
objects, clicks, timers, deletions, history = {}, {}, {}, {}, {}
device_commands = {}
enter(function() assert(load(source, "band11-progress-failure", "t"))() end)
drain()
reject_command = true
clicks[1](); drain()
assert(objects[4].text:find("失败步骤：恢复已启用模块",1,true))
assert(objects[4].text:find("fixture command rejected",1,true))
assert(#device_commands == 0)
local failed = objects[4].text
tick(); assert(objects[4].text == failed, "failed operation kept running")
reject_command = false

-- Missing timer support fails before touching the device, rather than
-- silently reverting to a synchronous flow with invisible progress.
setup()
io.open = function(...) return original(...) end
objects, clicks, timers, deletions, history = {}, {}, {}, {}, {}
local timer_constructor = lvgl.Timer
lvgl.Timer = nil
enter(function() assert(load(source, "band11-progress-no-timer", "t"))() end)
assert(objects[4].text:find("LuaLVGL Timer unavailable",1,true))
clicks[1](); assert(#device_commands == 0)
lvgl.Timer = timer_constructor

-- Wrong firmware has no Run/Clear actions; nil,error is not pcall success.
setup()
identity = "ro.build.version=4.100.138\nro.build.id=" .. build .. "\n"
objects, clicks = {}, {}
enter(function() assert(load(source, "band11-wrong-firmware", "t"))() end)
assert(#clicks == 0)
assert(fixture.counts() == 1)
__band11_fixture_open, __band11_test_execute = nil, nil

os, io, debug, _VERSION = native_os, native_io, native_debug, native_version
os.execute = native_execute
debug.setmetatable(loaded, nil)
loaded.os = original_loaded_os
registry[state_key] = nil
print("Band 11 recovery: native C-frame success/failures, registry cleanup and generated UI passed (" .. _VERSION .. ")")
