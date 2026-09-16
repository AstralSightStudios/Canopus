-- Generated Canopus installer entry. Edit the private source, not this file.
local h = {
-- @PAYLOAD@
}
for i = 1, #h do
    h[i] = h[i]:gsub('%x%x', function(v) return string.char(tonumber(v, 16)) end)
end
local b = table.concat(h)
h = nil
-- Fail before recovery if the VM uses another version or numeric format.
local signature = string.dump(function() end, true):sub(1, 31)
if b:sub(1, 31) ~= signature then
    error('Canopus: incompatible Lua bytecode format', 0)
end
local f, e = load(b, '@canopus-installer', 'b', _ENV)
b = nil
if not f then error(e, 0) end
return f(...) -- tail call: do not retain a frame above the recovery code
