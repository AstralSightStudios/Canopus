-- Embedded by build_band11_installer.py. Firmware addresses come from the
-- exact target's loader/lua-recovery.toml; no numeric-to-function conversion.
local M = {}
local STATE_KEY = "canopus.band11.execute_recovery.v1"

local function address(fn)
    if type(fn) ~= "function" then return nil end
    local text = tostring(fn)
    -- NuttX libc variants differ on the %p prefix. Accept both hexadecimal
    -- renderings, but still require the exact function label and address.
    local hex = text:match("^function:%s*0[xX](%x+)$")
        or text:match("^function:%s*(%x+)$")
    local value = hex and tonumber(hex, 16)
    return value and value - value % 2
end

local function read_identity(path)
    if type(io) ~= "table" or type(io.open) ~= "function" then return nil end
    local ok, file = pcall(io.open, path, "rb")
    if not ok or not file then return nil end
    local read_ok, content = pcall(file.read, file, 4097)
    local close_ok, closed = pcall(file.close, file)
    if not read_ok or not close_ok or not closed or
       type(content) ~= "string" or #content > 4096 then return nil end
    local properties = {}
    for line in (content .. "\n"):gmatch("([^\n]*)\n") do
        local key, value = line:gsub("\r$", ""):match("^([^=]+)=(.*)$")
        if key then
            if properties[key] ~= nil then return nil end
            properties[key] = value
        end
    end
    return properties
end

function M.recover(profile)
    local properties = read_identity(profile.identity_path)
    if not properties or properties["ro.build.version"] ~= profile.firmware_version
       or properties["ro.build.id"] ~= profile.firmware_build then
        return nil, "Firmware identity mismatch or unreadable"
    end
    if _VERSION ~= profile.lua_version then return nil, "Lua version mismatch" end
    local D = debug
    if type(D) ~= "table" then return nil, "Lua debug interface unavailable" end
    for _, key in ipairs({"getinfo", "getlocal", "getregistry", "getmetatable",
                           "setmetatable"}) do
        if type(D[key]) ~= "function" then return nil, "Lua debug interface incomplete" end
    end
    local old_os = os
    if type(old_os) ~= "table" then return nil, "OS library unavailable" end
    if type(old_os.execute) == "function" then
        if address(old_os.execute) ~= profile.os_execute then
            return nil, "Unexpected execute implementation"
        end
        return old_os.execute, "Native execute available"
    end

    local registry = D.getregistry()
    if rawget(registry, STATE_KEY) then
        return nil, "Recovery already attempted; reload requires a new Lua state"
    end
    local init, path, loop, root
    -- Read at the same depth as getinfo. On first initialization the firmware's
    -- loop-context getter pops the original third argument (0x0c6c8a34).
    -- pmain then puts its error handler in slot 3 before calling the script.
    for level = 2, 32 do
        local info = D.getinfo(level, "fS")
        if not info then break end
        if info.what == "C" and address(info.func) == profile.pmain then
            init = info.func
            local _
            _, path = D.getlocal(level, 1)
            _, loop = D.getlocal(level, 2)
            _, root = D.getlocal(level, 3)
            break
        end
    end
    if not init then return nil, "Initial pmain frame unavailable" end
    local cached_root = false
    if type(path) == "string" and type(loop) == "userdata" and
       type(root) == "function" and address(root) == profile.pmain_error_handler then
        root = rawget(registry, "luavgl_key")
        -- This exact firmware creates a 16-byte full userdata with one nil
        -- uservalue and no metatable. Its first three words are the original
        -- root object and callbacks. Do not substitute arbitrary userdata.
        if type(root) ~= "userdata" or type(D.getuservalue) ~= "function" or
           D.getmetatable(root) ~= nil then return nil, "Root context unavailable" end
        local valid, value, present = pcall(D.getuservalue, root, 1)
        if not valid then return nil, "Unexpected root context" end
        local _, extra = D.getuservalue(root, 2)
        if not present or value ~= nil or extra then
            return nil, "Unexpected root context"
        end
        cached_root = true
    end
    if type(path) ~= "string" or type(loop) ~= "userdata" or
       type(root) ~= "userdata" then
        return nil, "Unexpected pmain arguments: " .. type(path) .. "/" ..
            type(loop) .. "/" .. type(root)
    end
    local loaded = rawget(registry, "_LOADED")
    if type(loaded) ~= "table" or rawget(loaded, "os") ~= old_os or
       D.getmetatable(loaded) ~= nil then return nil, "Unexpected module registry" end
    if cached_root and not rawget(loaded, "lvgl") then
        return nil, "LVGL initialization unavailable"
    end

    local stop, saved = {}, nil
    rawset(registry, STATE_KEY, {status = "attempted"})
    D.setmetatable(loaded, {
        __newindex = function(t, key, value)
            if key == "os" then
                if type(value) == "table" then saved = rawget(value, "execute") end
                -- Stop before package-path edits, repeated io wrappers and
                -- recursive script execution. One native strdup is leaked;
                -- see the exact-target audit. Never retry this reentry.
                error(stop, 0)
            end
            rawset(t, key, value)
        end,
    })
    rawset(loaded, "os", nil)
    -- luavgl_key's fourth word is a destructor, whereas the original root
    -- argument's fourth word is dataman. pmain only stores this word in the
    -- registry userdata before our sentinel. Give it a temporary dataman
    -- entry so the live dataman payload is never overwritten; restore on both
    -- success and failure. See loader/pmain-startup-fix-2026-09-10.md.
    local old_dataman = rawget(registry, "dataman.meta")
    if cached_root then rawset(registry, "dataman.meta", nil) end
    local ok, err = pcall(init, path, loop, root)
    if cached_root then rawset(registry, "dataman.meta", old_dataman) end
    D.setmetatable(loaded, nil)
    rawset(loaded, "os", old_os)
    rawset(_G, "os", old_os)
    if ok or err ~= stop then return nil, "OS initialization was not intercepted" end
    if address(saved) ~= profile.os_execute or
       D.getinfo(saved, "S").what ~= "C" then
        return nil, "Recovered execute address mismatch"
    end
    rawset(old_os, "execute", saved)
    rawset(registry, STATE_KEY, {status = "recovered"})
    return saved, "Native execute restored; device test pending"
end

return M
