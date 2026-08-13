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
pub const ABI_MINOR: u16 = 4;

pub const MAX_NODES: usize = 32;
pub const MAX_DEPTH: usize = 8;
pub const STRING_CAPACITY: usize = 1536;
pub const NO_NODE: u16 = 0xFFFF;

pub const CAP_EXTENDED_COMPONENTS: u32 = 1 << 0;
pub const CAP_STYLE: u32 = 1 << 1;
pub const CAP_LAYOUT: u32 = 1 << 2;
pub const CAP_VALUES: u32 = 1 << 3;
pub const CAP_NAVIGATION_HEADER: u32 = 1 << 4;
pub const CAP_IMAGE: u32 = 1 << 5;
pub const CAP_PROGRESS: u32 = 1 << 6;

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
    List = 8,
    Scroll = 9,
    Dialog = 10,
    Toast = 11,
    Image = 12,
    Icon = 13,
    RichText = 14,
    Checkbox = 15,
    RadioRow = 16,
    Slider = 17,
    Progress = 18,
    Divider = 19,
    Spacer = 20,
    NavigationHeader = 21,
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
            8 => Some(NodeKind::List),
            9 => Some(NodeKind::Scroll),
            10 => Some(NodeKind::Dialog),
            11 => Some(NodeKind::Toast),
            12 => Some(NodeKind::Image),
            13 => Some(NodeKind::Icon),
            14 => Some(NodeKind::RichText),
            15 => Some(NodeKind::Checkbox),
            16 => Some(NodeKind::RadioRow),
            17 => Some(NodeKind::Slider),
            18 => Some(NodeKind::Progress),
            19 => Some(NodeKind::Divider),
            20 => Some(NodeKind::Spacer),
            21 => Some(NodeKind::NavigationHeader),
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
pub const FLAG_SELECTED: u32 = 1 << 2;
pub const FLAG_INDETERMINATE: u32 = 1 << 3;
pub const FLAG_VISIBLE: u32 = 1 << 4;
pub const FLAG_WRAP: u32 = 1 << 5;
pub const FLAG_HEADER_BACK: u32 = 1 << 6;
pub const FLAG_HEADER_CENTERED: u32 = 1 << 7;
pub const FLAG_HEADER_ELEVATED: u32 = 1 << 8;

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
#[repr(u16)]
pub enum ComponentVariant {
    #[default]
    Default = 0,
    Plain = 1,
    Filled = 2,
    Outlined = 3,
    Tonal = 4,
    Destructive = 5,
    Compact = 6,
}

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
#[repr(u16)]
pub enum ColorRole {
    #[default]
    Inherit = 0,
    Surface = 1,
    SurfaceAlt = 2,
    TextPrimary = 3,
    TextSecondary = 4,
    Accent = 5,
    Success = 6,
    Warning = 7,
    Danger = 8,
    Disabled = 9,
    Transparent = 10,
}

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
#[repr(u8)]
pub enum Axis {
    #[default]
    Vertical = 0,
    Horizontal = 1,
}

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
#[repr(u8)]
pub enum Alignment {
    #[default]
    Auto = 0,
    Start = 1,
    Center = 2,
    End = 3,
    Stretch = 4,
}

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
#[repr(u8)]
pub enum Justification {
    #[default]
    Start = 0,
    Center = 1,
    End = 2,
    SpaceBetween = 3,
    SpaceAround = 4,
    SpaceEvenly = 5,
}

/// Target-independent semantic appearance. Signed dimensions use `-1` for
/// automatic/inherited values; color roles map to approved stock tokens.
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct Style {
    pub variant: u16,
    pub text_style: u16,
    pub foreground: u16,
    pub background: u16,
    pub accent: u16,
    pub border_color: u16,
    pub corner_radius: i16,
    pub border_width: i16,
    pub opacity: u16,
    pub reserved: u16,
}

impl Default for Style {
    fn default() -> Self {
        Self {
            variant: ComponentVariant::Default as u16,
            text_style: TextStyle::Body as u16,
            foreground: ColorRole::Inherit as u16,
            background: ColorRole::Inherit as u16,
            accent: ColorRole::Inherit as u16,
            border_color: ColorRole::Inherit as u16,
            corner_radius: -1,
            border_width: -1,
            opacity: 0,
            reserved: 0,
        }
    }
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct Layout {
    pub width: i16,
    pub height: i16,
    pub min_width: i16,
    pub min_height: i16,
    pub max_width: i16,
    pub max_height: i16,
    pub margin_top: i16,
    pub margin_right: i16,
    pub margin_bottom: i16,
    pub margin_left: i16,
    pub padding_top: i16,
    pub padding_right: i16,
    pub padding_bottom: i16,
    pub padding_left: i16,
    pub gap: i16,
    pub axis: u8,
    pub align: u8,
    pub justify: u8,
    pub grow: u8,
    pub shrink: u8,
    pub reserved: [u8; 3],
}

impl Default for Layout {
    fn default() -> Self {
        Self {
            width: -1,
            height: -1,
            min_width: -1,
            min_height: -1,
            max_width: -1,
            max_height: -1,
            margin_top: 0,
            margin_right: 0,
            margin_bottom: 0,
            margin_left: 0,
            padding_top: 0,
            padding_right: 0,
            padding_bottom: 0,
            padding_left: 0,
            gap: 0,
            axis: Axis::Vertical as u8,
            align: Alignment::Auto as u8,
            justify: Justification::Start as u8,
            grow: 0,
            shrink: 1,
            reserved: [0; 3],
        }
    }
}

#[repr(C)]
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub struct Value {
    pub value: i32,
    pub minimum: i32,
    pub maximum: i32,
    pub step: i32,
    pub resource_id: u32,
}

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
            Some(
                NodeKind::Button
                    | NodeKind::ActionRow
                    | NodeKind::SwitchRow
                    | NodeKind::Checkbox
                    | NodeKind::RadioRow
                    | NodeKind::Slider
                    | NodeKind::Icon
                    | NodeKind::NavigationHeader
            )
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
    /// ABI 1.3 append-only metadata; the ABI 1.2 prefix remains unchanged.
    pub styles: [Style; MAX_NODES],
    pub layouts: [Layout; MAX_NODES],
    pub values: [Value; MAX_NODES],
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
            styles: [Style::default(); MAX_NODES],
            layouts: [Layout::default(); MAX_NODES],
            values: [Value {
                value: 0,
                minimum: 0,
                maximum: 0,
                step: 0,
                resource_id: 0,
            }; MAX_NODES],
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

    pub fn style(&self, index: usize) -> Option<&Style> {
        (index < self.node_count as usize).then(|| &self.styles[index])
    }

