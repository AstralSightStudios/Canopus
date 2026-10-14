-- Emulator/device-side driver for end-to-end tests. Runs under NSH `lua`:
--   lua /data/canopus/driver.lua status
--   lua /data/canopus/driver.lua install <token>
--   lua /data/canopus/driver.lua cmd <opname> [arg0]
--   lua /data/canopus/driver.lua cat <path>
-- Output is line oriented ("key=value ..."); tests parse it on the host.
local CMD_MAGIC, OPS = 0x43504331, {
    query = 0x43510001, enable = 0x43510003, disable = 0x43510004,
    remove = 0x43510005, rollback = 0x43510007, restore = 0x4351000A,
    scan = 0x4351000B,
}

local function u32(s, o) return (string.unpack("<I4", s, o + 1)) end
local function s32(v) if v >= 0x80000000 then return v - 0x100000000 end return v end

local function roundtrip(path, request, size)
    local f = assert(io.open(path, "wb"))
    f:write(request)
    f:close()
    f = assert(io.open(path, "rb"))
    local response = f:read(size)
    f:close()
    return response
end

local function show(st)
    local out = {string.format("err=%d count=%d pending_op=%x pending_state=%d",
        s32(u32(st, 32)), u32(st, 16), u32(st, 20), u32(st, 24))}
    for i = 0, 15 do
        local o = 128 + i * 16
        if u32(st, o) ~= 0 then
            out[#out + 1] = string.format("slot%d=state:%d,class:%d,version:%d,flags:%d",
                i, u32(st, o), u32(st, o + 4), u32(st, o + 8), u32(st, o + 12))
        end
    end
    out[#out + 1] = string.format("scan=%08x", u32(st, 88))
    print("@@ " .. table.concat(out, " "))
end

local function status()
    show(roundtrip("/dev/canopus", string.pack("<I4I4I4I4", CMD_MAGIC, OPS.query, 0, 0), 384))
end

local function install(token)
    local payload = token .. "\0"
    local request = string.pack("<I4I2I2I2I2I4I4I4I4I4I4",
        0x43504332, 36, 1, 1, 0, 36 + #payload, 2, 1, 0, 0, #payload) .. payload
    local r = roundtrip("/canopus/install", request, 40)
    print(string.format("@@ install result=%d diag=%d", u32(r, 28), s32(u32(r, 36))))
end

local op, a, b = ...
if op == "status" then status()
elseif op == "install" then install(a)
elseif op == "cmd" then
    -- the command's own response: a following QUERY would clear the error code
    show(roundtrip("/dev/canopus", string.pack("<I4I4I4I4", CMD_MAGIC, assert(OPS[a]), tonumber(b or 0), 0), 384))
elseif op == "cat" then
    local f = io.open(a, "rb")
    print("@@ " .. (f and ("content=" .. (f:read("a"):gsub("%s+$", ""))) or "content=<missing>"))
    if f then f:close() end
else error("unknown op " .. tostring(op)) end
