-- Bootstrap orchestration checks. ARM execution is tested separately in Unicorn.
local loader = assert(loadfile('watchfaces/canopus-installer-prod/xiaomi-band-11/src/native_loader.lua'))()
local target = arg[1] or 'xiaomi-band-11-4.100.139'
local root = 'watchfaces/canopus-installer-prod/xiaomi-band-11/'
local profile = assert(loadfile(root .. 'canopus_loader_profile-' .. target .. '.bin'))()
local resources = {}
for _,name in ipairs({'stage1','stage2','supervisor'}) do
    local path=root .. 'canopus_' .. name .. '-' .. target .. '.bin'
    local f=assert(io.open(path,'rb'));resources[path]=f:read('*a');f:close()
end
assert(loader.crc32('123456789')==0xcbf43926)
local original = debug.upvalueid
for _,fault in ipairs({'ok','crc','mpu','heap','upvalue','layout','mailbox','native','staging','cache_disabled','clean','invalidate','barrier1','barrier2','cancel'}) do
    local files,memory,commands={},{},{}
    for k,v in pairs(resources) do files[k]=v end
    memory[0xe000ed94]=fault=='mpu' and 1 or 5
    memory[0xe000edc0]=0x00447722;memory[0x200f5190]=0x0f
    for _,row in ipairs(profile.fingerprints) do memory[row[1]]=row[2] end
    memory[0x200b2590]=0x3c356b40;memory[0x3c356b5c]=0x3c356ca8;memory[0x3c356b60]=0x3cfefdf8
    if fault=='heap' then memory[0x200b2590]=0x1234 end
    memory[0x07ffa000]=1;memory[0x07ffc000]=1
    if fault=='cache_disabled' then memory[0x07ffa000]=0 end
    local upvalue,object=0x3c400000,0x3c410000
    local weak_owner = setmetatable({}, {__mode='v'})
    debug.upvalueid=function(owner,index)
        assert(index==1)
        local value=owner()
        weak_owner[1] = owner
        memory[upvalue+8]=fault=='upvalue' and 0 or upvalue+16
        memory[upvalue+24]=0x54;memory[upvalue+16]=object
        memory[object+4]=fault=='layout' and 4 or 0x14;memory[object+12]=#value
        memory[object+16]=0x7ffffffe;memory[object+48]=string.unpack('<I4',value,33)
        return 'userdata: 0x3c400000'
    end
    if fault=='crc' then local p=root .. 'canopus_stage2-' .. target .. '.bin';files[p]='corrupt' end
    local entered,barriers,cache_commands=0,0,{}
    local cache_critical = false
    local function run(command)
        commands[#commands+1]=command
        if command=='mkdir /data/canopus' then return true end
        local a,path=command:match('^mw (%x+) 4 > (.+)$')
        if a then
            local addr=tonumber(a,16);assert(memory[addr]~=nil,string.format('unexpected read %x',addr))
            files[path]=string.format('0x%08x = 0x%08x',addr,memory[addr]);return true
        end
        local a,v=command:match('^mw (%x+)=(%x+)$')
        if a then
            a,v=tonumber(a,16),tonumber(v,16)
            assert(a==object+52,'Lua may only write its owned mailbox parameter through mw')
            if a~=object+52 or fault~='mailbox' then memory[a]=v else memory[a]=0 end
            return true
        end
        if command==string.format('exec 0x%08x',profile.cache_d_clean) then cache_critical=true;cache_commands[#cache_commands+1]='clean';return fault~='clean' end
        if command==string.format('exec 0x%08x',profile.cache_i_invalidate) then cache_commands[#cache_commands+1]='invalidate';return fault~='invalidate' end
        if command==string.format('exec 0x%08x',profile.cache_barrier) then
            barriers=barriers+1;cache_commands[#cache_commands+1]='barrier'
            return fault~=('barrier' .. barriers)
        end
        local a=command:match('^exec 0x(%x+) > /data/canopus/bootstrap%-exec.txt$')
        assert(tonumber(a,16)==object+48-0x20000000+1,command)
        assert(table.concat(cache_commands,',')=='clean,barrier,invalidate,barrier')
        assert(weak_owner[1], 'owned payload was collected while the UI yielded')
        entered=entered+1;memory[object+16]=fault=='native' and 0xfffffecf or 0
        cache_critical=false
        return true
    end
    local function write(path,data) if fault=='staging' then return false end;files[path]=data;return true end
    local phases = {}
    local task = coroutine.create(function()
        return loader.load(profile,root .. 'canopus_stage1-' .. target .. '.bin',root .. 'canopus_stage2-' .. target .. '.bin',root .. 'canopus_supervisor-' .. target .. '.bin',run,function(path) return files[path] end,write,
            function(text)
                assert(not cache_critical, 'yield inserted between cache maintenance and exec')
                phases[#phases+1]=text
                coroutine.yield(text)
            end)
    end)
    local resumed,ok,message
    repeat
        collectgarbage('collect')
        resumed,ok,message=coroutine.resume(task)
        assert(resumed,tostring(ok))
        if fault=='cancel' and type(ok)=='string' and ok:find('同步缓存',1,true) then
            assert(weak_owner[1], 'cancellation case never allocated its owner')
            assert(coroutine.close(task))
            loader.release()
            ok,message=false,'cancelled'
        end
    until coroutine.status(task)=='dead'
    assert(ok==(fault=='ok'),fault .. ': ' .. tostring(message))
    assert(entered==((fault=='ok' or fault=='native') and 1 or 0),fault)
    if fault=='crc' then assert(#commands==0) end
    if fault=='ok' then assert(#phases>20 and phases[#phases]=='确认原生加载结果') end
    collectgarbage('collect')
    assert(weak_owner[1]==nil, 'completed or failed loader retained its temporary owner')
end
debug.upvalueid=original
print('Band 11 native Lua orchestration: ownership, CRC, MPU preflight, cache sequence and failure returns passed')
