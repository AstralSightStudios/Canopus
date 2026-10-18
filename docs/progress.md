# Canopus 实施进度

> 2026-10-14 起任务进度的唯一来源是 [`refactor-plan.md`](refactor-plan.md)；本文只保留能力状态与历史记录。

权威进度追踪位于两份文档：

- 任务进度追踪区：[`architecture.md`](archive/architecture.md) §23。
- P0/P1/P2 强制整改状态（每个 CAN-P?-??? ID 的 OPEN / BLOCKED-EVIDENCE /
  HOST-FIXED / CLOSED-GATED / CLOSED）：[`native-manager-ui-plan.md`](native-manager-ui-plan.md) §15。

本文档是轻量索引，不与上述权威表重复。任何"预计通过"不得写成"已通过"。

## 原生投递能力边界（2026-10-17）

- 原生 `.canopus` CPC2 分块暂存、回执校验与 host 测试：host implemented。
- .043 / .155 SRAM L2 回调与加密 ABI：static recovered；普通构建自动包含对应 target 的后端，不需要额外启用参数。双 owner 后端将 UI 命令处理与 Bluetooth 任务的响应发送分开，避免跨 NuttX task-group 写蓝牙 fd。
- Band 11 `.155`：生产安装器 bootstrap、真实 AstroBox core 加密 L2 原生 `.canopus` 安装与模块列表查询已在 Mirage 通过（2026-10-17，仅 emulator 证据）。待打包目录随普通构建包含通信后端。`.043` 的独立原生安装已通过，但完整 fresh-device smoke 的 phone flow 尚未闭环。
- 相关符号保持 restricted / PENDING / not_probed；没有提升为 device verified 或 production approved。
- 快应用容器、快应用目录和列表管理方案已撤销；历史验证记录不作为新数据面证据。

## 平台硬件服务边界（2026-10-18）

- 原先的占位实现已移除：亮度恒 100、息屏状态恒亮、振动忽略时长、电量恒 85、蓝牙恒已连接、唤醒锁无归属、
  `l2_register_rx` 只保存不派发、通知忽略标题。现在每个钩子要么调用 exact-target 的固件入口，要么为空并返回
  `UNSUPPORTED` / 文档化的"未知"值。
- 文件服务补全：绝对路径任意读写，只保护 Canopus 自己的状态（`/data/canopus`、`/dev/canopus`、`/canopus`，不区分大小写、先规整再判断）；
  相对路径落在 `/data/canopus/m.<id>/`（原先的 `m.<id>.<name>` 扁平命名会让 `mod.a`/`b.x` 与 `mod.a.b`/`x` 撞名，已改为每模块一个目录）；
  `mkdir`、`fsync`、`stat`（含 mtime）、`APPEND`/`READ|WRITE`、固件 errno 的如实映射（缺失文件为 `NOTFOUND`）。
- 系统事件广播（亮灭屏、电量、蓝牙状态）由定时器泵每 500 ms 采样发布，模块无法伪造这三个主题。
- Band 11 `.139/.155/.177` 普通 UI 构建补齐 10 ms LVGL timer pump，入口来自已有精确 target 私有符号，
  沿用现有软件 timer 调度；headless 不支持。LVGL 暂停时不保证后台触发，UI 重建后的自动恢复仍不支持；
  这些 timer 入口保持 `restricted / STATIC_RECOVERED / PENDING / not_probed`，不属于上述新硬件入口的缺失项。
- host implemented：上述服务逻辑与 C host 测试（含 ASan/UBSan）。
- static recovered（`restricted` / `PENDING`，`device: not_probed`）：Band 10 Pro .036/.043 与 Band 11 .139/.155 的 PowerManager、
  MiWearScreen、vibrator、property、`mkdir` 入口，证据 `EVID-PLATFORM-HW-*`。.036/.139 由 .043/.155 的函数体匹配得到。
  Band 11 .177 无镜像，新增入口全部为空钩子。