    pub fn layout(&self, index: usize) -> Option<&Layout> {
        (index < self.node_count as usize).then(|| &self.layouts[index])
    }

    pub fn value(&self, index: usize) -> Option<&Value> {
        (index < self.node_count as usize).then(|| &self.values[index])
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

/// Properties shared by the extended semantic prefab catalog.
#[derive(Copy, Clone, Debug)]
pub struct ComponentSpec<'a> {
    pub primary: &'a str,
    pub secondary: &'a str,
    pub event_id: u32,
    pub flags: u32,
    pub value: Value,
}

impl Default for ComponentSpec<'_> {
    fn default() -> Self {
        Self {
            primary: "",
            secondary: "",
            event_id: 0,
            flags: FLAG_VISIBLE,
            value: Value::default(),
        }
    }
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
        self.snapshot.styles[index] = Style::default();
        self.snapshot.layouts[index] = Layout::default();
        self.snapshot.values[index] = Value::default();
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
        self.append(
            key,
            NodeKind::NavigationPage,
            title,
            "",
            0,
            FLAG_VISIBLE,
            true,
        )
    }

    pub fn section(&mut self, key: NodeId, title: &str) -> Result<(), UiError> {
        self.append(key, NodeKind::Section, title, "", 0, FLAG_VISIBLE, true)
    }

    pub fn text(&mut self, key: NodeId, text: &str, style: TextStyle) -> Result<(), UiError> {
        self.append(key, NodeKind::Text, text, "", 0, FLAG_VISIBLE, false)?;
        let index = self.snapshot.node_count as usize - 1;
        self.snapshot.styles[index].text_style = style as u16;
        Ok(())
    }

    pub fn status_row(&mut self, key: NodeId, label: &str, value: &str) -> Result<(), UiError> {
        self.append(
            key,
            NodeKind::StatusRow,
            label,
            value,
            0,
            FLAG_VISIBLE,
            false,
        )
    }

    /// Appends an image resource. `source` is a target-resolvable file path;
    /// `resource_id` is a stable semantic identity used for cache invalidation.
    pub fn image(
        &mut self,
        key: NodeId,
        resource_id: u32,
        source: &str,
        layout: Layout,
    ) -> Result<(), UiError> {
        if resource_id == 0 || source.is_empty() {
            return Err(UiError::Argument);
        }
        self.component(
            key,
            NodeKind::Image,
            ComponentSpec {
                primary: source,
                value: Value {
                    resource_id,
                    ..Value::default()
                },
                ..ComponentSpec::default()
            },
        )?;
        self.set_layout(key, layout)
    }

    /// Appends a non-interactive determinate progress indicator.
    pub fn progress(
        &mut self,
        key: NodeId,
        value: i32,
        minimum: i32,
        maximum: i32,
        layout: Layout,
    ) -> Result<(), UiError> {
        if minimum >= maximum || value < minimum || value > maximum {
            return Err(UiError::Argument);
        }
        self.component(
            key,
            NodeKind::Progress,
            ComponentSpec {
                value: Value {
                    value,
                    minimum,
                    maximum,
                    step: 0,
                    resource_id: 0,
                },
                ..ComponentSpec::default()
            },
        )?;
        self.set_layout(key, layout)
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
        let flags = FLAG_VISIBLE | if enabled { FLAG_ENABLED } else { 0 };
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
        let flags = FLAG_VISIBLE | if enabled { FLAG_ENABLED } else { 0 };
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
        let mut flags = FLAG_VISIBLE;
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

    /// Appends any extended system-semantic component. `List`, `Scroll` and
    /// `Dialog` are containers and must later be closed with [`Tree::end`].
    pub fn component(
        &mut self,
        key: NodeId,
        kind: NodeKind,
        spec: ComponentSpec<'_>,
    ) -> Result<(), UiError> {
        let valid = matches!(
            kind,
            NodeKind::List
                | NodeKind::Scroll
                | NodeKind::Dialog
                | NodeKind::Toast
                | NodeKind::Image
                | NodeKind::Icon
                | NodeKind::RichText
                | NodeKind::Checkbox
                | NodeKind::RadioRow
                | NodeKind::Slider
                | NodeKind::Progress
                | NodeKind::Divider
                | NodeKind::Spacer
                | NodeKind::NavigationHeader
        );
        if !valid {
            return Err(UiError::Argument);
        }
        let interactive = matches!(
            kind,
            NodeKind::Checkbox | NodeKind::RadioRow | NodeKind::Slider
        );
        if interactive && spec.event_id == 0 {
            return Err(UiError::Argument);
        }
        if kind == NodeKind::Image && spec.value.resource_id == 0 {
            return Err(UiError::Argument);
        }
        if matches!(kind, NodeKind::Slider | NodeKind::Progress)
            && (spec.value.minimum > spec.value.maximum
                || spec.value.value < spec.value.minimum
                || spec.value.value > spec.value.maximum
                || spec.value.step < 0
                || (kind == NodeKind::Progress && spec.value.minimum == spec.value.maximum))
        {
            return Err(UiError::Argument);
        }
        let container = matches!(
            kind,
            NodeKind::List | NodeKind::Scroll | NodeKind::Dialog | NodeKind::NavigationHeader
        );
        self.append(
            key,
            kind,
            spec.primary,
            spec.secondary,
            spec.event_id,
            spec.flags,
            container,
        )?;
        let index = self.snapshot.node_count as usize - 1;
        self.snapshot.values[index] = spec.value;
        Ok(())
    }

    pub fn set_style(&mut self, key: NodeId, style: Style) -> Result<(), UiError> {
        if style.variant > ComponentVariant::Compact as u16
            || style.text_style > TextStyle::Warning as u16
            || style.foreground > ColorRole::Transparent as u16
            || style.background > ColorRole::Transparent as u16
            || style.accent > ColorRole::Transparent as u16
            || style.border_color > ColorRole::Transparent as u16
            || style.corner_radius < -1
            || style.border_width < -1
            || style.opacity > 1000
            || style.reserved != 0
        {
            return Err(UiError::Argument);
        }
        let index = self.snapshot.nodes[..self.snapshot.node_count as usize]
            .iter()
            .position(|node| node.key == key)
            .ok_or(UiError::Argument)?;
        self.snapshot.styles[index] = style;
        Ok(())
    }

    pub fn set_layout(&mut self, key: NodeId, layout: Layout) -> Result<(), UiError> {
        if layout.width < -1
            || layout.height < -1
            || layout.min_width < -1
            || layout.min_height < -1
            || layout.max_width < -1
            || layout.max_height < -1
            || layout.gap < -1
            || layout.axis > Axis::Horizontal as u8
            || layout.align > Alignment::Stretch as u8
            || layout.justify > Justification::SpaceEvenly as u8
            || layout.reserved != [0; 3]
        {
            return Err(UiError::Argument);
        }
        let index = self.snapshot.nodes[..self.snapshot.node_count as usize]
            .iter()
            .position(|node| node.key == key)
            .ok_or(UiError::Argument)?;
        self.snapshot.layouts[index] = layout;
        Ok(())
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
///
/// Message types must be bounded, copyable values with an explicit stable event
/// encoding. A closure or non-copy action cannot be retained by the view:
///
/// ```compile_fail
/// use canopus_ui_core::{ActionRow, Tree, View};
///
/// struct UnboundedAction(core::cell::Cell<u32>);
/// let row = ActionRow {
///     key: 1,
///     label: "Run",
///     detail: "",
///     event: UnboundedAction(core::cell::Cell::new(7)),
///     enabled: true,
/// };
/// let mut tree = Tree::begin();
/// row.render(&mut tree).unwrap();
/// ```
pub trait View<Message: Copy + Into<u32>> {
    fn render(&self, tree: &mut Tree) -> Result<(), UiError>;
}

/// Allocation-free declarative syntax. It intentionally expands to ordinary
/// typed values, so diagnostics point at the component fields and device builds
/// do not depend on a procedural macro runtime. One tuple is bounded to eight
/// children; larger pages must nest semantic containers rather than creating an
/// unbounded flat list.
///
/// ```compile_fail
/// use canopus_ui_core::{Tree, View};
///
/// let too_many = ((), (), (), (), (), (), (), (), ());
/// let mut tree = Tree::begin();
/// <_ as View<u32>>::render(&too_many, &mut tree).unwrap();
/// ```
#[macro_export]
macro_rules! view {
    ($single:expr $(,)?) => { $single };
    ($first:expr, $($rest:expr),+ $(,)?) => { ($first, $($rest,)+) };
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

// ---------------------------------------------------------------------------
// Bounded application state and commands
// ---------------------------------------------------------------------------

/// A small state cell used by application models and bindings.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct State<T> {
    value: T,
}

impl<T> State<T> {
    pub const fn new(value: T) -> Self {
        Self { value }
    }

    pub const fn get(&self) -> &T {
        &self.value
    }

    pub fn get_mut(&mut self) -> &mut T {
        &mut self.value
    }

    pub fn set(&mut self, value: T) {
        self.value = value;
    }

    pub fn binding(&mut self) -> Binding<'_, T> {
        Binding {
            value: &mut self.value,
        }
    }
}

/// A temporary two-way reference to model state. It never escapes the borrow
/// of the model and therefore carries no runtime pointer into a UI snapshot.
pub struct Binding<'a, T> {
    value: &'a mut T,
}

