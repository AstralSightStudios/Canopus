# p67 native Feature and ferry registration contract

The lifecycle takes native pointers, not JS objects. Descriptor28, Lifecycle24,
member12. Prototype.manager12,prototype.JSValue56 (after onCreate),
instance.prototype28,instance.JSValue112. Manager.registry4,adapter8,async60.
Create(ctx,prototype),Required(ctx,instance),Destroy(ctx,prototype).

The creator0xC86236C returns manager, not raw registry. It cannot be flash-hooked
by current Supervisor. Use real framework singleton0x2013CFE0 (get0xC8FF468),
whose cached primary vptr is0x2CDA8CD8. Copy15 words including prefix2; wrap
factoryslot8 (0xC8E54AC). Implementation at framework+4, recursive mutex at
impl+348, application vector begin/end at impl+460/464. Gate activation on empty
vector under that mutex.

Factory returns480-byte Application with primary vptr0x2CDA7C80. Copy52 words
including prefix2; wrap init slot2 (0xC8DB1F8). Preserve all secondary vptrs.
Init returns1 and creates runtime at app+100; manager at runtime+164; registry
at manager+4. Register after init then return its original result. Descriptor
register0xC862898 returns1 for success. App run0xC8D9108 handles later source
initialization/evaluation.

Owner async is112 bytes, initialized0xC2E14E8 and closed0xC2DB874 only on owner
loop. loop pointer+4,busy+24,callback+96,pending+108; loop readfd360/writefd364.
Retained-file PipeWake avoids producer task-group numeric fd resolution.
Mutex init/lock/unlock wrappers0xC2DCCCC/0xC2DCDA8/0xC2DCDC4.

Evidence details and instruction addresses: EVID-PORT-1043-FERRY-001.json.
Static findings only, except explicitly attributed main-session Mirage singleton
observation. No production-device verification or policy approval is implied.

Direct property wrapper:0xC296F14 int(ctx,JSValue object,const char*name,JSValue value,int flags). Consumes value, borrows object/name. Stack name0/value8/flags16 exactly shared shim. Generic define0xC2886A8 receives flags|0x2700; atom and value released inline. Direct CString accessor0xC29460C(ctx,len*,value,cesu8) retains string backing and returns header+16; release0xC278848. Earlier composition conclusions for both were false.
