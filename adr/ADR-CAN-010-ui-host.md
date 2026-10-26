# ADR-CAN-010：原生 UI 宿主后端与模块页面

状态：host 实现完成；Mirage 上 Band 10 Pro `.043` 与 Band 11 `.155` 的 Manager 与模块页面端到端通过，两个家族的控件、渲染与操作方式对齐（2026-10-18）。**2026-10-19 起：`.043` 与 `.155` 真机验证通过（维护者反馈）并批准为生产可用**，见「证据与限制」末条；其它 target 保持原状态。

## 背景

`ctx->ui` 与 `sdk/c/canopus_ui.h` 提供语义 UI 树（页面、分区、各类行、文本、图片、进度、对话框……），但 Supervisor 没有给模块的
渲染后端：唯一接到固件控件上的后端是 Manager 自己的 `target_ui_apply`，它焊在 Manager 的三个页面上（页面下标、确认对话框
靠 Manager 专有的事件号识别、返回键靠页面下标）。第三方模块要在屏幕上画东西，只能自己调各 target 的固件控件
（如 `native-app-demo` 的 `FW_LVX_*`）或自带后端。

## 决定

1. **通用后端 `manager/target/canopus_ui_host.{h,c}`**（Band 10 Pro 与 Band 11 同一份源，`CANOPUS_MGR_BAND11` 只区分固件入口）：
   把一个语义快照画到一个固件页面上，并把行激活作为语义事件送回。Manager 的页面与模块打开的页面都是它的 *view*。
   - 两个家族画同样的东西：导航页/导航头（页面标题）、分区/列表/滚动/分隔/间距（展平进页面的滚动主体）、文本与富文本（真正的标签，
     MiSans-Medium：TITLE 28 px，其余 24 px；宽度为一行宽、自动换行）、状态/操作/开关/复选/单选行、按钮、图片、进度条、对话框。
   - 滑块已移除（`CANOPUS_UI_NODE_RETIRED_SLIDER`，17 号保留不再使用）：它只能画成只读的条，不如不提供。
   - 对话框：消息加两个按钮的对话框用固件 msgbox（文字取第一个文本，按钮取前两个按钮/操作行，按树序即从左到右）：
     Band 10 Pro 用 mode 2（两个宽文字按钮），Band 11 用 mode 4（上下两个窄按钮，放得进 212 px 的屏幕）。
     没有恢复 msgbox 入口的目标和其它形状的对话框，子节点按普通行内联显示。
   - 不支持的节点返回失败并保留上一版内容（后端契约）；节点数受 `CANOPUS_UI_MAX_NODES`=32 约束。
   - 不再出现在页面里的行、标签、图片、进度条：隐藏（`lv_obj_add_flag` HIDDEN），再出现时显示；行按 key 复用控件槽，
     所以重绘不会重建行，也保持滚动位置（整条放置链不变就不重新对齐）。内容放得下时关掉内容容器的滚动（`SCROLLABLE`），避免拖动把内容甩到底部。
   - 事件沿用原有机制（每槽一个静态回调，`interactive` 的 view 接收），回调数统一为 32；标题返回与对话框按钮同样是静态回调。
   - 命中目标（Band 11）：原生事件只投递给手指命中的那个对象（`event` 的目标在 +4），预制行的标题/描述文字是行内部的独立对象
     （list item 字段 `+0x3c`/`+0x40`，trailing 在 `+0x54`），合成行的标签同理。行的回调除容器外也注册到这些命中对象上，
     分发时白名单即该集合（兼做换页后的过期事件校验），标题组件同此（icon/label 双目标）。因此文字区域与行表面同等可点，
     副标题标签由固件懒创建时在 updater 路径补注册。10 Pro 无需此机制且不移植（实测 + 反编译证据：
     `evidence/band10pro-row-text-hit/`，10 Pro 的行对象从行内任意位置含文字都收得到事件）。
   - Manager 专有的部分变成可选钩子（返回键、事件前后、渲染完成）；确认对话框改为语义 `DIALOG`（取消、确认的顺序即 msgbox 的左右顺序）。
