//! Target-private full-trust ABI facade.
//!
//! Band 10 Pro and Band 11 select exact firmware backends. Band 9 Pro and
//! Band 9 retain compile-only candidate facades. Private static recovery and
//! physical device validation are recorded separately in each target pack.

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]
#![allow(non_snake_case)]
#![allow(clippy::missing_safety_doc)]

#[cfg(feature = "target-xiaomi-band-10-pro-3-101-036")]
#[path = "generated_symbols_1036.rs"]
pub mod generated_symbols;

#[cfg(feature = "target-xiaomi-band-10-pro-3-101-043")]
#[path = "generated_symbols_1043.rs"]
pub mod generated_symbols;

#[cfg(not(any(
    feature = "target-xiaomi-band-10-pro-3-101-036",
    feature = "target-xiaomi-band-10-pro-3-101-043",
    feature = "target-xiaomi-band-9-pro-3-1-175",
    feature = "target-xiaomi-band-11-4-100-139",
    feature = "target-xiaomi-band-11-4-100-155",
    feature = "target-xiaomi-band-9-3-1-32"
)))]
compile_error!("canopus-target-private requires exactly one target-feature");

#[cfg(any(
    all(
        feature = "target-xiaomi-band-10-pro-3-101-036",
        any(
            feature = "target-xiaomi-band-10-pro-3-101-043",
            feature = "target-xiaomi-band-9-pro-3-1-175",
            feature = "target-xiaomi-band-11-4-100-139",
            feature = "target-xiaomi-band-9-3-1-32"
        )
    ),
    all(
        feature = "target-xiaomi-band-10-pro-3-101-043",
        any(
            feature = "target-xiaomi-band-9-pro-3-1-175",
            feature = "target-xiaomi-band-11-4-100-139",
            feature = "target-xiaomi-band-9-3-1-32"
        )
    ),
    all(
        feature = "target-xiaomi-band-9-pro-3-1-175",
        any(
            feature = "target-xiaomi-band-11-4-100-139",
            feature = "target-xiaomi-band-9-3-1-32"
        )
    ),
    all(
        feature = "target-xiaomi-band-11-4-100-139",
        feature = "target-xiaomi-band-9-3-1-32"
    )
))]
compile_error!("canopus-target-private requires exactly one target feature");

#[cfg(feature = "target-xiaomi-band-10-pro-3-101-036")]
#[path = "targets/xiaomi_band_10_pro_3_101_036.rs"]
mod selected;

#[cfg(feature = "target-xiaomi-band-10-pro-3-101-043")]
#[path = "targets/xiaomi_band_10_pro_3_101_043.rs"]
mod selected;

#[cfg(all(
    feature = "target-xiaomi-band-11-4-100-155",
    any(
        feature = "target-xiaomi-band-10-pro-3-101-036",
        feature = "target-xiaomi-band-10-pro-3-101-043",
        feature = "target-xiaomi-band-9-pro-3-1-175",
        feature = "target-xiaomi-band-11-4-100-139",
        feature = "target-xiaomi-band-9-3-1-32"
    )
))]
compile_error!("canopus-target-private requires exactly one target feature");

#[cfg(any(
    feature = "target-xiaomi-band-11-4-100-139",
    feature = "target-xiaomi-band-11-4-100-155"
))]
#[path = "targets/xiaomi_band_11_4_100_139.rs"]
mod selected;

#[cfg(any(
    feature = "target-xiaomi-band-9-pro-3-1-175",
    feature = "target-xiaomi-band-9-3-1-32"
))]
#[path = "targets/static_candidate.rs"]
mod selected;

#[cfg(any(
    feature = "target-xiaomi-band-10-pro-3-101-036",
    feature = "target-xiaomi-band-10-pro-3-101-043",
    feature = "target-xiaomi-band-9-pro-3-1-175",
    feature = "target-xiaomi-band-11-4-100-139",
    feature = "target-xiaomi-band-11-4-100-155",
    feature = "target-xiaomi-band-9-3-1-32"
))]
pub use selected::*;
