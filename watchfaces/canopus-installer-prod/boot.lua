local PENDING = "/data/canopus/boot.pending"

local function run(command)
    local ok = os.execute(command)
    return ok == true or ok == 0
end
local function read_all(path)
    local file = io.open(path, "rb")
    if not file then return nil end
    local data = file:read("a")
    file:close()
    return data
end
local function write_all(path, data)
    local file = io.open(path, "wb")
    if not file then return false end
    local ok = file:write(data)
    local closed = file:close()
    return ok ~= nil and closed ~= nil
end

-- The Supervisor removes this counter once the Manager is registered. A start
-- that dies before that is retried twice, then left alone.
local attempts = tonumber(read_all(PENDING)) or 0
if attempts >= 3 then return end
assert(write_all(PENDING, tostring(attempts + 1)))

local present = io.open("/dev/canopus", "rb")
if present then
    present:close()
else
    -- @CANOPUS_BOOT_LOAD@
end

local CMD_START_BOOT = 25
local request = string.pack("<I4I2I2I2I2I4I4I4I4I4I4",
    0x43504332, 36, 1, 1, 0, 36, CMD_START_BOOT, 1, 0, 0, 0)
local control = assert(io.open("/dev/canopus", "wb"))
assert(control:write(request))
control:close()
control = assert(io.open("/dev/canopus", "rb"))
local response = control:read(36)
control:close()
assert(response and #response == 36 and string.unpack("<I4", response, 29) == 5,
    "Canopus boot scheduling failed")