2. **模块页面**：`ctx->ui` 直接提供 `set_entry`、`close_page`、`route`、`node_set_style`、`node_set_layout`、`node_set_visual`、`control`、`post`。
   早期的 `ctx->ui_ext` 已并入 `ctx->ui` 并删除（Canopus 未发版，公共 ABI 直接演进到 2.0，不保留旧对象、旧前缀或适配层）。
   - 模块 `set_entry` 登记页面入口（标签、`on_open`/`on_event`/`on_back`/`on_close`）；模块运行期间，Manager 的模块详情页多一行「打开界面」。
   - 页面由 Manager 应用的第 4 个固件页面（`module_ui`）承载：打开时在固件堆上分配 view 与 UI 上下文，调用 `on_open` 构树并提交；
     提交失败或没有提交则取消页面。同一时刻只开一个模块页面；模块卸载时入口先被清除，再关闭其页面，回调不再被调用。
   - 标题返回（两个家族）先交给 `on_back`（返回 0 表示已处理，例如弹出自己的路由），否则关闭页面。Band 11 的原生标题点一下本来就会发返回键；
     宿主把标题的点击回调换成自己的，所以 Manager 与模块页面的返回走同一条路。
     在精确 `.043` / `.155` / `.179` 目标上，模块页也通过描述符 `+104` 拦截完成阶段的系统右滑，调用同一个 `on_back`；
     无内部返回可处理时，由宿主关闭页面一次。进度/取消阶段不触发模块返回；其它目标保持原有系统手势行为。
     该拦截同时覆盖 Manager 的「打开界面」和模块启动器入口，不改变 Manager 自身页面的系统返回。
   - 页面标题可随路由更新（ADR-CAN-013）：标题文本或返回状态变化时重建标题对象，过期的标题事件按事件目标（Band 11）
     或注册令牌（Band 10 Pro）校验后丢弃，不会驱动新页面。
   - 多页条目（2026-10-26）：`canopus_ui_entry_v1.page_count` 大于 0 时，target 在发布阶段为该 entry 申请 `page_count`
     个固件页描述符（`CANOPUS_UI_ENTRY_PAGES_MAX` = 8），`on_open` 服务 route 0，之后 `ctx->ui->route` 前进用
     `page_goto`（暂停源页并播 stock push）、后退用 `page_finish`（播 stock pop，下方页面 `on_resume` 恢复）。
     一个 UI 上下文约 12 KB 固件堆，因此只有栈顶页持有它：`on_pause` 释放，`on_resume` 用 `on_create` 记下的 root 重建，
     下层页面靠已绘制且被固件冻结的原生树参与转场。`page_count` 为 0 时保持原单页契约，`route` 返回 `UNSUPPORTED`。
     系统右滑完成阶段若模块 decline（`on_back` 返回非 0），描述符 +104 返回 0，把默认 finish 与 page-out 动画让给固件；
     只有模块内部消费（route pop、替换树、自行 `close_page`）才返回 1。
3. **启动器入口**：模块自己的启动器图标见 ADR-CAN-011；它复用这里的模块页面。
4. **Supervisor 构建预算**：Band 10 Pro 的 `supervisor_max_size` 由 98,304 提到 114,688。新增代码和 4 个视图使 .043 达到约 104 KB。
   该值本来就是构建期预算而不是实测的固件上限（stock modlib 没有固定的插入上限，Band 11 用 131,072）。
   这个预算没有在真机上验证过。

系统右滑的精确目标 ABI、描述符父链查找、借用载荷与返回值依据见
[系统返回调查](../docs/investigations/native-system-back.md)。`.155` / `.179` 完成阶段为 `0x48`，`.043` 为 `0x4a`；
模块回调自行关页后仍消费本次手势，避免固件继续执行默认关闭。`on_back` 返回后，宿主检查页面是否仍存在、是否已停用，避免再次关闭或访问已释放的页面。
回归 `python3 -m unittest discover -s scripts/tests -p 'test_module_page_back.py'` 编译实际 ARM target 源码，
覆盖两种描述符初始化、完成/非完成阶段、内部返回、根页退出、外来/暂停/过期页面及同步和异步自行关页；固件入口与渲染由模型替代。
这次改动属于静态证据与 host/emulator 检查，没有新增真机验证，也不改变 restricted / APPROVED 或本文已有能力状态。

## Incremental snapshot application

The backend compares rendered properties with the context's existing committed
snapshot; it does not keep another snapshot per view. Identical body commits
refresh event bindings without repeating native row, label, image, progress or
visibility setters. Text and row comparisons use exact string contents, so
changes to string-arena offsets do not count as content changes. Image source
updates also consider `resource_id`; change it to refresh an image whose file
was replaced at the same path.