impl<T> Binding<'_, T> {
    pub fn get(&self) -> &T {
        self.value
    }

    pub fn set(&mut self, value: T) {
        *self.value = value;
    }

    pub fn update(&mut self, update: impl FnOnce(&mut T)) {
        update(self.value);
    }
}

/// Fixed-capacity list for model-owned repeated state.
pub struct BoundedList<T: Copy, const N: usize> {
    items: [Option<T>; N],
    len: usize,
}

impl<T: Copy, const N: usize> BoundedList<T, N> {
    pub const fn new() -> Self {
        Self {
            items: [None; N],
            len: 0,
        }
    }

    pub const fn len(&self) -> usize {
        self.len
    }

    pub const fn capacity(&self) -> usize {
        N
    }

    pub const fn is_empty(&self) -> bool {
        self.len == 0
    }

    pub const fn is_full(&self) -> bool {
        self.len == N
    }

    pub fn get(&self, index: usize) -> Option<&T> {
        self.items.get(index).and_then(Option::as_ref)
    }

    pub fn push(&mut self, value: T) -> Result<(), UiError> {
        if self.is_full() {
            return Err(UiError::Capacity);
        }
        self.items[self.len] = Some(value);
        self.len += 1;
        Ok(())
    }

    pub fn pop(&mut self) -> Option<T> {
        if self.len == 0 {
            return None;
        }
        self.len -= 1;
        self.items[self.len].take()
    }

    pub fn remove(&mut self, index: usize) -> Option<T> {
        if index >= self.len {
            return None;
        }
        let removed = self.items[index].take();
        for current in index..self.len - 1 {
            self.items[current] = self.items[current + 1].take();
        }
        self.len -= 1;
        self.items[self.len] = None;
        removed
    }

    pub fn iter(&self) -> impl Iterator<Item = &T> {
        self.items[..self.len].iter().filter_map(Option::as_ref)
    }
}

impl<T: Copy, const N: usize> Default for BoundedList<T, N> {
    fn default() -> Self {
        Self::new()
    }
}

/// Deferred work requested by an application's `update` function.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum Command<Message> {
    Emit(Message),
    Rebuild,
}

/// Sink abstraction lets an update function remain independent of queue size.
pub trait CommandSink<Message> {
    fn submit(&mut self, command: Command<Message>) -> Result<(), UiError>;
}

/// FIFO command queue with compile-time capacity and no allocator.
pub struct CommandQueue<Message: Copy, const N: usize> {
    commands: [Option<Command<Message>>; N],
    head: usize,
    len: usize,
}

impl<Message: Copy, const N: usize> CommandQueue<Message, N> {
    pub const fn new() -> Self {
        Self {
            commands: [None; N],
            head: 0,
            len: 0,
        }
    }

    pub const fn len(&self) -> usize {
        self.len
    }

    pub const fn is_empty(&self) -> bool {
        self.len == 0
    }

    pub const fn is_full(&self) -> bool {
        self.len == N
    }

    pub fn pop(&mut self) -> Option<Command<Message>> {
        if self.len == 0 {
            return None;
        }
        let command = self.commands[self.head].take();
        if N != 0 {
            self.head = (self.head + 1) % N;
        }
        self.len -= 1;
        command
    }

    pub fn clear(&mut self) {
        while self.pop().is_some() {}
        self.head = 0;
    }
}

impl<Message: Copy, const N: usize> CommandSink<Message> for CommandQueue<Message, N> {
    fn submit(&mut self, command: Command<Message>) -> Result<(), UiError> {
        if self.is_full() {
            return Err(UiError::Capacity);
        }
        let tail = if N == 0 {
            0
        } else {
            (self.head + self.len) % N
        };
        self.commands[tail] = Some(command);
        self.len += 1;
        Ok(())
    }
}

impl<Message: Copy, const N: usize> Default for CommandQueue<Message, N> {
    fn default() -> Self {
        Self::new()
    }
}

/// Model/message/update contract used by allocator-free applications.
pub trait Application {
    type Model;
    type Message: Copy + Into<u32>;

