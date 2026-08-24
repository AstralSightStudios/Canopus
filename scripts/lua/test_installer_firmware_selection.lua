-- Firmware/resource and family-layout tests for canopus-installer-prod.

local native_io = io
local native_execute = os.execute
local native_remove = os.remove
local output_path = "/data/canopus-installer-firmware-version.tmp"
local getprop_command = "getprop 'ro.build.version' > '" .. output_path .. "'"

local objects
local subscriptions
local lvgl = {
    FLAG = { SCROLLABLE = 1, CLICKABLE = 2, EVENT_BUBBLE = 4 },
    EVENT = { CLICKED = 7 },
    ALIGN = { CENTER = 1 },
    OPA = function(value) return value end,
    Font = function(name, size) return { name = name, size = size } end,
}
local horizontal_resolution = 336
local vertical_resolution = 480
lvgl.HOR_RES = function() return horizontal_resolution end
lvgl.VER_RES = function() return vertical_resolution end

local object_methods = {}
function object_methods:clear_flag() return self end
function object_methods:add_flag() return self end
function object_methods:onClicked(callback) self._clicked = callback; return self end
function object_methods:onevent(event, callback)
    assert(event == lvgl.EVENT.CLICKED)
    self._clicked = callback
    return self
end
function object_methods:set(properties)
    for key, value in pairs(properties) do self[key] = value end
    return self
