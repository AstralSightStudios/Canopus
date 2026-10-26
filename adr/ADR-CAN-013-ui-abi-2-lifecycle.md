# ADR-CAN-013：UI ABI 2.0、输入/滚动/虚拟内容与模块生命周期通知

状态：host 实现已有全量 CI、五目标构建与隔离 Mirage 证据；2026-10-23 复审确认 R1–R14，已全部修复并重新验收（正式回归 + 隔离 Mirage review7）。
**本 ADR 只声明 host/emulator 级证据；不提升任何能力状态**，`.043`/`.155` 已有的真机批准仅覆盖 ADR-CAN-010 列出的旧调用路径。

## 背景

Corona Manager 需要六个页面（首页、资源列表、资源详情、拼接项列表、来源选择、传输进度），要求：

1. 首页状态卡保留既有视觉，其余页面用系统组件；业务规则不能因为缺 API 被简化。
2. 状态用模块内部快照 + 变更通知，不用状态文件、不轮询；Corona 的状态结构、一次性快照应答、重载命令与回执属于应用层。
3. 完整输入（按下/长按/移动/松开/取消、坐标与时间）、拖动位移、长按拖动排序与邻项让位、边缘自动滚动。
4. 长列表可以超过 32 项，返回/恢复滚动位置，不能分页按钮也不能盲目扩大固定数组。
5. 系统行支持多行长文、图片预览加选中标记、合适行高。
6. 真 Toast（显示、超时、连续替换、关页/卸载不悬空）。
7. 路由切换要更新标题与返回态，过期事件不能操作新页面。
8. Supervisor 的模块启动/停止/失败/再激活通知 + 当前状态查询，独立 Manager 在资源模块退出后立即失效旧快照。

原契约（ADR-CAN-010）只能做点击事件、无真实布局求解、无滚动查询、标题不可更新、无 Toast，且 32 节点上限让长列表无法表达。
Canopus 尚未发版，公共 API/ABI 可以直接演进，不必保留旧版本兼容。

## 决定

### 1. 公共 ABI 直接演进到 2.0，删除被取代的接口

- `CANOPUS_ABI_MAJOR` / `CANOPUS_UI_ABI_MAJOR` 均为 2，minor 0；旧 major 的 `canopus_module_descriptor_v1` 不再发布 native app。
- **`ctx->ui_ext` 删除**，其成员并入 `ctx->ui`（`canopus_ui_obj`）：`node_set_style`、`node_set_layout`、`set_entry`、`close_page`，
  另加 `capabilities`、`node_set_visual`、`control`、`post`。没有旧前缀、兼容分支、fallback 或人为附加的扩展对象。
- `canopus_ui_entry_v1.on_event` 的签名由 `(key, event_id)` 改为 `(const struct canopus_ui_event_v1 *event)`。
- 所有在树调用方（Manager、SDK C/Rust、示例模块、CLI 脚手架模板、host 测试）同步迁移。

### 2. 能力协商，而不是静默忽略

`ctx->ui->capabilities(context)` 返回后端实测的 `CANOPUS_UI_CAP_*` 位。不支持的请求显式返回 `ERR_UNSUPPORTED`。
能力是**按 target** 判定的（同一个家族的不同固件可以不同），保留真实的目标能力检测；这与「不保留旧版本兼容」不冲突。