    /// Converts a semantic event id back into the application's typed message.
    /// Unknown ids must return `None`; the runtime never guesses enum layouts.
    fn decode_message(event_id: u32) -> Option<Self::Message>;

    fn update(
        &mut self,
        model: &mut Self::Model,
        message: Self::Message,
        commands: &mut impl CommandSink<Self::Message>,
    );
}

/// Declarative wrapper for every extended prefab. This single type keeps the
/// component catalog exhaustive without duplicating style/layout APIs.
pub struct SystemComponent<'a, Message, Children = ()> {
    pub key: NodeId,
    pub kind: NodeKind,
    pub primary: &'a str,
    pub secondary: &'a str,
    pub event: Option<Message>,
    pub flags: u32,
    pub value: Value,
    pub style: Style,
    pub layout: Layout,
    pub children: Children,
}

impl<'a, M> SystemComponent<'a, M, ()> {
    pub fn leaf(key: NodeId, kind: NodeKind, primary: &'a str) -> Self {
        Self {
            key,
            kind,
            primary,
            secondary: "",
            event: None,
            flags: FLAG_VISIBLE,
            value: Value::default(),
            style: Style::default(),
            layout: Layout::default(),
            children: (),
        }
    }
}

impl<'a, M, C> SystemComponent<'a, M, C> {
    pub fn container(key: NodeId, kind: NodeKind, primary: &'a str, children: C) -> Self {
        Self {
            key,
            kind,
            primary,
            secondary: "",
            event: None,
            flags: FLAG_VISIBLE,
            value: Value::default(),
            style: Style::default(),
            layout: Layout::default(),
            children,
        }
    }

    pub fn secondary(mut self, secondary: &'a str) -> Self {
        self.secondary = secondary;
        self
    }

    pub fn event(mut self, event: M) -> Self {
        self.event = Some(event);
        self
    }

    pub fn enabled(mut self, enabled: bool) -> Self {
        if enabled {
            self.flags |= FLAG_ENABLED;
        } else {
            self.flags &= !FLAG_ENABLED;
        }
        self
    }

    pub fn checked(mut self, checked: bool) -> Self {
        if checked {
            self.flags |= FLAG_CHECKED;
        } else {
            self.flags &= !FLAG_CHECKED;
        }
        self
    }

    pub fn selected(mut self, selected: bool) -> Self {
        if selected {
            self.flags |= FLAG_SELECTED;
        } else {
            self.flags &= !FLAG_SELECTED;
        }
        self
    }

    pub fn indeterminate(mut self, indeterminate: bool) -> Self {
        if indeterminate {
            self.flags |= FLAG_INDETERMINATE;
        } else {
            self.flags &= !FLAG_INDETERMINATE;
        }
        self
    }

    pub fn visible(mut self, visible: bool) -> Self {
        if visible {
            self.flags |= FLAG_VISIBLE;
        } else {
            self.flags &= !FLAG_VISIBLE;
        }
        self
    }

    pub fn wrap(mut self, wrap: bool) -> Self {
        if wrap {
            self.flags |= FLAG_WRAP;
        } else {
            self.flags &= !FLAG_WRAP;
        }
        self
    }

    pub fn value(mut self, value: Value) -> Self {
        self.value = value;
        self
    }

    pub fn resource(mut self, resource_id: u32) -> Self {
        self.value.resource_id = resource_id;
        self
    }

    pub fn style(mut self, style: Style) -> Self {
        self.style = style;
        self
    }

    pub fn layout(mut self, layout: Layout) -> Self {
        self.layout = layout;
        self
    }
}

impl<M: Copy + Into<u32>, C: View<M>> View<M> for SystemComponent<'_, M, C> {
    fn render(&self, tree: &mut Tree) -> Result<(), UiError> {
        let container = matches!(
            self.kind,
            NodeKind::List | NodeKind::Scroll | NodeKind::Dialog | NodeKind::NavigationHeader
        );
        tree.component(
            self.key,
            self.kind,
            ComponentSpec {
                primary: self.primary,
                secondary: self.secondary,
                event_id: self.event.map(Into::into).unwrap_or(0),
                flags: self.flags,
                value: self.value,
            },
        )?;
        tree.set_style(self.key, self.style)?;
        tree.set_layout(self.key, self.layout)?;
        if container {
            let result = self.children.render(tree);
            if result.is_ok() {
                tree.end()?;
            }
            result
        } else {
            Ok(())
        }
    }
}

/// Stock page titlebar. The semantic node carries the title, optional subtitle
/// and generation-checked back event; a target backend maps it to the approved
/// firmware titlebar prefab. Trailing actions can be supplied as child icons.
pub struct NavigationHeader<'a, Message, Children = ()> {
    pub key: NodeId,
    pub title: &'a str,
    pub subtitle: &'a str,
    pub back: Option<Message>,
    pub centered: bool,
    pub elevated: bool,
    pub style: Style,
    pub layout: Layout,
    pub children: Children,
}

impl<M: Copy + Into<u32>, C: View<M>> View<M> for NavigationHeader<'_, M, C> {
    fn render(&self, tree: &mut Tree) -> Result<(), UiError> {
        let mut flags = FLAG_VISIBLE;
        if self.back.is_some() {
            flags |= FLAG_ENABLED | FLAG_HEADER_BACK;
        }
        if self.centered {
            flags |= FLAG_HEADER_CENTERED;
        }
        if self.elevated {
            flags |= FLAG_HEADER_ELEVATED;
        }
        tree.component(
            self.key,
            NodeKind::NavigationHeader,
            ComponentSpec {
                primary: self.title,
                secondary: self.subtitle,
                event_id: self.back.map(Into::into).unwrap_or(0),
                flags,
                value: Value::default(),
            },
        )?;
        tree.set_style(self.key, self.style)?;
        tree.set_layout(self.key, self.layout)?;
        let result = self.children.render(tree);
        if result.is_ok() {
            tree.end()?;
        }
        result
    }
}

/// Root container rendered as a navigation page.
pub struct NavigationPage<'a, C> {
    pub key: NodeId,
    pub title: &'a str,
    pub children: C,
}

impl<M: Copy + Into<u32>, C: View<M>> View<M> for NavigationPage<'_, C> {
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
pub struct Section<'a, C> {
    pub key: NodeId,
    pub title: &'a str,
    pub children: C,
}

impl<M: Copy + Into<u32>, C: View<M>> View<M> for Section<'_, C> {
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
pub struct StatusRow<'a> {
    pub key: NodeId,
    pub label: &'a str,
    pub value: &'a str,
}

