-- watchface_smoke.lua — load every watchface under a stubbed lvgl binding and
-- fire every button handler, catching nil-global / scope bugs (e.g. a
-- function referencing a `local status` declared later, which resolves to a
-- nil global).
--
-- Usage: lua scripts/lua/watchface_smoke.lua [watchface-dir...]
-- Returns non-zero on any load/click failure.

local lvgl = {}
local horizontal_resolution = 336
local vertical_resolution = 480
lvgl.HOR_RES = function() return horizontal_resolution end
lvgl.VER_RES = function() return vertical_resolution end
lvgl.OPA = function(v) return v end
lvgl.Font = function(name, size) return { name = name, size = size } end
lvgl.FLAG = { SCROLLABLE = 1, CLICKABLE = 2, EVENT_BUBBLE = 4 }
lvgl.EVENT = { CLICKED = 7 }
lvgl.ALIGN = { CENTER = 1, TOP_MID = 2, BOTTOM_MID = 3 }

local created = {}
local subscriptions = {}
local obj_mt = {}
function obj_mt:clear_flag() return self end
function obj_mt:add_flag() return self end
function obj_mt:set(props) self._last_set = props; return self end
function obj_mt:onClicked(fn) self._click = fn; table.insert(created, self); return self end
function obj_mt:onevent(event, fn)
    assert(event == lvgl.EVENT.CLICKED)
    self._click = fn
    table.insert(created, self)
    return self
end
function lvgl.Object(parent, props)
    local o = setmetatable({ _parent = parent, _props = props }, { __index = obj_mt })
    table.insert(created, o)
    return o
end
function lvgl.Label(parent, props)
    local o = setmetatable({ _parent = parent, _props = props }, { __index = obj_mt })
    table.insert(created, o)
    return o
end

_G.SCRIPT_PATH = "/fake/"
package.loaded["lvgl"] = lvgl
package.loaded["dataman"] = {
    subscribe = function(name, owner, callback)
        subscriptions[#subscriptions + 1] = {
            name = name,
            owner = owner,
            callback = callback,
        }
    end,
}

local original_io_open = io.open
local original_os_execute = os.execute

local function module_installer_io(fault)
    local files = {
        ["/fake/receipt.bin"] = "CMI1" .. string.rep("\0", 252),
        ["/fake/module.bin"] = "\127ELF" .. string.rep("\0", 508),
    }
    local device_request
    local function open(path, mode)
        if path == "/dev/canopus" then
            if mode == "wb" then
                return {
                    write = function(_, data)
                        device_request = data
                        if fault == "short_write" then return #data - 1 end
                        return #data
                    end,
                    close = function() return true end,
                }
            end
            local function le32(value)
                return string.char(value % 256, math.floor(value / 256) % 256,
                    math.floor(value / 65536) % 256,
                    math.floor(value / 16777216) % 256)
            end
            local request_id = fault == "stale_response" and 9 or 1
            local response = "2CPC" .. string.char(36, 0, 2, 0, 1, 0, 0, 0)
                .. le32(36) .. le32(2) .. le32(request_id) .. le32(0)
                .. le32(5) .. le32(0)
            return {
                read = function() return response end,
                close = function() return true end,
            }
        end
        if mode == "rb" then
            if files[path] == nil then return nil end
            return {
                read = function() return files[path] end,
                close = function() return true end,
            }
        end
        if mode == "wb" then
            return {
                write = function(_, data) files[path] = data; return true end,
                close = function() return true end,
            }
        end
        return nil
    end
    return open, function()
        return device_request, files
    end
end