| 能力位 | 含义 | `.043` / `.155` 现状 |
|---|---|---|
| `STYLE` / `LAYOUT` / `VALUES` | 样式、布局求解、数值 | 已实现；**属性 ID 按 target 审计**（`.155` 圆角/边框/透明度/裁剪，`.043` 仅圆角/裁剪，其余目标仅 setter 颜色）。快照携带未审计属性整次提交 `ERR_UNSUPPORTED` |
| `VISUAL` | `visual_v1`（RGB/绝对坐标/裁剪/透明度/位移） | 已实现 |
| `POINTER_EVENTS` / `CAPTURE` | 完整指针事件、按住捕获 | 已实现 |
| `SCROLL` | 滚动查询/定位/滚动事件 | 已实现 |
| `VIRTUAL_CONTENT` | `content_height` 虚拟内容窗口 | 已实现 |
| `TOAST` | Toast | 已实现 |
| `NAVIGATION_HEADER` / `IMAGE` / `PROGRESS` | 导航头、图片、进度条 | 已实现 |
| `IMAGE_FIT_CONTAIN` / `COVER` / `STRETCH` | 图片适配模式 | `.043` / `.155` **host 已实现**，Q8 原生缩放 + 尺寸查询 + 父容器裁剪；其它目标仅 `NATIVE`，见 ADR-CAN-010 / P7.13 |
| `FONT` (`font_px`) | 任意字号（`CANOPUS_UI_CAP_FONT`） | `0/24/28` 全目标支持；`.043` / `.155` **host 已实现**任意非零 16-bit `font_px`（按字号缓存 managed wrapper + 原生标签 `DELETE` 绑定 + 渲染队列/VG wait 退役），stock 行/标题/msgbox 与其余目标返回 `UNSUPPORTED`，见 ADR-CAN-010 / P7.13 |
| `EXTENDED_COMPONENTS` | 扩展组件 | 目标相关 |

未审计到对应固件原语的 target **不开放**这些能力位，调用返回 `ERR_UNSUPPORTED`。

### 3. 输入事件与坐标系

`struct canopus_ui_event_v1`：`generation` / `key` / `event_id`、`type`、`time_ms`、**视口坐标** `x`/`y`、
`delta_x`/`delta_y`、`scroll_y` / `viewport_height` / `content_height`。事件类型
`ACTIVATE`、`VALUE_CHANGED`、`PRESS`、`LONG_PRESS`、`MOVE`、`RELEASE`、`CANCEL`、`SCROLL`。

- 坐标是**视口坐标系**：原点是可滚动内容容器的左上角，单位像素，向右为 x 正方向、向下为 y 正方向；`delta_*` 是相邻采样的位移。
- 一次按压期间绑定保持在起始 key 上，不随指针移动换目标；`CANCEL` 用于取消并恢复被锁定的原生滚动。
- 捕获（`CONTROL_CAPTURE` / `RELEASE_CAPTURE`）在同路由的成功提交中**随有效 key 与事件绑定迁移**（虚拟窗口切换不断手）。
  原 press 或捕获目标禁用、自身或祖先隐藏、消失、事件绑定改变，以及路由/生命周期变化，会用旧绑定发送一次 `CANCEL`，回调前恢复原生滚动。
  转移捕获保留原 press 目标，释放捕获后 MOVE/RELEASE 回到该目标。所有候选分配成功后才取消旧手势，失败提交保留旧页面与捕获。
  拖动中用 `CONTROL_TRANSLATE` 实时位移，不需要改文本或重建控件。
- 事件代码按各 target 的固件表映射（PRESS 1 / MOVE 2 / PRESS_LOST 3 / LONG 5 / RELEASE 8 / SCROLL 12），
  不是每个 target 都一样；映射缺失就能力位关闭。

### 4. 滚动与虚拟列表

`CONTROL_SCROLL_QUERY` 写 `scroll_metrics_v1`（`y`/`viewport_height`/`content_height`），`CONTROL_SCROLL_TO` 把 `scroll_y`
夹到 `[0, content_height - viewport_height]`。`visual.content_height` 表示内容总高：**完整数据留在模块模型里，树里只放可见窗口**，
所以列表可以超过 `CANOPUS_UI_MAX_NODES`（32）。同页提交保留原生滚动位置，标题/back 显示更新不会归零。
根 `NAVIGATION_PAGE` key 标识路由：同页保持 key，切换路由换 key，清理旧路由状态并重置共享 viewport；返回位置由模块模型保存
`SCROLL`/查询所得 metrics，再用 `visual.scroll_y` 或 `CONTROL_SCROLL_TO` 恢复。

### 5. Toast 与标题

