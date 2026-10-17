-- Exact RW6 profile: restricted signed installer endpoint; native Manager
-- remains the owner of enable/activate operations.
local ENDPOINT, REPLY_SIZE = "/canopus/install", 40
local SUPERVISOR_MISSING = "请先打开 Canopus RW6 启动入口"
local BUILD_PROPERTY = "ro.build.id"
local function fallback_identity() return nil, nil end
local function prepare_inbox(checkpoint)
    checkpoint("准备模块安装目录")
    os.execute("mkdir /data/canopus")
    os.execute("mkdir /data/canopus/inbox")
end
local function failure_detail(response)
    return " error " .. string.unpack("<i4", response, 37)
end
installation_outcome = function(response)
    -- Reopening the installer must not misreport an existing registration as
    -- a signature/load failure. Keep Supervisor's duplicate protection: this
    -- does not install, enable, update or activate an existing module.
    if string.unpack("<I4", response, 29) == 6
        and string.unpack("<i4", response, 37) == -105 then
        return "已有模块安装记录，无需重复安装。\n\n请打开 Canopus Manager 查看 corona 的状态，选择启用，再选择立即激活。\n\n之后打开 Corona 管理器导入／应用主题。"
    end
end
