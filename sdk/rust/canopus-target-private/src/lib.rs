//! Target-private full-trust ABI facade.
//!
//! The two Band 10 Pro backends are approved build backends. The Band 9 Pro,
//! Band 11, and Band 9 backends deliberately select a compile-only static
//! candidate facade until their exact ABI/LVGL/loader gates are approved.

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
    feature = "target-xiaomi-band-11-4-100-108",
    feature = "target-xiaomi-band-9-3-1-32"
)))]
compile_error!("canopus-target-private requires exactly one target-feature");

#[cfg(any(
    all(
        feature = "target-xiaomi-band-10-pro-3-101-036",
        any(
            feature = "target-xiaomi-band-10-pro-3-101-043",
            feature = "target-xiaomi-band-9-pro-3-1-175",
            feature = "target-xiaomi-band-11-4-100-108",
            feature = "target-xiaomi-band-9-3-1-32"
        )
    ),
    all(
        feature = "target-xiaomi-band-10-pro-3-101-043",
        any(
            feature = "target-xiaomi-band-9-pro-3-1-175",
            feature = "target-xiaomi-band-11-4-100-108",
            feature = "target-xiaomi-band-9-3-1-32"
        )
    ),
    all(
        feature = "target-xiaomi-band-9-pro-3-1-175",
        any(
            feature = "target-xiaomi-band-11-4-100-108",
            feature = "target-xiaomi-band-9-3-1-32"
        )
    ),
    all(
        feature = "target-xiaomi-band-11-4-100-108",
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

#[cfg(any(
    feature = "target-xiaomi-band-9-pro-3-1-175",
    feature = "target-xiaomi-band-11-4-100-108",
    feature = "target-xiaomi-band-9-3-1-32"
))]
#[path = "targets/static_candidate.rs"]
mod selected;

#[cfg(any(
    feature = "target-xiaomi-band-10-pro-3-101-036",
    feature = "target-xiaomi-band-10-pro-3-101-043",
    feature = "target-xiaomi-band-9-pro-3-1-175",
    feature = "target-xiaomi-band-11-4-100-108",
    feature = "target-xiaomi-band-9-3-1-32"
))]
pub use selected::*;