- 未提升任何能力状态：没有 device verified，也没有 production approved。services 测试已在 Mirage 运行（仅 emulator 证据）：Band 10 Pro `.043` 22/26（4 项显式跳过：timer、振动、screen-on、通知）、Band 11 `.155` 25/26（仅振动跳过，模拟器里没有可达的 vibrator）；.036/.139/.177 没有模拟器运行。
- `get_rssi` 与仅 CPU 的唤醒锁已从 ABI 移除；`is_connected` 改读固件 `system_all_state` 的 MiWear 链路状态（.036/.043/.139/.155，
  .177 无镜像为 `UNSUPPORTED`）；通知图标改为调用方传入的绝对路径（`post_with_icon` 的 `icon_path`）。

## CanopusConnect（2026-10-18）

- 模块与 AstroBox 插件按模块 id 互发文本/二进制消息（`ctx->connect`，ADR-CAN-009）：CPC2 新增 opcode 19/20/21，走 L2 0x43，
  先于管理器分发处理、不碰管理器状态，发出的事件用独立的 2 槽队列并排在管理器响应之后；`ctx->bt->l2_send` 现在拒绝 0x43。
- host implemented：设备端交付/拒绝/监听按会话失效/与管理器状态隔离（C host 测试，含 ASan/UBSan）；Rust ABI 与 `canopus-runtime::connect`；
  AstroBox core 的线格式与事件、pluginsystem（API Level 4）的 `canopusconnect` 宿主接口与可选的 `on-message` 导出、插件权限提示（zh_CN / en_US）。
- 仅 .043 / .155 提供（需要 L2 后端），其它 target 的 `ctx->connect` 为空。发送队列、`l2_peer` 与 owner 上的发送顺序是 ARM 代码，
  Mirage `.043` / `.155` 的 services 测试已执行 listen/send/remote listening（仅 emulator 证据）；没有 AstroBox 端到端收发，也没有真机运行；未提升任何符号或能力状态。

## 模块注册入口统一（2026-10-18）

- Band 10 Pro 原厂 modlib 的构造函数没有 ctx，只能向 `/dev/canopus` 写 CMR1；Band 11 / portable 的构造函数在 r0 拿到 ctx，走 `ctx->services->module_register`。
  现在两条路只有一个 SDK 入口 `sdk/c/canopus_module_entry.h`（`CANOPUS_MODULE_ENTRY` / `canopus_module_register`），按目标生成配置在编译期选路径；见 ADR-CAN-006 与服务规范 §3。
- 顺带查出并修正：`canopus new` 的 target 模板只有 TODO 桩、`canopus module new` 与 Rust 构造函数 shim、`hello.c` 从来没有注册 descriptor（没有任何 loader 读 `canopus_module_descriptor` 符号）；
  `hello.c`、no-heap-counter 与旧脚手架的 `module_id` 与清单 id 不符，Supervisor 会拒绝注册；Rust 模块此前被限制在 stock-modlib 目标，现在 Band 11 也能构建、通过 verifier，并在 Mirage `.155` 上激活（`canopus new --lang rust` 的模块）。Rust 产物原先带着 debug 段和内嵌 bitcode，其 `.rel.debug_*` 指向未装载的段，Canopus 自己的 ELF loader 判为无效；链接时用 `sdk/rust/c_shim/module.ld` 丢弃它们（51 KB → 12 KB），no-heap-counter 的 `--whole-archive`（2871 个段，超过 loader 的 256 上限）也已去掉，CI 现在用 host loader 检查 Rust 与 C 产物；
  services 测试里仅 Band 10 Pro 才有的原始固件写文件（与 `ctx->fs` 重复）已删。