Placement still uses the existing placement chain. A row-content-only update or
an image-source-only update at the same explicit dimensions does not realign
controls. Changing order, count or layout inputs can still realign controls and
affect the scroll offset. Transforms, touch capture and virtual content are
separate ABI 2.0 features (ADR-CAN-013); they reuse this incremental apply and
do not change its comparison rules.

Native switches mark their row dirty before event dispatch, so submitting the
unchanged model restores a rejected toggle. A failed backend apply invalidates
property comparisons until a successful retry; this repairs partial native
updates but does not introduce transactional native rollback. Visibility tracks
actual writes independently. Unchanged dialogs retain their native box and
refresh button bindings to the new generation; changed message or button text
recreates the box.

Host regression: `python3 -m unittest discover -s scripts/tests -p 'test_ui_backend.py'`
executes the real backend and UI runtime with counted firmware substitutes for
both families. Set `CANOPUS_UI_BACKEND_SANITIZE=1` to enable ASan/UBSan; CI runs
that variant in its C host gate. These counts measure avoided native API calls,
not device frame rate or physical rendering.
The optimization itself has no new device validation or capability promotion.

## Native image fit and font size capability matrix (2026-10-23)

UI ABI remains 2.0. Exact `.043` and `.155` firmware analysis confirms independent
Q8 image scales (256 is identity), a 12-byte native source-header query, center
alignment and native parent clipping. `lvx_image_set_scale` is bound privately as
`FW_UI_IMAGE_SCALE`, and `lv_image_decoder_get_info` as `FW_UI_IMAGE_INFO`.
`.043` uses corrected runtime callables `0x1c06c971` / `0x1c036359`; `.155` uses
`0x0c3ae46f` / `0x0c38e4e5`. Arbitrary `font_px` (`CANOPUS_UI_CAP_FONT`, bit 17)
is bound privately on `.043` / `.155` via managed factory `native_font_create`
(`0x0c85f6cd` / `0x0c494381`), `native_font_release` (`0x0c85f8fd` / `0x0c917385`),
and `native_font_wait` (`0x0c15c061` / `0x0c399941`), with UIKit root-slot manager
`*(root + 28)` and local style property 90 (`0x5a`). Evidence:
`EVID-UI-1043-IMAGE-FIT-001`, `EVID-UI-4155-IMAGE-FIT-001`,
`EVID-UI-1043-FONT-LIFECYCLE-001`, `EVID-UI-4155-FONT-LIFECYCLE-001`,
`EVID-UI-DEFENSIVE-ABLATION-001`, and `EVID-UI-FIT-FONT-HOST-001`.

| Native capability | Firmware primitive / binding | `.043` / `.155` | Other active targets |
|---|---|---|---|
| `IMAGE_FIT_NATIVE` | Existing native image source and box | Existing behavior | Existing behavior |
| `IMAGE_FIT_CONTAIN` | Scale + decoder query; native center | Host implemented; uniform minimum ratio, rounded down | `UNSUPPORTED`, bit off |
| `IMAGE_FIT_COVER` | Scale + query + clipped parent viewport | Host implemented; uniform maximum ratio, rounded up, center crop | `UNSUPPORTED`, bit off |
| `IMAGE_FIT_STRETCH` | Independent native X/Y scale + query + clipped viewport | Host implemented; per-axis ratio, rounded up | `UNSUPPORTED`, bit off |
| `font_px = 0/24/28` | `FW_LVX_STYLE_APPLY`, borrowed MiSans-Medium styles | Fixed styles, counted regression coverage | Same existing contract |
| Arbitrary `font_px` (`CAP_FONT`) | `FW_UI_FONT_CREATE` (`manager, &key`), property 90 setter, `DELETE` event 36 + `FW_UI_FONT_WAIT` + `FW_UI_FONT_RELEASE` | Host implemented for labels, section headings and checkbox primary text; stock rows/titles/msgbox return `UNSUPPORTED` | `UNSUPPORTED`, bit off |

Fit uses the resolved semantic box, preflights decoding and scale range before
publication, and preserves unchanged setters. Q8 quantization cannot represent
every exact ratio; cover/stretch clip the rounded-up excess. Fitted images use a
page-owned viewport and child, reusing an otherwise unused slot pointer without
adding persistent view state. Returning to NATIVE stages a new image with
identity scales. This preserves native ownership on failures and subtree deletion.

