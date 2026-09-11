-- Protocol, target selection, raw-device access and visible progress checks.
-- arg: generated main.lua, product stem, optional real Lua C-frame fixture.
local host_io, host_os, host_debug, host_version = io, os, debug, _VERSION
local host_execute = os.execute
local f = assert(io.open(assert(arg[1]), 'rb'))
local source = f:read('*a'); f:close()
local product = assert(arg[2])
local token = product:gsub('-', '_')

local checks, skipped = 0, 0
local targets = {
    ['3.101.036'] = {'xiaomi-band-10-pro-3.101.036', '662d67f5e247e31e194d3161024890ba93b9d29d70b290fadb9aac8ce8ec3c81'},
    ['3.101.043'] = {'xiaomi-band-10-pro-3.101.043', '519307675665e4866d722a8119a98589c397b614ac3294cb87bfc86de45756ec'},
    ['4.100.139'] = {'xiaomi-band-11-4.100.139', '31ce82257f7c127950dc5070b86316730cf468a41f0d004559e41e7d923b2c74'},
}
local function padded(s, n) return s .. string.rep('\0', n - #s) end
local function rawhex(s) return (s:gsub('..', function(v) return string.char(tonumber(v,16)) end)) end
local function run_case(version, fault)
    local target = targets[version]
    local is_band11 = version == '4.100.139'
    checks=checks+1
    local objects, timers, callbacks, files, commands = {}, {}, {}, {}, {}
    local request, painted
    local function after_paint()
        assert(painted and painted==objects[4].text, 'side effect happened before status paint')
    end
    local lvgl = {
        HOR_RES=function() return is_band11 and 212 or 336 end,
        VER_RES=function() return is_band11 and 520 or 480 end,
        Font=function(_,size) return size end, OPA=function(n) return n end,
        ALIGN={CENTER=1,TOP_MID=2},FLAG={SCROLLABLE=16},EVENT={DELETE=33},
    }
    local methods = {
        set=function(self,props) for k,v in pairs(props) do self[k]=v end end,
        clear_flag=function() end,
        onevent=function(_,_,cb) callbacks[#callbacks+1]=cb end,
    }
    local function object(_, props) objects[#objects+1]=props;return setmetatable(props,{__index=methods}) end
    lvgl.Object,lvgl.Label=object,object
    lvgl.Timer=function(props)
        function props:resume() self.paused=false end
        function props:delete() self.deleted=true end
        timers[#timers+1]=props;return props
    end
    package.loaded.lvgl=lvgl
    local module = '\127ELF\1\1\1' .. string.rep('\0',9) .. string.pack('<I2I2',1,40) .. string.rep('\0',492)
    if target then
        local receipt=string.pack('<I4I4I4I4I4I4I4I4',0x31494d43,1,256,0,1,1,#module,0)
            .. padded(token,32) .. padded(target[1],48) .. rawhex(target[2]) .. string.rep('\0',112)
        if fault=='wrong_receipt' then receipt=receipt:sub(1,64)..padded('other-target',48)..receipt:sub(113) end
        if fault=='wrong_firmware' then receipt=receipt:sub(1,112)..string.rep('\0',32)..receipt:sub(145) end
        files['/fake/'..product..'-'..target[1]..'.bin']=module
        files['/fake/'..product..'-'..target[1]..'.cmi.bin']=receipt
        if fault=='missing_module' then files['/fake/'..product..'-'..target[1]..'.bin']=nil end
    end
    local assets=product=='bluetooth-audio' and {'appicon_headphones.bin'} or
        {'appicon_lyra.bin','lyra-previous.bin','lyra-play.bin','lyra-pause.bin','lyra-next.bin'}
    for _,name in ipairs(assets) do
        files['/fake/'..name]='\25\16\0\0'..string.pack('<I2I2I2I2',2,2,8,0)..string.rep('\0',16)
    end
    if fault=='missing_icon' then files['/fake/'..assets[#assets]]=nil end
    local build = source:match('%["build"%] = "([^"]+)"')
    -- Build identities are taken from the generated target block for each version.
    local config_block = source:match('%["'..version:gsub('%.','%%.')..'"%] = ({.-})')
    build = config_block and config_block:match('%["build"%] = "([^"]+)"') or ''
    files['/etc/build.prop']='ro.build.version='..version..'\nro.build.id='..build..'\n' 
    if fault=='identity' then files['/etc/build.prop']=files['/etc/build.prop']:gsub('cn%-202608280000','cn-000000000000') end
    if fault=='duplicate_identity' then files['/etc/build.prop']=files['/etc/build.prop']..'ro.build.version='..version..'\n' end
    local function open(path,mode)
        if path=='/canopus/install' then
            if fault=='no_supervisor' then return nil end
            return {
                write=function(self,data)
                    after_paint()
                    request=data
                    if fault=='short_write' then return #data-1 end
                    return self
                end,
                read=function()
                    local id=fault=='stale_response' and 9 or 1
                    return string.pack('<I4I2I2I2I2I4I4I4I4I4I4',0x43504332,36,2,1,0,40,2,id,0,5,4)..string.pack('<I4',0)
                end, close=function() return true end,
            }
        end
        if mode=='wb' then
            return {write=function(self,data)
                        after_paint();files[path]=data
                        if fault=='staging_short_write' then return #data-1 end
                        if fault=='corrupt_readback' then files[path]=data..'x' end
                        return self
                    end,close=function() return true end}
        end
        if files[path] then
            return {read=function(_,size) return type(size)=='number' and files[path]:sub(1,size) or files[path] end,
                    close=function() return true end}
        end
    end
    local function execute() error('external installer must not execute commands') end
    io,os,debug,_VERSION={open=open},{execute=execute},nil,host_version
    SCRIPT_PATH='/fake/'
    assert(load(source,'prod-fixture','t'))()
    assert(#objects==4 and objects[2].w==lvgl.HOR_RES())
    assert(request==nil, 'install ran before the initial UI turn')
    if fault=='cancel' then callbacks[1]() end
    for _=1,150 do
        painted=objects[4].text
        for _,timer in ipairs(timers) do if not timer.deleted and not timer.paused then timer.cb(timer) end end
    end
    local success=target and fault=='ok'
    if success then
        assert(request and request:sub(37)==token..'\0',objects[4].text)
        assert(objects[4].text:find('安装完成',1,true),objects[4].text)
        assert(objects[4].text:find('查看和启用模块',1,true))
        assert(files['/data/canopus/inbox/'..token..'.ko']==module)
        for _,name in ipairs(assets) do assert(files['/data/canopus/'..name]==files['/fake/'..name]) end
    elseif fault=='cancel' then assert(request==nil and #commands==0)
    else
        assert(objects[4].text:find('安装失败',1,true),fault..': '..objects[4].text)
        if fault~='short_write' and fault~='stale_response' then assert(request==nil) end
    end
    for _,timer in ipairs(timers) do assert(timer.deleted) end
end
for version,target in pairs(targets) do
    if source:find('["id"] = "'..target[1]..'"',1,true) then
        for _,fault in ipairs({'ok','wrong_receipt','wrong_firmware','missing_module','missing_icon',
            'no_supervisor','short_write','stale_response','cancel','duplicate_identity',
            'staging_short_write','corrupt_readback'}) do run_case(version,fault) end
        if version=='4.100.139' then run_case(version,'identity') end
    end
end
run_case('3.101.999','unsupported')
io,os,debug,_VERSION=host_io,host_os,host_debug,host_version
os.execute=host_execute
__band11_fixture_open,__band11_test_execute=nil,nil
print(product..': '..checks..' prod selection, ordinary IO, staging, progress and failure checks passed')