- Band 11 的 Inline Hook：引擎把 `0x1C…` 指令别名当只读 Flash 一概拒绝，模块自己的代码因此无法 Inline Hook（`.155` services 测试里两项失败，HEAD 上本来就有）。现在平台注册代码视图，经数据视图写入补丁与 trampoline 并做 cache 发布（规范 §2.8）；Unicorn 与 Mirage `.155` 通过，真机 cache 维护未验证。
- CI gate 5 现在对全部 active target 脚手架并构建 Rust、target C 与 portable C 模块。
- 证据级别：host 构建/verifier 与 host 测试；设备侧运行见各 emulator 记录，均不提升能力状态。

## 原生 UI 宿主后端与模块页面（2026-10-18）

- 通用后端 `manager/target/canopus_ui_host.c`（Band 10 Pro / Band 11 共用，ADR-CAN-010）：Manager 自己的三个页面改走它；`ctx->ui_ext`
  让模块登记页面入口，Manager 详情页提供「打开界面」，由 Manager 应用的第 4 个固件页面承载。Manager 的确认框改为语义 `DIALOG`。
- host implemented：入口登记/校验/释放、详情页行与路由、对话框节点顺序（C host 测试，含 ASan/UBSan）；Band 11 固件仿真下 Manager 渲染全部通过。
- Band 10 Pro 的 `supervisor_max_size` 98,304 → 114,688（构建预算，不是实测的固件上限，未在真机验证）。未提升任何符号或能力状态。

## 阶段状态

| Phase | 状态 | 备注 |
|---|---|---|
| 0 项目边界与 schema | READY | 待独立仓库确认（BLK-001） |
| 1 Host CLI 与 target registry | READY（host） | `canopus` CLI：registry/schema/veneer/bindings/verify/package/re |
| 2 C ABI 与 portable runtime | READY（host） | control/lifecycle/resource/diagnostics/module/protocol/supervisor host tests |
| 3 首个 target adapter | READY（host） | xiaomi-band-10-pro-3.101.036 trusted pack、3.101.043 promoted pack、以及 9/9 Pro/11 新生成 static candidate packs、veneer、bindings |
| 4 C module build/package | READY（host） | hello 模块过 verifier；package build/sign/verify 闭环 |
| 5 Device supervisor MVP | HOST-FIXED/DEVICE-PENDING | `/dev/canopus` 注册、v2 transport、生命周期、safe mode host 通过 |
| 6 Native App 与 Launcher adapter | BLOCKED-EVIDENCE | launcher 注册/注销 ABI 需真机证据（CAN-P0-007） |
| 7-12 | 部分 READY（host） | 见 architecture.md §23 与 §15 总表 |

状态语义（来自 `native-manager-ui-plan.md` §15）：

- **OPEN**：不得在 production build 中绕过。
- **BLOCKED-EVIDENCE**：必须先取得 exact-target 证据，不允许猜 ABI。
- **HOST-FIXED/DEVICE-PENDING**：host 完成判据通过，真机 gate 未通过。
- **CLOSED-GATED**：代码修复已存在，仍缺真机 gate。
- **CLOSED**：完成判据全部满足（当前没有任何 P0/P1 标记为 CLOSED）。

## P0/P1 整改（host 侧已关闭，device 待验证）

以下 ID 已在 host 完成修复并测试（写失败测试先行、ASan/UBSan 干净），
状态为 **HOST-FIXED/DEVICE-PENDING**，不代表真机已验证：

- CAN-P0-002 渲染器越界、CAN-P0-003 包归档边界 + INSTALL token、
  CAN-P0-005 removable stop/drain/unload、CAN-P0-006 safe mode policy、
  CAN-P0-008 v2 transport 统一
- CAN-P1-002 pending 状态机、CAN-P1-003 sequence snapshot、
  CAN-P1-004 status writer、CAN-P1-005 event ring、
  CAN-P1-006 store journal/recovery、CAN-P1-007 slot 回收、
  CAN-P1-008 error 语义、CAN-P1-009 ordered-list、
  CAN-P1-010 descriptor 校验、CAN-P1-011 ELF 绝对地址扫描、
  CAN-P1-012 codegen approval gate、CAN-P1-013 资源签名、
  CAN-P1-014 store syscall 正确性

