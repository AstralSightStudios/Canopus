-- Exact q66cn .139 bootstrap. No firmware cave and no global MPU disable.
local M = {}
local retained_owner -- keep the closed upvalue reachable until NSH exec returns
local function pointer(value)
    local text = tostring(value)
    return tonumber(text:match("0[xX](%x+)$") or text:match(":%s*(%x+)$"), 16)
end
local function crc32(data)
    local crc = 0xffffffff
    for i = 1, #data do
        crc = crc ~ data:byte(i)
        for _ = 1, 8 do crc = (crc >> 1) ~ (0xedb88320 & -(crc & 1)) end
    end
    return (~crc) & 0xffffffff
end
local function retain(value) return function() return value end end
function M.load(profile, stage1_path, stage2_path, supervisor_path, run, read_all, write_all)
    local function check(ok, message) if not ok then error(message, 0) end end
    local function work()
        check(type(debug.upvalueid) == "function", "debug.upvalueid unavailable")
        local stage1, stage2, supervisor = read_all(stage1_path), read_all(stage2_path), read_all(supervisor_path)
        for _, item in ipairs({{stage1,"stage1"},{stage2,"stage2"},{supervisor,"supervisor"}}) do
            check(type(item[1]) == "string" and #item[1] == profile[item[2] .. "_size"]
                and crc32(item[1]) == profile[item[2] .. "_crc"], item[2] .. " resource mismatch")
        end
        run("mkdir /data/canopus")
        check(write_all("/data/canopus/stage2.bin", stage2)
            and read_all("/data/canopus/stage2.bin") == stage2, "stage2 write verification failed")
        check(write_all("/data/canopus/supervisor.elf", supervisor)
            and read_all("/data/canopus/supervisor.elf") == supervisor, "Supervisor write verification failed")
        local function read_word(address)
            check(type(address) == "number" and address % 4 == 0, "unaligned memory read")
            local path = "/data/canopus/bootstrap-word.txt"
            check(run(string.format("mw %08x 4 > %s", address, path)), "memory read failed")
            local text = read_all(path, "r") or ""
            local value = text:match("=%s*0[xX](%x+)") or text:match("%-%>%s*0[xX](%x+)")
            value = value and tonumber(value, 16)
            check(value ~= nil and value <= 0xffffffff, "invalid memory read response")
            return value
        end
        local function write_word(address, value)
            check(run(string.format("mw %08x=%08x", address, value)), "memory write failed")
        end
        check(read_word(0xe000ed94) == 5, "unexpected MPU control; reboot with stock firmware")
        check(read_word(0xe000edc0) == 0x00447722, "unexpected MPU memory attributes")
        check((read_word(0x200f5190) & 0x7f) == 0x0f, "MPU leases already occupied; reboot")
        for _, fingerprint in ipairs(profile.fingerprints) do
            check(read_word(fingerprint[1]) == fingerprint[2], "firmware instruction fingerprint mismatch")
        end
        local heap = read_word(0x200b2590)
        check(heap == 0x3c356b40, "unexpected Umem heap descriptor")
        local lo, hi = read_word(heap + 0x1c), read_word(heap + 0x20)
        check(lo >= 0x3c000000 and hi > lo and hi <= 0x3d000000, "unexpected PSRAM heap bounds")
        local function owned_range(p, n)
            return type(p) == "number" and p % 4 == 0 and p > lo and p + n <= hi
        end
        -- A fresh, non-interned long string. The only patched bytes are the
        -- result word and stage1's mailbox parameter, both in its own payload.
        retained_owner = retain(string.pack("<I4", 0x7ffffffe) .. string.rep("\0", 28) .. stage1 .. string.rep("\0", 32))
        local upvalue = pointer(debug.upvalueid(retained_owner, 1))
        check(owned_range(upvalue, 32), "upvalue is outside Umem")
        local value = read_word(upvalue + 8)
        check(value == upvalue + 16 and (read_word(value + 8) & 0xff) == 0x54, "unexpected closed long-string upvalue layout")
        local object = read_word(value)
        local length = #retained_owner()
        check(owned_range(object, length + 17), "string is outside Umem")
        check((read_word(object + 4) & 0xff) == 0x14 and read_word(object + 12) == length,
            "unexpected TString layout")
        local mailbox, code = object + 16, object + 48
        check(read_word(mailbox) == 0x7ffffffe and read_word(code) == string.unpack("<I4", stage1), "owned payload mismatch")
        write_word(code + 4, mailbox)
        check(read_word(code + 4) == mailbox, "mailbox parameter write failed")
        -- Startup 0x0c0c024c proves 3c... data <-> 1c... instruction alias.
        -- MPU CTRL=5 preserves the privileged default Code map at 1c... .
        -- No RNR/RBAR/RLAR writes occur from Lua. Native leases are atomic.
        local executable = code - 0x20000000
        check((read_word(0x07ffa000) & 1) == 1 and (read_word(0x07ffc000) & 1) == 1, "unexpected BES cache state")
        -- Fixed leaf entries in this exact firmware establish their own
        -- arguments: D clean-all; then the already-enabled I cache's
        -- clean/invalidate-all branch. Avoid mw on cache command ports:
        -- cmd_mw reads those ports before and after every write.
        check(run("exec 0x0c93021b"), "data-cache clean failed")
        check(run("exec 0x0c91e543"), "cache clean barrier failed")
        check(run("exec 0x0c0c181f"), "instruction-cache invalidation failed")
        check(run("exec 0x0c91e543"), "instruction-cache barrier failed")
        -- Run command status is not the native return code; read the owned
        -- mailbox after exec, even if NSH returns a command error.
        local command_ok = run(string.format("exec 0x%08x > /data/canopus/bootstrap-exec.txt", executable | 1))
        local result = read_word(mailbox)
        if result >= 0x80000000 then result = result - 0x100000000 end
        write_all("/data/canopus/bootstrap-result.txt", string.format("target=%s\nresult=%d\nexec_ok=%s\n", profile.target_id, result, tostring(command_ok)))
        check(result == 0, "native loader rc=" .. result)
        check(command_ok, "NSH exec did not complete successfully")
        return true
    end
    local ok, result = pcall(work)
    retained_owner = nil -- exec is synchronous; no native callback retains it
    return ok and result == true, ok and nil or tostring(result)
end
M.crc32 = crc32
return M
