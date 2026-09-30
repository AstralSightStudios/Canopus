-- P65 deployment withdrawn after a device Logo boot-loop report.
-- Disabled actions perform no shell, filesystem or device writes.
local DEPLOYMENT_ENABLED = false
local lvgl = require("lvgl")
local DEVICE = "/dev/canopus"
local MODULE_NAME = "canopus_p65_sup"
local MODULE_PATH = SCRIPT_PATH .. "supervisor.bin"
local VERSION_PATH = "/tmp/canopus-p65-version.txt"
local EXPECTED_SIZE = 0 -- Replaced by the resource-bundle builder.
local blocked = false
local busy = false

local root = lvgl.Object(nil, {
    w = lvgl.HOR_RES(), h = lvgl.VER_RES(), bg_color = 0x07111F,
    bg_opa = lvgl.OPA(100), border_width = 0, pad_all = 0,
})
local title = lvgl.Label(root, {
    text = "Canopus · P65", text_color = 0xFFFFFF,
    align = { type = lvgl.ALIGN.TOP_MID, x_ofs = 0, y_ofs = 24 },
})
local status = lvgl.Label(root, {
    text = "P65 deployment withdrawn.\nReported boot loop.\nDo not install or retry.",
    text_color = 0xBFD9FF, w = lvgl.HOR_RES() - 48, h = 116,
    align = { type = lvgl.ALIGN.TOP_MID, x_ofs = 0, y_ofs = 70 },
})
local function set_status(text) status:set { text = text } end
local function read(path, size)
    local f = io.open(path, "rb")
    if not f then return nil end
    local ok, value = pcall(f.read, f, size or "*a")
    local closed, result = pcall(f.close, f)
    if not ok or not closed or result == nil then return nil end
    return value
