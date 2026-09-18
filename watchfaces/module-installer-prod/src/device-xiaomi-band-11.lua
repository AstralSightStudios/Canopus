-- Band 11 Lua denies /dev and has no shell. The Supervisor's separate
-- /canopus/install node accepts INSTALL only and appends a u32 diagnostic;
-- the framework installer prepares /data/canopus/inbox.
local ENDPOINT, REPLY_SIZE = "/canopus/install", 40
local SUPERVISOR_MISSING = "请更新并运行 Canopus 框架安装表盘"
local BUILD_PROPERTY = "ro.build.id"
local function fallback_identity() return nil end
local function prepare_inbox() end
local function failure_detail(response) return " error " .. u32(response, 36) end