- Toast 是页面持有的原生反馈（`CANOPUS_UI_NODE_TOAST`）：同一时刻只显示最近一条，`values[i].value` 是毫秒时长（0 按 2000 ms；显式 1..60000，负数拒绝）；
  上一快照也可见且内容与参数完全相同的提交不重启计时；到期但未变化的可见节点不重播，隐藏→显示会重新显示并计时，允许初始隐藏节点。
  同一快照携带多个 Toast 节点时只有**最近一个可见**节点驱动共享反馈对象，其余（含尾随隐藏占位）是别名，
  不替换 owner、不写样式、不重启计时、也不会压掉前面的可见 Toast；快照内无可见 Toast 时由最后一个 Toast 节点驱动隐藏。
  **隐藏的 Toast 节点（含 fresh）从不替换 owner、也不把自身样式写入共享对象**——样式只来自可见驱动的自 diff
  或替换/重显的全量重绘，隐藏期写入的样式不得残留到下次显示。
  驱动身份变化（换节点驱动）时以新标签整体替换共享对象并重绘文本与样式——新驱动的样式相对其自身上一节点未变也必须写入，
  旧驱动的样式不得残留（样式/文本状态不跨节点混合）。
  **不变量：共享对象的文本被重写 ⇒ 其样式必同步重绘**（重显/replay 路径写文本时强制 `style_changed`），
  两者不得分离成两个独立门控。
  Toast 对象的删除只归反馈路径，通用清理按节点类型跳过，多 Toast 批次中移除其中一个不会二次删除共享对象。
  页面分离/暂停时清除（含交互关闭 `set_interactive(0)` 时的隐藏清除），模块卸载时随页面销毁，没有悬空回调。
- 显式 `navigation_header` 拥有标题栏，返回派发 header 的 key / `back_event_id` 对应的 `ACTIVATE`；普通导航页标题继续走页面 `on_back`。
  `show_back == 0` 可以保留非零 ID，但不会派发；header 容器必须在正文前 `end()`。模块根路由通过 `close_page` 退出。
- 导航标题随路由更新：文本、返回状态或返回事件绑定变化时重建标题对象。同一快照多个 `navigation_header` 时
  仅**第一个** header 驱动共享标题对象（含事件绑定），其余 header 不写、不争夺 owner，无 header 时由导航页驱动；
  逐字节相同的重提交不重建标题（零 mutation）。**过期标题事件不能操作新页面**——Band 11 比对当前标题的
  图标/文字子控件（固件 constructor 在这两个 child 上绑定回调），Band 10 Pro 比对创建时注册的 `title_token`（`FW_LVX_EVENT_GET_USER_DATA`）。
  未变化的显式 header 不会被根导航页标题反复替换，也不会占据正文流布局的高度。

### 6. 模块生命周期通知：走既有 bus，不发明新服务

- 保留主题 `"canopus/module/state"`，负载是原生 C `struct canopus_module_state_v1`（借用指针，返回前拷贝）。
- 配套 `ctx->bus` 的 `query_module` 做当前状态查询。**首次订阅语义**：没有初始事件、没有历史重放；启动移除发生在订阅之前，
  缺席模块报 `NOTFOUND`。owner 任务上「先 subscribe 再 query_module、中间不让出」即可无丢失地拿到初始状态。
- 回调在执行该转换的任务上**同步**调用；现有同步 bus 不当作跨线程队列。
- 状态持久化失败但内存变更仍保留且 `query_module` 可见时，仍发布变化；涵盖 SCAN 新模块/状态及原生发布错误设置、改变和清除。
  未变化重试不重复通知，状态事件不代表写盘成功。
- **最小 UI 调度**：`ctx->ui->post(ctx, fn, user)` 把工作投递到前台 UI 心跳（当前回调返回之后的下一次心跳）。
  `sleep_ms` 与普通 `timers_poll` 不排空 UI 工作；前台心跳先排工作再跑普通定时器，定时器回调中新投递的工作留给下一心跳。
  相同工作合并，不同的待处理工作返回 `BUSY`，模块释放时取消待处理工作。这就是状态通知触发 UI 更新的唯一入口，
  不引入线程，也不轮询。

### 7. 私有固件原语按 target 逐个审计，不暴露给模块

