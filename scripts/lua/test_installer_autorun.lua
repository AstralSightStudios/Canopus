-- Auto-run of the Band 10 Pro production installer: opt-in switch, the pending
-- marker that turns it off after an unfinished run, and the delayed start.

local INSTALLER_DIR = "watchfaces/canopus-installer-prod/xiaomi-band-10-pro/"
local FLAG, PENDING = "/data/canopus/autorun", "/data/canopus/autorun.pending"
local GETPROP = "getprop 'ro.build.version' > '/data/canopus-installer-firmware-version.tmp'"
local native_io, native_execute, native_remove = io, os.execute, os.remove

local function fake_run(files, options)
    local objects, timers, commands = {}, {}, {}
    local loaded = false
    local lvgl = {
        FLAG = { SCROLLABLE = 1, CLICKABLE = 2 },
        EVENT = { CLICKED = 7 },
        ALIGN = { CENTER = 1 },
        OPA = function(v) return v end,
        HOR_RES = function() return 336 end,
        VER_RES = function() return 480 end,
    }
    local methods = {}
    function methods:clear_flag() return self end
    function methods:add_flag() return self end
    function methods:onClicked(callback) self.clicked = callback; return self end
    function methods:set(props) for k, v in pairs(props) do self[k] = v end; return self end
    local function object(parent, props)
        local o = setmetatable({ parent = parent }, { __index = methods })
        for k, v in pairs(props or {}) do o[k] = v end
        objects[#objects + 1] = o
        return o
    end
    lvgl.Object, lvgl.Label = object, object
    function lvgl.Timer(props)
        local timer = setmetatable({ deleted = false }, { __index = {
            resume = function() end, ready = function() end,
            delete = function(self) self.deleted = true end,
        } })
        for k, v in pairs(props) do timer[k] = v end
        timers[#timers + 1] = timer
        return timer
    end
    package.loaded.lvgl = lvgl

    io = setmetatable({}, { __index = native_io })
    local request
    io.open = function(path, mode)
        if path == "/dev/canopus" then
            if not loaded then return nil end
            if mode == "wb" then
                return { write = function(_, data) request = data; return true end,
                         close = function() return true end }
            end
            return { read = function(_, size)
                local reply = request:sub(1, 36)
                reply = reply:sub(1, 28) .. string.pack("<I4", 5) .. reply:sub(33)
                return (reply .. string.rep("\0", size)):sub(1, size)
            end, close = function() return true end }
        end
        if mode == "wb" then
            local data = ""
            return { write = function(_, chunk) data = data .. chunk; return true end,
                     close = function() files[path] = data; return true end }
        end
        local content = files[path]
        if content == nil then return nil end
        local position = 1
        return {
            read = function(_, count)
                if count == "*a" then position = #content + 1; return content end
                local chunk = content:sub(position, position + count - 1)
                position = position + #chunk
                return chunk
            end,
            seek = function() return #content end,
            close = function() return true end,
        }
    end
    os.execute = function(command)
        commands[#commands + 1] = command
        if command == GETPROP then
            files["/data/canopus-installer-firmware-version.tmp"] = "3.101.043\n"
            return 0
        end
        if command:match("^insmod ") then
            files["at insmod: pending"] = files[PENDING]
            loaded = not options.insmod_fails
            return loaded and 0 or false
        end
        if command == "rm -rf /data/canopus" then
            files[FLAG], files[PENDING] = nil, nil
        end
        return 0
    end
    os.remove = function(path) files[path] = nil; return true end

    SCRIPT_PATH = INSTALLER_DIR
    assert(loadfile(INSTALLER_DIR .. "main.lua"))()
    local page = { objects = objects, timers = timers, commands = commands, files = files }
    function page.status()
        for _, o in ipairs(objects) do
            if type(o.align) == "table" and o.align.y_ofs == 170 then return o.text end
        end
    end
    function page.button(label)
        for _, o in ipairs(objects) do
            if o.text and o.text:match("^" .. label) and o.parent and o.parent.clicked then
                return o.parent
            end
        end
    end
    function page.label(label)
        for _, o in ipairs(objects) do
            if o.text and o.text:match("^" .. label) then return o.text end
        end
    end
    return page
end

local function with_files(extra)
    local files = {
        [INSTALLER_DIR .. "canopus_supervisor-xiaomi-band-10-pro-3.101.043.bin"] =
            "\127ELF\1\1\1" .. string.rep("\0", 9) .. "\1\0\40\0" .. string.rep("X", 492),
        [INSTALLER_DIR .. "manager_icon.bin"] = "\25\0\0\0\1\0\1\0\0\0\0\0" .. "\0\0\0\0",
    }
    for k, v in pairs(extra or {}) do files[k] = v end
    return files
end

local function fire(timer)
    timer.cb(timer)
end

-- Off by default: nothing is scheduled and no command touches /data/canopus.
do
    local page = fake_run(with_files(), {})
    assert(#page.timers == 0)
    assert(page.label("Auto%-run") == "Auto-run: OFF")
    assert(#page.commands == 1 and page.commands[1] == GETPROP)
end

-- The switch writes and removes the flag and shows its state.
do
    local page = fake_run(with_files(), {})
    page.button("Auto%-run").clicked()
    assert(page.files[FLAG] == "1" and page.label("Auto%-run") == "Auto-run: ON")
    page.button("Auto%-run").clicked()
    assert(page.files[FLAG] == nil and page.label("Auto%-run") == "Auto-run: OFF")
end

-- Enabled: a delayed start; the pending marker is on disk before insmod runs;
-- a finished Run leaves Auto-run on and clears the marker.
do
    local page = fake_run(with_files({ [FLAG] = "1" }), {})
    assert(#page.timers == 1 and page.timers[1].period == 4000)
    fire(page.timers[1])
    assert(page.timers[1].deleted)
    assert(page.files["at insmod: pending"] == "1", "marker must precede insmod")
    assert(page.files[PENDING] == "1")
    local run_timer = page.timers[2]
    local guard = 0
    while not run_timer.deleted do
        fire(run_timer)
        guard = guard + 1
        assert(guard < 20, "Run did not finish")
    end
    assert(page.status() == "Run completed", page.status())
    assert(page.files[PENDING] == nil and page.files[FLAG] == "1")
end

-- A Run that fails cleanly switches Auto-run off.
do
    local page = fake_run(with_files({ [FLAG] = "1" }), { insmod_fails = true })
    fire(page.timers[1])
    assert(page.status():match("^LOAD failed"), page.status())
    assert(page.files[PENDING] == nil and page.files[FLAG] == nil)
    assert(page.label("Auto%-run") == "Auto-run: OFF")
end

-- A pending marker at start is an automatic Run that never finished.
do
    local page = fake_run(with_files({ [FLAG] = "1", [PENDING] = "1" }), {})
    assert(#page.timers == 0)
    assert(page.files[FLAG] == nil and page.files[PENDING] == nil)
    assert(page.status():match("did not finish"), page.status())
    assert(page.label("Auto%-run") == "Auto-run: OFF")
end

io, os.execute, os.remove = native_io, native_execute, native_remove
print("installer autorun: PASS")
