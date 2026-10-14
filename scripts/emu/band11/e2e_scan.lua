local control_open
if type(debug) == "table" and type(debug.getupvalue) == "function" then
    local _, original = debug.getupvalue(io.open, 1)
    if type(original) == "function" then control_open = original end
end
local lvgl = require("lvgl")
local root = lvgl.Object(nil, { w = lvgl.HOR_RES(), h = lvgl.VER_RES(), bg_color = 0x07111F, bg_opa = lvgl.OPA(100), border_width = 0 })
local label = lvgl.Label(root, { text = "E2E running", text_color = 0xFFFFFF, align = { type = lvgl.ALIGN.CENTER, x_ofs = 0, y_ofs = 0 } })

local LOG = "/data/canopus/e2e.log"
local lines = {}
local function log(s) lines[#lines + 1] = s end
local function u32(s, o) return (string.unpack("<I4", s, o + 1)) end
local function s32(v) if v >= 0x80000000 then return v - 0x100000000 end return v end
local request_id = 0
local function exchange(op, payload, reply_size)
    payload = payload or ""
    request_id = request_id + 1
    local request = string.pack("<I4I2I2I2I2I4I4I4I4I4I4", 0x43504332, 36, 1, 1, 0,
        36 + #payload, op, request_id, 0, 0, #payload) .. payload
    local f = (control_open or io.open)("/dev/canopus", "wb")
    if not f then return nil, "open w failed" end
    f:write(request); f:close()
    f = (control_open or io.open)("/dev/canopus", "rb")
    if not f then return nil, "open r failed" end
    local reply = f:read(reply_size); f:close()
    if not reply or #reply < 36 or u32(reply, 0) ~= 0x43504332 then return nil, "bad reply" end
    return reply
end
local function status(tag)
    local d, e = exchange(9, nil, 72)
    if not d then log(tag .. " ERR " .. tostring(e)); return end
    local out = { string.format("err=%d count=%d scan=%08x", s32(u32(d, 36 + 24)), u32(d, 36 + 16), u32(d, 36 + 32)) }
    for slot = 0, 15 do
        local m = exchange(8, string.pack("<I4", slot), 100)
        if m and u32(m, 28) == 5 and #m == 100 then
            out[#out + 1] = string.format("slot%d=state:%d,class:%d,version:%d,flags:%d", slot,
                u32(m, 36 + 12), u32(m, 36 + 16), u32(m, 36 + 20), u32(m, 36 + 24))
        end
    end
    log(tag .. " " .. table.concat(out, " "))
end

log("e2e start")
log("control_open=" .. tostring(control_open ~= nil))
status("before")
local r, e = exchange(12, nil, 36)
log("scan result=" .. (r and tostring(u32(r, 28)) or ("ERR " .. tostring(e))))
status("after-scan")
local f = io.open(LOG, "wb")
if f then f:write(table.concat(lines, "\n") .. "\n"); f:close() end
pcall(function() label:set_text("E2E done") end)