Arbitrary `font_px` caches one managed wrapper per distinct `(manager, font_px)`
and binds reference ownership to each actual native label via an independent heap
record registered on native `DELETE` (event 36, userdata at `event + 12`), plus
per-apply preflight pins before input cancellation. Zero-reference cache entries
are released only after checking that no display has rendering bit `byte 58 & 2`,
no linked layer has queued draw tasks (`layer + 100`), and no draw unit has an
active CPU task (`unit + 32`), then invoking `native_font_wait(vg_unit)` to drain
pending CPU glyph references before `native_font_release(manager, font)`.

Before implementation, `.155` measured 130,696 file / 182,014 resident bytes.
With `-Oz` on `canopus_ui_host.c`, image fit measured 129,736 / 180,646 bytes.
Adding arbitrary font sizing brought `.155` to 131,440 / 182,062 bytes before
trimming (+1,704 / +1,416 bytes over image fit, 368 bytes above the 131,072 file
threshold). Empirical defensive-code ablation ([ablation report](../docs/native-ui-defensive-ablation.md))
and minimal font-integration trimming removed 428 file and 420 resident bytes on
`.155`, finishing at **131,012 file / 181,642 resident bytes** (`.043` **124,740 /
162,546 bytes**). `supervisor_max_size` in both family TOMLs is raised `131072 ->
133120` (+2,048 B small build margin; **build threshold, not a proven hardware
loader limit, and device loading remains unproven / `not_probed`**);
`supervisor_max_resident` (`163840` / `184320`) is unchanged.

See [verification and per-target budgets](../docs/native-ui-image-font-verification.md),
[managed-font ABI and retirement](../docs/native-ui-font-lifecycle-abi.md), and
[defensive-code ablation](../docs/native-ui-defensive-ablation.md).
The implementation adds **host/static evidence only**. No symbol or capability
is promoted to device verified or production approved; restricted records retain
their existing approval state (`PENDING`), and no new firmware callable is public.

## Overall opacity across targets

`canopus_ui_style_v1.opacity` is a target-independent value in `0..1000`.
Nonzero values are explicit; zero inherits unless
`CANOPUS_UI_VISUAL_FLAG_OPACITY` is present, in which case it means fully
transparent. The native host converts explicit values to `0..255` and uses the
exact target's private overall-opacity property through its reviewed local
style dispatcher. Background opacity is a separate property and cannot replace
whole-object opacity. Clearing `CANOPUS_UI_NODE_FLAG_ENABLED` only gates semantic
events; applications set opacity explicitly when they want a dimmed appearance.

```text
opacity = 1000   -> opaque
opacity =  500   -> about 50% opacity
opacity =    0   -> inherited (default)
opacity =    0 + VISUAL_FLAG_OPACITY -> fully transparent
```

Repeated identical commits do not repeat native setters. Explicit opacity
changes reuse the keyed object; returning from explicit opacity to inheritance
rebuilds its subtree because the host has no reviewed single-property removal
binding. Overall opacity is written only to the semantic node's native object.
Internal section headings and synthesized row labels inherit it, avoiding a
second multiplication of the same opacity. Native msgbox button styling remains
outside this contract.

| Target | Overall opacity | Private dispatcher |
|---|---|---|
| Band 10 Pro `3.101.036` | `FW_UI_STYLE_OPA 95u`; `EVID-UI-1036-OPACITY-001` | SRAM callable `0x002BD5FD`, independently confirmed by the stock XIP veneer |
| Band 10 Pro `3.101.043` | `FW_UI_STYLE_OPA 95u`; `EVID-UI-1043-OPACITY-001` | SRAM callable `0x002BD925` |
| Band 11 `4.100.139` | `FW_UI_STYLE_OPA 95u`; `EVID-UI-4139-OPACITY-001` | XIP callable `0x0C3872B5` |
| Band 11 `4.100.155` | Existing property95; `EVID-UI-4155-LAYOUT-INPUT-001` | XIP callable `0x0C3872B5` |
| Band 11 `4.100.179` | Existing property95; `EVID-UI-4179-LAYOUT-INPUT-001` | XIP callable `0x0C383435` |
| Band 9 `3.1.32` | Unsupported: the dedicated v8 backend has no STYLE/VISUAL capabilities or native section hierarchy | No reviewed overall-opacity binding |
| Band 9 Pro `3.1.190` | Unsupported: v8 backend/platform integration is incomplete | No reviewed overall-opacity binding |
| Band 10 `3.1.28` | Unsupported: v8 backend/platform integration is incomplete | No reviewed overall-opacity binding |
| Redmi Watch 6 `3.100.043` | Unsupported: platform integration is incomplete and the style dispatcher remains a candidate | No reviewed overall-opacity binding |

