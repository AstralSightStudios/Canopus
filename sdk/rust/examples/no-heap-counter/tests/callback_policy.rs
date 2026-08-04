//! CAN-RUST-009 — Rust callback/resident policy.
//!
//! A retained callback captures the module generation at registration. When
//! the module is deactivated/stopped (generation bumped), a stale callback
//! firing must be a harmless no-op. This mirrors the C hello module's
//! `on_timer` stale-callback guard, exercised end-to-end through the host
//! fake target's timer wheel.

use std::sync::Arc;
use std::sync::atomic::{AtomicU32, Ordering};

use canopus_host_fake::{timer_cancel, timer_register, timer_tick};
use canopus_runtime::*;

#[test]
fn stale_callback_guard_end_to_end() {
    // Generation as an atomic counter (the module owns the generation; the
    // fake target owns the timer).
    let gen = Arc::new(AtomicU32::new(1));
    let captured = gen.load(Ordering::SeqCst);

    let fires = Arc::new(AtomicU32::new(0));
    let f = Arc::clone(&fires);
    let g = Arc::clone(&gen);

    let handle = timer_register(
        move || {
            // stale-callback guard: only act while the captured generation is
            // current (architecture §10.5).
            if g.load(Ordering::SeqCst) == captured {
                f.fetch_add(1, Ordering::SeqCst);
            }
        },
        1,
    )
    .unwrap();

    // active phase: fires are counted
    timer_tick();
    timer_tick();
    assert_eq!(fires.load(Ordering::SeqCst), 2);

    // deactivate/stop bumps the generation
    gen.store(2, Ordering::SeqCst);

    // stale phase: the same registered callback now fires as a no-op
    timer_tick();
    timer_tick();
    timer_tick();
    assert_eq!(fires.load(Ordering::SeqCst), 2);

    assert!(timer_cancel(handle));
}

#[test]
fn resident_policy_has_no_unload_path() {
    // The lifecycle table must never offer a removable unload edge to a
    // resident-class module once active (architecture §10.3, CAN-RUST-009).
    let mut lc = LifecycleV1 {
        state: 0,
        lifecycle_class: LIFECYCLE_ALWAYS_RESIDENT,
        generation: 0,
        reserved: 0,
    };
    assert!(lifecycle_init(&mut lc, LIFECYCLE_ALWAYS_RESIDENT));
    for s in [
        STATE_VERIFIED,
        STATE_INSTALLED,
        STATE_DISABLED,
        STATE_ENABLED,
        STATE_LOADING,
        STATE_PREPARING,
        STATE_READY,
        STATE_ACTIVE,
        STATE_BOOT_RESIDENT,
    ] {
        assert!(lifecycle_transition(&mut lc, s), "to {}", state_name(s));
    }
    assert!(!lifecycle_transition(&mut lc, STATE_STOPPING));
    assert!(!lifecycle_transition(&mut lc, STATE_DRAINING));
    assert!(!lifecycle_transition(&mut lc, STATE_UNLOADED));
}
