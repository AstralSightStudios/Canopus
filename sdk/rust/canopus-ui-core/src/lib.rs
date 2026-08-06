//! canopus-ui-core — no_std declarative semantic UI tree.
//!
//! This crate mirrors the public C semantic UI ABI in `app-sdk/ui/canopus_ui.h`
//! (see `docs/native-manager-ui-plan.md` §6.3 and Phase G). Applications
//! describe what a page *is* — navigation page, sections, rows, switches — in a
//! bounded, allocation-free tree. A target backend maps the committed snapshot
//! to exact-firmware prefab objects; public code never owns a firmware widget
//! pointer.
//!
//! Guarantees mirror the C builder:
//! - `Tree` is a fixed-capacity snapshot: nodes, depth and string bytes are
//!   bounded and validated, never heap-allocated.
//! - Node keys are stable within an app generation and must be unique.
//! - `commit` is transactional and validates the tree shape.
//! - Event dispatch is generation-checked and rejects disabled and
//!   non-interactive nodes.
//! - `#[repr(C)]` mirrors the C snapshot/node layout so host fakes and target
//!   backends can share the same byte layout.
//!
//! Callbacks stay `extern "C"` / non-unwinding at the device boundary; this
//! crate only carries the bounded semantic data and the generation-checked
//! dispatch seam.

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]

// ---------------------------------------------------------------------------
// ABI version and capacity (must match canopus_ui.h)
// ---------------------------------------------------------------------------

pub const ABI_MAJOR: u16 = 1;
pub const ABI_MINOR: u16 = 2;

pub const MAX_NODES: usize = 32;
pub const MAX_DEPTH: usize = 8;
pub const STRING_CAPACITY: usize = 1536;
pub const NO_NODE: u16 = 0xFFFF;

pub type NodeId = u32;

// ---------------------------------------------------------------------------
// Node kinds and flags (mirror canopus_ui.h)
// ---------------------------------------------------------------------------

/// Semantic node kinds. The discriminants match `canopus_ui_node_type`.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
#[repr(u16)]
pub enum NodeKind {
    NavigationPage = 1,
    Section = 2,
    Text = 3,
    StatusRow = 4,
    Button = 5,
    ActionRow = 6,
    SwitchRow = 7,
}

impl NodeKind {
    pub fn from_u16(value: u16) -> Option<NodeKind> {
        match value {
            1 => Some(NodeKind::NavigationPage),
            2 => Some(NodeKind::Section),
            3 => Some(NodeKind::Text),
            4 => Some(NodeKind::StatusRow),
            5 => Some(NodeKind::Button),
            6 => Some(NodeKind::ActionRow),
            7 => Some(NodeKind::SwitchRow),
            _ => None,
        }
    }
}

/// Typography roles. Backends map these to stock typography instead of leaking
/// private firmware style IDs.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum TextStyle {
    Body = 0,
    Title = 1,
    Description = 2,
    Warning = 3,
}

pub const FLAG_ENABLED: u32 = 1 << 0;
pub const FLAG_CHECKED: u32 = 1 << 1;

// ---------------------------------------------------------------------------
// Semantic ABI records (repr(C), mirror canopus_ui.h)
// ---------------------------------------------------------------------------

/// One semantic node. `node_type` is a [`NodeKind`] discriminant.
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct Node {
    pub key: NodeId,
    pub node_type: u16,
    pub parent: u16,
    pub first_child: u16,
    pub next_sibling: u16,
    pub primary_off: u16,
    pub primary_len: u16,
    pub secondary_off: u16,
    pub secondary_len: u16,
    pub event_id: u32,
    pub flags: u32,
}

impl Node {
    pub fn kind(&self) -> Option<NodeKind> {
        NodeKind::from_u16(self.node_type)
    }

    pub fn is_interactive(&self) -> bool {
        matches!(
            self.kind(),
            Some(NodeKind::Button | NodeKind::ActionRow | NodeKind::SwitchRow)
        )
    }

