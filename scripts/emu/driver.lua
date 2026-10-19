-- Emulator/device-side driver for end-to-end tests. Runs under NSH `lua`:
--   lua /data/canopus/driver.lua status
--   lua /data/canopus/driver.lua install <token>
--   lua /data/canopus/driver.lua cmd <opname> [slot]
--   lua /data/canopus/driver.lua cat <path>
--   lua /data/canopus/driver.lua diag   (the Manager target's click record)
-- Speaks CPC2 (the only device protocol) to /dev/canopus and /canopus/install.
-- Output is line oriented ("@@ key=value ..."); tests parse it on the host.
local OPS = {
    install = 2, enable = 3, disable = 4, remove = 5, rollback = 7, query_module = 8,
    query_device = 9, activate = 11, scan = 12, native_stage = 13, restore = 14,
}
local request_id = 0

local function u32(s, o) return (string.unpack("<I4", s, o + 1)) end
local function s32(v) if v >= 0x80000000 then return v - 0x100000000 end return v end

-- One CPC2 request/reply pair; returns the whole reply record.
local function exchange(path, op, payload, reply_size)
    payload = payload or ""
    request_id = request_id + 1
    local request = string.pack("<I4I2I2I2I2I4I4I4I4I4I4", 0x43504332, 36, 1, 1, 0,
        36 + #payload, op, request_id, 0, 0, #payload) .. payload
    local f = assert(io.open(path, "wb"))
    f:write(request)
    f:close()
    f = assert(io.open(path, "rb"))
    local reply = f:read(reply_size)
    f:close()
    assert(reply and #reply >= 36 and #reply <= reply_size and u32(reply, 0) == 0x43504332, "bad CPC2 reply")
    return reply
end

local function query_device()
    return exchange("/dev/canopus", OPS.query_device, nil, 120)
end

local function query_module(slot)
    local r = exchange("/dev/canopus", OPS.query_module, string.pack("<I4", slot), 100)
    if u32(r, 28) ~= 5 or #r ~= 100 then return nil end
    return r
end

local function show(result)
    local d = query_device()
    local out = {string.format("err=%d count=%d pending_state=%d", s32(u32(d, 36 + 24)),
        u32(d, 36 + 16), result or 0)}
    for slot = 0, 15 do
        local m = query_module(slot)
        if m then
            out[#out + 1] = string.format("slot%d=state:%d,class:%d,version:%d,flags:%d,act_err:%d",
                slot, u32(m, 36 + 12), u32(m, 36 + 16), u32(m, 36 + 20), u32(m, 36 + 24), s32(u32(m, 36 + 60)))
        end
    end
    out[#out + 1] = string.format("scan=%08x", u32(d, 36 + 32))
    print("@@ " .. table.concat(out, " "))
end

local function install(token)
    local r = exchange("/canopus/install", OPS.install, token .. "\0", 40)
    print(string.format("@@ install result=%d diag=%d", u32(r, 28), s32(u32(r, 36))))
end

local function slot_id(slot)
    local m = assert(query_module(slot), "no module in slot " .. slot)
    return string.sub(m, 36 + 28 + 1, 36 + 28 + 32)
end

local op, a, b = ...
if op == "status" then show(0)
elseif op == "install" then install(a)
elseif op == "cmd" then
    local code = assert(OPS[a], "unknown op " .. tostring(a))
    local payload
    if a == "enable" or a == "disable" or a == "remove" or a == "rollback" or a == "activate" then
        payload = slot_id(tonumber(b or 0))
    elseif a == "native_stage" then
        payload = string.pack("<I4", tonumber(b or 0))
    end
    local reply = exchange("/dev/canopus", code, payload, 36)
    show(u32(reply, 28))
elseif op == "diag" then
    local d = query_device()
    local names = {"build", "clicks", "row", "generation", "key", "event", "sel_before", "sel_after", "detail"}
    local out = {}
    for i, name in ipairs(names) do out[#out + 1] = string.format("%s=%d", name, u32(d, 36 + 48 + (i - 1) * 4)) end
    print("@@ diag " .. table.concat(out, " "))
elseif op == "cat" then
    local f = io.open(a, "rb")
    print("@@ " .. (f and ("content=" .. (f:read("a"):gsub("%s+$", ""))) or "content=<missing>"))
    if f then f:close() end
else error("unknown op " .. tostring(op)) end
