#!/usr/bin/env python3
"""Execute the built ARM stage1 -> stage2 -> Supervisor constructors in Unicorn.

Firmware allocation/VFS and MPU register banking are modeled. This validates
PIC rebasing, relocation, AAPCS return paths, ctor mailbox and /dev registration;
it cannot validate physical BES cache/alias behavior or a real display.
Run with build/band11-tests/bin/python scripts/tests/band11_arm_bootstrap.py.
"""
import ctypes
from pathlib import Path
import struct
import sys
import unittest
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_BLOCK, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_WRITE, UC_PROT_ALL
from unicorn.arm_const import *

ROOT = Path(__file__).resolve().parents[2]
TARGET = 'xiaomi-band-11-4.100.139'
RES = ROOT / 'watchfaces/canopus-installer-prod/xiaomi-band-11'

class Machine:
    def __init__(self, fault=None, code=0x1c400020, kernel_base=0x20110000):
        self.fault = fault
        self.uc = u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
        firmware = (ROOT / 'fwbins' / TARGET / 'vela_ap.bin').read_bytes()
        u.mem_map(0x0c0c0000, (len(firmware) + 4095) & ~4095)
        u.mem_write(0x0c0c0000, firmware)
        u.mem_map(0x2c0c0000, (len(firmware) + 4095) & ~4095)
        u.mem_write(0x2c0c0000, firmware)
        for base, size in [(0x20000000,0x160000),(0xe000e000,0x2000),(0x07ffa000,0x4000)]:
            u.mem_map(base,size)
        # BES cache controllers: I at 0x07ffa000, D at 0x07ffc000, both enabled
        # (bit0). Command ports are plain storage here, so a test reads back
        # which command was issued; no write hook is installed over them because
        # the enabled-I branch is an ITET block and Unicorn 2.1.4 corrupts
        # ITSTATE for conditional memory instructions under UC_HOOK_MEM_WRITE.
        u.mem_write(0x07ffa000,struct.pack('<I',1))
        u.mem_write(0x07ffc000,struct.pack('<I',1))
        # One physical PSRAM behind both windows: startup copy 0x0c0c024c
        # writes the image at 3c... and its code runs at 1c... . Backing the
        # two ranges with the same host buffer models the alias itself; the
        # cache behaviour that makes a write visible to instruction fetch is
        # not modeled and remains a hardware question.
        self.psram = ctypes.create_string_buffer(0x1000000)
        psram_ptr = ctypes.cast(self.psram, ctypes.c_void_p).value
        for base in (0x1c000000, 0x3c000000):
            u.mem_map_ptr(base,0x1000000,UC_PROT_ALL,psram_ptr)
        # Startup-copy mappings from the actual image; permits executing libc
        # and HAL veneers instead of replacing their target bodies with mocks.
        u.mem_map(0x00260000,0x140000)
        for dst,src,size in [(0x2006be00,0x1b3c,0xb710),
                             (0x20079420,0xd24c,0x2f8c)]:
            u.mem_write(dst,firmware[src:src+size])
            u.mem_write(dst-0x1fe00000,firmware[src:src+size])
        self.code, self.mailbox, self.stop = code, 0x3c400000, 0x1c000100
        blob = bytearray((RES / f'canopus_stage1-{TARGET}.bin').read_bytes())
        struct.pack_into('<I',blob,4,self.mailbox)
        u.mem_write(code,bytes(blob)); self.word(self.mailbox,0x7ffffffe)
        self.next_alloc = kernel_base
        self.next_temp = 0x3c500000
        # Largest free chunk each modeled heap reports through mm_mallinfo.
        # None == "whatever is left in this harness's arena"; a test sets a
        # number to reproduce the real .139 Kmem, which is mostly resident
        # firmware. mm_malloc PANICs instead of returning NULL on both known
        # heaps, so the supervisor must consult this before every request.
        self.kernel_largest = None
        self.temp_largest = None
        self.alloc_calls = 0; self.peak_kernel = 0
        self.allocations, self.frees, self.files, self.next_fd = {}, [], {}, 3
        self.registered = False; self.fops = None; self.installer_fops = None; self.disk = {}
        self.regions = {i:(0,0) for i in range(8)}
        # Synthetic reserved RO/XN region: never claimed or cleared. Actual
        # .139 scheduling uses MSPLIM/PSPLIM, not a proven region-7 stack guard.
        self.regions[7] = (0x2015cee7,0x2015cee3)
        self.word(0xe000ed90,8 << 8); self.word(0xe000ed94,5)
        self.word(0xe000edc0,0x00447722); self.word(0xe000ed98,7)
        self.word(0x200f5190,0x0f)
        self.word(0x200b01c8,0x2010e9e0)
        self.word(0x2010e9e0+0x1c,0x2010eb48)
        self.word(0x2010e9e0+0x20,0x2015f2b0)
        self.word(0x200b2590,0x3c356b40)
        self.word(0x3c356b40+0x1c,0x3c356ca8)
        self.word(0x3c356b40+0x20,0x3cfefdf8)
        self.word(0x200e0000,2)
        self.register_calls = 0
        self.app = 0; self.pages = []; self.launcher = []; self.widgets = {}; self.next_widget = 0x200a0000
        hooks = {0x0c3507e8:self.allocate,0x0c34ccb8:self.free,
                 0x0c34cd2c:self.free,0x0c34f0a0:self.mallinfo,
                 0x0c342c54:self.open,0x0c33818c:self.close,0x0c33d784:self.read,
                 0x0c914f64:self.register,0x0c349538:lambda:0x200e0000,
                 0x0c33dc4e:self.write,0x0c33dbec:self.unlink,0x0c33d79c:self.rename,0x0c6ac54c:lambda:self.app,
                 0x0c6ab350:self.app_install,0x0c547b54:self.launcher_add}
        for addr in [0xc380686,0xc3ab300,0xc89e7dc,0xc3ae1d0,0xc3ad7f4]:hooks[addr]=self.widget_create
        for addr in [0xc387ba8,0xc387b2e,0xc3b3f5c,0xc38404c,0xc3840d8,0xc3877b0,0xc37fc1c,0xc3b2c28,0xc3ad920,0xc3ad8b4]:hooks[addr]=lambda:0
        hooks[0xc3b3948]=self.widget_text
        self.firmware_hooks = {}
        for addr,fn in hooks.items():
            self.firmware_hooks[addr] = u.hook_add(UC_HOOK_CODE,self.firmware_call,fn,addr,addr)
        u.hook_add(UC_HOOK_CODE,lambda uc,a,s,d:uc.emu_stop(),None,self.stop,self.stop)
        u.hook_add(UC_HOOK_MEM_WRITE,self.mpu_write,None,0xe000ed98,0xe000eda3)
        u.hook_add(UC_HOOK_MEM_READ,self.mpu_read,None,0xe000ed9c,0xe000eda3)
        self.access_hooks = [
            u.hook_add(UC_HOOK_MEM_WRITE,self.check_memory,None,0x20000000,0x2015ffff),
            u.hook_add(UC_HOOK_MEM_READ,self.check_memory,None,0x20000000,0x2015ffff)]
        u.hook_add(UC_HOOK_BLOCK,self.check_execution)
        self.stack_top = 0x2015df00; self.stack_low = self.stack_top - 4096
        self.stack_highwater = 0
        self.control = 0
        self.mpu_write_attempts = 0; self.firmware_mpu_setup = False
        if fault == 'overlap': self.regions[3] = (kernel_base | 1, (kernel_base + 0x1000) | 3)
        if fault == 'occupied': self.word(0x200f5190,0x7f)
        if fault == 'hardware_occupied':
            for i in (4,5,6): self.regions[i] = (0x200c0007 + i*32,0x200c0003 + i*32)
        if fault == 'bad_context': self.word(0xe000edc0,0)
        if fault == 'wrong_identity': u.mem_write(0x0ca0d216,b'4.100.138')
    def word(self,a,v=None):
        if v is not None: self.uc.mem_write(a,struct.pack('<I',v & 0xffffffff))
        return struct.unpack('<I',self.uc.mem_read(a,4))[0]
    def reg(self,n): return self.uc.reg_read([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3][n])
    def string(self,a):
        out=bytearray()
        for i in range(1024):
            b=self.uc.mem_read(a+i,1)[0]
            if not b:return out.decode()
            out.append(b)
        raise AssertionError('unterminated firmware argument')
    def firmware_call(self,u,a,size,fn):
        value=fn()
        if value is None:return # modeled VFS tail-call into the real native fops
        # AAPCS permits these registers to be destroyed by every external call.
        for reg in (UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R12):
            u.reg_write(reg,0xdeadc0de)
        u.reg_write(UC_ARM_REG_R0,value & 0xffffffff)
        u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
    def heap_largest(self,heap):
        if heap==0x2010e9e0:
            if self.kernel_largest is not None: return self.kernel_largest
            return max(0,self.stack_low-32-self.next_alloc)
        if self.temp_largest is not None: return self.temp_largest
        return max(0,0x3cfefdf8-self.next_temp)
    def mallinfo(self):
        # struct mallinfo mm_mallinfo(heap): AAPCS sret pointer in r0, heap in
        # r1, 7 words {arena, ordblks, aordblks, mxordblk, uordblks, fordblks,
        # usmblks}. Only mxordblk gates an allocation.
        out,heap=self.reg(0),self.reg(1)
        assert heap in (0x2010e9e0,0x3c356b40),hex(heap)
        largest=self.heap_largest(heap)
        used=sum(n for p,n in self.allocations.items()
                 if p not in self.frees and (p<0x30000000)==(heap==0x2010e9e0))
        for i,value in enumerate((largest+used,1,len(self.allocations),largest,used,largest,largest)):
            self.word(out+4*i,value)
        return out
    def allocate(self):
        heap,align,size=self.reg(0),self.reg(1),self.reg(2)
        assert heap in (0x2010e9e0,0x3c356b40) and align and align & (align-1)==0
        # Nothing may reach mm_malloc that mm_mallinfo said would not fit:
        # this firmware panics on the failure path rather than returning NULL.
        assert size <= self.heap_largest(heap),('unchecked allocation',hex(heap),size)
        self.alloc_calls += 1
        if self.fault=='nomem' or self.fault==f'alloc_{self.alloc_calls}': return 0
        if heap==0x2010e9e0:
            p=(self.next_alloc+align-1)&~(align-1); self.next_alloc=p+size+16
            assert self.next_alloc < self.stack_low-32, (hex(p),size)
        else:
            p=(self.next_temp+align-1)&~(align-1); self.next_temp=p+size+16
        self.allocations[p]=size
        live_kernel=sum(n for p,n in self.allocations.items() if p < 0x30000000 and p not in self.frees)
        self.peak_kernel=max(self.peak_kernel,live_kernel)
        if self.fault=='kernel_budget': assert live_kernel <= 105*1024
        self.uc.mem_write(p,b'\xcc'*size)
        return p
    def free(self):
        p=self.reg(0); assert p in self.allocations and p not in self.frees,hex(p)
        if self.uc.reg_read(UC_ARM_REG_PC)==0x0c34ccb8: assert p < 0x30000000
        else: assert p >= 0x3c000000
        self.frees.append(p); return 0
    def open(self):
        path=self.string(self.reg(0))
        if path=='/dev/canopus':
            if not self.registered:return -1
            data='device'
        elif path=='/canopus/install':
            if self.installer_fops is None:return -1
            data='installer'
        elif path in ('/data/canopus/stage2.bin','/data/canopus/supervisor.elf'):
            name='stage2' if path.endswith('stage2.bin') else 'supervisor'
            if self.fault=='missing_stage2' and name=='stage2':return -1
            data=(RES/f'canopus_{name}-{TARGET}.bin').read_bytes()
            if self.fault=='bad_supervisor' and name=='supervisor':data=data[:-1]+bytes([data[-1]^1])
            if self.fault=='truncated_stage2' and name=='stage2':data=data[:-1]
            if self.fault=='extra_stage2' and name=='stage2':data+=b'\x00'
        else:
            assert path.startswith('/data/canopus/'),path
            if self.reg(1)&4:data=bytearray()
            elif path in self.disk:data=bytearray(self.disk[path])
            else:return -1
        fd=self.next_fd; self.next_fd+=1; self.files[fd]=[data,0,path]
        return fd
    def read(self):
        fd,ptr,count=self.reg(0),self.reg(1),self.reg(2)
        data,off,_=self.files[fd]
        if data=='device':
            self.uc.reg_write(UC_ARM_REG_R0,0);self.uc.reg_write(UC_ARM_REG_PC,self.word(self.fops+8));return None
        if data=='installer':
            self.uc.reg_write(UC_ARM_REG_R0,0);self.uc.reg_write(UC_ARM_REG_PC,self.word(self.installer_fops+8));return None
        block=data[off:off+min(count,173)] # partial reads
        self.files[fd][1]+=len(block)
        self.uc.mem_write(ptr,bytes(block));return len(block)
    def write(self):
        data,off,_=self.files[self.reg(0)]
        if data=='device':
            self.uc.reg_write(UC_ARM_REG_R0,0);self.uc.reg_write(UC_ARM_REG_PC,self.word(self.fops+12));return None
        if data=='installer':
            self.uc.reg_write(UC_ARM_REG_R0,0);self.uc.reg_write(UC_ARM_REG_PC,self.word(self.installer_fops+12));return None
        n=min(self.reg(2),173);data[off:off+n]=self.uc.mem_read(self.reg(1),n)
        self.files[self.reg(0)][1]+=n;return n
    def unlink(self):
        self.disk.pop(self.string(self.reg(0)),None);return 0
    def rename(self):
        a,b=self.string(self.reg(0)),self.string(self.reg(1))
        self.disk[b]=self.disk.pop(a);return 0
    def app_install(self):
        ptr,pages,count=self.reg(0),self.reg(1),self.reg(2)
        assert count==3 and self.string(self.word(ptr+12))=='com.canopus.manager'
        assert self.string(self.word(ptr+16))=='/data/canopus/manager_icon.bin'
        assert self.word(ptr+20)&0xffff==0xca and self.word(ptr+36)&1
        self.pages=[self.word(pages+4*i) for i in range(count)]
        for i,page in enumerate(self.pages):
            assert self.word(page+20)==0x00ca0000+i
            for off in (0x34,0x4c,0x50,0x5c,0x64):assert self.word(page+off)&1
            assert self.word(page+0x58)==0 and self.word(page+0x60)==0
        self.app=0x200c0000;self.uc.mem_write(self.app,bytes(self.uc.mem_read(ptr,80)))
        self.uc.mem_write(ptr+23,b'\x01\x01') # firmware defaults mutate input
        return 0
    def launcher_add(self):
        assert self.reg(0)==0xca and self.app
        self.launcher.append(0xca);return 0
    def widget_create(self):
        p=self.next_widget;self.next_widget+=0x80;self.widgets[p]={'parent':self.reg(0)};return p
    def widget_text(self):
        self.widgets[self.reg(0)]['text']=self.string(self.reg(1));return 0
    def close(self):
        data,off,path=self.files.pop(self.reg(0))
        if isinstance(data,bytearray):self.disk[path]=bytes(data)
        return 0
    def register(self):
        path=self.string(self.reg(0))
        assert path in ('/dev/canopus','/canopus/install') and self.reg(2)==0
        fops=self.reg(1); self.register_calls+=1
        for offset in (0,4,8,12):assert self.word(fops+offset)&1
        if self.fault=='register_fail':return -5
        if path=='/dev/canopus': self.fops=fops;self.registered=True
        else:self.installer_fops=fops
        return 0
    def mpu_write(self,u,access,address,size,value,data):
        if address in (0xe000ed9c,0xe000eda0):
            assert u.reg_read(UC_ARM_REG_PRIMASK)&1,'non-atomic MPU mutation'
            region=self.word(0xe000ed98)
            assert 0<=region<=7 if self.firmware_mpu_setup else 4<=region<=6,region
            b,l=self.regions[region]
            if address==0xe000eda0 and value&1:
                self.mpu_write_attempts+=1
                if self.fault=='mpu_write_rejected':value=0
            self.regions[region]=(value,l) if address==0xe000ed9c else (b,value)
    def mpu_read(self,u,access,address,size,value,data):
        b,l=self.regions[self.word(0xe000ed98)]
        self.word(address,b if address==0xe000ed9c else l)
    def check_memory(self,u,access,address,size,value,data):
        matches=[(b,l) for b,l in self.regions.values() if l&1 and (b&~31)<=address and address+size-1<=(l|31)]
        assert len(matches)<=1,('MPU overlap',hex(address))
        if access==UC_MEM_WRITE:
            assert not any(b&4 for b,l in matches),('write to RO region',hex(address))
            if self.stack_low-32 <= address < self.stack_top:
                assert address>=self.stack_low,'4 KiB NSH stack overflow'
                self.stack_highwater=max(self.stack_highwater,self.stack_top-address)
        for p in self.frees:
            assert not (p<=address<p+self.allocations[p]),('use after free',hex(address))
    def check_execution(self,u,address,size,data):
        if 0x20000000<=address<0x20160000:
            matches=[(b,l) for b,l in self.regions.values() if l&1 and (b&~31)<=address and address+size-1<=(l|31)]
            assert len(matches)==1 and not matches[0][0]&1,('execution lacks MPU lease',hex(address))
            assert any(p<=address<p+n and p not in self.frees for p,n in self.allocations.items()),'execute freed storage'
    def call(self,entry,arg0=0):
        u=self.uc; sp=self.stack_top
        initial_mask=u.reg_read(UC_ARM_REG_PRIMASK)
        # Whole-range snapshots also detect writes when broad access hooks are
        # unavailable. Unicorn 2.1.4 corrupts ITSTATE for conditional memory
        # instructions under UC_HOOK_MEM_WRITE (see standalone reproducer).
        immutable=[(b&~31,bytes(u.mem_read(b&~31,(l|31)-(b&~31)+1)))
                   for i,(b,l) in self.regions.items() if i in (4,5,6) and l&1 and b&4]
        u.mem_write(self.stack_low-32,b'\xa5'*32)
        saved={reg:0x11220000+reg for reg in (UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11)}
        for reg,value in saved.items():u.reg_write(reg,value)
        u.reg_write(UC_ARM_REG_CONTROL,self.control)
        u.reg_write(UC_ARM_REG_SP,sp); u.reg_write(UC_ARM_REG_LR,self.stop|1)
        u.reg_write(UC_ARM_REG_R0,arg0)
        u.emu_start(entry|1,self.stop,count=80000000)
        assert u.reg_read(UC_ARM_REG_PC)==self.stop,hex(u.reg_read(UC_ARM_REG_PC))
        assert u.reg_read(UC_ARM_REG_SP)==sp,'NSH stack was not restored'
        for reg,value in saved.items():assert u.reg_read(reg)==value,('callee-saved register',reg)
        assert u.reg_read(UC_ARM_REG_PRIMASK)==initial_mask,'interrupt mask was not restored'
        assert u.reg_read(UC_ARM_REG_CONTROL)==self.control,'thread stack selection changed'
        assert bytes(u.mem_read(self.stack_low-32,32))==b'\xa5'*32,'stack guard was overwritten'
        for start,before in immutable:
            assert bytes(u.mem_read(start,len(before)))==before,'resident RO region changed'
        return u.reg_read(UC_ARM_REG_R0)
    def finish_access_monitor(self):
        for handle in self.access_hooks:self.uc.hook_del(handle)
        self.access_hooks=[]
    def nsh_exec(self,entry,dispatch=False):
        # Real .139 NSH command, argument conversion, printf wrapper and normal
        # LR path. Only vprintf's I/O is modeled; it returns a positive count.
        state,argv,command,arg,io=0x200d1000,0x200d2000,0x200d2040,0x200d2060,0x200d2100
        self.uc.mem_write(state,bytes(0x600))
        self.word(state+0x1c,0x0c36c6f7);self.word(state+0x18,0x0c36c71d)
        self.uc.mem_write(command,b'exec\0');self.uc.mem_write(arg,f'0x{entry|1:08x}\0'.encode())
        self.word(argv,command);self.word(argv+4,arg);self.word(argv+8,0)
        self.uc.mem_write(io,bytes(24));self.word(io,0xffffffff);self.word(io+4,0xffffffff)
        hooks={0x0c34b728:lambda:17,0x0c90a01c:lambda:0,0x0c909fa8:lambda:0,
               0x0c34834c:lambda:0xffffffff,0x0c355bdc:lambda:0}
        handles=[self.uc.hook_add(UC_HOOK_CODE,self.firmware_call,fn,a,a) for a,fn in hooks.items()]
        self.uc.reg_write(UC_ARM_REG_R1,2);self.uc.reg_write(UC_ARM_REG_R2,argv);self.uc.reg_write(UC_ARM_REG_R3,io)
        try:return self.call(0x0c3692c0 if dispatch else 0x0c36c698,state)
        finally:
            for h in handles:self.uc.hook_del(h)
    def boot(self):
        self.call(self.code)
        assert self.word(0xe000ed98)==7
        assert self.regions[7]==(0x2015cee7,0x2015cee3)
        assert not self.files
        return struct.unpack('<i',self.uc.mem_read(self.mailbox,4))[0]

