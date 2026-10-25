-- Band 9 (3.1.32 / 3.1.190) runs the framework installer's shell, so the
-- device-proven /dev/canopus flow applies: the Supervisor registers the node
-- and answers INSTALL with the 36-byte CPC2 header. build.prop carries only
-- ro.build.version (no build id); the receipt's target_id + firmware_sha256
-- are what bind the artifact to the firmware.
local ENDPOINT, REPLY_SIZE = "/dev/canopus", 36
local SUPERVISOR_MISSING = "请先运行 Canopus 框架安装表盘"
-- The Supervisor refuses a read shorter than its whole reply, and the CQD1
-- device record is 88 bytes (it grew from 84), so ask for the full length.
local QUERY_DEVICE_SIZE = 36 + 88
-- build.prop carries only ro.build.version, so there is no build id
-- to compare; the staged receipt binds target_id + firmware_sha256 instead.
local BUILD_PROPERTY = nil
local execute = os.execute
local function fallback_identity() return nil end
local function prepare_inbox(checkpoint)
    if type(execute) ~= "function" then return end
    checkpoint("准备模块安装目录")
    execute("mkdir /data/canopus")
    execute("mkdir /data/canopus/inbox")
end
local function failure_detail(_, exchange)
    local ok, record = pcall(exchange,
        string.pack("<I4I2I2I2I2I4I4I4I4I4I4", 0x43504332, 36, 1, 2, 0, 36, 9, 0x7FFF, 0, 0, 0),
        QUERY_DEVICE_SIZE)
    if not ok or type(record) ~= "string" or #record ~= QUERY_DEVICE_SIZE
        or u32(record, 0) ~= 0x43504332 then
        return ""
    end
    return " error " .. string.unpack("<i4", record, 36 + 24 + 1)
end
-- 3.1.190's build.prop has no ro.build.version, so the version comes from the
-- Supervisor's own identity (CQD1 target_id "xiaomi-band-9-pro-3.1.190").
local function device_firmware_version(_, exchange)
    local ok, record = pcall(exchange,
        string.pack("<I4I2I2I2I2I4I4I4I4I4I4", 0x43504332, 36, 1, 2, 0, 36, 9, 0x7FFE, 0, 0, 0),
        QUERY_DEVICE_SIZE)
    if not ok or type(record) ~= "string" or #record ~= QUERY_DEVICE_SIZE
        or u32(record, 0) ~= 0x43504332 or u32(record, 28) ~= 5 then
        return nil
    end
    local id = record:sub(36 + 36 + 1, 36 + 36 + 47):match("^[^%z]*") or ""
    return id:match("%-(%d+%.%d+%.%d+)$")
end