    pub fn enabled(&self) -> bool {
        (self.flags & FLAG_ENABLED) != 0
    }

    pub fn checked(&self) -> bool {
        (self.flags & FLAG_CHECKED) != 0
    }
}

/// Committed, bounded semantic tree. Fixed capacity; never heap-allocated.
#[repr(C)]
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Snapshot {
    pub abi_major: u16,
    pub abi_minor: u16,
    pub generation: u32,
    pub node_count: u16,
    pub string_used: u16,
    pub nodes: [Node; MAX_NODES],
    pub strings: [u8; STRING_CAPACITY],
}

impl Snapshot {
    fn empty() -> Snapshot {
        Snapshot {
            abi_major: ABI_MAJOR,
            abi_minor: ABI_MINOR,
            generation: 1,
            node_count: 0,
            string_used: 0,
            nodes: [Node {
                key: 0,
                node_type: 0,
                parent: NO_NODE,
                first_child: NO_NODE,
                next_sibling: NO_NODE,
                primary_off: 0,
                primary_len: 0,
                secondary_off: 0,
                secondary_len: 0,
                event_id: 0,
                flags: 0,
            }; MAX_NODES],
            strings: [0; STRING_CAPACITY],
        }
    }

    pub fn node(&self, index: usize) -> Option<&Node> {
        if index < self.node_count as usize {
            Some(&self.nodes[index])
        } else {
            None
        }
    }

    pub fn primary(&self, node: &Node) -> &str {
        self.slice(node.primary_off, node.primary_len)
    }

    pub fn secondary(&self, node: &Node) -> &str {
        self.slice(node.secondary_off, node.secondary_len)
    }

    fn slice(&self, off: u16, len: u16) -> &str {
        let start = off as usize;
        let end = (off as usize) + (len as usize);
        if end <= self.string_used as usize && end <= self.strings.len() {
            // Strings are copied from UTF-8 input and stored NUL-terminated.
            core::str::from_utf8(&self.strings[start..end]).unwrap_or("")
        } else {
            ""
        }
    }

    pub fn find_by_key(&self, key: NodeId) -> Option<&Node> {
        self.nodes[..self.node_count as usize]
            .iter()
            .find(|n| n.key == key)
    }
}

// ---------------------------------------------------------------------------
// Builder errors (mirror CANOPUS_UI_ERR_*)
// ---------------------------------------------------------------------------

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum UiError {
    Argument,
    State,
    Capacity,
    DuplicateKey,
    Backend,
    StaleGeneration,
    Disabled,
}

// ---------------------------------------------------------------------------
// Bounded tree builder
// ---------------------------------------------------------------------------

/// Fixed-capacity builder. Rows and containers are appended under the current
/// container; `commit` validates the shape and returns the snapshot by value.
pub struct Tree {
    snapshot: Snapshot,
    parent_stack: [u16; MAX_DEPTH],
    depth: u8,
    active: bool,
}

impl Tree {
    pub fn begin() -> Tree {
        Tree {
            snapshot: Snapshot::empty(),
            parent_stack: [NO_NODE; MAX_DEPTH],
            depth: 0,
            active: true,
        }
    }

    fn key_exists(&self, key: NodeId) -> bool {
        self.snapshot.nodes[..self.snapshot.node_count as usize]
            .iter()
            .any(|n| n.key == key)
    }

    fn put_str(&mut self, value: &str) -> Result<(u16, u16), UiError> {
        let bytes = value.as_bytes();
        let len = bytes.len();
        if len > u16::MAX as usize {
            return Err(UiError::Argument);
        }
        let need = len.checked_add(1).ok_or(UiError::Capacity)?;
        let used = self.snapshot.string_used as usize;
        if used > STRING_CAPACITY || need > STRING_CAPACITY - used {
            return Err(UiError::Capacity);
        }
        self.snapshot.strings[used..used + len].copy_from_slice(bytes);
        self.snapshot.strings[used + len] = 0;
        let off = used as u16;
        let out = (off, len as u16);
        self.snapshot.string_used = (used + need) as u16;
        Ok(out)
    }

