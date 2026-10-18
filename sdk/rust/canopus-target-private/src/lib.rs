//! Target-private full-trust ABI facade.
//!
//! Band 10 Pro and Band 11 select exact firmware backends. Private static
//! recovery and physical device validation are recorded separately in each
//! target pack.
//!
//! Supported target devices:
//! - Xiaomi Band 10 Pro (3.101.036): `targets/xiaomi_band_10_pro_3_101_036.rs`
//! - Xiaomi Band 10 Pro (3.101.043): `targets/xiaomi_band_10_pro_3_101_043.rs`
//! - Xiaomi Band 11 (4.100.139): `targets/xiaomi_band_11_4_100_139.rs`
//! - Xiaomi Band 11 (4.100.155): `targets/xiaomi_band_11_4_100_155.rs`
//! - Xiaomi Band 11 (4.100.177): `targets/xiaomi_band_11_4_100_177.rs`

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]
#![allow(non_snake_case)]
#![allow(clippy::missing_safety_doc)]

#[cfg(feature = "target-xiaomi-band-10-pro-3-101-036")]
#[path = "generated_symbols_xiaomi_band_10_pro_3_101_036.rs"]
pub mod generated_symbols;

#[cfg(feature = "target-xiaomi-band-10-pro-3-101-043")]
#[path = "generated_symbols_xiaomi_band_10_pro_3_101_043.rs"]
pub mod generated_symbols;

#[cfg(any(
    feature = "target-xiaomi-band-10-pro-3-101-036",
    feature = "target-xiaomi-band-10-pro-3-101-043"
))]
pub mod band10_pro;

#[cfg(any(
    feature = "target-xiaomi-band-11-4-100-139",
    feature = "target-xiaomi-band-11-4-100-155",
    feature = "target-xiaomi-band-11-4-100-177"
))]
pub mod band11;

#[cfg(feature = "target-xiaomi-band-10-pro-3-101-036")]
#[path = "targets/xiaomi_band_10_pro_3_101_036.rs"]
mod selected;

#[cfg(feature = "target-xiaomi-band-10-pro-3-101-043")]
#[path = "targets/xiaomi_band_10_pro_3_101_043.rs"]
mod selected;

#[cfg(feature = "target-xiaomi-band-11-4-100-139")]
#[path = "targets/xiaomi_band_11_4_100_139.rs"]
mod selected;

#[cfg(feature = "target-xiaomi-band-11-4-100-155")]
#[path = "targets/xiaomi_band_11_4_100_155.rs"]
mod selected;

#[cfg(feature = "target-xiaomi-band-11-4-100-177")]
#[path = "targets/xiaomi_band_11_4_100_177.rs"]
mod selected;

pub use selected::*;
