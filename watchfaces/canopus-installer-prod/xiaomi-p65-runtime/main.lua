-- Manual post-reboot bootstrap. No system boot scripts or firmware are changed.
-- @PROGRESS@
local lvgl = require("lvgl")
local EXPECTED_SIZE = 0 -- Replaced by the builder.
local EXPECTED_CRC = "" -- Replaced by the builder.
local DEVICE = "/dev/canopus"
local MODULE_PATH = "/data/canopus/p65-runtime-supervisor.bin"
local MODULE_NAME = "canopus_p65_runtime"
local blocked, runner = false, nil
local function check(value, message) if not value then error(message, 0) end end
local function read(path, size)
    local f = io.open(path, "rb")
    if not f then return nil end
    local ok, data = pcall(f.read, f, size or "*a")
    local closed, result = pcall(f.close, f)
    return ok and closed and result and data or nil
end
local function write(path, data)
    local f = io.open(path, "wb")
    check(f, "Cannot open " .. path)
    local ok, result = pcall(f.write, f, data)
    local closed, close_result = pcall(f.close, f)
    check(ok and result and (type(result) ~= "number" or result == #data)
        and closed and close_result, "Write failed: " .. path)
end
local function execute(command)
    local ok, result = pcall(os.execute, command)
    return ok and (result == true or result == 0)
end
local function u32(data, off)
    if type(data) ~= "string" or #data < off + 4 then return nil end
    return string.unpack("<I4", data, off + 1)
end
local function crc32(data)
    local crc = 0xffffffff
    for i = 1, #data do
        crc = crc ~ data:byte(i)
        for _ = 1, 8 do crc = (crc >> 1) ~ ((crc & 1) ~= 0 and 0xedb88320 or 0) end
    end
    -- Byte form compares identically on firmware Lua32 and host Lua64.
    return string.pack("<I4", (crc ~ 0xffffffff) & 0xffffffff)
end
local function status_record(query)
    if query then write(DEVICE, string.pack("<I4I4I4I4", 0x43504331, 0x43510001, 0, 0)) end
    local data = read(DEVICE, 384)
    if not data or #data ~= 384 or u32(data, 0) ~= 0x43505331 or u32(data, 4) ~= 1
        or u32(data, 36) ~= u32(data, 40) or u32(data, 36) % 2 ~= 0 then return nil end
    return data
end
local function command(op, argument)
    write(DEVICE, string.pack("<I4I4I4I4", 0x43504331, op, argument or 0, 0))
    local data = status_record(false)
    check(data and u32(data, 20) == op, "Invalid supervisor response")
    check(u32(data, 24) == 5, "Canopus error " .. tostring(string.unpack("<i4", data, 33)))
    return data
end
local width, height = lvgl.HOR_RES(), lvgl.VER_RES()
local root = lvgl.Object(nil, {w=width, h=height, bg_color=0x07111f,
    bg_opa=lvgl.OPA(100), border_width=0, pad_all=0})
root:clear_flag(lvgl.FLAG.SCROLLABLE)
lvgl.Label(root, {text="Canopus / RW6", text_color=0xffffff,
    align={type=lvgl.ALIGN.TOP_MID, x_ofs=0, y_ofs=38}})
local status = lvgl.Label(root, {text="Starting Canopus...", text_color=0xbfd9ff,
    w=width-40, h=240, align={type=lvgl.ALIGN.TOP_MID, x_ofs=0, y_ofs=90}})
local function bootstrap()
    runner.checkpoint("Checking firmware 3.100.043")
    check(not blocked, "Restart the watch before retrying a failed native load")
    local properties = read("/etc/build.prop")
    check(type(properties) == "string", "Cannot read firmware identity")
    local version, model
    for line in (properties .. "\n"):gmatch("([^\n]*)\n") do
        local k,v = line:gsub("\r$", ""):match("^([^=]+)=(.*)$")
        if k == "ro.build.version" then check(not version, "Duplicate identity"); version=v end
        if k == "ro.product.device" then model=v end
    end
    check(version == "3.100.043", "Unsupported firmware: " .. tostring(version))
    -- Native entry verifies the exact AP build and driver, beyond build.prop.
    local present = read(DEVICE, 384)
    if present then
        check(status_record(true), "An incompatible Canopus is running; restart first")
    else
        runner.checkpoint("Preparing native framework")
        execute("mkdir /data/canopus")
        execute("mkdir /data/canopus/inbox")
        local module = read(SCRIPT_PATH .. "supervisor.bin")
        check(module and #module == EXPECTED_SIZE and crc32(module) == EXPECTED_CRC
            and module:sub(1,7) == "\127ELF\1\1\1", "Invalid framework resource")
        write(MODULE_PATH, module)
        local copied = read(MODULE_PATH)
        check(copied and #copied == EXPECTED_SIZE and crc32(copied) == EXPECTED_CRC,
            "Framework readback failed")
        module, copied = nil, nil
        local icon = read(SCRIPT_PATH .. "manager_icon.bin")
        check(icon and #icon > 12, "Missing manager icon")
        write("/data/canopus/manager_icon.bin", icon)
        check(read("/data/canopus/manager_icon.bin") == icon, "Icon readback failed")
        runner.checkpoint("Loading resident framework")
        -- No nested module loads or native app registration inside insmod.
        blocked = true
        check(execute("insmod " .. MODULE_PATH .. " " .. MODULE_NAME), "Native framework load failed")
        check(status_record(true), "Framework endpoint unavailable")
        blocked = false
    end
    runner.checkpoint("Registering Canopus Manager")
    command(0x43510002, 0)
    runner.checkpoint("Restoring enabled modules")
    local data = command(0x4351000a, 0)
    check(u32(data,12) == 0, "Canopus is in safe mode; inspect Manager")
    return "Canopus ready.\nEnabled modules restored.\nCanopus is in the app list.\nEnable corona and activate it there.\nThen open Corona Manager.\n\nYou may now switch watchfaces.\nOpen this entry after each reboot."
end
runner = progress.new(lvgl, function(message) status:set{text=message} end,
    function(ok, message) status:set{text=ok and message or ("FAILED\n"..tostring(message)),
        text_color=ok and 0x8ff0a4 or 0xff9a9a} end)
local function start() local ok,err=runner.start(bootstrap); if not ok then status:set{text=tostring(err)} end end
local button = lvgl.Object(root, {w=width-64, h=52, radius=12, bg_color=0x153454,
    bg_opa=lvgl.OPA(100), border_width=0, align={type=lvgl.ALIGN.TOP_MID,x_ofs=0,y_ofs=394}})
lvgl.Label(button, {text="START / RESTORE", text_color=0xffffff, align=lvgl.ALIGN.CENTER})
button:onClicked(start)
if lvgl.EVENT.DELETE then root:onevent(lvgl.EVENT.DELETE, function() runner.cancel() end) end
start()
