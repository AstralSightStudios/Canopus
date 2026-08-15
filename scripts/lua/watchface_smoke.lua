-- watchface_smoke.lua — load every watchface under a stubbed lvgl binding and
-- fire every button handler, catching nil-global / scope bugs (e.g. a
-- function referencing a `local status` declared later, which resolves to a
-- nil global).
--
-- Usage: lua scripts/lua/watchface_smoke.lua [watchface-dir...]
-- Returns non-zero on any load/click failure.

local lvgl = {}
lvgl.HOR_RES = function() return 336 end
lvgl.VER_RES = function() return 480 end
lvgl.OPA = function(v) return v end
lvgl.FLAG = { SCROLLABLE = 1, CLICKABLE = 2 }
lvgl.ALIGN = { CENTER = 1, TOP_MID = 2, BOTTOM_MID = 3 }

local created = {}
local obj_mt = {}
function obj_mt:clear_flag() return self end
function obj_mt:add_flag() return self end
function obj_mt:set(props) self._last_set = props; return self end
function obj_mt:onClicked(fn) self._click = fn; table.insert(created, self); return self end
function lvgl.Object(parent, props)
    local o = setmetatable({ _parent = parent, _props = props }, { __index = obj_mt })
    table.insert(created, o)
    return o
end
function lvgl.Label(parent, props)
    local o = setmetatable({ _parent = parent, _props = props }, { __index = obj_mt })
    table.insert(created, o)
    return o
end

_G.SCRIPT_PATH = "/fake/"
package.loaded["lvgl"] = lvgl

local original_io_open = io.open
local original_os_execute = os.execute

local function module_installer_io(fault)
    local files = {
        ["/fake/receipt.bin"] = "CMI1" .. string.rep("\0", 252),
        ["/fake/module.bin"] = "\127ELF" .. string.rep("\0", 508),
    }
    local device_request
    local function open(path, mode)
        if path == "/dev/canopus" then
            if mode == "wb" then
                return {
                    write = function(_, data)
                        device_request = data
                        if fault == "short_write" then return #data - 1 end
                        return #data
                    end,
                    close = function() return true end,
                }
            end
            local function le32(value)
                return string.char(value % 256, math.floor(value / 256) % 256,
                    math.floor(value / 65536) % 256,
                    math.floor(value / 16777216) % 256)
            end
            local request_id = fault == "stale_response" and 9 or 1
            local response = "2CPC" .. string.char(36, 0, 2, 0, 1, 0, 0, 0)
                .. le32(36) .. le32(2) .. le32(request_id) .. le32(0)
                .. le32(5) .. le32(0)
            return {
                read = function() return response end,
                close = function() return true end,
            }
        end
        if mode == "rb" then
            if files[path] == nil then return nil end
            return {
                read = function() return files[path] end,
                close = function() return true end,
            }
        end
        if mode == "wb" then
            return {
                write = function(_, data) files[path] = data; return true end,
                close = function() return true end,
            }
        end
        return nil
    end
    return open, function()
        return device_request, files
    end
end

