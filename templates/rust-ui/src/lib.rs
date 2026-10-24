//! {{id}} - Canopus no_std Rust module with one UI page: the Manager's
//! "打开界面" opens it. The C shim `canopus build` links in registers
//! `canopus_module_descriptor` (sdk/c/canopus_module_entry.h) and calls
//! `canopus_mod_stop` on unload.
#![no_std]

use canopus_abi::*;
use core::ffi::{c_char, c_void};
use core::mem::size_of;
use core::sync::atomic::{AtomicU32, Ordering};

const EV_ADD: u32 = 1;
const EV_TOGGLE: u32 = 2;

static CLICKS: AtomicU32 = AtomicU32::new(0);
static CHECKED: AtomicU32 = AtomicU32::new(0);

/// The context with its service objects (`struct canopus_context_v1`).
fn context<'a>(ctx: *const ContextV1) -> Option<&'a CanopusContext> {
    unsafe { (ctx as *const CanopusContext).as_ref() }
}

/// A NUL-terminated literal as the (pointer, byte length) pair the UI takes.
fn text(s: &'static [u8]) -> (*const c_char, u32) {
    (s.as_ptr() as *const c_char, s.len() as u32 - 1)
}

fn format_u32(out: &mut [u8; 12], mut value: u32) -> u32 {
    let mut digits = [0u8; 10];
    let mut n = 0;
    loop {
        digits[n] = b'0' + (value % 10) as u8;
        n += 1;
        value /= 10;
        if value == 0 {
            break;
        }
    }
    for i in 0..n {
        out[i] = digits[n - 1 - i];
    }
    out[n] = 0;
    n as u32
}

fn props<T>(p: &T) -> *const c_void {
    p as *const T as *const c_void
}

/// Builds the whole page again and commits it; the host redraws only what changed.
unsafe fn render(ui_api: &CanopusUiObj, ui: *mut c_void) -> i32 {
    let mut tree: *mut c_void = core::ptr::null_mut();
    let rc = unsafe { (ui_api.tree_begin)(ui, &mut tree) };
    if rc != UI_OK {
        return rc;
    }
    let mut count = [0u8; 12];
    let count_len = format_u32(&mut count, CLICKS.load(Ordering::Relaxed));
    let (title, title_len) = text(b"{{id}}\0");
    let page = UiNavigationPagePropsV1 {
        struct_size: size_of::<UiNavigationPagePropsV1>() as u32,
        title,
        title_len,
    };
    let (label, label_len) = text("点击次数\0".as_bytes());
    let status = UiStatusRowPropsV1 {
        struct_size: size_of::<UiStatusRowPropsV1>() as u32,
        label,
        label_len,
        value: count.as_ptr() as *const c_char,
        value_len: count_len,
    };
    let (label, label_len) = text("点击\0".as_bytes());
    let (detail, detail_len) = text("点击一次加一\0".as_bytes());
    let add = UiActionRowPropsV1 {
        struct_size: size_of::<UiActionRowPropsV1>() as u32,
        label,
        label_len,
        detail,
        detail_len,
        event_id: EV_ADD,
        enabled: 1,
    };
    let (label, label_len) = text("开关\0".as_bytes());
    let (detail, detail_len) = text(b"\0");
    let toggle = UiSwitchRowPropsV1 {
        struct_size: size_of::<UiSwitchRowPropsV1>() as u32,
        label,
        label_len,
        detail,
        detail_len,
        event_id: EV_TOGGLE,
        checked: CHECKED.load(Ordering::Relaxed),
        enabled: 1,
    };
    unsafe {
        let mut rc = (ui_api.navigation_page)(tree, 1, props(&page));
        if rc == UI_OK {
            rc = (ui_api.status_row)(tree, 2, props(&status));
        }
        if rc == UI_OK {
            rc = (ui_api.action_row)(tree, 3, props(&add));
        }
        if rc == UI_OK {
            rc = (ui_api.switch_row)(tree, 4, props(&toggle));
        }
        if rc == UI_OK {
            rc = (ui_api.end)(tree); // page
        }
        if rc == UI_OK {
            return (ui_api.tree_commit)(tree);
        }
        (ui_api.tree_abort)(tree);
        rc
    }
}