`.155` 新增静态证据 `EVID-UI-4155-LAYOUT-INPUT-001`（restricted / STATIC_RECOVERED / PENDING）：样式属性 ID（radius 12、
border color 49、border width 48、border opacity 50、background 28/29、text 88/89/94、opacity 95、clip corner 45），
以及 `lv_event_get_indev`、`lv_indev_get_point`、`lv_obj_get_scroll_y`、`lv_obj_scroll_to_y`（三参数）、`lv_obj_update_layout`。
事件代码与坐标偏移同样按证据生成私有宏。这些只进 `target-private` 绑定，不进模块可见的公开 ABI。
`.043` 的输入/滚动原语另行静态恢复，原生 setter 与 renderer 共同确认 radius 12、clip corner 45；裁剪位改变时显式写入 1/0，复用对象移除该位也会清除圆角裁剪。
精确启动拷贝表与 stock veneer 确认这些 UI 函数须使用 RAMX/PSRAMX 运行地址；修正既有样式/图片和新增输入/布局/滚动绑定，保留原有 policy/proof/approval 字段。
例如 dispatcher 的加载地址 `0x0c107790` 对应运行地址 `0x002bd924`，从加载地址执行会使跨段相对调用落入 AP 头部空白区。
绝对滚动绑定 `0x1c02ddb1`（四参数），与 `0x1c02d861` 的有界相对滚动区分，支持回到零偏移；图片 constructor 的第二个可选初始化字节指针按 stock caller 显式传 `NULL`，避免未初始化寄存器被当成指针。
生成物回归钉住 11 个 UI 运行地址、绝对滚动入口与图片 ABI；无需修改固件或 Mirage。
`.139`/`.179` 没有独立证据就不绑定输入/滚动原语，能力位关闭（`.179` 的记录是库存占位，无地址、不绑定）。

### 8. 构建预算

`supervisor_max_size` 是**构建期预算**而不是实测固件上限。Band 10 Pro 由 114,688 提到 131,072（与 Band 11 对齐）。
五个 active target 全部构建 + ELF 校验通过（0 未定义符号，2026-10-23 复审修复版）：`.036` 116,964/131,072（常驻 151,586/163,840）、
`.043` 123,348/131,072（161,958/163,840）、`.139` 125,336/131,072（172,550/184,320）、`.155` 130,096/131,072（181,474/184,320）、
`.179` 128,520/131,072（179,966/184,320）。常驻统计包含全部 SHF_ALLOC 节，不只是 BSS；上限是真正的 RAM 槽约束，未放宽。
捕获锁从指针数组改为节点槽位图，Cortex-M33 每视图状态由 176 降为 48 字节，16 视图共省 2,048 字节 BSS，没有减少节点/视图/模块配额。
`.043` 常驻余量 1,882 字节，`.155` 2,846 字节（文件预算仅余 976 字节），后续功能需重新评估。没有真机加载验证，预算调整不代表设备接受。

### 9. 编译器运行时补位

Freestanding Clang 即使在 `-ffreestanding -fno-builtin` 下也会把聚合初始化与结构体赋值降成 `__aeabi_mem*` 调用，
零导入校验会拒绝这些未定义符号。因此模块与 Supervisor 都链接 `sdk/c/canopus_aeabi_mem.c`（ARM RTABI 签名，
volatile 存储避免自递归），SDK 的 props 结构体初始化习惯用法得以成立。

## 证据与限制

