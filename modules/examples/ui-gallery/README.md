# ui_gallery

One sub-page per widget kind of `ctx->ui`, shown through the Manager's "打开界面" (ADR-CAN-010). It runs
on Band 10 Pro (`.036`, `.043`) and Band 11 (`.139`, `.155`, `.177`), which draw the same controls. The title's
back reaches the module's `on_back`: on a sub-page it returns to the list, from the list the page closes.

| Page | Nodes |
|---|---|
| 表单控件 | status row, switch row, two checkboxes, three radio rows (the module keeps them exclusive), button |
| 进度条 | progress bar, action rows that change its value |
| 文本 | text in the four styles, rich text, divider, spacer |
| 图片 | an image (`/data/canopus/manager_icon.bin`) sized through `ctx->ui_ext->node_set_layout` |
| 对话框 | a dialog container with a message and two buttons (the firmware message box; inline where a target has none) |
| 样式与布局 | `node_set_style` / `node_set_layout` |
| 关闭本页 | the module closes its own page with `ctx->ui_ext->close_page` |

Every event appends one line (`<what> <route>/<value>`) to `ui_probe.txt` in the module's private
directory, which the emulator harness reads back:

```bash
python3 scripts/emu/ui_pages.py 043 --module ui-gallery   # or 155
```

Notes for module authors that this module exercises: a dialog container must be closed with `end`, and a tree that is neither committed nor aborted is dropped by the
Manager after the callback returns instead of freezing the page.