local function band9_installer_io(fault)
    local cave = {
        [0x20084e10] = 0, [0x20084e14] = 0,
        [0x20084e18] = 0, [0x20084e1c] = 0,
        [0x20084e20] = 0x726f6e5f, [0x20084e24] = 0x73616c66,
        [0x20084e28] = 0x70615f68, [0x20084e2c] = 0x72665f69,
    }
    local files = {
        ["/fake/canopus_stage1_band9.lua"] =
            "return { size=8, words={0xbf00bf00,0xbf00bf00} }",
        ["/fake/canopus_stage2-band9.bin"] = string.rep("S", 64),
        ["/fake/canopus_supervisor-band9.bin"] =
            "\127ELF\1\1\1" .. string.rep("\0", 9) .. "\1\0\40\0" .. string.rep("X", 492),
    }
    local device_present = false
    local used_insmod = false
    local freed = false
    local released = false
    local configured_rlar
    if fault == "cave_mismatch" then cave[0x20084e10] = 1 end
    local function le32(value)
        return string.char(value % 256, math.floor(value / 256) % 256,
            math.floor(value / 65536) % 256,
            math.floor(value / 16777216) % 256)
    end
    local function status()
        local words = { 0x43505331, 1, 1, 0, 0, 0, 0, 0, 0, 2, 2, 0 }
        local out = ""
        for _, value in ipairs(words) do out = out .. le32(value) end
        return out .. string.rep("\0", 384 - #out)
    end
    local function open(path, mode)
        if path == "/dev/canopus" then
            if not device_present then return nil end
            if mode == "wb" then
                return { write = function(_, data) return #data end,
                    close = function() return true end }
            end
            return { read = function() return status() end,
                close = function() return true end }
        end
        if mode == "rb" or mode == "r" then
            if files[path] == nil then return nil end
            local position = 1
            return {
                read = function(_, count)
                    if count == "*a" or count == nil then
                        local result = files[path]:sub(position)
                        position = #files[path] + 1
                        return result
                    end
                    local result = files[path]:sub(position, position + count - 1)
                    position = position + #result
                    return result
                end,
                seek = function(_, where, offset)
                    offset = offset or 0
                    if where == "end" then position = #files[path] + 1 + offset
                    elseif where == "set" then position = 1 + offset end
                    return position - 1
                end,
                close = function() return true end }
        end
        if mode == "wb" then
            return { write = function(_, data) files[path] = data; return true end,
                close = function() return true end }
        end
        return nil
    end
    local function execute(command)
        local property_output = command:match(
            "^getprop 'ro%.build%.version' > '(.+)'$")
        if property_output then
            files[property_output] = "3.1.175\n"
            return true
        end
        if command:match("^insmod ") then used_insmod = true; return false end
        local address, value = command:match("^mw ([0-9a-fA-F]+)=([0-9a-fA-F]+)$")
        if address then
            local addr = tonumber(address, 16)
            local word = tonumber(value, 16)
            cave[addr] = word
            if addr == 0xe000eda0 and word ~= 0 then
                configured_rlar = word
                if fault == "mpu_configuration_failure" then return false end
            end
            return true
        end
        local read_address, output = command:match("^mw ([0-9a-fA-F]+) 4 > (.+)$")
        if read_address then
            local addr = tonumber(read_address, 16)
            files[output] = string.format("  0x%08x = 0x%08x\n", addr, cave[addr] or 0)
            return true
        end
        local exec_address = command:match("^exec ([0-9a-fA-F]+)$")
        if exec_address then
            local addr = tonumber(exec_address, 16)
            if addr == 0x20084e11 then
                local callable = cave[0x20084e28]
                if fault == "mailbox_exec_failure" then return false end
                local result = 0
                if callable == 0x0c0f21ed then
                    result = fault == "allocation_failure" and 0 or 0x21000000
                elseif callable == 0x0c51d8d1 then
                    result = fault == "mpu_exhaustion" and 255 or 3
                elseif callable == 0x0c51d929 then
                    released = true
                elseif callable == 0x0c0f1b01 then
                    freed = true
                end
                cave[0x20084e2c] = result
                return result == 0
            end
            if addr == 0x21000001 then
                if fault == "stage1_exec_failure" then return false end
                device_present = true
                return true
            end
        end
        return true
    end
    return open, execute, function()
        return cave, files, device_present, used_insmod, freed, released,
            configured_rlar
    end
end

local function check(path, installer_fault)
    created = {}
    local installer_state
    local band9_state
    local band9_fault = installer_fault and installer_fault:match("^band9:(.+)$")
    if path:match("canopus%-installer/main%.lua$") then
        io.open, os.execute, band9_state = band9_installer_io(band9_fault)
    elseif path:match("canopus_hello/main%.lua$") then
        io.open, installer_state = module_installer_io(installer_fault)
        os.execute = function() return true end
    end
    local ok, err = pcall(dofile, path)
    if not ok then
        io.open = original_io_open
        os.execute = original_os_execute
        print("LOAD FAIL:", path, err)
        return false
    end
    if installer_state then
        local request, files = installer_state()
        if type(request) ~= "string" or request:sub(1, 4) ~= "2CPC"
            or request:byte(17) ~= 2
            or request:sub(-14) ~= "canopus_hello\0"
            or files["/data/canopus/inbox/canopus_hello.cmi"] == nil
            or files["/data/canopus/inbox/canopus_hello.ko"] == nil then
            print("INSTALL FLOW FAIL:", path)
            return false
        end
        if installer_fault then
            local diagnosed = false
            for _, object in ipairs(created) do
                local text = object._last_set and object._last_set.text
                if type(text) == "string" and text:match("Install failed") then
                    diagnosed = true
                end
            end
            if not diagnosed then
                print("INSTALL DIAGNOSTIC FAIL:", path, installer_fault)
                return false
            end
        end
    end
    local clicks = 0
    for _, o in ipairs(created) do
        if o._click then
            local ok2, err2 = pcall(o._click)
            if not ok2 then
                io.open = original_io_open
                os.execute = original_os_execute
                print("CLICK FAIL:", path, tostring(err2))
                return false
            end
            clicks = clicks + 1
        end
    end
    if band9_state then
        local cave, files, device_present, used_insmod, freed, released,
            configured_rlar = band9_state()
        local original = { 0, 0, 0, 0, 0x726f6e5f, 0x73616c66,
            0x70615f68, 0x72665f69 }
        if band9_fault == "cave_mismatch" then original[1] = 1 end
        for i, value in ipairs(original) do
            if cave[0x20084e10 + (i - 1) * 4] ~= value then
                io.open = original_io_open
                os.execute = original_os_execute
                print("BAND9 CAVE RESTORE FAIL:", path, installer_fault or "success")
                return false
            end
        end
        local staged = files["/data/canopus/stage2.bin"] ~= nil
            and files["/data/canopus/supervisor.elf"] ~= nil
        local fault_ok =
            (band9_fault == "cave_mismatch" and not freed and not released)
            or (band9_fault == "mailbox_exec_failure" and not freed and not released)
            or (band9_fault == "allocation_failure" and not freed and not released)
            or (band9_fault == "mpu_exhaustion" and freed and not released)
            or (band9_fault == "mpu_configuration_failure" and freed and released)
            or (band9_fault == "stage1_exec_failure" and freed and released)
        if band9_fault then
            if device_present or used_insmod or not staged or not fault_ok then
                io.open = original_io_open
                os.execute = original_os_execute
                print("BAND9 FAULT FLOW FAIL:", path, band9_fault,
                    "device=" .. tostring(device_present),
                    "insmod=" .. tostring(used_insmod),
                    "freed=" .. tostring(freed),
                    "released=" .. tostring(released))
                return false
            end
        elseif not device_present or used_insmod or not staged
            or not freed or not released
            or configured_rlar == nil or configured_rlar % 8 ~= 5 then
            io.open = original_io_open
            os.execute = original_os_execute
            print("BAND9 BOOTSTRAP FLOW FAIL:", path,
                "device=" .. tostring(device_present),
                "insmod=" .. tostring(used_insmod),
                "stage2=" .. tostring(files["/data/canopus/stage2.bin"] ~= nil),
                "supervisor=" .. tostring(files["/data/canopus/supervisor.elf"] ~= nil),
                "freed=" .. tostring(freed),
                "released=" .. tostring(released),
                "rlar=" .. tostring(configured_rlar))
            return false
        end
    end
    io.open = original_io_open
    os.execute = original_os_execute
    print(string.format("watchface OK: %s (%d buttons clicked)", path, clicks))
    return true
end

local args = { ... }
if #args == 0 then
    args = {
        "watchfaces/canopus-installer/main.lua",
        "watchfaces/canopus_hello/main.lua",
    }
end
local ok_all = true
for _, p in ipairs(args) do
    if not check(p) then ok_all = false end
end
if ok_all then
    local band9_faults = {
        "cave_mismatch", "mailbox_exec_failure", "allocation_failure",
        "mpu_exhaustion", "mpu_configuration_failure", "stage1_exec_failure",
    }
    for _, fault in ipairs(band9_faults) do
        if not check("watchfaces/canopus-installer/main.lua", "band9:" .. fault) then
            ok_all = false
        end
    end
    if not check("watchfaces/canopus_hello/main.lua", "short_write") then
        ok_all = false
    end
    if not check("watchfaces/canopus_hello/main.lua", "stale_response") then
        ok_all = false
    end
end
if not ok_all then os.exit(1) end
