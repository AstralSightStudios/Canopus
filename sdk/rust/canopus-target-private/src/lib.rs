//! Target-selectable private firmware ABI facade.
//!
//! Private bindings are grouped by exact target under [`targets`]. A consumer
//! selects exactly one `target-*` Cargo feature and imports this crate without
//! knowing which backend module implements the interface. Adding a firmware
//! target means adding a sibling backend and feature, then extending the
//! mutually-exclusive selection checks here; module source must not gain
//! target-specific addresses or imports.

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]
#![allow(non_snake_case)]
#![allow(clippy::missing_safety_doc)]

#[cfg(not(feature = "target-xiaomi-band-10-pro-3-101-030"))]
compile_error!(
    "canopus-target-private requires exactly one target feature; supported: \
     target-xiaomi-band-10-pro-3-101-030"
);

#[cfg(feature = "target-xiaomi-band-10-pro-3-101-030")]
#[path = "targets/xiaomi_band_10_pro_3_101_030.rs"]
mod selected;

#[cfg(feature = "target-xiaomi-band-10-pro-3-101-030")]
pub use selected::*;
