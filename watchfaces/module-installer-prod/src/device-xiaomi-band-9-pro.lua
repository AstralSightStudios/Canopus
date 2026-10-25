-- Band 9 (3.1.32 / 3.1.190) runs the framework installer's shell, so the
-- device-proven /dev/canopus flow applies: the Supervisor registers the node
-- and answers INSTALL with the 36-byte CPC2 header. build.prop carries only
-- ro.build.version (no build id); the receipt's target_id + firmware_sha256
-- are what bind the artifact to the firmware.
local ENDPOINT, REPLY_SIZE = "/dev/canopus", 36
local SUPERVISOR_MISSING = "请先运行 Canopus 框架安装表盘"
local BUILD_PROPERTY = "ro.build.id"
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
        string.pack("<I4I2I2I2I2I4I4I4I4I4I4", 0x43504332, 36, 1, 2, 0, 36, 9, 0x7FFF, 0, 0, 0), 120)
    if not ok or type(record) ~= "string" or #record ~= 120 or u32(record, 0) ~= 0x43504332 then
        return ""
    end
    return " error " .. string.unpack("<i4", record, 36 + 24 + 1)
end