The `.036` flash load copy `0x0C107465` must not execute as a dispatcher:
external relative calls require the copied SRAM address. Its canonical private
symbol and generated metadata use the verified runtime callable instead.
`.139` gains only overall opacity; no other layout/input constants are inferred
from its Band 11 siblings. Applications use the same public ABI on all targets;
unsupported backend fields still return `CANOPUS_UI_ERR_UNSUPPORTED`.
Rebuild and install the target's Supervisor to activate new host bindings.

Opacity is enabled only for targets with independent property and dispatcher
evidence. `native_opacity_requires_an_exact_target_review` checks every active
target's generated configuration, evidence identity, firmware hash and private
dispatcher; unreviewed targets must not acquire `FW_UI_STYLE_OPA` by inheriting
another firmware's constants. The counted host regression checks consecutive
opaque, half-opacity and explicit-zero commits on both native host families.
These are static/host checks, without new device verification or approval.

## 证据与限制

- host 测试（含 ASan/UBSan）覆盖入口登记/校验/随模块释放、`close_page`、无 Manager UI 的构建返回 UNSUPPORTED、
  Manager 详情页的「打开界面」行与路由、确认对话框的节点顺序。
- Band 11 固件仿真（Unicorn，真实固件的行构造函数、主题回调与页面生命周期，其余 LVGL 核心为模型）下，Manager 全部页面经新的宿主后端渲染，
  原有 `scripts/tests/band11_*.py` 全部通过。
- Mirage 端到端（`scripts/emu/ui_pages.py 043|155`）：真实 AstroBox core 装生产安装器表盘，Run 加载 Supervisor 并把 Manager 注册进启动器，
  手机装 `ui-demo`，之后全部靠触摸：启动器 > Manager > 模块 > ui_demo > 启用（确认）> 立即激活（确认）> 打开界面 > 页面
  （开关、点击三次、第二层、返回、退出）。断言：模块自己的探针文件逐行等于期望序列、模块最终为运行状态、每一步截图都亮屏
  （`.043` 的探针是 `pop`——标题返回交给 `on_back`；`.155` 是 `back`——页面里的返回行）。
- 第一版的 Band 10 Pro 宿主在 Mirage `.043` 上遇到三个问题，当时用「文本画成状态行、拒绝图片/进度条、删除不用的行」绕开；现在逐个找到根因并去掉绕行：
  库存标签默认字体没有 CJK 字形（显示缺字、整屏空白）——给标签套主题的 MiSans-Medium 24/28 样式（`lvx_theme_default_init` 数据流恢复）；
  进度条让页面停在条之前——`.043` 的 `lv_bar_create` 记录是错的（指向一个字面量，不是 LVGL 跳板，正确的是 `0x0C588B91`，与 `.036` 同一个库入口）；
  `lv_obj_set_hidden` 对离开页面的行没有效果——这个记录指向的库入口（`0x1C028D39`）既不是 add_flag 也不是 clear_flag，真正的跳板是
  `lvx_object_add_flag`（库 `0x1C028BD9`）与新记录的 `lv_obj_clear_flag`（库 `0x1C028C85`）。`lv_obj_set_hidden` 的 APPROVED 记录与导出的
  `canopus_fw_lv_obj_set_hidden` 名不副实，本改动没有碰它，宿主不再使用，建议另行复核。
- 新的静态证据（restricted / PENDING / 仍无设备验证）：`EVID-UI-1036-HOST-001`、`EVID-UI-1043-HOST-001`（Band 10 Pro 的 Medium 24/28 样式、clear_flag、
  `.043` 的 bar_create 修正，该记录批准状态重置为 PENDING）；`EVID-UI-4139-MSGBOX-001`、`EVID-UI-4155-MSGBOX-001`（Band 11 的 `lvx_msgbox_create`
  `0xc50fdbc`、`lvx_msgbox_set_content` `0xc50edd4`、`lvx_object_delete` `0xc380404`；之前的 ensemble 候选分别是字体度量函数、闹钟返回处理和 memset，已纠正）。
  Band 11 的 msgbox 按钮记录是 28 字节（Band 10 Pro 20 字节），多出的字段保持为零。