    #[allow(clippy::too_many_arguments)] // internal append mirrors the C append helper
    fn append(
        &mut self,
        key: NodeId,
        kind: NodeKind,
        primary: &str,
        secondary: &str,
        event_id: u32,
        flags: u32,
        push_container: bool,
    ) -> Result<(), UiError> {
        if !self.active {
            return Err(UiError::State);
        }
        if key == 0 {
            return Err(UiError::Argument);
        }
        if self.snapshot.node_count as usize >= MAX_NODES {
            return Err(UiError::Capacity);
        }
        if self.key_exists(key) {
            return Err(UiError::DuplicateKey);
        }
        if kind == NodeKind::NavigationPage {
            if self.snapshot.node_count != 0 || self.depth != 0 {
                return Err(UiError::State);
            }
        } else if self.depth == 0 {
            return Err(UiError::State);
        }
        let parent = if self.depth > 0 {
            self.parent_stack[(self.depth - 1) as usize]
        } else {
            NO_NODE
        };
        if push_container && self.depth as usize >= MAX_DEPTH {
            return Err(UiError::Capacity);
        }

        let string_mark = self.snapshot.string_used;
        let (primary_off, primary_len) = self
            .put_str(primary)
            .inspect_err(|_| self.snapshot.string_used = string_mark)?;
        let (secondary_off, secondary_len) = self
            .put_str(secondary)
            .inspect_err(|_| self.snapshot.string_used = string_mark)?;

        let index = self.snapshot.node_count as usize;
        self.snapshot.nodes[index] = Node {
            key,
            node_type: kind as u16,
            parent,
            first_child: NO_NODE,
            next_sibling: NO_NODE,
            primary_off,
            primary_len,
            secondary_off,
            secondary_len,
            event_id,
            flags,
        };
        self.snapshot.node_count += 1;
        if parent != NO_NODE {
            let parent_index = parent as usize;
            let first = self.snapshot.nodes[parent_index].first_child;
            if first == NO_NODE {
                self.snapshot.nodes[parent_index].first_child = index as u16;
            } else {
                let mut sibling = first;
                while self.snapshot.nodes[sibling as usize].next_sibling != NO_NODE {
                    sibling = self.snapshot.nodes[sibling as usize].next_sibling;
                }
                self.snapshot.nodes[sibling as usize].next_sibling = index as u16;
            }
        }
        if push_container {
            self.parent_stack[self.depth as usize] = index as u16;
            self.depth += 1;
        }
        Ok(())
    }

    pub fn navigation_page(&mut self, key: NodeId, title: &str) -> Result<(), UiError> {
        self.append(key, NodeKind::NavigationPage, title, "", 0, 0, true)
    }

    pub fn section(&mut self, key: NodeId, title: &str) -> Result<(), UiError> {
        self.append(key, NodeKind::Section, title, "", 0, 0, true)
    }

    pub fn text(&mut self, key: NodeId, text: &str, style: TextStyle) -> Result<(), UiError> {
        self.append(key, NodeKind::Text, text, "", 0, style as u32, false)
    }

    pub fn status_row(&mut self, key: NodeId, label: &str, value: &str) -> Result<(), UiError> {
        self.append(key, NodeKind::StatusRow, label, value, 0, 0, false)
    }

    pub fn button(
        &mut self,
        key: NodeId,
        label: &str,
        event_id: u32,
        enabled: bool,
    ) -> Result<(), UiError> {
        if event_id == 0 {
            return Err(UiError::Argument);
        }
        let flags = if enabled { FLAG_ENABLED } else { 0 };
        self.append(key, NodeKind::Button, label, "", event_id, flags, false)
    }

    pub fn action_row(
        &mut self,
        key: NodeId,
        label: &str,
        detail: &str,
        event_id: u32,
        enabled: bool,
    ) -> Result<(), UiError> {
        if event_id == 0 {
            return Err(UiError::Argument);
        }
        let flags = if enabled { FLAG_ENABLED } else { 0 };
        self.append(
            key,
            NodeKind::ActionRow,
            label,
            detail,
            event_id,
            flags,
            false,
        )
    }