/// Runs on the UI task: the first tree must be committed before it returns.
unsafe extern "C" fn on_open(
    ctx: *const ContextV1,
    ui: *mut c_void,
    _user_data: *mut c_void,
) -> i32 {
    match context(ctx) {
        Some(c) if !c.ui.is_null() => unsafe { render(&*c.ui, ui) },
        _ => UI_ERR_STATE,
    }
}

unsafe extern "C" fn on_event(
    ctx: *const ContextV1,
    ui: *mut c_void,
    event: *const UiEventV1,
    _user_data: *mut c_void,
) -> i32 {
    let (Some(c), Some(event)) = (context(ctx), unsafe { event.as_ref() }) else {
        return UI_ERR_ARGUMENT;
    };
    if event.type_ != UI_EVENT_ACTIVATE && event.type_ != UI_EVENT_VALUE_CHANGED {
        return UI_OK;
    }
    match event.event_id {
        EV_ADD => {
            CLICKS.fetch_add(1, Ordering::Relaxed);
        }
        EV_TOGGLE => {
            CHECKED.fetch_xor(1, Ordering::Relaxed);
        }
        _ => return UI_ERR_ARGUMENT,
    }
    unsafe { render(&*c.ui, ui) }
}

/// No on_back: the title's back closes the page. Supply one returning 0 to
/// handle it yourself, e.g. to pop a sub-page.
static ENTRY: UiEntryV1 = UiEntryV1 {
    struct_size: size_of::<UiEntryV1>() as u32,
    label: b"{{id}}\0".as_ptr() as *const c_char,
    on_open: Some(on_open),
    on_event: Some(on_event),
    on_back: None,
    on_close: None,
    user_data: core::ptr::null_mut(),
};

#[no_mangle]
pub extern "C" fn canopus_mod_prepare(_ctx: *const ContextV1) -> i32 {
    0
}

extern "C" fn activate(ctx: *const ContextV1) -> i32 {
    match context(ctx) {
        Some(c) if !c.ui.is_null() => unsafe { ((*c.ui).set_entry)(ctx, &ENTRY) },
        _ => -1,
    }
}

extern "C" fn deactivate(ctx: *const ContextV1) -> i32 {
    if let Some(c) = context(ctx) {
        if !c.ui.is_null() {
            unsafe { ((*c.ui).set_entry)(ctx, core::ptr::null()) };
        }
    }
    0
}

#[no_mangle]
pub extern "C" fn canopus_mod_stop(ctx: *const ContextV1) -> i32 {
    deactivate(ctx)
}

extern "C" fn query(_w: *mut StatusWriterV1) -> i32 {
    0
}

const fn pack<const N: usize>(s: &[u8]) -> [u8; N] {
    let mut out = [0u8; N];
    let mut i = 0;
    while i < s.len() && i < N {
        out[i] = s[i];
        i += 1;
    }
    out
}

#[no_mangle]
pub static canopus_module_descriptor: ModuleDescriptorV1 = ModuleDescriptorV1 {
    struct_size: size_of::<ModuleDescriptorV1>() as u32,
    abi_major: ABI_MAJOR,
    abi_minor: ABI_MINOR,
    flags: 0,
    module_id: pack(b"{{id}}"),
    module_version: pack(b"0.1.0"),
    build_id: pack(b"{{id}}-0.1.0"),
    // An exact target id, or "portable-thumbv8m"; set by `canopus build`.
    target_id: pack(env!("CANOPUS_TARGET_ID").as_bytes()),
    prepare: Some(canopus_mod_prepare),
    activate: Some(activate),
    deactivate: Some(deactivate),
    stop: Some(canopus_mod_stop),
    query: Some(query),
    publish_native_app: None,
    publish_native_app_stage: None,
};

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
