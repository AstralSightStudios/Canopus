//! Device build of the semantic UI tree (canopus-ui-core).
//!
//! This module is the Phase G compile gate: it proves `canopus-ui-core` builds
//! and links on the ARM no_std target without an allocator, and that a Manager
//! Overview snapshot is constructible on the device stack. Nothing here is
//! executed by the probe; the firmware page backend drives the committed
//! snapshot on the UI thread.

use canopus_ui_core::{Snapshot, TextStyle, Tree};

/// Builds a bounded Manager Overview snapshot on the device stack.
///
/// Returns `None` only if the fixed tree cannot be committed (unreachable for
/// this fixed shape, but handled instead of panicking).
pub fn manager_overview_snapshot() -> Option<Snapshot> {
    let mut tree = Tree::begin();
    tree.navigation_page(1, "Canopus").ok()?;
    tree.text(11, "Version 1 / revision 5", TextStyle::Description)
        .ok()?;
    tree.section(10, "Overview").ok()?;
    tree.status_row(12, "Modules", "2 installed / 1 active")
        .ok()?;
    tree.switch_row(13, "Safe mode", "Off / recovery available", 4, false, true)
        .ok()?;
    tree.end().ok()?;
    tree.end().ok()?;
    tree.commit().ok()
}