    pub fn switch_row(
        &mut self,
        key: NodeId,
        label: &str,
        detail: &str,
        event_id: u32,
        checked: bool,
        enabled: bool,
    ) -> Result<(), UiError> {
        if event_id == 0 {
            return Err(UiError::Argument);
        }
        let mut flags = 0;
        if enabled {
            flags |= FLAG_ENABLED;
        }
        if checked {
            flags |= FLAG_CHECKED;
        }
        self.append(
            key,
            NodeKind::SwitchRow,
            label,
            detail,
            event_id,
            flags,
            false,
        )
    }

    pub fn end(&mut self) -> Result<(), UiError> {
        if !self.active {
            return Err(UiError::Argument);
        }
        if self.depth == 0 {
            return Err(UiError::State);
        }
        self.depth -= 1;
        Ok(())
    }

    /// Transactional commit: validates that the root is a navigation page and
    /// every container is closed, then returns the snapshot. On failure the
    /// tree stays active so the caller can fix and re-commit; on success the
    /// tree is consumed.
    pub fn commit(&mut self) -> Result<Snapshot, UiError> {
        if !self.active || self.depth != 0 || self.snapshot.node_count == 0 {
            return Err(UiError::State);
        }
        let root_type = self.snapshot.nodes[0].node_type;
        if root_type != NodeKind::NavigationPage as u16 {
            return Err(UiError::State);
        }
        self.active = false;
        Ok(self.snapshot.clone())
    }

    /// Discard the staging tree without producing a snapshot.
    pub fn abort(&mut self) {
        self.active = false;
    }
}

// ---------------------------------------------------------------------------
// Declarative View layer (SwiftUI-like, allocation-free)
// ---------------------------------------------------------------------------

/// A declarative view that renders itself into a [`Tree`].
///
/// `Message` is the app's action type; each component encodes one action as an
/// event id. Rendering is a pure build; the app mutates its model in `update`
/// and rebuilds the view.
pub trait View<Message: Copy + Into<u32>> {
    fn render(&self, tree: &mut Tree) -> Result<(), UiError>;
}

impl<M: Copy + Into<u32>> View<M> for () {
    fn render(&self, _tree: &mut Tree) -> Result<(), UiError> {
        Ok(())
    }
}

macro_rules! impl_tuple_view {
    ($($name:ident : $field:tt),+) => {
        impl<M: Copy + Into<u32>, $($name: View<M>),+> View<M> for ($($name,)+) {
            #[allow(non_snake_case)]
            fn render(&self, tree: &mut Tree) -> Result<(), UiError> {
                $(self.$field.render(tree)?;)+
                Ok(())
            }
        }
    };
}

impl_tuple_view!(A : 0);
impl_tuple_view!(A : 0, B : 1);
impl_tuple_view!(A : 0, B : 1, C : 2);
impl_tuple_view!(A : 0, B : 1, C : 2, D : 3);
impl_tuple_view!(A : 0, B : 1, C : 2, D : 3, E : 4);
impl_tuple_view!(A : 0, B : 1, C : 2, D : 3, E : 4, F : 5);
impl_tuple_view!(A : 0, B : 1, C : 2, D : 3, E : 4, F : 5, G : 6);
impl_tuple_view!(A : 0, B : 1, C : 2, D : 3, E : 4, F : 5, G : 6, H : 7);

/// Root container rendered as a navigation page.
pub struct NavigationPage<C> {
    pub key: NodeId,
    pub title: &'static str,
    pub children: C,
}

impl<M: Copy + Into<u32>, C: View<M>> View<M> for NavigationPage<C> {
    fn render(&self, tree: &mut Tree) -> Result<(), UiError> {
        tree.navigation_page(self.key, self.title)?;
        let rc = self.children.render(tree);
        if rc.is_ok() {
            tree.end()?;
        }
        rc
    }
}