impl<M: Copy + Into<u32>> View<M> for StatusRow<'_> {
    fn render(&self, tree: &mut Tree) -> Result<(), UiError> {
        tree.status_row(self.key, self.label, self.value)
    }
}

/// Navigable row with the stock forward affordance.
pub struct ActionRow<'a, Message> {
    pub key: NodeId,
    pub label: &'a str,
    pub detail: &'a str,
    pub event: Message,
    pub enabled: bool,
}

impl<M: Copy + Into<u32>> View<M> for ActionRow<'_, M> {
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
pub struct SwitchRow<'a, Message> {
    pub key: NodeId,
    pub label: &'a str,
    pub detail: &'a str,
    pub event: Message,
    pub checked: bool,
    pub enabled: bool,
}

impl<M: Copy + Into<u32>> View<M> for SwitchRow<'_, M> {
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
pub struct Text<'a> {
    pub key: NodeId,
    pub text: &'a str,
    pub style: TextStyle,
}

impl<M: Copy + Into<u32>> View<M> for Text<'_> {
    fn render(&self, tree: &mut Tree) -> Result<(), UiError> {
        tree.text(self.key, self.text, self.style)
    }
}

/// Image loaded by the target backend from a stable resource path.
pub struct Image<'a> {
    pub key: NodeId,
    pub resource_id: u32,
    pub source: &'a str,
    pub layout: Layout,
}

impl<M: Copy + Into<u32>> View<M> for Image<'_> {
    fn render(&self, tree: &mut Tree) -> Result<(), UiError> {
        tree.image(self.key, self.resource_id, self.source, self.layout)
    }
}

/// Native non-interactive determinate progress indicator.
pub struct Progress {
    pub key: NodeId,
    pub value: i32,
    pub minimum: i32,
    pub maximum: i32,
    pub layout: Layout,
}

impl<M: Copy + Into<u32>> View<M> for Progress {
    fn render(&self, tree: &mut Tree) -> Result<(), UiError> {
        tree.progress(
            self.key,
            self.value,
            self.minimum,
            self.maximum,
            self.layout,
        )
    }
}

// ---------------------------------------------------------------------------
// Bounded semantic routing
// ---------------------------------------------------------------------------

/// Allocation-free route stack. Routes are application-owned semantic values;
/// firmware page/widget pointers never cross into this public state.
pub struct Router<Route: Copy + Eq, const DEPTH: usize> {
    routes: [Route; DEPTH],
    depth: usize,
    generation: u32,
}

impl<Route: Copy + Eq, const DEPTH: usize> Router<Route, DEPTH> {
    pub fn new(root: Route) -> Result<Self, UiError> {
        if DEPTH == 0 {
            return Err(UiError::Capacity);
        }
        Ok(Self {
            routes: [root; DEPTH],
            depth: 1,
            generation: 1,
        })
    }

    pub fn current(&self) -> Route {
        self.routes[self.depth - 1]
    }

    pub const fn depth(&self) -> usize {
        self.depth
    }

    pub const fn generation(&self) -> u32 {
        self.generation
    }

    fn advance(&mut self) {
        self.generation = self.generation.wrapping_add(1);
        if self.generation == 0 {
            self.generation = 1;
        }
    }

    pub fn push(&mut self, route: Route) -> Result<(), UiError> {
        if self.depth == DEPTH {
            return Err(UiError::Capacity);
        }
        self.routes[self.depth] = route;
        self.depth += 1;
        self.advance();
        Ok(())
    }

    /// Replaces even an equal route, deliberately advancing generation so a
    /// reopened page cannot accept events emitted by its previous instance.
    pub fn replace(&mut self, route: Route) {
        self.routes[self.depth - 1] = route;
        self.advance();
    }

    pub fn pop(&mut self) -> Result<Route, UiError> {
        if self.depth == 1 {
            return Err(UiError::State);
        }
        self.depth -= 1;
        self.advance();
        Ok(self.current())
    }

    pub fn back(&mut self, generation: u32) -> Result<Route, UiError> {
        if generation != self.generation {
            return Err(UiError::StaleGeneration);
        }
        self.pop()
    }

    pub fn pop_to(&mut self, route: Route) -> Result<(), UiError> {
        let index = self.routes[..self.depth]
            .iter()
            .rposition(|candidate| *candidate == route)
            .ok_or(UiError::Argument)?;
        if index + 1 != self.depth {
            self.depth = index + 1;
            self.advance();
        }
        Ok(())
    }

    pub fn clear_to_root(&mut self) {
        if self.depth > 1 {
            self.depth = 1;
            self.advance();
        }
    }
}

// ---------------------------------------------------------------------------
// Generation-checked event dispatch
// ---------------------------------------------------------------------------

/// Holds the committed snapshot and generation-checked dispatch state.
pub struct Runtime {
    committed: Snapshot,
    has_committed: bool,
    pub dropped_events: u32,
}

impl Runtime {
    pub fn new() -> Runtime {
        Runtime {
            committed: Snapshot::empty(),
            has_committed: false,
            dropped_events: 0,
        }
    }

    pub fn current(&self) -> &Snapshot {
        &self.committed
    }

    /// Atomically replaces the committed snapshot.
    pub fn commit(&mut self, snapshot: Snapshot) {
        self.committed = snapshot;
        self.has_committed = true;
    }