end
local function write(path, data)
    local f = io.open(path, "wb")
    if not f then return false end
    local ok, count = pcall(f.write, f, data)
    local closed, result = pcall(f.close, f)
    return ok and count ~= nil and closed and result ~= nil
        and (type(count) ~= "number" or count == #data)
end
local function word(n)
    return string.char(n % 256, math.floor(n / 256) % 256,
        math.floor(n / 65536) % 256, math.floor(n / 16777216) % 256)
end
local function u32(data, offset)
    if type(data) ~= "string" or #data < offset + 4 then return nil end
    local a, b, c, d = data:byte(offset + 1, offset + 4)
    return a + b * 256 + c * 65536 + d * 16777216
end
local function i32(data, offset)
    local value = u32(data, offset)
    if value == nil then return nil end
    return value >= 2147483648 and value - 4294967296 or value
end
local function execute(command)
    local ok, result = pcall(os.execute, command)
    return ok and (result == true or result == 0)
end
local function supervisor_status(allow_query)
    local data = read(DEVICE, 384)
    if type(data) == "string" and u32(data, 0) == 0x43504332
        and allow_query ~= false and DEPLOYMENT_ENABLED then
        -- The native Manager uses CPC2. Switch this client back to the
        -- legacy diagnostic view without clearing the previous error code.
        local query = word(0x43504331) .. word(0x43510001)
            .. word(0x43514431) .. word(0)
        if not write(DEVICE, query) then return nil end
        data = read(DEVICE, 384)
    end
    if type(data) ~= "string" or #data ~= 384 or u32(data, 0) ~= 0x43505331
        or u32(data, 4) ~= 1 then return nil end
    local first, last = u32(data, 36), u32(data, 40)
    if first ~= last or first % 2 ~= 0 then return nil end
    return data
end
local function fail(text)
    blocked = true
    set_status(text .. "\nStop testing. Do not reboot to retry.")
end
local function check_version()
    if not execute("getprop ro.build.version > " .. VERSION_PATH) then return false end
    local version = read(VERSION_PATH)
    return type(version) == "string" and version:match("^%s*(.-)%s*$") == "3.100.043"
end
local function install_supervisor()
    if not DEPLOYMENT_ENABLED then
        set_status("P65 deployment withdrawn.\nDo not install or retry.") return
    end
    if blocked then return end
    if not check_version() then fail("Firmware version check failed.") return end
    if supervisor_status() then
        set_status("Supervisor is already running.\nTap Register Manager.") return
    end
    local resource = read(MODULE_PATH)
    if type(resource) ~= "string" or #resource ~= EXPECTED_SIZE
        or resource:sub(1, 7) ~= "\127ELF\1\1\1"
        or resource:byte(17) ~= 1 or resource:byte(18) ~= 0
        or resource:byte(19) ~= 40 or resource:byte(20) ~= 0 then
        fail("Missing or invalid native resource.") return
    end
    resource = nil
    -- Existing directories are fine; the writable file is checked explicitly.
    execute("mkdir /data/canopus")
    execute("mkdir /data/canopus/inbox")
    local icon = read(SCRIPT_PATH .. "manager_icon.bin")
    if type(icon) ~= "string" or #icon == 0
        or not write("/data/canopus/manager_icon.bin", icon)
        or read("/data/canopus/manager_icon.bin") ~= icon then
        fail("Cannot stage Manager icon.") return
    end
    icon = nil
    -- Fixed module name prevents replacing a partially initialized image.
    -- Even an insmod error may have escaped native callbacks; do not retry.
    if not execute("insmod " .. MODULE_PATH .. " " .. MODULE_NAME) then
        fail("Supervisor load failed.") return
    end
    if not supervisor_status() then
        fail("Loaded, but endpoint unavailable.") return
    end
    set_status("Supervisor loaded.\nTap Register Manager.")
end
local function manager_diagnostic(data)
    if u32(data, 48) ~= 0x434E5431 then return "" end
    return "\nID: " .. tostring(i32(data, 52)) .. " · App: " .. tostring(i32(data, 56))
end
local function command(op, argument)
    if not DEPLOYMENT_ENABLED then
        set_status("P65 deployment withdrawn.\nDo not install or retry.") return
    end
    if blocked then return end
    if not supervisor_status() then fail("Supervisor endpoint unavailable.") return end
    local frame = word(0x43504331) .. word(op) .. word(argument or 0) .. word(0)
    if not write(DEVICE, frame) then fail("Supervisor command failed.") return end
    local data = supervisor_status(false)
    if not data or u32(data, 20) ~= op or u32(data, 24) ~= 5 then
        local code = data and i32(data, 32) or "unavailable"
        local text = "Operation failed: " .. tostring(code)
        if code == -13 or code == -17 then
            text = "Registry save failed: " .. tostring(code)
                .. "\nNative registration may have completed."
        elseif code == -5 then
            text = "Registration stage failed: -5"
        end
        if data then text = text .. manager_diagnostic(data) end
        fail(text) return
    end
    set_status("Operation complete.\nOpen Canopus from Launcher.\nResident until reboot.")
end
local function show_status()
    local data = supervisor_status()
    if not data then set_status("Supervisor endpoint unavailable.") return end
    set_status("Modules: " .. tostring(u32(data, 16))
        .. "\nResult: " .. tostring(u32(data, 24))
        .. " · Error: " .. tostring(i32(data, 32))
        .. manager_diagnostic(data) .. "\nP65 withdrawn. Do not retry.")
end
local function button(text, y, callback)
    local object = lvgl.Object(root, {
        w = lvgl.HOR_RES() - 64, h = 42, bg_color = 0x153454,
        bg_opa = lvgl.OPA(100), border_width = 0, radius = 12,
        align = { type = lvgl.ALIGN.TOP_MID, x_ofs = 0, y_ofs = y },
    })
    lvgl.Label(object, { text = text, text_color = 0xFFFFFF,
        align = lvgl.ALIGN.CENTER })
    object:onClicked(function()
        if busy then return end
        busy = true
        local ok = pcall(callback)
        busy = false
        if not ok then fail("Installer exception.") end
    end)
end
button("Install Supervisor", 200, install_supervisor)
button("Register Manager", 248, function() command(0x43510002, 0) end)
button("Register module apps", 296, function() command(0x43510002, 1) end)
button("Publish module apps", 344, function() command(0x43510002, 2) end)
button("Read status", 392, show_status)