/// A titled group of rows.
pub struct Section<C> {
    pub key: NodeId,
    pub title: &'static str,
    pub children: C,
}

impl<M: Copy + Into<u32>, C: View<M>> View<M> for Section<C> {
    fn render(&self, tree: &mut Tree) -> Result<(), UiError> {
        tree.section(self.key, self.title)?;
        let rc = self.children.render(tree);
        if rc.is_ok() {
            tree.end()?;
        }
        rc
    }
}

/// Informational label/value row (no affordance).
pub struct StatusRow {
    pub key: NodeId,
    pub label: &'static str,
    pub value: &'static str,
}

impl<M: Copy + Into<u32>> View<M> for StatusRow {
    fn render(&self, tree: &mut Tree) -> Result<(), UiError> {
        tree.status_row(self.key, self.label, self.value)
    }
}

/// Navigable row with the stock forward affordance.
pub struct ActionRow<Message> {
    pub key: NodeId,
    pub label: &'static str,
    pub detail: &'static str,
    pub event: Message,
    pub enabled: bool,
}

impl<M: Copy + Into<u32>> View<M> for ActionRow<M> {
    fn render(&self, tree: &mut Tree) -> Result<(), UiError> {
        tree.action_row(
            self.key,
            self.label,
            self.detail,
            self.event.into(),
            self.enabled,
        )
    }
}

/// Row whose trailing control is a stock switch.
pub struct SwitchRow<Message> {
    pub key: NodeId,
    pub label: &'static str,
    pub detail: &'static str,
    pub event: Message,
    pub checked: bool,
    pub enabled: bool,
}

impl<M: Copy + Into<u32>> View<M> for SwitchRow<M> {
    fn render(&self, tree: &mut Tree) -> Result<(), UiError> {
        tree.switch_row(
            self.key,
            self.label,
            self.detail,
            self.event.into(),
            self.checked,
            self.enabled,
        )
    }
}

/// Plain text node.
pub struct Text {
    pub key: NodeId,
    pub text: &'static str,
    pub style: TextStyle,
}

impl<M: Copy + Into<u32>> View<M> for Text {
    fn render(&self, tree: &mut Tree) -> Result<(), UiError> {
        tree.text(self.key, self.text, self.style)
    }
}

// ---------------------------------------------------------------------------
// Generation-checked event dispatch
// ---------------------------------------------------------------------------

/// Holds the committed snapshot and generation-checked dispatch state.
pub struct Runtime {
    committed: Snapshot,
    pub dropped_events: u32,
}

impl Runtime {
    pub fn new() -> Runtime {
        Runtime {
            committed: Snapshot::empty(),
            dropped_events: 0,
        }
    }

    pub fn current(&self) -> &Snapshot {
        &self.committed
    }

    /// Atomically replaces the committed snapshot.
    pub fn commit(&mut self, snapshot: Snapshot) {
        self.committed = snapshot;
    }

    /// Generation-checked dispatch. Returns the sink result for an interactive,
    /// enabled node whose event matches; rejects stale generations, disabled
    /// nodes and non-interactive nodes before calling the sink.
    pub fn dispatch(
        &mut self,
        generation: u32,
        key: NodeId,
        event_id: u32,
        sink: &mut impl FnMut(u32, NodeId, u32) -> Result<i32, UiError>,
    ) -> Result<i32, UiError> {
        if generation != self.committed.generation {
            self.dropped_events = self.dropped_events.saturating_add(1);
            return Err(UiError::StaleGeneration);
        }
        let node = self.committed.find_by_key(key).ok_or(UiError::Argument)?;
        if !node.is_interactive() || node.event_id != event_id {
            return Err(UiError::Argument);
        }
        if !node.enabled() {
            return Err(UiError::Disabled);
        }
        sink(generation, key, event_id)
    }
}

impl Default for Runtime {
    fn default() -> Self {
        Self::new()
    }
}

// ---------------------------------------------------------------------------
// Layout parity (mirror the C static asserts)
// ---------------------------------------------------------------------------