    fn next_generation(&self) -> u32 {
        if !self.has_committed {
            return 1;
        }
        let next = self.committed.generation.wrapping_add(1);
        if next == 0 { 1 } else { next }
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

/// Transactional sink for committed semantic snapshots. A target
/// implementation may cross the C ABI here; it must not retain pointers into
/// the temporary Rust builder.
pub trait SnapshotBackend {
    fn apply(&mut self, snapshot: &Snapshot) -> Result<(), UiError>;
}

/// C ABI adapter for [`SnapshotBackend`]. It mirrors the `apply(cookie,
/// snapshot)` seam used by `canopus_ui_backend_v1`, while keeping the complete
/// target backend and its firmware pointers outside public Rust UI values.
pub struct CAbiBackend {
    cookie: *mut core::ffi::c_void,
    apply: unsafe extern "C" fn(*mut core::ffi::c_void, *const Snapshot) -> i32,
}

impl CAbiBackend {
    /// Creates an adapter around a C backend callback.
    ///
    /// # Safety
    /// `cookie` must remain valid for every call to `apply`, and the callback
    /// must only read the snapshot during the call. It must not unwind or retain
    /// the temporary snapshot pointer.
    pub const unsafe fn new(
        cookie: *mut core::ffi::c_void,
        apply: unsafe extern "C" fn(*mut core::ffi::c_void, *const Snapshot) -> i32,
    ) -> Self {
        Self { cookie, apply }
    }
}

impl SnapshotBackend for CAbiBackend {
    fn apply(&mut self, snapshot: &Snapshot) -> Result<(), UiError> {
        // SAFETY: upheld by `CAbiBackend::new`; `snapshot` remains valid for the
        // complete synchronous callback and is not retained.
        let result = unsafe { (self.apply)(self.cookie, snapshot) };
        if result == 0 {
            Ok(())
        } else {
            Err(UiError::Backend)
        }
    }
}

/// Allocation-free Model/Message runtime. It validates a firmware event against
/// the committed generation, decodes it into a typed message, drains a bounded
/// command queue, rebuilds the view, and publishes only after the backend has
/// accepted the complete snapshot.
pub struct ApplicationRuntime<A: Application, B: SnapshotBackend, const COMMANDS: usize> {
    app: A,
    model: A::Model,
    backend: B,
    runtime: Runtime,
    commands: CommandQueue<A::Message, COMMANDS>,
}

impl<A: Application, B: SnapshotBackend, const COMMANDS: usize> ApplicationRuntime<A, B, COMMANDS> {
    pub fn new(app: A, model: A::Model, backend: B) -> Self {
        Self {
            app,
            model,
            backend,
            runtime: Runtime::new(),
            commands: CommandQueue::new(),
        }
    }

    pub fn model(&self) -> &A::Model {
        &self.model
    }

    pub fn model_mut(&mut self) -> &mut A::Model {
        &mut self.model
    }

    pub fn backend(&self) -> &B {
        &self.backend
    }

    pub fn backend_mut(&mut self) -> &mut B {
        &mut self.backend
    }

    pub fn current(&self) -> &Snapshot {
        self.runtime.current()
    }

    pub fn dropped_events(&self) -> u32 {
        self.runtime.dropped_events
    }

    /// Builds and transactionally publishes a view. The closure form permits
    /// model-borrowed strings without requiring them to be `'static`.
    pub fn rebuild(
        &mut self,
        render: impl FnOnce(&A::Model, &mut Tree) -> Result<(), UiError>,
    ) -> Result<(), UiError> {
        let mut tree = Tree::begin();
        render(&self.model, &mut tree)?;
        let mut snapshot = tree.commit()?;
        snapshot.generation = self.runtime.next_generation();
        self.backend.apply(&snapshot)?;
        self.runtime.commit(snapshot);
        Ok(())
    }

    /// Processes one checked UI event and publishes the resulting model view.
    /// At most `COMMANDS` queued commands are executed per event, preventing an
    /// `Emit` cycle from becoming an unbounded UI-thread loop.
    pub fn dispatch_event(
        &mut self,
        generation: u32,
        key: NodeId,
        event_id: u32,
        render: impl FnOnce(&A::Model, &mut Tree) -> Result<(), UiError>,
    ) -> Result<(), UiError> {
        self.runtime
            .dispatch(generation, key, event_id, &mut |_, _, _| Ok(0))?;
        let message = A::decode_message(event_id).ok_or(UiError::Argument)?;
        if !self.commands.is_empty() {
            self.commands.clear();
            return Err(UiError::State);
        }

        self.app
            .update(&mut self.model, message, &mut self.commands);
        let mut processed = 0usize;
        while !self.commands.is_empty() {
            if processed == COMMANDS {
                self.commands.clear();
                return Err(UiError::Capacity);
            }
            let command = match self.commands.pop() {
                Some(command) => command,
                None => return Err(UiError::State),
            };
            match command {
                Command::Emit(next) => {
                    self.app.update(&mut self.model, next, &mut self.commands);
                }
                Command::Rebuild => {}
            }
            processed += 1;
        }
        self.rebuild(render)
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
        let prefix = 12 + MAX_NODES * 28 + STRING_CAPACITY;
        assert_eq!(offset_of!(Snapshot, nodes), 12);
        assert_eq!(offset_of!(Snapshot, strings), 12 + MAX_NODES * 28);
        assert_eq!(offset_of!(Snapshot, styles), prefix);
        assert_eq!(
            offset_of!(Snapshot, layouts),
            prefix + MAX_NODES * size_of::<Style>()
        );
        assert_eq!(
            offset_of!(Snapshot, values),
            prefix + MAX_NODES * (size_of::<Style>() + size_of::<Layout>())
        );
        assert_eq!(size_of::<Style>(), 20);
        assert_eq!(size_of::<Layout>(), 38);
        assert_eq!(size_of::<Value>(), 20);
        assert_eq!(size_of::<Snapshot>(), 4940);
        assert_eq!(
            size_of::<Snapshot>(),
            prefix + MAX_NODES * (size_of::<Style>() + size_of::<Layout>() + size_of::<Value>())
        );
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

    #[test]
    fn declarative_catalog_exposes_style_layout_and_values() {
        let slider_style = Style {
            variant: ComponentVariant::Tonal as u16,
            foreground: ColorRole::TextPrimary as u16,
            background: ColorRole::SurfaceAlt as u16,
            accent: ColorRole::Accent as u16,
            corner_radius: 8,
            opacity: 900,
            ..Style::default()
        };
        let slider_layout = Layout {
            width: 240,
            min_height: 44,
            padding_left: 12,
            padding_right: 12,
            axis: Axis::Horizontal as u8,
            align: Alignment::Center as u8,
            justify: Justification::SpaceBetween as u8,
            grow: 1,
            ..Layout::default()
        };
        let catalog = view!(NavigationPage {
            key: 1,
            title: "Catalog",
            children: SystemComponent {
                key: 2,
                kind: NodeKind::Scroll,
                primary: "",
                secondary: "",
                event: None,
                flags: FLAG_VISIBLE,
                value: Value::default(),
                style: Style::default(),
                layout: Layout::default(),
                children: (
                    SystemComponent {
                        key: 3,
                        kind: NodeKind::Slider,
                        primary: "Brightness",
                        secondary: "65%",
                        event: Some(ManagerEvent::SafeMode),
                        flags: FLAG_VISIBLE | FLAG_ENABLED,
                        value: Value {
                            value: 65,
                            minimum: 0,
                            maximum: 100,
                            step: 5,
                            resource_id: 0,
                        },
                        style: slider_style,
                        layout: slider_layout,
                        children: (),
                    },
                    SystemComponent {
                        key: 4,
                        kind: NodeKind::Image,
                        primary: "Module icon",
                        secondary: "",
                        event: None,
                        flags: FLAG_VISIBLE,
                        value: Value {
                            resource_id: 0xCA10,
                            ..Value::default()
                        },
                        style: Style::default(),
                        layout: Layout::default(),
                        children: (),
                    },
                ),
            },
        });

        let mut tree = Tree::begin();
        catalog.render(&mut tree).unwrap();
        let snapshot = tree.commit().unwrap();
        assert_eq!(
            snapshot.find_by_key(3).unwrap().kind(),
            Some(NodeKind::Slider)
        );
        assert_eq!(snapshot.value(2).unwrap().value, 65);
        assert_eq!(
            snapshot.style(2).unwrap().variant,
            ComponentVariant::Tonal as u16
        );
        assert_eq!(snapshot.layout(2).unwrap().width, 240);
        assert_eq!(snapshot.value(3).unwrap().resource_id, 0xCA10);
    }

    #[test]
    fn typed_image_and_progress_preserve_semantics() {
        let image_layout = Layout {
            width: 180,
            height: 180,
            align: Alignment::Center as u8,
            ..Layout::default()
        };
        let progress_layout = Layout {
            width: 280,
            height: 12,
            ..Layout::default()
        };
        let view = NavigationPage {
            key: 1,
            title: "Media",
            children: (
                Image {
                    key: 2,
                    resource_id: 7,
                    source: "/data/canopus/test.bin",
                    layout: image_layout,
                },
                Progress {
                    key: 3,
                    value: 25,
                    minimum: 0,
                    maximum: 100,
                    layout: progress_layout,
                },
            ),
        };
        let mut tree = Tree::begin();
        <_ as View<ManagerEvent>>::render(&view, &mut tree).unwrap();
        let snapshot = tree.commit().unwrap();
        assert_eq!(
            snapshot.find_by_key(2).unwrap().kind(),
            Some(NodeKind::Image)
        );
        assert_eq!(snapshot.value(1).unwrap().resource_id, 7);
        assert_eq!(snapshot.layout(1).unwrap(), &image_layout);
        assert_eq!(
            snapshot.find_by_key(3).unwrap().kind(),
            Some(NodeKind::Progress)
        );
        assert_eq!(snapshot.value(2).unwrap().value, 25);
        assert_eq!(snapshot.value(2).unwrap().maximum, 100);
        assert_eq!(snapshot.layout(2).unwrap(), &progress_layout);
    }

    #[test]
    fn typed_image_and_progress_reject_invalid_inputs() {
        let mut tree = Tree::begin();
        tree.navigation_page(1, "Invalid").unwrap();
        assert_eq!(
            tree.image(2, 0, "missing", Layout::default()),
            Err(UiError::Argument)
        );
        assert_eq!(
            tree.progress(3, 0, 0, 0, Layout::default()),
            Err(UiError::Argument)
        );
        assert_eq!(
            tree.progress(4, 101, 0, 100, Layout::default()),
            Err(UiError::Argument)
        );
    }

    #[test]
    fn catalog_rejects_invalid_ranges_and_metadata() {
        let mut tree = Tree::begin();
        tree.navigation_page(1, "Invalid").unwrap();
        assert_eq!(
            tree.component(
                2,
                NodeKind::Slider,
                ComponentSpec {
                    event_id: 1,
                    value: Value {
                        value: 101,
                        minimum: 0,
                        maximum: 100,
                        step: 1,
                        resource_id: 0,
                    },
                    ..ComponentSpec::default()
                }
            ),
            Err(UiError::Argument)
        );
        let style = Style {
            opacity: 1001,
            ..Style::default()
        };
        assert_eq!(tree.set_style(1, style), Err(UiError::Argument));
        let layout = Layout {
            width: -2,
            ..Layout::default()
        };
        assert_eq!(tree.set_layout(1, layout), Err(UiError::Argument));
    }

    #[test]
    fn fluent_system_component_customizes_semantics_and_metadata() {
        let checkbox = SystemComponent::leaf(2, NodeKind::Checkbox, "Telemetry")
            .secondary("Send bounded diagnostics")
            .event(ManagerEvent::SafeMode)
            .enabled(true)
            .checked(true)
            .selected(true)
            .wrap(true)
            .style(Style {
                variant: ComponentVariant::Outlined as u16,
                ..Style::default()
            })
            .layout(Layout {
                min_height: 44,
                ..Layout::default()
            });
        let view = NavigationPage {
            key: 1,
            title: "Settings",
            children: checkbox,
        };
        let mut tree = Tree::begin();
        view.render(&mut tree).unwrap();
        let snapshot = tree.commit().unwrap();
        let node = snapshot.find_by_key(2).unwrap();
        assert_eq!(node.kind(), Some(NodeKind::Checkbox));
        assert!(node.enabled());
        assert!(node.checked());
        assert_ne!(node.flags & FLAG_SELECTED, 0);
        assert_ne!(node.flags & FLAG_WRAP, 0);
        assert_eq!(snapshot.style(1).unwrap().variant, 3);
        assert_eq!(snapshot.layout(1).unwrap().min_height, 44);
    }

    #[test]
    fn declarative_views_copy_model_borrowed_text() {
        let snapshot = {
            let title_storage = *b"Borrowed title";
            let value_storage = *b"runtime value";
            let title = core::str::from_utf8(&title_storage).unwrap();
            let value = core::str::from_utf8(&value_storage).unwrap();
            let view = NavigationPage {
                key: 1,
                title,
                children: StatusRow {
                    key: 2,
                    label: "State",
                    value,
                },
            };
            let mut tree = Tree::begin();
            <_ as View<u32>>::render(&view, &mut tree).unwrap();
            tree.commit().unwrap()
        };

        assert_eq!(
            snapshot.primary(snapshot.node(0).unwrap()),
            "Borrowed title"
        );
        assert_eq!(
            snapshot.secondary(snapshot.node(1).unwrap()),
            "runtime value"
        );
    }

    #[test]
    fn c_abi_backend_forwards_snapshot_synchronously() {
        unsafe extern "C" fn apply(
            cookie: *mut core::ffi::c_void,
            snapshot: *const Snapshot,
        ) -> i32 {
            if cookie.is_null() || snapshot.is_null() {
                return -1;
            }
            // SAFETY: the test creates both pointers from live, aligned values
            // and the callback does not retain either pointer.
            unsafe {
                *cookie.cast::<u32>() = (*snapshot).generation;
            }
            0
        }

        let mut observed = 0u32;
        // SAFETY: `observed` outlives the adapter and `apply` obeys the
        // synchronous callback contract.
        let mut backend = unsafe {
            CAbiBackend::new(
                core::ptr::addr_of_mut!(observed).cast::<core::ffi::c_void>(),
                apply,
            )
        };
        let snapshot = Snapshot::empty();
        backend.apply(&snapshot).unwrap();
        assert_eq!(observed, 1);
    }

    #[test]
    fn application_runtime_decodes_drains_rebuilds_and_commits_transactionally() {
        #[derive(Copy, Clone)]
        enum Message {
            Increment,
            AddTen,
            Cycle,
        }

        impl From<Message> for u32 {
            fn from(message: Message) -> Self {
                match message {
                    Message::Increment => 41,
                    Message::AddTen => 42,
                    Message::Cycle => 43,
                }
            }
        }

        struct App;

        impl Application for App {
            type Model = u32;
            type Message = Message;

            fn decode_message(event_id: u32) -> Option<Self::Message> {
                match event_id {
                    41 => Some(Message::Increment),
                    42 => Some(Message::AddTen),
                    43 => Some(Message::Cycle),
                    _ => None,
                }
            }

            fn update(
                &mut self,
                model: &mut Self::Model,
                message: Self::Message,
                commands: &mut impl CommandSink<Self::Message>,
            ) {
                match message {
                    Message::Increment => {
                        *model += 1;
                        let _ = commands.submit(Command::Emit(Message::AddTen));
                    }
                    Message::AddTen => *model += 10,
                    Message::Cycle => {
                        let _ = commands.submit(Command::Emit(Message::Cycle));
                    }
                }
            }
        }

        struct Backend {
            applied: Snapshot,
            apply_count: u32,
            fail: bool,
        }

        impl SnapshotBackend for Backend {
            fn apply(&mut self, snapshot: &Snapshot) -> Result<(), UiError> {
                self.apply_count += 1;
                if self.fail {
                    return Err(UiError::Backend);
                }
                self.applied = snapshot.clone();
                Ok(())
            }
        }

        fn render(model: &u32, tree: &mut Tree) -> Result<(), UiError> {
            tree.navigation_page(1, "Runtime")?;
            tree.button(2, "Increment", 41, true)?;
            tree.button(3, "Cycle", 43, true)?;
            tree.status_row(4, "Value", if *model == 0 { "0" } else { "11" })?;
            tree.end()
        }

        let backend = Backend {
            applied: Snapshot::empty(),
            apply_count: 0,
            fail: false,
        };
        let mut runtime: ApplicationRuntime<App, Backend, 2> =
            ApplicationRuntime::new(App, 0, backend);
        runtime.rebuild(render).unwrap();
        assert_eq!(runtime.current().generation, 1);

        runtime.dispatch_event(1, 2, 41, render).unwrap();
        assert_eq!(*runtime.model(), 11);
        assert_eq!(runtime.current().generation, 2);
        assert_eq!(runtime.backend().applied.generation, 2);
        assert_eq!(runtime.backend().apply_count, 2);

        assert_eq!(
            runtime.dispatch_event(1, 2, 41, render),
            Err(UiError::StaleGeneration)
        );
        assert_eq!(runtime.dropped_events(), 1);

        runtime.backend_mut().fail = true;
        assert_eq!(
            runtime.dispatch_event(2, 2, 41, render),
            Err(UiError::Backend)
        );
        assert_eq!(runtime.current().generation, 2);
        assert_eq!(runtime.backend().applied.generation, 2);

        runtime.backend_mut().fail = false;
        assert_eq!(
            runtime.dispatch_event(2, 3, 43, render),
            Err(UiError::Capacity)
        );
        assert!(runtime.commands.is_empty());
        assert_eq!(runtime.current().generation, 2);
    }

    #[test]
    fn navigation_header_and_router_form_a_bounded_page_frame() {
        #[derive(Copy, Clone, Debug, PartialEq, Eq)]
        enum Route {
            Overview,
            Modules,
            Detail,
        }

        let mut router: Router<Route, 3> = Router::new(Route::Overview).unwrap();
        let root_generation = router.generation();
        router.push(Route::Modules).unwrap();
        let modules_generation = router.generation();
        router.push(Route::Detail).unwrap();
        assert_eq!(router.push(Route::Detail), Err(UiError::Capacity));
        assert_eq!(
            router.back(modules_generation),
            Err(UiError::StaleGeneration)
        );
        assert_eq!(router.back(router.generation()), Ok(Route::Modules));
        assert_eq!(router.depth(), 2);
        router.replace(Route::Modules);
        assert_ne!(router.generation(), root_generation);
        router.pop_to(Route::Overview).unwrap();
        assert_eq!(router.current(), Route::Overview);
        assert_eq!(router.pop(), Err(UiError::State));

        let header = NavigationHeader {
            key: 2,
            title: "Modules",
            subtitle: "2 installed",
            back: Some(ManagerEvent::SafeMode),
            centered: true,
            elevated: false,
            style: Style::default(),
            layout: Layout::default(),
            children: (),
        };
        let view = NavigationPage {
            key: 1,
            title: "Canopus",
            children: header,
        };
        let mut tree = Tree::begin();
        view.render(&mut tree).unwrap();
        let snapshot = tree.commit().unwrap();
        let node = snapshot.find_by_key(2).unwrap();
        assert_eq!(node.kind(), Some(NodeKind::NavigationHeader));
        assert_eq!(snapshot.primary(node), "Modules");
        assert_eq!(snapshot.secondary(node), "2 installed");
        assert_ne!(node.flags & FLAG_HEADER_BACK, 0);
        assert_ne!(node.flags & FLAG_HEADER_CENTERED, 0);
        assert!(node.enabled());
    }

    #[test]
    fn bounded_state_list_and_command_queue_are_allocator_free() {
        let mut state = State::new(4u32);
        {
            let mut binding = state.binding();
            binding.update(|value| *value += 3);
            assert_eq!(*binding.get(), 7);
            binding.set(9);
        }
        assert_eq!(*state.get(), 9);

        let mut list: BoundedList<u32, 2> = BoundedList::new();
        assert!(list.is_empty());
        list.push(10).unwrap();
        list.push(20).unwrap();
        assert_eq!(list.push(30), Err(UiError::Capacity));
        assert_eq!(list.iter().copied().sum::<u32>(), 30);
        assert_eq!(list.remove(0), Some(10));
        assert_eq!(list.pop(), Some(20));
        assert!(list.is_empty());

        let mut commands: CommandQueue<u32, 2> = CommandQueue::new();
        commands.submit(Command::Emit(7)).unwrap();
        commands.submit(Command::Rebuild).unwrap();
        assert_eq!(commands.submit(Command::Emit(8)), Err(UiError::Capacity));
        assert_eq!(commands.pop(), Some(Command::Emit(7)));
        commands.submit(Command::Emit(8)).unwrap();
        assert_eq!(commands.pop(), Some(Command::Rebuild));
        assert_eq!(commands.pop(), Some(Command::Emit(8)));
        assert_eq!(commands.pop(), None);

        let mut zero: CommandQueue<u32, 0> = CommandQueue::new();
        assert_eq!(zero.submit(Command::Rebuild), Err(UiError::Capacity));
    }
}