- Band 10 Pro 的 `ui_demo` 在 Mirage 里：手机装模块后系统通知会盖住下一次亮屏，脚本因此在装完后重启再 Run 一次；
  模拟器的屏幕几秒就熄，页面推入或滚动后立刻点击会被吞掉，这些等待是在 Mirage 上量出来的，不是固件契约。
- 控件覆盖（`ui_pages.py 043|155 --module ui-gallery`，仅 emulator 证据）：表单（开关、复选、单选、按钮）、进度条、文本、图片、对话框（固件 msgbox）、
  样式与布局、模块自己关页，每一页的事件都由模块探针逐行断言，标题返回由 `on_back` 探针断言。Band 11 上它查出并修掉了：单选行画得像普通操作行、看不出选中
  （现在带开关显示）；页面重绘把滚动位置重置到顶部；页面里内容放得下时拖动会把内容甩到底部且不回弹；模块提交失败或树没配平会让页面永久卡死
  （Manager 在每个模块回调返回后回收没提交也没取消的树）。文本的 DESCRIPTION / WARNING 样式在两个家族上与 BODY 看起来相同（只有 TITLE 换字体），
  分隔线与间距不画，`node_set_style` / `node_set_layout` 的样式变体与窄宽度也还没有落到控件上。
- **真机验证与批准（2026-10-19）**：维护者在真机 `.043`（Band 10 Pro 3.101.043）与 `.155`（Band 11 4.100.155）上运行了本后端，反馈 Manager 与模块页面（表单、进度条、文本、图片、对话框、标题返回）都正常，
  并授权把相关符号与能力状态提升为生产可用。落实为：UI 宿主在这两个 target 上调用的固件入口（`.043` 28 个、`.155` 30 个，清单钉死在
  `tools/canopus-core/tests/generated_stability.rs` 的 `DEVICE_VERIFIED_UI`）改为 `DEVICE_PROVEN` / `APPROVED`，`proof.device = proven`，附 promotion 记录，
  证据为 `EVID-UI-1043-DEVICE-001`、`EVID-UI-4155-DEVICE-001`；Band 11 原生地址头生成器随之接受 `DEVICE_PROVEN`。
  **策略不变**：这些符号保持 `restricted`（目标私有，Supervisor 的 Manager 使用），没有给模块开放新的公开固件调用；生成物只有输入摘要变化。
  限制：证据是维护者的口头反馈，仓库里没有设备日志或录屏；仅覆盖本后端的这些调用路径，不含 Supervisor 安装路径与注销生命周期；`.036`、`.139` 没有真机验证，状态不变。
  **纠正 `lv_obj_set_hidden`（2026-10-19）**：`.036`/`.043` 的这条记录（APPROVED、native-full-trust，库入口 `0x1C028D39`）实际是 LVGL 的 `lv_obj_remove_state(obj, state)`，不是可见性开关；
  桩代码的调用点（禁用项用 `0x80` = `LV_STATE_DISABLED` 成对调用 `0x1C028D09`/`0x1C028D39`）证明了这一点，`set_hidden(obj, 1)` 只是清 `CHECKED` 位。LVGL 9 没有 set_hidden，隐藏就是对 HIDDEN 标志的 `add_flag` / `clear_flag`，UI 宿主用的正是这对。
  两个 target 的记录已改名为 `lv_obj_remove_state`，降为 `restricted` / `PENDING`（旧名下的批准不适用于新身份），不再生成公开封装；唯一的调用方是 Rust 门面 `canopus-target-private` 的 `lvx_set_hidden`（`.036/.043` 一直接在这条记录上，所以没有隐藏效果），现已改成与 Band 11 相同的 add_flag / clear_flag 实现，`.043` 的 `lvx_object_add_flag` 也由「fail closed」空桩改为真实调用；证据见 `EVID-UI-1036-HOST-001`、`EVID-UI-1043-HOST-001`。
  Band 11 `.139/.155` 上同名的记录是从 `.043` 按函数体匹配出来的低置信度 CANDIDATE（restricted / PENDING，无人使用），沿用了错误的名字，没有改动；Band 11 的隐藏用的是 `lvx_object_add_flag` / `lv_obj_clear_flag`。
