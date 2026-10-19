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
   - Manager 专有的部分变成可选钩子（返回键、事件前后、渲染完成）；确认对话框改为语义 `DIALOG`（取消、确认的顺序即 msgbox 的左右顺序）。
2. **模块页面**：新对象 `ctx->ui_ext`（`node_set_style`、`node_set_layout`、`set_entry`、`close_page`），独立成对象是为了让
   旧 Supervisor 靠 `ctx->struct_size` 被识别，与 `ctx->connect` 同理，ABI minor 不变。
   - 模块 `set_entry` 登记页面入口（标签、`on_open`/`on_event`/`on_back`/`on_close`）；模块运行期间，Manager 的模块详情页多一行「打开界面」。
   - 页面由 Manager 应用的第 4 个固件页面（`module_ui`）承载：打开时在固件堆上分配 view 与 UI 上下文，调用 `on_open` 构树并提交；
     提交失败或没有提交则取消页面。同一时刻只开一个模块页面；模块卸载时入口先被清除，再关闭其页面，回调不再被调用。
   - 标题返回（两个家族）先交给 `on_back`（返回 0 表示已处理，例如弹出自己的路由），否则关闭页面。Band 11 的原生标题点一下本来就会发返回键；
     宿主把标题的点击回调换成自己的，所以 Manager 与模块页面的返回走同一条路。右滑返回仍是系统手势，不经过 `on_back`。
   - 页面内容不能自行更新标题（固件的页面标题只在创建时设置，更新契约尚未恢复）。
3. **启动器入口**：模块自己的启动器图标见 ADR-CAN-011；它复用这里的模块页面。
4. **Supervisor 构建预算**：Band 10 Pro 的 `supervisor_max_size` 由 98,304 提到 114,688。新增代码和 4 个视图使 .043 达到约 104 KB。
   该值本来就是构建期预算而不是实测的固件上限（stock modlib 没有固定的插入上限，Band 11 用 131,072）。
   这个预算没有在真机上验证过。

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