end
function lvgl.Object(parent, properties)
    local object = setmetatable({ parent = parent, _props = properties or {} },
        { __index = object_methods })
    for key, value in pairs(properties or {}) do object[key] = value end
    objects[#objects + 1] = object
    return object
end
function lvgl.Label(parent, properties)
    local label = setmetatable({ parent = parent, _props = properties or {} },
        { __index = object_methods })
    for key, value in pairs(properties or {}) do label[key] = value end
    if parent then parent._button_text = properties and properties.text end
    objects[#objects + 1] = label
    return label
end

local dataman = {}
function dataman.subscribe(name, owner, callback)
    subscriptions[#subscriptions + 1] = {
        name = name,
        owner = owner,
        callback = callback,
    }
end

package.loaded.lvgl = lvgl
package.loaded.dataman = dataman

local fake_elf = "\127ELF\1\1\1" .. string.rep("\0", 9)
    .. "\1\0\40\0" .. string.rep("X", 492)

local function memory_file(content)
    local position = 1
    return {
        read = function(_, count)
            if count == "*a" or count == nil then
                local result = content:sub(position)
                position = #content + 1
                return result
            end
            local result = content:sub(position, position + count - 1)
            position = position + #result
            return result
        end,
        seek = function(_, where, offset)
            offset = offset or 0
            if where == "end" then position = #content + 1 + offset
            elseif where == "set" then position = 1 + offset end
            return position - 1
        end,
        close = function() return true end,
    }
end

local function install_environment(files, property_value, command_succeeds)
    local property_output
    local commands = {}
    io = {}
    for key, value in pairs(native_io) do io[key] = value end
    io.open = function(path, mode)
        if path == output_path and mode == "r" and property_output then
            return memory_file(property_output)
        end
        local content = files[path]
        if content ~= nil and (mode == "r" or mode == "rb") then
            return memory_file(content)
        end
        return nil
    end
    os.execute = function(command)
        commands[#commands + 1] = command
        if command ~= getprop_command or not command_succeeds then return false end
        property_output = property_value
        return 0
    end
    os.remove = function(path)
        assert(path == output_path)
        property_output = nil
        return true
    end
    return commands
end

local function inspect_page()
    local buttons = {}
    local unsupported_text
    for _, object in ipairs(objects) do
        if object._button_text == "Run" or object._button_text == "Clear Env" then
            buttons[#buttons + 1] = object._button_text
        end
        if type(object.text) == "string"
            and (object.text:match("^Firmware version not supported")
                or object.text == "Installer resources unavailable") then
            unsupported_text = object.text
        end
    end
    table.sort(buttons)
    return buttons, unsupported_text
end

local function run_band10_case(property_value, command_succeeds,
                               resource_present, expected_version)
    local installer_dir = "watchfaces/canopus-installer-prod/xiaomi-band-10-pro/"
    horizontal_resolution = 336
    vertical_resolution = 480
    objects = {}
    subscriptions = {}
    local files = {}
    local expected_module = expected_version and
        (installer_dir .. "canopus_supervisor-xiaomi-band-10-pro-"
            .. expected_version .. ".bin") or nil
    if resource_present then files[expected_module] = fake_elf end
    local commands = install_environment(files, property_value, command_succeeds)
    SCRIPT_PATH = installer_dir
    assert(loadfile(installer_dir .. "main.lua"))()

    local buttons, unsupported_text = inspect_page()
    assert(#commands == 1 and commands[1] == getprop_command)
    if resource_present then
        assert(#buttons == 2 and buttons[1] == "Clear Env" and buttons[2] == "Run")
        assert(unsupported_text == nil)
    else
        assert(#buttons == 0)
        assert(unsupported_text == "Firmware version not supported\n"
            .. tostring(expected_version or "Unknown"))
    end
end

local function band9_profile()
    return [[
return {
    target_id = "xiaomi-band-9-3.1.32",
    firmware_sha256 = "9c02dab4020b2cc9666ee7d34cf27d311b76aadcec519a38361bbcbd94c53264",
    status = "STATIC_RECOVERED",
    loader_family = "nsh-mw-stage1-stage2",
    memalign = 0x0c16ab8d,
    free = 0x0c16a425,
    mpu_alloc = 0x0c5228a5,
    mpu_configure = 0x0c52272d,
    mpu_release = 0x0c5228fd,
    mpu_rnr = 0xe000ed98,
    mpu_rbar = 0xe000ed9c,
    mpu_rlar = 0xe000eda0,
    mpu_region_count = 8,
    cave = 0x2006a9b0,
    cave_result = 0x2006a9cc,
    cave_original = {
        0, 0, 0, 0, 0x726f6e5f, 0x73616c66, 0x70615f68, 0x72665f69,
    },
}
]]
end

local function run_band9_case(property_value, command_succeeds, resource_present)
    local installer_dir = "watchfaces/canopus-installer-prod/xiaomi-band-9/"
    local target_id = "xiaomi-band-9-3.1.32"
    horizontal_resolution = 192
    vertical_resolution = 490
    objects = {}
    subscriptions = {}
    local files = {}
    if resource_present then
        files[installer_dir .. "canopus_loader_profile-" .. target_id .. ".bin"] =
            band9_profile()
        files[installer_dir .. "canopus_stage1-" .. target_id .. ".bin"] =
            "return { size = 8, words = { 0xbf00bf00, 0xbf00bf00 } }"
        files[installer_dir .. "canopus_stage2-" .. target_id .. ".bin"] =
            string.rep("S", 64)
        files[installer_dir .. "canopus_supervisor-" .. target_id .. ".bin"] = fake_elf
    end
    local commands = install_environment(files, property_value, command_succeeds)
    SCRIPT_PATH = installer_dir
    assert(loadfile(installer_dir .. "main.lua"))()

    local buttons, unsupported_text = inspect_page()
    assert(#commands == 0, "Band 9 installer must not query firmware version")
    if resource_present then
        assert(#buttons == 2 and buttons[1] == "Clear Env" and buttons[2] == "Run")
        assert(unsupported_text == nil)
        assert(#subscriptions == 0, "Band 9 installer must not depend on dataman")
        local fixed_band10_geometry = false
        for _, object in ipairs(objects) do
            if object._props.w == 336 or object._props.h == 480 then
                fixed_band10_geometry = true
            end
        end
        assert(not fixed_band10_geometry,
            "Band 9 production page must not inherit Band 10 fixed geometry")
    else
        assert(#buttons == 0)
        assert(unsupported_text == "Installer resources unavailable")
    end
end

run_band10_case("3.101.036\n", true, true, "3.101.036")
run_band10_case("3.101.043\n", true, true, "3.101.043")
run_band10_case("3.101.999\n", true, false, "3.101.999")
run_band10_case("3.101.036;insmod /bad\n", true, false, nil)
run_band10_case(nil, false, false, nil)
run_band9_case("3.1.32\n", true, true)
run_band9_case("3.1.175\n", true, false)
run_band9_case(nil, false, false)

io = native_io
os.execute = native_execute
os.remove = native_remove
print("installer firmware selection: PASS")
