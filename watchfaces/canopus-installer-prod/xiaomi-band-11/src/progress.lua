-- One checkpoint per UI timer tick. Never busy-wait or call timer:ready():
-- the event loop must get a chance to paint before the next operation.
local M = {}
function M.new(lvgl, changed, finished)
    local timer, task
    local active = false
    local function stop()
        active = false
        if timer then
            local old = timer
            timer = nil
            old:delete()
        end
        task = nil
    end
    local function advance()
        if not active then return end
        local ok, result = coroutine.resume(task)
        if not ok then
            stop()
            finished(false, tostring(result))
        elseif coroutine.status(task) == "dead" then
            stop()
            finished(true, result)
        else
            changed(result)
        end
    end
    return {
        start = function(work)
            if active then return false, "An operation is already running" end
            if type(lvgl.Timer) ~= "function" then return false, "LuaLVGL Timer unavailable" end
            task = coroutine.create(work)
            local ok, created = pcall(lvgl.Timer, {
                period = 120, repeat_count = -1, paused = true,
                cb = function()
                    local tick_ok, message = pcall(advance)
                    if not tick_ok then stop(); finished(false, tostring(message)) end
                end,
            })
            if not ok or not created then task = nil; return false, tostring(created) end
            timer, active = created, true
            local started, message = pcall(function()
                advance() -- first resume only publishes the first checkpoint
                if timer then timer:resume() end
            end)
            if not started then stop(); return false, tostring(message) end
            return true
        end,
        checkpoint = function(text) coroutine.yield(text) end,
        cancel = function()
            local old = task
            stop()
            if old and coroutine.status(old) == "suspended" then coroutine.close(old) end
        end,
    }
end
return M