class BootstrapTests(unittest.TestCase):
    def test_full_ctor_chain_at_two_rebases(self):
        for code in (0x1c400020,0x1c712348):
            m=Machine(code=code)
            self.assertEqual(m.boot(),0)
            self.assertTrue(m.registered)
            self.assertEqual(m.register_calls,2)
            self.assertIsNotNone(m.installer_fops)
            self.assertEqual(m.word(0x200f5190),0x2f) # stage2 slot4 released, Supervisor slot5 resident
            self.assertEqual(sum(p not in m.frees for p in m.allocations),1)
            buffer=0x200d0000
            m.uc.reg_write(UC_ARM_REG_R1,buffer);m.uc.reg_write(UC_ARM_REG_R2,384)
            self.assertEqual(m.call(m.word(m.fops+8)),384)
            self.assertEqual(m.word(buffer),0x43505331)
    def test_install_registers_launcher_and_renders_manager(self):
        m=Machine();self.assertEqual(m.boot(),0)
        # ITT-NE in client_exchange triggers a Unicorn 2.1.4 memory-hook defect.
        # Keep execution/MPU checks, RO snapshots and stack canaries active.
        m.finish_access_monitor()
        from band11_native_ui_firmware import NativeUI
        ui = NativeUI(m)
        frame=0x200d0000
        for command,arg0 in [(0x4351000a,0),(0x43510002,0),(0x43510002,1),(0x43510002,2)]:
            m.uc.mem_write(frame,struct.pack('<4I',0x43504331,command,arg0,0))
            m.uc.reg_write(UC_ARM_REG_R1,frame);m.uc.reg_write(UC_ARM_REG_R2,16)
            self.assertEqual(m.call(m.word(m.fops+12)),16)
        self.assertEqual(m.launcher,[0xca])
        page=m.pages[0]
        m.uc.reg_write(UC_ARM_REG_R1,0x200b0000);m.uc.reg_write(UC_ARM_REG_R2,0)
        self.assertEqual(m.call(m.word(page+0x4c),page),0)
        self.assertEqual(m.call(m.word(page+0x50),page),0)
        texts=[w.get('text','') for w in m.widgets.values()]
        self.assertTrue(any('Canopus' in t or TARGET in t for t in texts),texts)
        self.assertTrue(any('模块' in t for t in texts),texts)
        rows = [p for p, w in m.widgets.items() if w.get('kind') == 'row']
        self.assertTrue(rows)
        for row in rows:
            self.assertEqual((m.widgets[row]['width'], m.widgets[row]['height']), (204, 96))
            self.assertEqual(ui.get(m.word(row + 0x3c), 0, 0x5a), ui.FONT28)
            if m.word(row + 0x40):
                self.assertEqual(ui.get(m.word(row + 0x40), 0, 0x5a), ui.FONT24)
        before = len(m.widgets)
        self.assertEqual(m.call(m.word(page+0x50),page),0)
        self.assertEqual(len(m.widgets),before, 'resume allocated duplicate native rows')
        self.assertEqual(m.call(m.word(page+0x5c),page),0)
        self.assertEqual(m.call(m.word(page+0x64),page),0)
        self.assertFalse(m.files)

    def test_failures_return_and_release_temporary_storage(self):
        for fault,expected in [('nomem',-303),('missing_stage2',-304),('occupied',-305),
                               ('overlap',-305),('hardware_occupied',-305),('mpu_write_rejected',-305),
                               ('bad_context',-302),('truncated_stage2',-304),('extra_stage2',-304),
                               ('alloc_2',-311),('alloc_3',-312),('alloc_4',-312),('alloc_5',-403),
                               ('bad_supervisor',-313),('wrong_identity',-201),('register_fail',-73733)]:
            with self.subTest(fault=fault):
                m=Machine(fault=fault); self.assertEqual(m.boot(),expected)
                if fault not in ('wrong_identity','register_fail'):
                    self.assertEqual(set(m.allocations),set(m.frees))
                else:
                    self.assertEqual(sum(p not in m.frees for p in m.allocations),1)
                self.assertFalse(m.registered)
    def test_starved_kernel_heap_is_declined_before_the_allocator_panics(self):
        # A 4.100.139 device reported Total:329940 free:56136 largest:51056 for
        # Kmem while restoring an enabled module; mm_malloc 0x0c35056c ends its
        # failure path in _assert(mm_malloc.c, 417, "panic") for that heap, so
        # an over-budget request crashes the watch instead of returning NULL.
        # Every allocation must therefore be declined from mm_mallinfo first.
        # `allocate` asserts that nothing oversized ever reaches mm_memalign.
        m=Machine(); m.kernel_largest=51056
        self.assertEqual(m.boot(),-403) # CANOPUS_ELF_LOAD_NOMEM from stage2
        self.assertEqual(set(m.allocations),set(m.frees))
        self.assertFalse(m.registered)
        # Umem stays the domain for the loader's input and bookkeeping, so a
        # starved Kmem must not have stopped those from being served.
        self.assertTrue(any(p>=0x3c000000 for p in m.allocations))

    def test_real_nsh_exec_and_nonnegative_result_normalization(self):
        m=Machine(fault='kernel_budget')
        m.control=2 # privileged thread using PSP, as the .139 scheduler does
        self.assertEqual(m.nsh_exec(0x0c91e542),17)
        self.assertEqual(m.nsh_exec(0x0c91e542,dispatch=True),0)
        self.assertEqual(m.nsh_exec(m.code,dispatch=True),0)
        self.assertEqual(m.word(m.mailbox),0)
        self.assertTrue(m.registered)
        self.assertLess(m.stack_highwater,3072)
        self.assertLessEqual(m.peak_kernel,105*1024)
        print(f'NSH + native measured stack={m.stack_highwater} bytes; peak Kmem={m.peak_kernel} bytes (modeled firmware calls)')
    def test_firmware_mpu_boot_and_exact_rbar_rlar_equivalence(self):
        m=Machine()
        m.firmware_mpu_setup=True
        m.word(0x200f5190,0)
        log=m.uc.hook_add(UC_HOOK_CODE,m.firmware_call,lambda:0,0x0c720f20,0x0c720f20)
        self.assertEqual(m.call(0x0c91e8e4),0)
        m.uc.hook_del(log)
        self.assertEqual(m.word(0xe000ed94),5)
        self.assertEqual(m.word(0xe000edc0),0x00447722)
        self.assertEqual(m.word(0x200f5190),0x0f)
        self.assertEqual(m.regions[0],(0x00000006,0x0001ffe3))
        self.assertEqual(m.regions[1],(0x0026bc06,0x003cbbe3))
        self.assertEqual(m.regions[2],(0xd0200006,0xd020ffe5))
        self.assertEqual(m.regions[3],(0x20079426,0x2007be23))
        self.assertEqual(m.regions[7][1],0)
        m.firmware_mpu_setup=False
        # Add a synthetic firmware reservation after boot, then load against the real
        # boot register image rather than an all-zero region model.
        m.regions[7]=(0x2015cee7,0x2015cee3);m.word(0xe000ed98,7)
        self.assertEqual(m.boot(),0)
        native=m.regions[5];base=native[0]&~31;size=(native[1]|31)-base+1
        # Configure an independent region using the firmware's own 5-argument
        # function, and compare the resulting bits to b11_map_exec.
        m.uc.reg_write(UC_ARM_REG_R1,base);m.uc.reg_write(UC_ARM_REG_R2,size)
        m.uc.reg_write(UC_ARM_REG_R3,1);m.word(m.stack_top,1)
        self.assertEqual(m.call(0x0c91e594,6),0)
        self.assertEqual(m.regions[6],native)
        m.regions[6]=(0,0)
        # Execute actual relocated HAL save/restore code. These are called with
        # interrupts masked by firmware power transitions; no call is shipped
        # to the device installer. All eight MPU banks must survive, including
        # our resident image. This tests register preservation, not physical sleep.
        m.firmware_mpu_setup=True
        m.uc.reg_write(UC_ARM_REG_PRIMASK,1)
        saved=dict(m.regions)
        m.call(0x0027a7dc) # raw 0x0c0ce608 -> startup SRAM execution alias
        m.regions={i:(0,0) for i in range(8)}
        m.word(0xe000ed94,0);m.word(0xe000edc0,0)
        m.call(0x0027a818) # raw 0x0c0ce644
        self.assertEqual(m.regions,saved)
        self.assertEqual(m.word(0xe000ed94),5)
        self.assertEqual(m.word(0xe000edc0),0x00447722)

if __name__=='__main__':unittest.main()
