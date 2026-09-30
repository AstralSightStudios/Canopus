-- Isolated host smoke for the real P65 installer source. No host shell I/O.
local source_path, fault, mode = ...
local host_open = io.open
local file = assert(host_open(source_path, "rb"))
local source = file:read("*a")
file:close()
local expected = assert(tonumber(source:match("local EXPECTED_SIZE = (%d+)")))
assert(expected >= 52)
local enabled_model = mode == "enabled-model"
if enabled_model then
    -- In-memory host-only model of the historical flow, never packaged.
    local count
    source, count = source:gsub("local DEPLOYMENT_ENABLED = false", "local DEPLOYMENT_ENABLED = true")
    assert(count == 1)
end
local callbacks, labels, commands = {}, {}, {}
local lvgl = {
    HOR_RES = function() return 466 end,
    VER_RES = function() return 466 end,
    OPA = function(n) return n end,
    ALIGN = { TOP_MID = 1, CENTER = 2 },
}
function lvgl.Object(parent, properties)
    local object = { parent = parent, properties = properties }
    function object:onClicked(callback) callbacks[#callbacks + 1] = callback end
    return object
end
function lvgl.Label(parent, properties)
    local object = { parent = parent, properties = properties }
    function object:set(value)
        self.properties = value
        labels[#labels + 1] = value.text
    end
    return object
end
package.preload.lvgl = function() return lvgl end
SCRIPT_PATH = "/resource/"
local function word(n)
    return string.char(n % 256, math.floor(n / 256) % 256,
        math.floor(n / 65536) % 256, math.floor(n / 16777216) % 256)
end
local function u32(data, offset)
    local a, b, c, d = data:byte(offset + 1, offset + 4)
    return a + b * 256 + c * 65536 + d * 16777216
end
local elf = "\127ELF\1\1\1" .. string.rep("\0", 9)
    .. "\1\0\40\0" .. string.rep("\0", expected - 20)
local files = { [SCRIPT_PATH .. "supervisor.bin"] = elf,
                [SCRIPT_PATH .. "manager_icon.bin"] = "test-icon" }
if fault == "resource" then files[SCRIPT_PATH .. "supervisor.bin"] = "bad" end
local running = fault == "already_running" or fault == "native_client" or fault == "withdrawn_diagnostics"
local native_response = fault == "native_client"
local op, result = 0, 5
if fault == "withdrawn_diagnostics" then op, result = 0x43510002, 4 end
local stages = {}
local device_writes, write_mode_opens, filesystem_writes = 0, 0, 0
local function status()
    if native_response then return word(0x43504332) .. string.rep("\0", 32) end
    local data = word(0x43505331) .. word(1) .. word(1) .. word(0)
        .. word(0) .. word(op) .. word(result) .. word(0)
        .. word((fault == "command_result" or fault == "withdrawn_diagnostics") and 4294967291
            or fault == "registry_result" and 4294967283 or 0)
        .. word(2) .. word(fault == "torn_status" and 4 or 2) .. word(0)
        .. word(0x434E5431) .. word(0)
        .. word(fault == "withdrawn_diagnostics" and 4294967295 or 0)
    return data .. string.rep("\0", 384 - #data)
end
io.open = function(path, mode)
    if mode and (mode:find("w") or mode:find("a") or mode:find("+")) then
        write_mode_opens = write_mode_opens + 1
    end
    if path == "/dev/canopus" then
        if not running or fault == "endpoint" then return nil end
        if mode == "rb" then
            return { read = function() return status() end,
                     close = function() return true end }
        end
        return {
            write = function(_, data)
                device_writes = device_writes + 1
                assert(#data == 16 and u32(data, 0) == 0x43504331)
                op = u32(data, 4)
                native_response = false
                if op ~= 0x43510001 then stages[#stages + 1] = u32(data, 8) end
                result = (fault == "command_result" or fault == "registry_result") and 4 or 5
                if fault == "short_write" then return #data - 1 end
                return #data
            end,
            close = function() return true end,
        }
    end
    if mode == "rb" then
        local data = files[path]
        if not data then return nil end
        return { read = function() return data end,
                 close = function() return true end }
    end
    if mode == "wb" then
        if fault == "icon" then return nil end
        return { write = function(_, data)
                     filesystem_writes = filesystem_writes + 1
                     files[path] = data; return #data
                 end,
                 close = function() return true end }
    end
    return nil
end
os.execute = function(command)
    commands[#commands + 1] = command
    if command:match("^getprop") then
        if fault == "getprop" then return false end
        files["/tmp/canopus-p65-version.txt"] =
            (fault == "version" and "3.100.042" or "3.100.043") .. "\n"
        return true
    end
    if command:match("^mkdir") then return true end
    if command:match("^insmod") then
        if fault == "insmod" then return false end
        running = true
        return true
    end
    error("Unexpected shell command: " .. command)
end
assert(load(source, source_path))()
assert(#commands == 0 and #stages == 0 and write_mode_opens == 0
    and filesystem_writes == 0 and device_writes == 0, "installer mutated state before tap")
assert(#callbacks == 5)
callbacks[1]()
callbacks[2]()
callbacks[3]()
callbacks[4]()
callbacks[5]()
local insmods = 0
for _, command in ipairs(commands) do
    if command:match("^insmod") then insmods = insmods + 1 end
    assert(not command:match("rmmod"))
end
if not enabled_model then
    assert(#commands == 0 and #stages == 0 and insmods == 0 and device_writes == 0
        and write_mode_opens == 0 and filesystem_writes == 0,
        "withdrawn installer performed a mutation")
elseif fault == nil or fault == "success" or fault == "already_running" or fault == "native_client" then
    assert(insmods == ((fault == nil or fault == "success") and 1 or 0))
    assert(#stages == 3 and stages[1] == 0 and stages[2] == 1 and stages[3] == 2)
else
    assert(insmods <= 1)
    local before = insmods
    callbacks[1]()
    local after = 0
    for _, command in ipairs(commands) do
        if command:match("^insmod") then after = after + 1 end
    end
    assert(after == before, "failure allowed a second load")
    if fault == "command_result" or fault == "registry_result" or fault == "short_write" then
        assert(#stages == 1)
    else
        assert(#stages == 0)
    end
end
local joined = table.concat(labels, "\n")
assert(not joined:find("Reboot before retrying", 1, true))
if not enabled_model and fault == "withdrawn_diagnostics" then
    assert(joined:find("Error: -5", 1, true))
    assert(joined:find("ID: 0 · App: -1", 1, true))
    assert(not joined:find("4294967291", 1, true))
elseif enabled_model and fault == "command_result" then
    assert(joined:find("Registration stage failed: -5", 1, true))
    assert(not joined:find("4294967291", 1, true))
elseif enabled_model and fault == "registry_result" then
    assert(joined:find("Registry save failed: -13", 1, true))
    assert(joined:find("Native registration may have completed", 1, true))
end
io.open = host_open
print("P65 installer smoke OK: " .. tostring(fault or "success"))
