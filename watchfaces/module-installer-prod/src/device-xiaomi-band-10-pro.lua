-- Band 10 Pro Lua keeps os.execute and /dev access, so this keeps the
-- device-proven flow of the earlier installers: /dev/canopus (registered by
-- every Supervisor, including those without the ordinary-IO installer node),
-- a 36-byte reply and a CPS1 status query for the diagnostic. Its build.prop
-- carries ro.build.id without the build date; the exact build string is
-- ro.build.customer_version.
local ENDPOINT, REPLY_SIZE = "/dev/canopus", 36
local SUPERVISOR_MISSING = "请先运行 Canopus 框架安装表盘"
local BUILD_PROPERTY = "ro.build.customer_version"
local execute = os.execute
local function getprop(key)
    local temp = "/data/" .. CONFIG.token .. "-installer-prop.tmp"
    local ok = execute("getprop " .. key .. " > " .. temp)
    local value = (ok == true or ok == 0) and read_all(temp)
    if type(os.remove) == "function" then pcall(os.remove, temp) end
    value = type(value) == "string" and value:match("^%s*(.-)%s*$")
    return value ~= "" and value or nil
end
local function fallback_identity()
    -- The framework installer identifies this firmware through getprop.
    if type(execute) ~= "function" then return nil end
    return getprop("ro.build.version"), getprop(BUILD_PROPERTY)
end
local function prepare_inbox(checkpoint)
    if type(execute) ~= "function" then return end
    checkpoint("准备模块安装目录")
    -- mkdir may report EEXIST; the staged writes determine usability.
    execute("mkdir /data/canopus")
    execute("mkdir /data/canopus/inbox")
end
local function failure_detail(_, exchange)
    local ok, record = pcall(exchange,
        string.pack("<I4I4I4I4", 0x43504331, 0x43510001, 0x43514431, 0), 384)
    if not ok or u32(record, 0) ~= 0x43505331 then return "" end
    return " error " .. string.unpack("<i4", record, 33)
end
