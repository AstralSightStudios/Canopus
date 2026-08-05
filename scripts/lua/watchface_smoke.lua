-- watchface_smoke.lua — load every watchface under a stubbed lvgl binding and
-- fire every button handler, catching nil-global / scope bugs (e.g. a
-- function referencing a `local status` declared later, which resolves to a
-- nil global).
--
-- Usage: lua scripts/lua/watchface_smoke.lua [watchface-dir...]
-- Returns non-zero on any load/click failure.

local lvgl = {}
lvgl.HOR_RES = function() return 336 end
lvgl.VER_RES = function() return 480 end
lvgl.OPA = function(v) return v end
lvgl.FLAG = { SCROLLABLE = 1, CLICKABLE = 2 }
lvgl.ALIGN = { CENTER = 1, TOP_MID = 2, BOTTOM_MID = 3 }

local created = {}
local obj_mt = {}
function obj_mt:clear_flag() return self end
function obj_mt:add_flag() return self end
function obj_mt:set(props) self._last_set = props; return self end
function obj_mt:onClicked(fn) self._click = fn; table.insert(created, self); return self end
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

local function check(path)
    created = {}
    local ok, err = pcall(dofile, path)
    if not ok then
        print("LOAD FAIL:", path, err)
        return false
    end
    local clicks = 0
    for _, o in ipairs(created) do
        if o._click then
            local ok2, err2 = pcall(o._click)
            if not ok2 then
                print("CLICK FAIL:", path, tostring(err2))
                return false
            end
            clicks = clicks + 1
        end
    end
    print(string.format("watchface OK: %s (%d buttons clicked)", path, clicks))
    return true
end

local args = { ... }
if #args == 0 then
    args = { "watchfaces/canopus-installer/main.lua" }
end
local ok_all = true
for _, p in ipairs(args) do
    if not check(p) then ok_all = false end
end
if not ok_all then os.exit(1) end