仍 **OPEN / BLOCKED-EVIDENCE**：

- CAN-P0-004 真实 load/unload adapter（stub fail-closed，需 modlib 真机证据）
- CAN-P0-007 launcher/native app ABI 证据（BLOCKED-EVIDENCE）

## 变更日志

| 日期 | 变更 | 证据/备注 |
|---|---|---|
| 2026-08-05 | 建立独立仓库 /Volumes/EXT0/Canopus | AGPL-3.0；CLI 采用 Rust；target id 重命名见 ADR-CAN-001 |
| 2026-08-05 | P0/P1 host 整改批次（见上） | 每项一个 commit、测试先行、`./scripts/ci.sh` 全绿 |
| 2026-08-23 | 043 BluetoothAudio Bond `-1107` 第一轮静态纠错 | 撤销错误的 036 16-word/global ABI，恢复 043 stock bts globals 与 17-word descriptor；中间提出的 adapter+120/client+72 布局随后被 `-1113` 真机反馈推翻，不是闭环 |
| 2026-08-23 | 043 BluetoothAudio Bond `-1113` host 修复，device retest pending | exact IDB 证明 adapter+120 是仅 0x24-byte 的 callbacks_list manager，registration handle 才是 `{cookie, descriptor}` 8-byte node；移除 manager+72 越界访问，改为 register mirror → unregister original → 更新 stock handle；新增 `-1114..-1117` 分支诊断与 failure evidence；无真机通过结论 |
| 2026-08-23 | 043 BluetoothAudio `Bond 3/2 0F -1105` 通用 mHDT 修复，device retest pending | 配对已成功但 mHDT rewrite 未命中；删除 target policy bool 与 target-private 重复 packet transform，portable core 从 wire Connection Response 预取 local CID，避免 Configuration 早于 firmware callback 的调度竞态；新增 hook-installed 0x80 / rewrite-hit 0x40 诊断；无真机通过结论 |
| 2026-08-23 | 删除旧 030/9-Pro/11 pack，重提取 9/9-Pro/11 corpus 并生成新 static candidate packs | exact IDB SHA 绑定；036→目标 ensemble 各 123/126；ABI、loader、真机 gate pending |
| 2026-08-23 | 三个新 target 接入 BluetoothAudio 与 LyraPlayer compile-only 构建矩阵 | 6 个 exact-target ELF 均通过 verifier（0 undefined）；candidate facade 无固件 callable、identity gate fail-closed；无真机结论 |
| 2026-08-23 | 按 exact firmware 分离 Band 9/9 Pro NSH mw/exec bootstrap profile | 9175/9132：各自 NSH/VFS/heap/MPU/SRAM cave 静态恢复，两个 exact-target host-check、Lua success/fault smoke、portable ELF loader harness 通过；当前 9175 遗留 Supervisor 被新 verifier 拒绝，9132 尚无重建 Supervisor，release staging 均 fail-closed；无真机结论 |
| 2026-09-08 | 删除低准确度 Band 11 `4.100.108` pack，新建 `xiaomi-band-11-4.100.139`（sha 31ce8225…；exact IDB 35,192 函数、16,127 data objects） | 043→4.100.139 ensemble 匹配 123/124（跨型号，中位 margin 0.0085，仅 14 个 margin≥0.10 记为 STATIC_RECOVERED，其余 CANDIDATE，全部 restricted/PENDING、无 callable wrapper）；idalib 二次核验确认 identity 串（`4.100.139`@0x0CA0D216、`user-4.100.139-cn-202608280000`@0x0CA0D28E）与 4 个锚点/高价值符号（controller_crash_dump、lv_image_set_src、app_install、lvx_page_title_create），并**证伪** register_driver 候选（0xC46C2FE 为 command-311 dispatcher，保持 CANDIDATE）；LVGL v9 确认；loader **BLOCKED**（无 insmod，无 exact Band 11 staged bootstrap，q66 MCU≠Band 9，不得把 Band 9 当成功路线）；SDK generated/private 编译通过、generated stability + fixture + workspace 全绿；BluetoothAudio/LyraPlayer 接入 4.100.139 compile-only env，BluetoothAudio exact-target ELF 过 verifier（27 sections、0 undefined）；无真机结论 |
| 2026-09-08 | Band 11 bootstrap 独立复核与 Lua 执行恢复资源接入 | 原始 Thumb 调用点恢复 register_driver/open/close/read/write/lseek/malloc/free 八项，仍 restricted/PENDING；撤销被误认作 insmod 的 strchr-like 地址；明确 Band 9 inode type=7/private+0x1C 与 Band 11 type=1/private+0x18 不兼容。未完成 custom platform 不再生成候选调用宏。Band 11 main.lua 内嵌 exact-profile pmain 重入恢复，Lua 5.4.0/5.5 真实 C 帧及失败分支测试、资源构建、完整 CI 通过；仅提供命令测试，Supervisor/MPU/cave/Manager 仍 BLOCKED；恢复提前退出会泄漏一份 strdup 路径，未实机验证，见 EVID-BOOTSTRAP-4139-002。 |
| 2026-09-09 | Band 11 完整原生安装器实机测试候选 | 130 symbols 中45项 STATIC_RECOVERED、84项 CANDIDATE、1项 FORBIDDEN；通用 SDK callable 仍为0。独立纠正 app80/page120/launcher28 字节布局及回调偏移；Lua自有长字符串 stage1→Kmem stage2→ELF Supervisor，原子 MPU4–6 租用、保留栈保护7，含缓存行处理与 os.execute 恢复。Run/Clear Env 与 Band9 UI结构一致，根层仅main.lua+5个.bin。真实Lua5.4.0、ARM双重基址/错误回收/设备节点/INSTALL/Manager页面生命周期仿真、ELF verifier和全CI通过；物理设备仍NOT_PROBED，Rust未核验接口仍restricted。 |

