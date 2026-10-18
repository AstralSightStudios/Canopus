# ui_demo

Portable example of a page a module shows through the Manager (ADR-CAN-010). It uses only the
Supervisor service table (`ctx->ui`, `ctx->ui_ext`, `ctx->fs`), so the same ELF loads on Band 10 Pro
and Band 11.

```c
ctx->ui_ext->set_entry(ctx, &entry);     // in activate; set_entry(ctx, NULL) in deactivate
```

While the module runs, its detail page in the Manager offers **打开界面** (detail: the entry label).
Opening it calls `on_open`, which builds the first tree into the `ui` context the Supervisor
hands over and commits it. The page has two levels:

| Level | Content |
|---|---|
| 1 | status row (clicks), switch row, progress bar, "点击" (adds one), "详情页" |
| 2 | a text, the click count, "返回" |

On Band 10 Pro the title's back reaches `on_back` first, which pops from level 2 to level 1 and only
closes the page from level 1. Band 11's stock title finishes the page itself. The module writes
`ui_probe.txt` (`<what> <route>/<clicks>/<switch>`) into its private directory for the emulator harness.

```bash
canopus build   --manifest modules/examples/ui-demo/Canopus.toml
canopus package --manifest modules/examples/ui-demo/Canopus.toml
```