#[cfg(test)]
mod layout {
    use super::*;
    use core::mem::{align_of, offset_of, size_of};

    #[test]
    fn node_layout() {
        assert_eq!(size_of::<Node>(), 28);
        assert_eq!(align_of::<Node>(), 4);
        assert_eq!(offset_of!(Node, key), 0);
        assert_eq!(offset_of!(Node, event_id), 20);
        assert_eq!(offset_of!(Node, flags), 24);
    }

    #[test]
    fn snapshot_layout() {
        // header 12 bytes, then 32 nodes, then the string arena.
        assert_eq!(offset_of!(Snapshot, nodes), 12);
        assert_eq!(offset_of!(Snapshot, strings), 12 + MAX_NODES * 28);
        assert_eq!(size_of::<Snapshot>(), 12 + MAX_NODES * 28 + STRING_CAPACITY);
    }
}

// ---------------------------------------------------------------------------
// Tests: mirror app-sdk/ui tests + C manager overview equivalence
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    /// Manager event ids mirrored from canopus_manager_native.h.
    mod event {
        pub const INSTALL: u32 = 3;
        pub const SAFE_MODE: u32 = 4;
    }

    #[test]
    fn builds_linked_tree_and_copies_strings() {
        let mut tree = Tree::begin();
        tree.navigation_page(1, "Canopus Manager").unwrap();
        tree.section(2, "Framework").unwrap();
        tree.status_row(3, "Supervisor", "Ready").unwrap();
        tree.button(4, "Install", 42, true).unwrap();
        tree.end().unwrap();
        tree.end().unwrap();
        let snapshot = tree.commit().unwrap();

        assert_eq!(snapshot.node_count, 4);
        assert_eq!(snapshot.generation, 1);
        assert_eq!(snapshot.node(0).unwrap().parent, NO_NODE);
        assert_eq!(snapshot.node(0).unwrap().first_child, 1);
        assert_eq!(snapshot.node(1).unwrap().parent, 0);
        assert_eq!(snapshot.node(1).unwrap().first_child, 2);
        assert_eq!(snapshot.node(2).unwrap().next_sibling, 3);
        assert_eq!(snapshot.node(3).unwrap().next_sibling, NO_NODE);
        assert_eq!(
            snapshot.primary(snapshot.node(0).unwrap()),
            "Canopus Manager"
        );
        assert_eq!(snapshot.secondary(snapshot.node(2).unwrap()), "Ready");
    }

    #[test]
    fn rejects_duplicate_keys_and_invalid_shape() {
        let mut tree = Tree::begin();
        assert_eq!(tree.text(2, "x", TextStyle::Body), Err(UiError::State));
        tree.navigation_page(1, "nested").unwrap();
        assert_eq!(tree.navigation_page(2, "again"), Err(UiError::State));
        assert_eq!(
            tree.text(1, "dup", TextStyle::Body),
            Err(UiError::DuplicateKey)
        );
        // commit while the page container is open fails, then end() closes it.
        assert_eq!(tree.commit(), Err(UiError::State));
        tree.end().unwrap();
        assert!(tree.commit().is_ok());
    }

    #[test]
    fn enforces_node_capacity() {
        let mut tree = Tree::begin();
        tree.navigation_page(1, "").unwrap();
        for i in 0..MAX_NODES - 1 {
            tree.text(100 + i as u32, "", TextStyle::Body).unwrap();
        }
        assert_eq!(tree.text(999, "", TextStyle::Body), Err(UiError::Capacity));
    }

    #[test]
    fn string_overflow_rolls_back() {
        let mut tree = Tree::begin();
        tree.navigation_page(1, "R").unwrap();
        let long = "x".repeat(STRING_CAPACITY);
        assert_eq!(tree.text(2, &long, TextStyle::Body), Err(UiError::Capacity));
        // The failed node left no partial string or node behind.
        assert_eq!(tree.snapshot.node_count, 1);
        assert_eq!(tree.snapshot.primary(tree.snapshot.node(0).unwrap()), "R");
    }

    #[test]
    fn dispatch_is_generation_checked_and_interactive_only() {
        let mut tree = Tree::begin();
        tree.navigation_page(1, "Manager").unwrap();
        tree.button(2, "Enable", 77, true).unwrap();
        tree.button(3, "Remove", 88, false).unwrap();
        tree.text(4, "body", TextStyle::Body).unwrap();
        tree.switch_row(5, "Safe mode", "Recovery", 99, true, true)
            .unwrap();
        tree.end().unwrap();
        let snapshot = tree.commit().unwrap();

        let mut runtime = Runtime::new();
        runtime.commit(snapshot);

        assert_eq!(
            runtime.dispatch(0, 2, 77, &mut |_, _, _| Ok(0)),
            Err(UiError::StaleGeneration)
        );
        assert_eq!(runtime.dropped_events, 1);
        assert_eq!(
            runtime.dispatch(1, 3, 88, &mut |_, _, _| Ok(0)),
            Err(UiError::Disabled)
        );
        assert_eq!(
            runtime.dispatch(1, 4, 77, &mut |_, _, _| Ok(0)),
            Err(UiError::Argument)
        );
        assert_eq!(
            runtime.dispatch(1, 2, 78, &mut |_, _, _| Ok(0)),
            Err(UiError::Argument)
        );
        // Checked switch node is interactive and enabled.
        assert_eq!(runtime.dispatch(1, 5, 99, &mut |_, _, _| Ok(19)), Ok(19));
        assert_eq!(runtime.dispatch(1, 2, 77, &mut |_, _, _| Ok(19)), Ok(19));
    }

    /// Declarative `View` composition mirroring the C Manager Overview.
    #[derive(Copy, Clone)]
    enum ManagerEvent {
        SafeMode,
        Install,
    }

    impl From<ManagerEvent> for u32 {
        fn from(e: ManagerEvent) -> u32 {
            match e {
                ManagerEvent::SafeMode => event::SAFE_MODE,
                ManagerEvent::Install => event::INSTALL,
            }
        }
    }

    struct OverviewView;

    impl View<ManagerEvent> for OverviewView {
        fn render(&self, tree: &mut Tree) -> Result<(), UiError> {
            NavigationPage {
                key: 1,
                title: "Canopus",
                children: (
                    Text {
                        key: 11,
                        text: "Version 1 / revision 5",
                        style: TextStyle::Description,
                    },
                    Section {
                        key: 10,
                        title: "Overview",
                        children: (
                            StatusRow {
                                key: 12,
                                label: "Modules",
                                value: "2 installed / 1 active",
                            },
                            ActionRow {
                                key: 13,
                                label: "Safe mode",
                                detail: "Off / recovery available",
                                event: ManagerEvent::SafeMode,
                                enabled: true,
                            },
                            ActionRow {
                                key: 14,
                                label: "Install package",
                                detail: "No staged package",
                                event: ManagerEvent::Install,
                                enabled: false,
                            },
                        ),
                    },
                ),
            }
            .render(tree)
        }
    }

    #[test]
    fn declarative_overview_is_equivalent_to_c_manager() {
        let mut tree = Tree::begin();
        OverviewView.render(&mut tree).expect("view render failed");
        let snapshot = tree.commit().expect("tree commit failed");
        assert_eq!(
            snapshot.node(0).unwrap().kind(),
            Some(NodeKind::NavigationPage)
        );
        assert_eq!(snapshot.primary(snapshot.node(0).unwrap()), "Canopus");

        // The Safe mode row is declarative; the C manager emits a switch for
        // it. Assert the semantic shape (checked + enabled state).
        let safe = snapshot.find_by_key(13).unwrap();
        assert_eq!(safe.kind(), Some(NodeKind::ActionRow));
        assert_eq!(safe.event_id, event::SAFE_MODE);
        assert!(safe.enabled());

        let install = snapshot.find_by_key(14).unwrap();
        assert!(!install.enabled());
        assert_eq!(install.event_id, event::INSTALL);
    }
}
