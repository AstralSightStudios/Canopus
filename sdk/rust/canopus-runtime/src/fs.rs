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

        let fd = unsafe {
            ((*fs).open)(
                ctx as *const ContextV1,
                path_buf.as_ptr() as *const c_char,
                flags,
            )
        };
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
        if rc < 0 { Err(rc) } else { Ok(rc as usize) }
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
        if rc < 0 { Err(rc) } else { Ok(rc as usize) }
    }

    pub fn seek(&mut self, offset: i64, whence: i32) -> Result<i64, i32> {
        let fs = unsafe { (*self.ctx).fs };
        let rc = unsafe { ((*fs).seek)(self.ctx as *const ContextV1, self.fd, offset, whence) };
        if rc < 0 { Err(rc as i32) } else { Ok(rc) }
    }

    pub fn size(&self) -> Result<i64, i32> {
        let fs = unsafe { (*self.ctx).fs };
        let rc = unsafe { ((*fs).size)(self.ctx as *const ContextV1, self.fd) };
        if rc < 0 { Err(rc as i32) } else { Ok(rc) }
    }

    pub fn sync(&self) -> Result<(), i32> {
        let fs = unsafe { (*self.ctx).fs };
        let rc = unsafe { ((*fs).sync)(self.ctx as *const ContextV1, self.fd) };
        if rc < 0 { Err(rc) } else { Ok(()) }
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
    let rc = unsafe {
        ((*fs).stat)(
            ctx as *const ContextV1,
            path_buf.as_ptr() as *const c_char,
            &mut st,
        )
    };
    if rc < 0 { Err(rc) } else { Ok(st) }
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
    if rc < 0 { Err(rc) } else { Ok(()) }
}

/// Removes an empty directory (FS service version 2).
///
/// Returns `-39` for a nonempty directory, `-20` for a file, or `-38` when
/// the Supervisor has no directory deletion service. Contents are never removed.
pub fn rmdir(ctx: *const CanopusContext, path: &str) -> Result<(), i32> {
    if ctx.is_null() {
        return Err(-22);
    }
    // Older Supervisors have a shorter FS object; check before reading rmdir.
    if !unsafe { services_has(ctx as *const ContextV1, SVC_FS, 2) } {
        return Err(-38);
    }
    let fs = unsafe { (*ctx).fs };
    if fs.is_null() {
        return Err(-38);
    }
    let mut path_buf = [0u8; 256];
    if path.is_empty() || path.len() >= path_buf.len() || path.as_bytes().contains(&0) {
        return Err(-22);
    }
    path_buf[..path.len()].copy_from_slice(path.as_bytes());
    let rc = unsafe { ((*fs).rmdir)(ctx as *const ContextV1, path_buf.as_ptr() as *const c_char) };
    if rc < 0 { Err(rc) } else { Ok(()) }
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
    if rc < 0 { Err(rc) } else { Ok(()) }
}

pub fn read_all(ctx: *const CanopusContext, path: &str, buffer: &mut [u8]) -> Result<usize, i32> {
    if ctx.is_null() {
        return Err(-22);
    }
    let fs = unsafe { (*ctx).fs };
    if fs.is_null() {
        return Err(-38);
    }
    let mut pb = [0u8; 128];
    if path.len() >= pb.len() {
        return Err(-22);
    }
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
    if ctx.is_null() {
        return Err(-22);
    }
    let fs = unsafe { (*ctx).fs };
    if fs.is_null() {
        return Err(-38);
    }
    let mut pb = [0u8; 128];
    if path.len() >= pb.len() {
        return Err(-22);
    }
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

pub fn data_path(ctx: *const CanopusContext, buf: &mut [u8]) -> Result<&str, i32> {
    if ctx.is_null() {
        return Err(-22);
    }
    let fs = unsafe { (*ctx).fs };
    if fs.is_null() {
        return Err(-38);
    }
    if buf.is_empty() {
        return Err(-22);
    }
    let rc = unsafe {
        ((*fs).get_data_path)(
            ctx as *const ContextV1,
            buf.as_mut_ptr() as *mut c_char,
            buf.len() as u32,
        )
    };
    if rc < 0 {
        Err(rc)
    } else {
        core::str::from_utf8(&buf[..rc as usize]).map_err(|_| -5)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rmdir_checks_version_before_accessing_the_appended_member() {
        let mut services = ServicesV1Head {
            struct_size: core::mem::size_of::<ServicesV1Head>() as u32,
            group_version: [0; SVC_GROUP_COUNT as usize],
        };
        services.group_version[SVC_FS as usize] = 1;
        // Context fields are integers and nullable raw pointers. The FS pointer
        // must not be read until the Supervisor advertises the new object tail.
        let mut ctx: CanopusContext = unsafe { core::mem::zeroed() };
        ctx.struct_size = core::mem::size_of::<CanopusContext>() as u32;
        ctx.services = &services as *const _ as *const core::ffi::c_void;
        ctx.fs = core::ptr::dangling();
        assert_eq!(rmdir(core::ptr::null(), "cache"), Err(-22));
        assert_eq!(rmdir(&ctx, "cache"), Err(-38));
        services.group_version[SVC_FS as usize] = 2;
        let long_path = [b'a'; 256];
        for invalid in ["", "cache\0other", core::str::from_utf8(&long_path).unwrap()] {
            assert_eq!(rmdir(&ctx, invalid), Err(-22));
        }
        ctx.fs = core::ptr::null();
        assert_eq!(rmdir(&ctx, "cache"), Err(-38));
    }
}

pub fn resolve_data_path<'a>(
    ctx: *const CanopusContext,
    relative: &str,
    out: &'a mut [u8],
) -> Result<&'a str, i32> {
    let mut dir_buf = [0u8; 96];
    let dir = data_path(ctx, &mut dir_buf)?;
    let needed = dir.len() + 1 + relative.len();
    if out.len() < needed {
        return Err(-22);
    }
    out[..dir.len()].copy_from_slice(dir.as_bytes());
    out[dir.len()] = b'/';
    out[dir.len() + 1..needed].copy_from_slice(relative.as_bytes());
    core::str::from_utf8(&out[..needed]).map_err(|_| -5)
}
