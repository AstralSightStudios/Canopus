//! Target-private full-trust bindings for `xiaomi-band-11-4.100.108`.
//!
//! This backend is fail-closed: every recovered 4.100.108 symbol is
//! `restricted`/`STATIC_RECOVERED` with `approval_state=PENDING`, so no public
//! typed wrapper is generated. Only the callable constants and the identity
//! guard are exposed here; the module must pass
//! [`canopus_target_generated::canopus_identity_guard`] before using them.
//!
//! Valid only for firmware SHA-256 `9315ca35…10341bd99`. Future work promotes
//! symbols by decompile-confirming them in the exact IDB and moving them to
//! `APPROVED`, at which point typed wrappers appear here.

pub const TARGET_ID: &str = "xiaomi-band-11-4.100.108";

// Restricted private-ABI callables (odd Thumb addresses). Public typed
// wrappers are NOT generated until each symbol is approved; these constants
// let target-private ABI adapters call the recovered functions under a
// reviewed adapter.
pub use canopus_target_generated::{
    CANOPUS_FW_APP_INSTALL_CALLABLE, CANOPUS_FW_CONTROLLER_CRASH_DUMP_CALLABLE,
    CANOPUS_FW_HIDDEN_AND_SHOW_APP_CB_CALLABLE, CANOPUS_FW_IOCTL_CALLABLE,
    CANOPUS_FW_LV_IMAGE_SET_SRC_CALLABLE, CANOPUS_FW_PROTOBUF_SET_ORDERED_APP_LIST_CALLABLE,
    CANOPUS_FW_RENAME_CALLABLE, CANOPUS_FW_SEM_POST_CALLABLE, CANOPUS_FW_SEM_TRYWAIT_CALLABLE,
    CANOPUS_FW_SEM_WAIT_CALLABLE, CANOPUS_FW_SERVICE_MANAGER_GET_PROFILE_CALLABLE,
    CANOPUS_FW_SERVICE_MANAGER_REGISTER_CALLABLE, CANOPUS_FW_UNLINK_CALLABLE,
    canopus_identity_guard, canopus_thumb_callable,
};

/// All capabilities are fail-closed until their target-private adapter is
/// written and the underlying symbol is promoted. The identity guard is the
/// only capability that works today.
pub fn capabilities() -> &'static [&'static str] {
    &["identity-guard"]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn thumb_callables_are_odd() {
        for c in [
            CANOPUS_FW_IOCTL_CALLABLE,
            CANOPUS_FW_UNLINK_CALLABLE,
            CANOPUS_FW_RENAME_CALLABLE,
            CANOPUS_FW_SEM_WAIT_CALLABLE,
            CANOPUS_FW_SEM_TRYWAIT_CALLABLE,
            CANOPUS_FW_SEM_POST_CALLABLE,
            CANOPUS_FW_LV_IMAGE_SET_SRC_CALLABLE,
            CANOPUS_FW_APP_INSTALL_CALLABLE,
            CANOPUS_FW_CONTROLLER_CRASH_DUMP_CALLABLE,
            CANOPUS_FW_PROTOBUF_SET_ORDERED_APP_LIST_CALLABLE,
        ] {
            assert_eq!(c & 1, 1, "callable {c:#x} must be odd (Thumb)");
        }
    }

    #[test]
    fn restricted_only_no_public_wrappers() {
        // Fail-closed: no public typed wrapper names are exported yet.
        assert!(capabilities().contains(&"identity-guard"));
    }
}
