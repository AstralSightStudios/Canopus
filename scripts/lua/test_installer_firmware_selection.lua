-- Firmware-resource selection tests for canopus-installer-prod.

local native_io = io
local native_execute = os.execute
local native_remove = os.remove
local installer_dir = "watchfaces/canopus-installer-prod/"
local output_path = "/data/canopus-installer-firmware-version.tmp"
local getprop_command = "getprop 'ro.build.version' > '" .. output_path .. "'"

local lvgl = {
    FLAG = { SCROLLABLE = 1, CLICKABLE = 2 },
    ALIGN = { CENTER = 1 },
    HOR_RES = function() return 336 end,
    VER_RES = function() return 480 end,
    OPA = function(value) return value end,
}
local objects
local object_methods = {}
function object_methods:clear_flag() return self end
function object_methods:add_flag() return self end
function object_methods:onClicked(callback) self._clicked = callback; return self end
function object_methods:set(properties)
    for key, value in pairs(properties) do self[key] = value end
    return self
end
function lvgl.Object(parent, properties)
    local object = setmetatable({ parent = parent }, { __index = object_methods })
    for key, value in pairs(properties or {}) do object[key] = value end
    objects[#objects + 1] = object
    return object
end
function lvgl.Label(parent, properties)
    local label = setmetatable({ parent = parent }, { __index = object_methods })
    for key, value in pairs(properties or {}) do label[key] = value end
    if parent then parent._button_text = properties and properties.text end
    objects[#objects + 1] = label
    return label
end
package.loaded.lvgl = lvgl

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

local function run_case(property_value, command_succeeds, resource_present,
                        expected_version)
    objects = {}
    local property_output
    local opened_module
    local commands = {}
    local expected_module = expected_version and
        (installer_dir .. "canopus_supervisor-xiaomi-band-10-pro-"
            .. expected_version .. ".bin") or nil

    io = {}
    for key, value in pairs(native_io) do io[key] = value end
    io.open = function(path, mode)
        if path == output_path and mode == "r" and property_output then
            return memory_file(property_output)
        end
        if path == expected_module and mode == "rb" and resource_present then
            opened_module = path
            return memory_file(fake_elf)
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
    SCRIPT_PATH = installer_dir
    assert(loadfile(installer_dir .. "main.lua"))()

    local button_count = 0
    local unsupported_text
    for _, object in ipairs(objects) do
        if object._button_text == "Run" or object._button_text == "Clear Env" then
            button_count = button_count + 1
        end
        if type(object.text) == "string"
            and object.text:match("^Firmware version not supported") then
            unsupported_text = object.text
        end
    end
    assert(#commands == 1 and commands[1] == getprop_command)
    if resource_present then
        assert(button_count == 2, "supported firmware must show both controls")
        assert(opened_module == expected_module,
            "installer did not verify the exact versioned Supervisor")
        assert(unsupported_text == nil)
    else
        assert(button_count == 0, "unsupported firmware must show no controls")
        assert(unsupported_text == "Firmware version not supported\n"
            .. tostring(expected_version or "Unknown"))
    end
end

run_case("3.101.030\n", true, true, "3.101.030")
run_case("3.101.036\n", true, true, "3.101.036")
run_case("3.101.999\n", true, false, "3.101.999")
run_case("3.101.036;insmod /bad\n", true, false, nil)
run_case(nil, false, false, nil)

io = native_io
os.execute = native_execute
os.remove = native_remove
print("installer firmware selection: PASS")
