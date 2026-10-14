//! File system and directory operations for Canopus modules.

use canopus_abi::*;
use core::ffi::c_char;

pub struct File {
    fd: i32,
    ctx: *const CanopusContext,
}

impl File {
    pub fn open(ctx: *const CanopusContext, path: &str, flags: u32) -> Result<Self, i32> {
        if ctx.is_null() {
            return Err(-22);
        }
        let fs = unsafe { (*ctx).fs };
        if fs.is_null() {
            return Err(-38);
        }
        let mut path_buf = [0u8; 128];
        if path.len() >= path_buf.len() {
            return Err(-22);
        }
        path_buf[..path.len()].copy_from_slice(path.as_bytes());
        path_buf[path.len()] = 0;

        let fd = unsafe { ((*fs).open)(ctx as *const ContextV1, path_buf.as_ptr() as *const c_char, flags) };
        if fd < 0 {
            Err(fd)
        } else {
            Ok(Self { fd, ctx })
        }
    }

    pub fn read(&mut self, buf: &mut [u8]) -> Result<usize, i32> {
        let fs = unsafe { (*self.ctx).fs };
        let rc = unsafe {
            ((*fs).read)(
                self.ctx as *const ContextV1,
                self.fd,
                buf.as_mut_ptr() as *mut core::ffi::c_void,
                buf.len() as u32,
            )
        };
        if rc < 0 {
            Err(rc)
        } else {
            Ok(rc as usize)
        }
    }

    pub fn write(&mut self, buf: &[u8]) -> Result<usize, i32> {
        let fs = unsafe { (*self.ctx).fs };
        let rc = unsafe {
            ((*fs).write)(
                self.ctx as *const ContextV1,
                self.fd,
                buf.as_ptr() as *const core::ffi::c_void,
                buf.len() as u32,
            )
        };
        if rc < 0 {
            Err(rc)
        } else {
            Ok(rc as usize)
        }
    }

    pub fn seek(&mut self, offset: i64, whence: i32) -> Result<i64, i32> {
        let fs = unsafe { (*self.ctx).fs };
        let rc = unsafe { ((*fs).seek)(self.ctx as *const ContextV1, self.fd, offset, whence) };
        if rc < 0 {
            Err(rc as i32)
        } else {
            Ok(rc)
        }
    }

    pub fn size(&self) -> Result<i64, i32> {
        let fs = unsafe { (*self.ctx).fs };
        let rc = unsafe { ((*fs).size)(self.ctx as *const ContextV1, self.fd) };
        if rc < 0 {
            Err(rc as i32)
        } else {
            Ok(rc)
        }
    }

    pub fn sync(&self) -> Result<(), i32> {
        let fs = unsafe { (*self.ctx).fs };
        let rc = unsafe { ((*fs).sync)(self.ctx as *const ContextV1, self.fd) };
        if rc < 0 {
            Err(rc)
        } else {
            Ok(())
        }
    }
}

impl Drop for File {
    fn drop(&mut self) {
        if !self.ctx.is_null() {
            let fs = unsafe { (*self.ctx).fs };
            if !fs.is_null() {
                unsafe { ((*fs).close)(self.ctx as *const ContextV1, self.fd) };
            }
        }
    }
}

pub fn stat(ctx: *const CanopusContext, path: &str) -> Result<CanopusFsStat, i32> {
    if ctx.is_null() {
        return Err(-22);
    }
    let fs = unsafe { (*ctx).fs };
    if fs.is_null() {
        return Err(-38);
    }
    let mut path_buf = [0u8; 128];
    if path.len() >= path_buf.len() {
        return Err(-22);
    }
    path_buf[..path.len()].copy_from_slice(path.as_bytes());
    path_buf[path.len()] = 0;

    let mut st = CanopusFsStat::default();
    let rc = unsafe { ((*fs).stat)(ctx as *const ContextV1, path_buf.as_ptr() as *const c_char, &mut st) };
    if rc < 0 {
        Err(rc)
    } else {
        Ok(st)
    }
}

pub fn exists(ctx: *const CanopusContext, path: &str) -> bool {
    stat(ctx, path).is_ok()
}

pub fn remove(ctx: *const CanopusContext, path: &str) -> Result<(), i32> {
    if ctx.is_null() {
        return Err(-22);
    }
    let fs = unsafe { (*ctx).fs };
    if fs.is_null() {
        return Err(-38);
    }
    let mut path_buf = [0u8; 128];
    if path.len() >= path_buf.len() {
        return Err(-22);
    }
    path_buf[..path.len()].copy_from_slice(path.as_bytes());
    path_buf[path.len()] = 0;

    let rc = unsafe { ((*fs).remove)(ctx as *const ContextV1, path_buf.as_ptr() as *const c_char) };
    if rc < 0 {
        Err(rc)
    } else {
        Ok(())
    }
}

pub fn rename(ctx: *const CanopusContext, old_path: &str, new_path: &str) -> Result<(), i32> {
    if ctx.is_null() {
        return Err(-22);
    }
    let fs = unsafe { (*ctx).fs };
    if fs.is_null() {
        return Err(-38);
    }
    let mut ob = [0u8; 128];
    let mut nb = [0u8; 128];
    if old_path.len() >= ob.len() || new_path.len() >= nb.len() {
        return Err(-22);
    }
    ob[..old_path.len()].copy_from_slice(old_path.as_bytes());
    nb[..new_path.len()].copy_from_slice(new_path.as_bytes());

    let rc = unsafe {
        ((*fs).rename)(
            ctx as *const ContextV1,
            ob.as_ptr() as *const c_char,
            nb.as_ptr() as *const c_char,
        )
    };
    if rc < 0 {
        Err(rc)
    } else {
        Ok(())
    }
}

pub fn read_all(ctx: *const CanopusContext, path: &str, buffer: &mut [u8]) -> Result<usize, i32> {
    if ctx.is_null() { return Err(-22); }
    let fs = unsafe { (*ctx).fs };
    if fs.is_null() { return Err(-38); }
    let mut pb = [0u8; 128];
    if path.len() >= pb.len() { return Err(-22); }
    pb[..path.len()].copy_from_slice(path.as_bytes());
    let rc = unsafe {
        ((*fs).read_all)(
            ctx as *const ContextV1,
            pb.as_ptr() as *const c_char,
            buffer.as_mut_ptr() as *mut core::ffi::c_void,
            buffer.len() as u32,
        )
    };
    if rc < 0 { Err(rc) } else { Ok(rc as usize) }
}

pub fn write_all(ctx: *const CanopusContext, path: &str, data: &[u8]) -> Result<usize, i32> {
    if ctx.is_null() { return Err(-22); }
    let fs = unsafe { (*ctx).fs };
    if fs.is_null() { return Err(-38); }
    let mut pb = [0u8; 128];
    if path.len() >= pb.len() { return Err(-22); }
    pb[..path.len()].copy_from_slice(path.as_bytes());
    let rc = unsafe {
        ((*fs).write_all)(
            ctx as *const ContextV1,
            pb.as_ptr() as *const c_char,
            data.as_ptr() as *const core::ffi::c_void,
            data.len() as u32,
        )
    };
    if rc < 0 { Err(rc) } else { Ok(rc as usize) }
}
