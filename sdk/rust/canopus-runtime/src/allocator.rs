//! Optional fixed-arena allocator (CAN-RUST-008).
//!
//! A tiny LIFO bump allocator over a static byte buffer. It needs no global
//! allocator and no `alloc` crate, so modules may use it without pulling in
//! heap machinery — or simply never call it. LIFO-free is deliberate: the
//! rollback model (architecture §10.4) releases resources in reverse order,
//! which is exactly a stack.
//!
//! `live()` / `max_live()` are the leak detectors: after a module is stopped,
//! `live()` must return 0 for a clean test.

/// Arena capacity in bytes.
pub const ARENA_SIZE: usize = 512;

/// Size of the inline length header per allocation.
const HEADER: usize = core::mem::size_of::<usize>();
/// Minimum allocation alignment (pointer-safe on 32-bit ARM and 64-bit host).
const ALIGN: usize = 8;

/// LIFO bump arena. `alloc` returns an 8-aligned pointer; `dealloc` only
/// accepts the most recent allocation (the top of the stack).
#[derive(Debug)]
pub struct BumpArena {
    buf: [u8; ARENA_SIZE],
    cursor: usize,
    live: usize,
    max_live: usize,
}

impl BumpArena {
    pub const fn new() -> Self {
        BumpArena {
            buf: [0u8; ARENA_SIZE],
            cursor: 0,
            live: 0,
            max_live: 0,
        }
    }

    /// Allocates `n` bytes. Returns `None` when the arena cannot fit the
    /// request (allocation never blocks or fails silently).
    pub fn alloc(&mut self, n: usize) -> Option<*mut u8> {
        let size = n.max(1).next_multiple_of(ALIGN) + HEADER;
        if self.cursor + size > ARENA_SIZE {
            return None;
        }
        let p = self.buf.as_ptr() as usize + self.cursor;
        // SAFETY: cursor+size <= ARENA_SIZE, so the header slot is in-bounds
        // and 8-aligned.
        unsafe {
            core::ptr::write(p as *mut usize, size);
        }
        self.cursor += size;
        self.live += 1;
        if self.live > self.max_live {
            self.max_live = self.live;
        }
        Some((p + HEADER) as *mut u8)
    }

    /// Frees the most recent allocation. Returns `false` for an unknown,
    /// already-freed, or non-LIFO pointer.
    pub fn dealloc(&mut self, p: *mut u8) -> bool {
        if p.is_null() {
            return false;
        }
        let header_addr = p as usize - HEADER;
        let size = unsafe { core::ptr::read(header_addr as *const usize) };
        // LIFO check: the allocation must end exactly at the cursor.
        if header_addr + size != self.buf.as_ptr() as usize + self.cursor {
            return false;
        }
        self.cursor -= size;
        self.live -= 1;
        true
    }

    /// Releases everything (rollback / module stop). All outstanding pointers
    /// become invalid.
    pub fn reset(&mut self) {
        self.cursor = 0;
        self.live = 0;
    }

    /// Number of live allocations; must be 0 after a clean module release.
    pub fn live(&self) -> usize {
        self.live
    }

    /// Peak live allocations ever reached (for capacity planning).
    pub fn max_live(&self) -> usize {
        self.max_live
    }

    /// Bytes still free in the arena.
    pub fn available(&self) -> usize {
        ARENA_SIZE - self.cursor
    }
}

impl Default for BumpArena {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn alloc_returns_distinct_aligned_pointers() {
        let mut a = BumpArena::new();
        let p1 = a.alloc(4).unwrap();
        let p2 = a.alloc(4).unwrap();
        assert_ne!(p1, p2);
        assert_eq!(p1 as usize % ALIGN, 0);
        assert_eq!(p2 as usize % ALIGN, 0);
        assert_eq!(a.live(), 2);
    }

    #[test]
    fn lifo_free_only() {
        let mut a = BumpArena::new();
        let p1 = a.alloc(8).unwrap();
        let p2 = a.alloc(8).unwrap();
        // non-LIFO free (p1 while p2 is on top) must be rejected
        assert!(!a.dealloc(p1));
        // LIFO free works
        assert!(a.dealloc(p2));
        assert!(a.dealloc(p1));
        assert_eq!(a.live(), 0);
        // double free rejected
        assert!(!a.dealloc(p1));
    }

    #[test]
    fn arena_full_rejects() {
        let mut a = BumpArena::new();
        // (ARENA_SIZE - HEADER) bytes is the max single allocation
        assert!(a.alloc(ARENA_SIZE - HEADER).is_some());
        assert!(a.alloc(1).is_none());
    }

    #[test]
    fn reset_clears_and_tracks_peak() {
        let mut a = BumpArena::new();
        assert_eq!(a.max_live(), 0);
        let _p = a.alloc(16).unwrap();
        let _q = a.alloc(16).unwrap();
        assert_eq!(a.max_live(), 2);
        a.reset();
        assert_eq!(a.live(), 0);
        assert_eq!(a.available(), ARENA_SIZE);
    }

    #[test]
    fn null_free_rejected() {
        let mut a = BumpArena::new();
        assert!(!a.dealloc(core::ptr::null_mut()));
    }
}