local function band9_installer_io(fault, firmware_version, require_pre_entry_sync)
    firmware_version = firmware_version or "3.1.175"
    local target_id = "xiaomi-band-9-pro-3.1.175"
    local firmware_sha256 =
        "4f43b325addd6d9e6e7c7e2a4d00ffe3f23d5fb1560d8fe503544002ac1f516b"
    local cave_base = 0x20084e10
    local memalign = 0x0c0f21ed
    local free = 0x0c0f1b01
    local mpu_alloc = 0x0c51d8d1
    local mpu_configure = 0x0c51d759
    local mpu_release = 0x0c51d929
    local kmem_malloc = 0x0c51d8e1
    local kmem_free = 0x0c51d8f1
    if firmware_version == "3.1.32" then
        target_id = "xiaomi-band-9-3.1.32"
        firmware_sha256 =
            "9c02dab4020b2cc9666ee7d34cf27d311b76aadcec519a38361bbcbd94c53264"
        cave_base = 0x200cb400
        memalign = 0x0c16ab8d
        free = 0x0c16a425
        mpu_alloc = 0x0c5228a5
        mpu_configure = 0x0c52272d
        mpu_release = 0x0c5228fd
        kmem_malloc = 0x0c16af05
        kmem_free = 0x0c16a4b5
    end
    local cave_result = cave_base + 64
    local stage0_writable_rbar = cave_base + 2
    local stage0_executable_rbar = cave_base + 6
    local stage0_rlar = cave_base + 64 - 32 + 3
    local mpu_rnr = 0xe000ed98
    local mpu_rbar = 0xe000ed9c
    local mpu_rlar = 0xe000eda0
    local mpu_sync = 0x0c5226db
    local resource_prefix = "/fake/"
    local resource_suffix = "-" .. target_id
    local cave = {
        [cave_base] = 0, [cave_base + 4] = 0,
        [cave_base + 8] = 0, [cave_base + 12] = 0,
        [cave_base + 16] = 0x726f6e5f, [cave_base + 20] = 0x73616c66,
        [cave_base + 24] = 0x70615f68, [cave_base + 28] = 0x72665f69,
        [cave_result] = 0,
    }
    local profile = string.format([[
return {
    target_id = %q,
    firmware_sha256 = %q,
    status = "STATIC_RECOVERED",
    device_status = "DEVICE_PROVEN",
    loader_family = "nsh-mw-stage1-stage2",
    memalign = 0x%08x,
    free = 0x%08x,
    kmem_malloc = 0x%08x,
    kmem_free = 0x%08x,
    mpu_alloc = 0x%08x,
    mpu_configure = 0x%08x,
    mpu_release = 0x%08x,
    mpu_sync = 0x0c5226db,
    mpu_rnr = 0xe000ed98,
    mpu_rbar = 0xe000ed9c,
    mpu_rlar = 0xe000eda0,
    mpu_region_count = 8,
    exec_access_attr = 1,
    exec_mem_attr = 1,
    rw_access_attr = 0,
    stage0_region = 7,
    stage0_size = 4096,
    stage0_exec_size = 64,
    cave = 0x%08x,
    cave_result = 0x%08x,
    cave_original = {
        0, 0, 0, 0, 0x726f6e5f, 0x73616c66, 0x70615f68, 0x72665f69,
    },
}
]], target_id, firmware_sha256, memalign, free, kmem_malloc, kmem_free,
        mpu_alloc, mpu_configure, mpu_release, cave_base, cave_result)
    local files = {
        ["/fake/manager_icon.bin"] = string.char(
            0x19, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        [resource_prefix .. "canopus_loader_profile" .. resource_suffix .. ".bin"] = profile,
        [resource_prefix .. "canopus_stage1" .. resource_suffix .. ".bin"] =
            "return { size=8, words={0xbf00bf00,0xbf00bf00} }",
        [resource_prefix .. "canopus_stage2" .. resource_suffix .. ".bin"] =
            string.rep("S", 64),
        [resource_prefix .. "canopus_supervisor" .. resource_suffix .. ".bin"] =
            "\127ELF\1\1\1" .. string.rep("\0", 9) .. "\1\0\40\0" .. string.rep("X", 492),
    }
    local device_present = false
    local used_insmod = false
    local freed = false
    local released = false
    local configured_rbar
    local configured_rlar
    local selected_region
    local mpu_events = {}
    local region_rbar = {}
    local region_rlar = {}
    local mpu_synced = false
    local pre_entry_syncs = 0
    local compound_commands = 0
    local pending_op = 0
    local pending_state = 0
    local stage1_exec_address
    local next_command_id = 0
    if fault == "cave_mismatch" then cave[cave_base] = 1 end
    local function le32(value)
        return string.char(value % 256, math.floor(value / 256) % 256,
            math.floor(value / 65536) % 256,
            math.floor(value / 16777216) % 256)
    end
    local function status()
        local words = { 0x43505331, 1, 1, 0, 0, pending_op, pending_state, 0, 0, 2, 2, 0 }
        local out = ""
        for _, value in ipairs(words) do out = out .. le32(value) end
        return out .. string.rep("\0", 384 - #out)
    end
    local function open(path, mode)
        if path == "/dev/canopus" then
            if not device_present then return nil end
            if mode == "wb" then
                return { write = function(_, data)
                        if #data >= 8 then
                            pending_op = data:byte(5)
                                + data:byte(6) * 0x100
                                + data:byte(7) * 0x10000
                                + data:byte(8) * 0x1000000
                            pending_state = 5
                        end
                        return #data
                    end,
                    close = function() return true end }
            end
            return { read = function() return status() end,
                close = function() return true end }
        end
        if mode == "rb" or mode == "r" then
            if files[path] == nil then return nil end
            local position = 1
            return {
                read = function(_, count)
                    if count == "*a" or count == nil then
                        local result = files[path]:sub(position)
                        position = #files[path] + 1
                        return result
                    end
                    local result = files[path]:sub(position, position + count - 1)
                    position = position + #result
                    return result
                end,
                seek = function(_, where, offset)
                    offset = offset or 0
                    if where == "end" then position = #files[path] + 1 + offset
                    elseif where == "set" then position = 1 + offset end
                    return position - 1
                end,
                close = function() return true end }
        end
        if mode == "wb" then
            return { write = function(_, data) files[path] = data; return true end,
                close = function() return true end }
        end
        return nil
    end
    local function execute(command, command_id)
        if not command_id then
            next_command_id = next_command_id + 1
            command_id = next_command_id
        end
        if command:find(";", 1, true) or command:find("&&", 1, true) then
            compound_commands = compound_commands + 1
            local all_ok = true
            for sequence in command:gmatch("[^;]+") do
                local sequence_ok = true
                for part in sequence:gmatch("[^&]+") do
                    if sequence_ok then
                        local trimmed = part:match("^%s*(.-)%s*$")
                        sequence_ok = execute(trimmed, command_id)
                        if not sequence_ok and fault == "mpu_configuration_failure" then
                            break
                        end
                    end
                end
                if not sequence_ok then
                    all_ok = false
                    if fault == "mpu_configuration_failure" then break end
                end
            end
            return all_ok
        end
        local property_output = command:match(
            "^getprop 'ro%.build%.version' > '(.+)'$")
        if property_output then
            files[property_output] = firmware_version .. "\n"
            return true
        end
        if command:match("^insmod ") then used_insmod = true; return false end
        local address, value = command:match("^mw ([0-9a-fA-F]+)=([0-9a-fA-F]+)$")
        if address then
            local addr = tonumber(address, 16)
            local word = tonumber(value, 16)
            cave[addr] = word
            if addr == mpu_rbar then
                configured_rbar = word
                region_rbar[selected_region] = word
                mpu_events[#mpu_events + 1] = { selected_region, "rbar", word, command_id }
                mpu_synced = false
            end
            if addr == mpu_rnr then
                selected_region = word
            end
            if addr == mpu_rlar then
                region_rlar[selected_region] = word
                mpu_events[#mpu_events + 1] = { selected_region, "rlar", word, command_id }
                mpu_synced = false
            end
            if addr == mpu_rlar and word ~= 0 then
                configured_rlar = word
                if fault == "mpu_configuration_failure"
                    and selected_region ~= 7 then
                    return false
                end
            end
            return true
        end
        local read_address, output = command:match("^mw ([0-9a-fA-F]+) 4 > (.+)$")
        if read_address then
            local addr = tonumber(read_address, 16)
            files[output] = string.format("  0x%08x = 0x%08x\n", addr, cave[addr] or 0)
            return true
        end
        local exec_address, exec_output = command:match(
            "^exec (0[xX][0-9a-fA-F]+) > (.+)$")
        if not exec_address then
            exec_address, exec_output = command:match(
                "^exec ([0-9a-fA-F]+) > (.+)$")
        end
        if not exec_address then
            exec_address = command:match("^exec (0[xX][0-9a-fA-F]+)$")
        end
        if not exec_address then
            exec_address = command:match("^exec ([0-9a-fA-F]+)$")
        end
        if exec_address then
            exec_address = exec_address:gsub("^0[xX]", "")
            local addr = tonumber(exec_address, 16)
            if addr == mpu_sync then
                mpu_synced = true
                mpu_events[#mpu_events + 1] = {
                    selected_region, "sync", addr, command_id }
                return true
            end
            if exec_output then
                files[exec_output] = string.format("Calling 0x%08x\\n", addr)
            end
            if addr == cave_base + 1 then
                mpu_events[#mpu_events + 1] = {
                    selected_region, "exec", addr, command_id }
                if require_pre_entry_sync and (not mpu_synced
                    or region_rbar[7] ~= stage0_executable_rbar
                    or region_rlar[7] ~= stage0_rlar) then
                    return false
                end
                if mpu_synced then pre_entry_syncs = pre_entry_syncs + 1 end
                local is_barrier = cave[cave_base] == 0x8f4ff3bf
                    and cave[cave_base + 8] == 0x47702001
                if is_barrier then return true end
                local entry_probe = cave[cave_base] == 0x49034802
                    and cave[cave_base + 4] == 0x4a036008
                    and cave[cave_base + 8] == 0xbf004710
                if entry_probe then
                    local result_address = cave[cave_base + 16]
                    if result_address == nil then return false end
                    if fault == "mailbox_exec_failure" then return false end
                    cave[result_address] = 0x5a5a5a5a
                    return true
                end
                local extended_mailbox = cave[cave_base + 8] == 0x49064805
                    and cave[cave_base + 12] == 0x4B074A06
                    and cave[cave_base + 16] == 0x4B076013
                    and cave[cave_base + 20] == 0x4A044798
                    and cave[cave_base + 24] == 0x4B066010
                    and cave[cave_base + 28] == 0xBF004718
                local new_mailbox = cave[cave_base + 8] == 0x49044803
                    and cave[cave_base + 12] == 0x47984B04
                    and cave[cave_base + 16] == 0x60104A04
                    and cave[cave_base + 20] == 0xBF004770
                local old_mailbox = cave[cave_base] == 0x49044803
                    and cave[cave_base + 4] == 0x47984B04
                    and cave[cave_base + 8] == 0x60104A04
                    and cave[cave_base + 12] == 0xBF004770
                if not extended_mailbox and not new_mailbox and not old_mailbox then
                    return false
                end
                local literal_base
                local callable
                local result_address
                local argument0
                local argument1
                local entry_marker
                local continuation
                if extended_mailbox then
                    local function load_literal(offset)
                        local container = cave[cave_base + offset - (offset % 4)]
                        if container == nil then return nil end
                        local instruction
                        if offset % 4 == 0 then
                            instruction = container % 0x10000
                        else
                            instruction = math.floor(container / 0x10000) % 0x10000
                        end
                        if instruction < 0x4800 or instruction >= 0x5000 then
                            return nil
                        end
                        local pc = cave_base + offset + 4
                        pc = pc - (pc % 4)
                        return cave[pc + (instruction % 0x100) * 4]
                    end
                    argument0 = load_literal(8)
                    argument1 = load_literal(10)
                    result_address = load_literal(12)
                    entry_marker = load_literal(14)
                    callable = load_literal(18)
                    continuation = load_literal(26)
                    if argument0 == nil or argument1 == nil
                        or result_address == nil or entry_marker == nil
                        or callable == nil or continuation == nil
                        or argument0 ~= cave[cave_base + 32]
                        or argument1 ~= cave[cave_base + 36]
                        or result_address ~= cave[cave_base + 40]
                        or entry_marker ~= cave[cave_base + 44]
                        or callable ~= cave[cave_base + 48]
                        or continuation ~= cave[cave_base + 52] then
                        return false
                    end
                else
                    if new_mailbox then
                        literal_base = cave_base + 24
                    else
                        literal_base = cave_base + 16
                    end
                    callable = cave[literal_base + 8]
                    result_address = cave[literal_base + 12]
                end
                if callable == nil or result_address == nil then return false end
                if extended_mailbox then cave[result_address] = 0xA5A5A5A5 end
                if fault == "mailbox_exec_failure" then return false end
                local result = 0
                if callable == memalign or callable == kmem_malloc then
                    result = fault == "allocation_failure" and 0 or
                        (callable == kmem_malloc and 0x21000008 or 0x21000000)
                    if callable == kmem_malloc and result ~= 0 then
                        local aligned = result + 31
                            - ((result + 31) % 32)
                        stage1_exec_address = aligned + 1
                    end
                elseif callable == mpu_alloc then
                    result = fault == "mpu_exhaustion" and 255 or
                        (fault == "mpu_region_collision" and 7 or 3)
                elseif callable == mpu_release then
                    released = true
                elseif callable == free or callable == kmem_free then
                    freed = true
                end
                cave[result_address] = result
                return result == 0
            end
            if addr == 0x21000001 or addr == stage1_exec_address then
                if require_pre_entry_sync and stage1_exec_address
                    and (not mpu_synced or region_rbar[3] ~= addr - 1 + 6
                        or region_rlar[3] ~= addr - 1 + 3) then
                    return false
                end
                if fault == "stage1_exec_failure" then return false end
                device_present = true
                return true
            end
        end
        return true
    end
    return open, execute, function()
        return cave, files, device_present, used_insmod, freed, released,
            configured_rbar, configured_rlar, compound_commands, cave_base,
            pre_entry_syncs, mpu_events, stage1_exec_address
    end
end

local function check(path, installer_fault, installer_firmware)
    created = {}
    subscriptions = {}
    horizontal_resolution = 336
    vertical_resolution = 480
    if path:match("canopus%-installer%-prod/xiaomi%-band%-9/main%.lua$") then
        horizontal_resolution = 192
        vertical_resolution = 490
    end
    local installer_state
    local band9_state
    local band9_fault = installer_fault and installer_fault:match("^band9:(.+)$")
    if path:match("canopus%-installer/main%.lua$")
        or path:match("canopus%-installer%-prod/xiaomi%-band%-9/main%.lua$") then
        local selected_firmware = installer_firmware
        if selected_firmware == nil
            and path:match("canopus%-installer%-prod/xiaomi%-band%-9/main%.lua$") then
            selected_firmware = "3.1.32"
        end
        local strict_mpu = path:match(
            "canopus%-installer%-prod/xiaomi%-band%-9/main%.lua$") ~= nil
        io.open, os.execute, band9_state =
            band9_installer_io(band9_fault, selected_firmware, strict_mpu)
    elseif path:match("canopus_hello/main%.lua$") then
        io.open, installer_state = module_installer_io(installer_fault)
        os.execute = function() return true end
    end
    local ok, err = pcall(dofile, path)
    if not ok then
        io.open = original_io_open
        os.execute = original_os_execute
        print("LOAD FAIL:", path, err)
        return false
    end
    if installer_state then
        local request, files = installer_state()
        if type(request) ~= "string" or request:sub(1, 4) ~= "2CPC"
            or request:byte(17) ~= 2
            or request:sub(-14) ~= "canopus_hello\0"
            or files["/data/canopus/inbox/canopus_hello.cmi"] == nil
            or files["/data/canopus/inbox/canopus_hello.ko"] == nil then
            print("INSTALL FLOW FAIL:", path)
            return false
        end
        if installer_fault then
            local diagnosed = false
            for _, object in ipairs(created) do
                local text = object._last_set and object._last_set.text
                if type(text) == "string" and text:match("Install failed") then
                    diagnosed = true
                end
            end
            if not diagnosed then
                print("INSTALL DIAGNOSTIC FAIL:", path, installer_fault)
                return false
            end
        end
    end
    local clicks = 0
    for _, o in ipairs(created) do
        if o._click then
            local ok2, err2 = pcall(o._click)
            if not ok2 then
                io.open = original_io_open
                os.execute = original_os_execute
                print("CLICK FAIL:", path, tostring(err2))
                return false
            end
            clicks = clicks + 1
        end
    end
    for _ = 1, 5 do
        for _, subscription in ipairs(subscriptions) do
            local ok2, err2 = pcall(subscription.callback, subscription.owner, 0)
            if not ok2 then
                io.open = original_io_open
                os.execute = original_os_execute
                print("SUBSCRIPTION FAIL:", path, tostring(err2))
                return false
            end
        end
    end
    if band9_state then
        local cave, files, device_present, used_insmod, freed, released,
            configured_rbar, configured_rlar, compound_commands, cave_base,
            pre_entry_syncs, mpu_events, stage1_exec_address = band9_state()
        local original = { 0, 0, 0, 0, 0x726f6e5f, 0x73616c66,
            0x70615f68, 0x72665f69 }
        if band9_fault == "cave_mismatch" then original[1] = 1 end
        for i, value in ipairs(original) do
            if cave[cave_base + (i - 1) * 4] ~= value then
                io.open = original_io_open
                os.execute = original_os_execute
                print("BAND9 CAVE RESTORE FAIL:", path, installer_fault or "success")
                return false
            end
        end
        local staged = files["/data/canopus/stage2.bin"] ~= nil
            and files["/data/canopus/supervisor.elf"] ~= nil
        local fault_ok =
            (band9_fault == "cave_mismatch" and not freed and not released)
            or (band9_fault == "mailbox_exec_failure" and not freed and not released)
            or (band9_fault == "allocation_failure" and not freed and not released)
            or (band9_fault == "mpu_exhaustion" and freed and not released)
            or (band9_fault == "mpu_region_collision" and freed and not released)
            or (band9_fault == "mpu_configuration_failure" and freed and released)
            or (band9_fault == "stage1_exec_failure" and freed and released)
        local strict_stage0_mpu = path:match(
            "canopus%-installer%-prod/xiaomi%-band%-9/main%.lua$") ~= nil
        local function matches_mpu_sequence(start, region, expected)
            if start < 1 or start + #expected - 1 > #mpu_events then
                return false
            end
            local command_id = mpu_events[start][4]
            for offset, item in ipairs(expected) do
                local actual = mpu_events[start + offset - 1]
                if actual[1] ~= region or actual[2] ~= item[1]
                    or actual[3] ~= item[2] or actual[4] ~= command_id then
                    return false
                end
            end
            return true
        end
        local function has_mpu_sequence(region, expected)
            for start = 1, #mpu_events - #expected + 1 do
                if matches_mpu_sequence(start, region, expected) then
                    return true
                end
            end
            return false
        end
        local stage0_expected = {
            { "rbar", cave_base + 2 },
            { "rlar", cave_base + 64 - 32 + 3 },
            { "rbar", cave_base + 6 },
            { "sync", 0x0c5226db },
            { "exec", cave_base + 1 },
            { "rlar", 0 },
            { "sync", 0x0c5226db },
        }
        local stage0_exec_count = 0
        local stage0_sequences = true
        for index, event in ipairs(mpu_events) do
            if event[1] == 7 and event[2] == "exec"
                and event[3] == cave_base + 1 then
                stage0_exec_count = stage0_exec_count + 1
                if not matches_mpu_sequence(index - 4, 7, stage0_expected) then
                    stage0_sequences = false
                end
            end
        end
        stage0_sequences = stage0_sequences and stage0_exec_count > 0
        local stage1_base = stage1_exec_address and stage1_exec_address - 1
        local stage1_sequence = stage1_base and has_mpu_sequence(3, {
            { "rbar", stage1_base + 2 },
            { "rlar", stage1_base + 3 },
            { "rbar", stage1_base + 6 },
        })
        if band9_fault then
            if device_present or used_insmod or not staged or not fault_ok then
                io.open = original_io_open
                os.execute = original_os_execute
                print("BAND9 FAULT FLOW FAIL:", path, band9_fault,
                    "device=" .. tostring(device_present),
                    "insmod=" .. tostring(used_insmod),
                    "freed=" .. tostring(freed),
                    "released=" .. tostring(released))
                return false
            end
        elseif not device_present or used_insmod or not staged
            or not freed or not released
            or (strict_stage0_mpu and (configured_rbar ~= cave_base + 6
                or configured_rlar ~= cave_base + 64 - 32 + 3
                or configured_rlar % 8 ~= 3
                or pre_entry_syncs == 0
                or not stage0_sequences or not stage1_sequence))
            or (path:match("canopus%-installer%-prod/xiaomi%-band%-9/main%.lua$")
                and compound_commands == 0) then
            io.open = original_io_open
            os.execute = original_os_execute
            print("BAND9 BOOTSTRAP FLOW FAIL:", path,
                "device=" .. tostring(device_present),
                "insmod=" .. tostring(used_insmod),
                "stage2=" .. tostring(files["/data/canopus/stage2.bin"] ~= nil),
                "supervisor=" .. tostring(files["/data/canopus/supervisor.elf"] ~= nil),
                "freed=" .. tostring(freed),
                "released=" .. tostring(released),
                "rlar=" .. tostring(configured_rlar))
            return false
        end
    end
    io.open = original_io_open
    os.execute = original_os_execute
    print(string.format("watchface OK: %s (%d buttons clicked)", path, clicks))
    return true
end

local args = { ... }
if #args == 0 then
    args = {
        "watchfaces/canopus-installer/main.lua",
        "watchfaces/canopus-installer-prod/xiaomi-band-9/main.lua",
        "watchfaces/canopus_hello/main.lua",
    }
end
local ok_all = true
for _, p in ipairs(args) do
    if not check(p) then ok_all = false end
end
if ok_all then
    if not check("watchfaces/canopus-installer/main.lua", nil, "3.1.32") then
        ok_all = false
    end
    local band9_firmware_versions = { "3.1.175", "3.1.32" }
    local band9_faults = {
        "cave_mismatch", "mailbox_exec_failure", "allocation_failure",
        "mpu_exhaustion", "mpu_region_collision", "mpu_configuration_failure", "stage1_exec_failure",
    }
    for _, firmware_version in ipairs(band9_firmware_versions) do
        for _, fault in ipairs(band9_faults) do
            if not check("watchfaces/canopus-installer/main.lua",
                "band9:" .. fault, firmware_version) then
                ok_all = false
            end
        end
    end
    for _, fault in ipairs(band9_faults) do
        if not check("watchfaces/canopus-installer-prod/xiaomi-band-9/main.lua",
            "band9:" .. fault, "3.1.32") then
            ok_all = false
        end
    end
    if not check("watchfaces/canopus_hello/main.lua", "short_write") then
        ok_all = false
    end
    if not check("watchfaces/canopus_hello/main.lua", "stale_response") then
        ok_all = false
    end
end
if not ok_all then os.exit(1) end
