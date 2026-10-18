# ADR-CAN-010：原生 UI 宿主后端与模块页面

状态：host 实现完成（2026-10-18）；Band 10 Pro / Band 11 的真实固件渲染见下文「证据与限制」。未提升任何符号或能力状态。

## 背景

`ctx->ui` 与 `app-sdk/ui` 提供语义 UI 树（页面、分区、各类行、文本、图片、进度、对话框……），但 Supervisor 没有给模块的
渲染后端：唯一接到固件控件上的后端是 Manager 自己的 `target_ui_apply`，它焊在 Manager 的三个页面上（页面下标、确认对话框
靠 Manager 专有的事件号识别、返回键靠页面下标）。第三方模块要在屏幕上画东西，只能自己调各 target 的固件控件
（如 `native-app-demo` 的 `FW_LVX_*`）或自带后端。

## 决定

1. **通用后端 `manager/target/canopus_ui_host.{h,c}`**（Band 10 Pro 与 Band 11 同一份源，`CANOPUS_MGR_BAND11` 区分固件差异）：
   把一个语义快照画到一个固件页面上，并把行激活作为语义事件送回。Manager 的页面与模块打开的页面都是它的 *view*。
   - 支持：导航页/导航头（页面标题）、分区/列表/滚动/分隔/间距（展平进页面的滚动主体）、文本与富文本、
     状态/操作/开关/复选/单选行、按钮、图片、进度条与滑块（只读）、对话框。
   - 对话框：Band 10 Pro 用固件 msgbox（文字取第一个文本，按钮取前两个按钮/操作行，按树序即从左到右）；
     Band 11 没有已恢复的 msgbox，子节点按普通行内联显示。
   - 不支持的节点返回失败并保留上一版内容（后端契约）；节点数受 `CANOPUS_UI_MAX_NODES`=32 约束。
   - 事件沿用原有机制（每槽一个静态回调，`interactive` 的 view 接收），回调数从 16/25 统一为 32。
   - Manager 专有的部分变成可选钩子（返回键、事件前后、渲染完成）；确认对话框改为语义 `DIALOG`（取消、确认的顺序即 msgbox 的左右顺序）。
2. **模块页面**：新对象 `ctx->ui_ext`（`node_set_style`、`node_set_layout`、`set_entry`、`close_page`），独立成对象是为了让
   旧 Supervisor 靠 `ctx->struct_size` 被识别，与 `ctx->connect` 同理，ABI minor 不变。
   - 模块 `set_entry` 登记页面入口（标签、`on_open`/`on_event`/`on_back`/`on_close`）；模块运行期间，Manager 的模块详情页多一行「打开界面」。
   - 页面由 Manager 应用的第 4 个固件页面（`module_ui`）承载：打开时在固件堆上分配 view 与 UI 上下文，调用 `on_open` 构树并提交；
     提交失败或没有提交则取消页面。同一时刻只开一个模块页面；模块卸载时入口先被清除，再关闭其页面，回调不再被调用。
   - Band 10 Pro 的标题返回先交给 `on_back`（返回 0 表示已处理，例如弹出自己的路由），否则关闭页面；
     Band 11 的原生标题自己结束页面，模块的多层导航请在页面内放返回行。
   - 页面内容不能自行更新标题（固件的页面标题只在创建时设置，更新契约尚未恢复）。
3. **Supervisor 构建预算**：Band 10 Pro 的 `supervisor_max_size` 由 98,304 提到 114,688。新增代码和 4 个视图使 .043 达到约 101 KB。
   该值本来就是构建期预算而不是实测的固件上限（stock modlib 没有固定的插入上限，Band 11 用 131,072）。
   这个预算没有在真机上验证过。

## 证据与限制

- host 测试（含 ASan/UBSan）覆盖入口登记/校验/随模块释放、`close_page`、无 Manager UI 的构建返回 UNSUPPORTED、
  Manager 详情页的「打开界面」行与路由、确认对话框的节点顺序。
- Band 11 固件仿真（Unicorn，真实固件的行构造函数、主题回调与页面生命周期，其余 LVGL 核心为模型）下，Manager 全部页面经新的宿主后端渲染，
  原有 `scripts/tests/band11_*.py` 全部通过；模块页面的构树、渲染与事件由 Mirage 模拟器上的 `ui-demo` 模块验证，见 `docs/progress.md`。
- 真机未验证。所有相关符号仍是 restricted / PENDING / not_probed；复用 Manager 已用到的固件入口，没有新增符号，也没有提升状态。