- **host 测试**（`scripts/ci.sh` 全绿，2026-10-23 复审修复版）：双家族 native 后端回归（含相同提交零原生写，活动捕获下也为零），
  输入状态机与捕获迁移（`test_ui_input.py` / `tests/host/test_ui_input.c`，ASan+UBSan）、UI ABI/事件/控制、生命周期与 `post` 队列
  （提交持久化回滚、启动激活失败、回调重入、息屏延迟、排空期间取消）、Supervisor native app 发布拒绝旧 major。
  复审 R1–R14 均有正式回归：`test_hidden_toast_and_reappearance`、`test_label_style_preserves_bounds`、
  `test_inherited_style_rebuilds_subtree`、`test_section_heading_allocation_is_atomic`、`test_stretch_relayouts_descendants`、
  `test_nested_stretch_constraints`、`test_generic_row_padding_and_stock_rejection`、
  `test_page_style_reset_preserves_scroll_and_capture`、`test_failed_candidates_preserve_captured_gesture`、
  `test_host_capture_invalidation_and_detach`、`test_dialog_allocation_failure_preserves_state`、
  `test_identical_commit_preserves_capture_without_mutation`、
  `ui_posts_from_page_callbacks_wait_through_sleep_and_generic_poll`、
  `ui_posts_from_timers_wait_for_following_foreground_heartbeat`、
  `supervisor_scan_notifies_retained_changes_on_save_failure`、
  `supervisor_publication_notifies_error_changes_and_retained_clear`。
  R1–R8/R10 的修复前失败、修复后通过与 input/R12–R14 的红绿记录归档在
  [`docs/evidence/native-ui-abi2-review-fixes-2026-10-23`](../docs/evidence/native-ui-abi2-review-fixes-2026-10-23/README.md)。
- **Rust**：CI 中 215 项 Rust 测试（含 doctest 与 C 头文件一致性检查）通过；typed API 接收完整输入，`Unsupported` 保真，
  隐藏/可见返回独立控制；rich event 仅显式请求 `Command::Rebuild` 时提交，点击/值变更仍自动重建。
- **生成物**：`canopus target generate --all --check` 通过；五个 active target Supervisor 构建 + ELF 校验（0 未定义符号）通过。
- **集成回归（2026-10-23）**：页面根原生事件回调、绝对位置 overflow、虚拟原生范围标记、滚动归零、捕获祖先替换、
  header/正文 viewport 所有权、已滚动容器中新窗口的内容坐标定位、标题变化保留滚动、裁剪关闭→开启→关闭及相同提交零写入、Toast 独立几何/替换/到期/暂停/分离均有双后端 ASan/UBSan 回归。
  开关原生值变更派发 `VALUE_CHANGED`，普通行点击派发 `ACTIVATE`；启用/禁用两种路径均有双后端覆盖。图片构造测试验证可选初始化参数为 `NULL`。
  输入/范围/捕获五处修复和窗口定位分别验证了撤销修复时对应断言失败，裁剪回归也验证只开启而不清除时断言失败。
- **模拟器**：`scripts/emu/native_ui_v2.py` 经生产安装器与加密 CPC2 投递 `native-ui-lab`，全部操作用触摸完成，
  运行在独立 `MIRAGE_LIBRARY` 的新测试设备，不碰日常实例。工具链前置条件见 `docs/mirage-emulator.md`。
  `.155` 已通过完整场景：四个路由、卡片/图片、100 项虚拟列表（窗口到 44，选中第 46 项）、捕获拖动、选择控件、Toast 显示/到期、标题返回态与关页；
  `.179` 已通过其支持的导航/选择/Toast/标题/关页路径，缺少独立输入/滚动原语，明确 gated `virtual-list` / `pointer-drag` / `pointer-capture`。
  `.155` 返回列表后的截图与选中第 46 项时一致，确认滚动与选中态恢复。`.043` 完整场景通过：窗口到 37、选中第 40 项，返回后恢复原位置与选中态，卡片/图片、捕获拖动、选择、Toast、标题与关页均通过。
  `.043` 首次卡片失败由 UI 入口误用启动拷贝代码的闪存加载地址引起；运行地址、图片 ABI 与绝对滚动入口已修正。随后虚拟列表未滚动是驱动起点超出其视口，改为面板高度减 80（`.043` 为 y=400）后通过原有超过 32 项的断言。
  归档见 [`docs/evidence/native-ui-abi2-2026-10-23`](../docs/evidence/native-ui-abi2-2026-10-23/README.md)，含实际构建/运行哈希、截图、事件记录、CI 日志及修复前失败现场。`.036`/`.139` 本轮仅构建与 host 回归。
