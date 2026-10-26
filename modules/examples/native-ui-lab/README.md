# Native UI Lab

A target-bound C example for the public Canopus UI ABI 2.0. The same source uses
`ctx->ui` on Band 10 Pro and Band 11; it reads backend capabilities before
requesting optional input, scrolling or image modes.

The home page opens four independent examples:

- **Card & overlay**: nested sections, horizontal layout, absolute image/text
  placement and controlled RGB colours. The update row adds a section heading and
  changes colour, radius, border and opacity, then restores inherited styles on
  the next activation while the overlay keeps its explicit text dimensions.
- **100 items**: a native scroll viewport with a 7,200 px virtual content range.
  The module keeps all item state internally and emits only a ten-row window.
  `SCROLL` events replace that window; selecting a row records its logical item.
- **Drag**: pointer capture, viewport coordinates and immediate translation with
  `control()`. The tile remains bound until release or cancellation.
- **Selections**: checkbox, radio rows and a title/back-state toggle.

An explicit navigation header dispatches its `BACK` event to `on_event`. On a
subpage that returns to the module home; on the home page it calls
`ctx->ui->close_page(ctx)` to return to Manager. Header containers are closed
before body nodes are appended.

The root `NAVIGATION_PAGE` key identifies the route: the lab uses
`1u + route * 10000u`, keeping that key stable for updates within a page and
assigning a different one to each route. Changing the root key resets the shared
content viewport, replaces the previous route's native state and cancels its
pointer/capture binding. Updating only the title or back visibility preserves
content scroll. The header key is not a route identifier. The virtual list keeps
its scroll offset in the module model and supplies `visual.scroll_y` when that
route is rendered again; visible item keys remain tied to their logical items.

Build and package from the repository root:

```sh
target/debug/canopus build --manifest modules/examples/native-ui-lab/Canopus.toml
target/debug/canopus package --manifest modules/examples/native-ui-lab/Canopus.toml
```

For isolated Mirage validation, first build the Supervisor installer resources
for the chosen target, then run:

```sh
python3 -u scripts/emu/native_ui_v2.py 155 \
  --library "$CANOPUS_UI_TEST_LIBRARY" \
  --installer-dir watchfaces/canopus-installer-prod/xiaomi-band-11 \
  --work "$CANOPUS_UI_EVIDENCE_DIR"
```

Set `MIRAGECTL`, `MAB_SMOKE` and `FACETORY_PACKAGER` to the local tools; see
`docs/mirage-emulator.md` for their requirements. The library must be separate
from the daily Mirage library. The driver creates a new test device and removes
it after the run unless `--keep-device` is supplied.

The lab posts work from `on_open` and card-update callbacks, then calls
`sleep_ms(1)` before returning. Its trace must show three later foreground jobs
and no reentrant execution. Checkbox activation also changes fixed-row padding.

The lab writes `ui_probe.txt` through its module-private filesystem solely as an
integration-test trace. This is not application state: rendering, item selection,
scroll windows and drag state live in the module's in-memory model. Applications
that exchange state should use an internal snapshot plus `ctx->bus` change
notifications and `ctx->ui->post` for UI updates.

`run.json` records target, firmware, capability gates, Supervisor/package hashes
and screenshot hashes. A successful run must reach every page, receive selection
and title/back-state events, and close the module page; pointer/scroll assertions apply
only when advertised. Missing primitives produce an explicit gated result.
Mirage evidence does not establish real-device performance or production
approval. The exact evidence and remaining limits are recorded in
`adr/ADR-CAN-013-ui-abi-2-lifecycle.md`.