## 2026-09-15 Band 11 修复与跨版本适配

- `.139` crash3：已确认通知消费者无条件读取空的 message+92 上下文，PC `0x0c55fe06`、BFAR `0x0d`。改为两份16字节驻留上下文；旧路径精确复现、修复后真实消费者回归通过，真机复测 pending。
- 新增 `.155` 独立 target、C native 生成地址、Rust feature/bindings；139/155 共用 Band 11 prod 目录、统一入口和版本化资源。102 STATIC_RECOVERED / 52 CANDIDATE / 1 FORBIDDEN；继承的拒绝不升级。ARM 启动、Manager 原生列表、通知、Lua编排、ELF verifier 通过；使用本地签名最小ELF验证安装提交与通知（不等于业务模块加载/激活或设备验证）。
- fw-match 增加完整 Thumb 指纹与双向唯一种子，修正相同源函数的别名被错误分配到不同目标地址。139→155 初始种子27,039对，140个名字对应138个函数，10项独立小型oracle命中；不宣称整体准确率或自动ABI批准。

详见 [crash3](../targets/xiaomi-band-11-4.100.139/loader/notification-crash3.md)、[155 target](../targets/xiaomi-band-11-4.100.155/loader/README.md) 与 [matcher说明](../tools/fw-match/README.md)。

2026-09-16：Band 11 框架 prod 合并为一个目录/入口，同时携带 139/155 的四件 native 资源，根层共1个Lua+9个bin。两个版本的真实Lua C帧选择、错误build/重复身份拒绝、ARM启动及通知回归通过。Bluetooth Audio、Lyra Player 增加155 feature/profile，原生页面销毁槽位与Lyra屏幕布局/释放事件分支覆盖155；两模块prod同目录支持139/155，真实155签名产物过verifier、安装通知和ARM模块加载测试。RF/音频/真实显示仍DEVICE-PENDING。