- **复审重新验收（2026-10-23，review7）**：R1–R14 修复后在独立 Mirage 实例重跑（每目标新设备、独立 library、不碰日常实例）。
  Lab 卡片更新拆为通用提交（标题/几何/颜色）与扩展样式尝试：目标缺少样式属性 ID 时 `stylefallback 1/8` 降级并 gate `style-card`
  （`.155` 无降级，`.043`/`.179` 各一次，三者均无 `renderfail`）；Toast 演示 10 秒窗口，显示/隐藏/重显/到期四帧按截图像素验收
  （文本像素 872/0/872/0，`.043` 1613/766/1613/766），隐藏帧落在原 Toast 仍在期内的时刻，证明提前消失而非等到期；
  已选 radio 重点击保持面板点亮，重渲染不重启计时、到期后不重播。`.155` passed 无 gate；`.043` partial（`style-card`）；
  `.179` partial（`virtual-list`/`pointer-drag`/`pointer-capture`/`style-card`）。构建哈希、截图与逐项 R 结果归档于
  [`docs/evidence/native-ui-abi2-review-fixes-2026-10-23`](../docs/evidence/native-ui-abi2-review-fixes-2026-10-23/README.md)。
- **没有新的真机验证**：新增能力目前只有 host/emulator 证据；生命周期总线与 `post` 的状态语义由 host 回归覆盖。
  M5/QEMU 时序不能外推到 BES1503 真机性能。`.043`/`.155` 已有的真机批准（ADR-CAN-010，2026-10-19）覆盖的是旧调用路径，不覆盖本次新增。
- **能力状态未提升**：本 ADR 不改动 `docs/progress.md`、`docs/native-manager-ui-plan.md` §15 里的任何能力状态，
  新增符号保持 `STATIC_RECOVERED` / `PENDING` / `restricted`。
- **残余限制（交给 Corona 工作区）**：
  - 图片适配由 P7.13 在 `.043`/`.155` 接入 `CONTAIN/COVER/STRETCH`（静态证据 + 双后端 host 回归）；`.043` 缩放/查询记录的加载地址已纠正为运行地址。其它目标仍只有 `NATIVE`，不得从同家族外推能力。Q8 量化、裁剪与验证边界见 ADR-CAN-010 及 `docs/native-ui-image-font-verification.md`；尚无新 fit 路径的真机像素验证。
  - 公共后端只绑定 MiSans-Medium 24/28 两档，没有字重/斜体/自定义字体；固件中虽有 17/24/26/30/32 的 stock 样式线索，尚未审计接入，21 px 与数值字重 600 均未落实。通用字体分配、缓存与排队渲染引用的销毁边界未闭环，不能复用 calendar 专项批准。P7.13 的 [font feasibility](../docs/native-ui-font-sizing-feasibility.md) 记录精确工厂/销毁入口、`.155` failure-font 地址不一致与预算；任意字号保持显式 `UNSUPPORTED`。
  - 样式属性 ID 按 target 审计：`.155` 有圆角/边框色/边框宽/透明度/圆角裁剪，`.043` 只有圆角与圆角裁剪，`.036`/`.139`/`.179` 只有样式 setter（颜色）。携带未审计属性（含 `FLAG_CLIP` 对应的圆角裁剪）的快照整次提交 `ERR_UNSUPPORTED`；示例 Lab 的扩展样式卡片仅在 `.155` 完整呈现，`.043`/`.179` 以 `stylefallback` 降级并在运行记录中 gate `style-card`。
  - 原生按钮、action/status/switch row 非零内边距返回 `ERR_UNSUPPORTED`；复选/单选行支持自定义内边距。显式样式变回继承通过替换原生子树恢复固件默认主题。
  - 布局只支持 `grow == 0`、`shrink == 1`，不是通用 CSS。
  - 单次语义树最多 32 个节点；虚拟窗口由模块按逻辑项维护，仅提交可见行。
  - 单页条目的 route 内切换仍无转场（`ANIM_TIME` 不是裁剪属性，未使用）。多页条目（`canopus_ui_entry_v1.page_count`）
    由 ADR-CAN-010 的 `ctx->ui->route` 走固件页面栈，每次 push/pop 都是 stock 转场；`ANIM_TIME` 仍不使用。
  - 模块生命周期没有历史事件流，只有当前快照 + 后续变更；需要事件历史要在应用层记账。
