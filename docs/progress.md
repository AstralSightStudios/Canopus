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

## 四款固件的快速静态适配（2026-10-24，仅静态）

- 新增 exact target（全部记录 `restricted` / `PENDING` / `not_probed`，无批准、无能力状态）：
  - `xiaomi-band-9-3.1.32`（n66，SHA `9c02dab4…3264`）：从 tag `band9-frozen` 恢复 143 条精确记录，原 40 条 APPROVED/managed 全部降为 restricted/PENDING；
  - `xiaomi-band-9-pro-3.1.190`（n67cn，SHA `3e264bc2…f82f`）：从 3.1.175 包移植，20,956 个互唯一种子，123 条函数中 108 条 STATIC_RECOVERED、15 条 CANDIDATE；
  - `xiaomi-band-10-3.1.28`（o66tc，SHA `fb726472…8845`）：从 Band 9 3.1.32 包移植，47 条 STATIC_RECOVERED、92 条 CANDIDATE、3 条 FORBIDDEN；
  - `redmi-watch-6-3.100.043`（p65，SHA `a19b6014…5858`）：从 Band 10 Pro `.043` 包移植，仅接受函数体相等且有调用/位置支撑的记录，其余保持 CANDIDATE。
- 生成物可重建（`canopus target generate --all --check` 9/9），`generated_stability` 18/18 通过。四者的 loader 均标注为 Band-11 风格 Lua-owned staged loader（无 `insmod`、无 Lua `os.execute` 恢复），但 stage1/stage2、installer staging 与设备/模拟器验证尚未实现；`loader/README.md` 写明边界。
- **UI family**：Band 9 / Band 9 Pro / Band 10 是 LVGL v8，需要专门的 v8 Manager 后端（旧的 v8 后端随 `band9-frozen` 删除）；Band 11 / Band 10 Pro / REDMI Watch 6 是 LVGL v9。
- 四个 target 均生成 `canopus-target-generated` Rust 绑定与 feature（限制记录不生成 public typed wrapper）；未提升任何能力状态。
- 2026-10-25 起 Band 9 / Band 9 Pro 已从静态包推进到可构建链路：新增显式 `static_test_abi`（只为 restricted STATIC_RECOVERED 记录生成地址常量，生成头标注 `CANOPUS_SUP_STATIC_TEST_ABI`，不是批准）；恢复 LVGL v8 Manager 后端与 Band 9 staged loader（stage1/stage2、ELF32 loader、installer 资源）。`.32` Supervisor 112,876 B、`.190` 112,976 B，均 verifier PASS（0 undefined）；`.32` stage1/stage2 417/3,524 B，`.190` 421/3,544 B。v8 的模块 Launcher/通知服务入口仍 fail-closed。
- Mirage（2026-10-25，emulator 证据）：Band 9 必须使用**完整固件包**（只导 `vela_ap.bin` 时 system/resource 分区为空，屏幕全黑）。完整 `n66cn_v3.1.32_full_489f2050`（AP SHA 与 target 相同）已导入；安装器表盘需用打了 `scripts/emu/band11/facetory-lua-layer.patch` 的 Facetory、protocol `0x0800` 打包。**Run 已完整通过**：stage0 → stage1 → stage2 → Supervisor → `/dev/canopus` 注册 → CPC2 INSTALL 0/2、RESTORE_AFTER_BOOT、INSTALL 1/2 全部 result 5，`/data/canopus/registry.bin` 写入 `CDR1`。证据 `EVID-EMU-9132-001`。
- 同轮修复：`register_driver` 原记录指向 inode type 7（`0xc4e38ce`），`/dev/canopus` open 返回 ENOSYS；正确字符驱动注册是 type 1 的 `0xc4f7c40`（`EVID-ABI-9132-REGISTER-001`）。stage0 cave 从 `0x200CB400`（运行时日志缓冲，读到 "tsig"）移到同一 gap 内的 `0x200D0000`；Supervisor 常驻镜像 149,866 B 从 Kmem 改为 Umem 分配并经默认 map 执行（运行时 MPU region 已几乎用尽）。v8 身份 guard 接受 build.prop 行尾 LF。
- Band 9 Pro `.190`：**Mirage 端到端通过**（2026-10-25，仅 emulator 证据，`EVID-EMU-9190-001`）。Supervisor 112,924 B / 149,922 resident，verifier PASS；stage1/2 413/3,580 B。Run 完成 cave stage0 → stage1 → stage2 → Supervisor → `/dev/canopus` 注册 → CPC2 INSTALL 0/2、RESTORE_AFTER_BOOT、INSTALL 1/2（全部 result 5），`registry.bin` = `CDR1`，`install_info_v1.db` 含 `com.canopus.manager`。修复：VFS/app/LVGL 记录按 3.1.32 重新匹配（原 3175 移植地址错误，`EVID-PORT-9190-CORRECT`）；`kmem_malloc` 改回 kmm 描述符的 `0xc0f11f4`；cave trampoline 保存并恢复 LR；v8 app 描述符改用 3.1.190 布局（回调 +0x1c，`-DCANOPUS_MGR_APPINFO_190`）；profile 使用 `entry_mode = "cave"`。真机验证仍 pending。
- Band 10 o66 `.28`：**Mirage 端到端通过**（2026-10-25，仅 emulator 证据，`EVID-EMU-1028-001`）。Supervisor 112,956 B / 149,954 resident，verifier PASS；stage1/2 425/3,580 B。AUTORUN 跑完 cave stage0 → stage1 → stage2 → Supervisor → `/dev/canopus` → CPC2 全部 result 5（页面显示 `Run completed`），`registry.bin` = `CDR1`；Supervisor target record 的 identity/app_install/launcher_add 均为 0，Manager 应用已注册。修复：`app_install` 改为 `0xc4737e8`、`unregister_driver` 改为 `0xc3938c0`（`EVID-PORT-1028-CORRECT`）；v8 app 描述符改用 3.1.28 布局（回调 +0x1c、hidden +0x3c、0x40 字节，`CANOPUS_MGR_APPINFO_1028`）；cave trampoline 保存 LR。该固件 Lua 表盘仍不派发按钮触摸（本次由 AUTORUN 驱动）。LVGL v8 object creator 已于 2026-10-25 消歧（`EVID-STATIC-1028-V8-001`）：`lv_obj_create` 是 `0xc2bc5b8`（`lv_obj_class` 在 `0x2c686a74`，经 `lv_label_class.base_class` 追到；调用 `lv_obj_class_create_obj` `0xc2b9734` + `lv_obj_class_init_obj` `0xc2bc534`），原记录 `0xc127924` 是字符串拷贝辅助函数；v8 backend 改为两参数调用（第二参数传 0，与 `lv_label_create` 一致）。同一轮还修好两处只影响构建的回归：`.32` app 描述符缺了开头三个字段（n66 Supervisor 静态断言失败）、`image_region_size` 只在 `CANOPUS_BAND9_IMAGE_MPU_LEASE` 下使用（其余 band-9 家族构建报 `-Wunused-function`）。**Manager 页面仍未在设备上打开验证。** 2026-10-25 用固件的 `/log` 确认了根因链：安装器从表盘页跑 Run 时，固件打 `[launcher] launcher_page_insert_icon: launcher UI not created`，`launcher_page_insert_icon`（o66 `0xc29b21c`）此时直接返回，所以 Manager 只进了 app registry、没进 launcher 列表；紧接着调用 `launcher_reset_order_info`（`0xc4fc508`，它不检查 launcher UI 是否存在）会让下一次打开 launcher 复位整机。已修（commit `1bc1e8a`）：v8 安装不再调用 order reset，装完设备保持运行、Run 显示 `Run completed`。剩下的一环是从表盘页 `page_goto` 推 Manager 页同样会复位（OPEN_MANAGER 实验，代码已撤回），而 OPEN_MANAGER 若走 launcher 又需要条目先存在。下一步二选一：把 launcher 条目改到「launcher UI 创建之后」再插入（需要 hook 或延迟重试），或逐项核对 v8 页面创建路径（`FW_LV_PAGE_TITLE_CREATE`、`FW_LV_LABEL_CREATE` 等）为何在 push 时 fault。下一步（已开始核对）：v8 的 LVGL 记录里至少还有三条是错的——`FW_LV_IMAGE_CREATE 0xc3269f5` 实际是单例 getter、`FW_LV_BAR_CREATE 0xc392c89` 实际是字段释放函数、`FW_LV_EVENT_ADD 0xc20dfbd` 实际是绘制例程，三者都会让 Manager 页 on_create 立刻 fault，这和「从表盘页 page_goto 复位」的现象一致；对照实体调用点已确认可用的是 `lv_obj_create 0xc2bc5b8`、`lv_label_create 0xc347b8c`、`lv_obj_set_size 0xc2bcc22`、`lv_obj_add_flag 0xc2bb826`、`lv_obj_clear_flag 0xc2bb8b2`、`lv_obj_align_to 0xc2bd39c`、`lvx_page_title_create 0xc4f9364`，`lv_event_add` 的候选是 `0xc2b7c40`（insert_icon 里以 (obj, cb, code, data) 调用）。把这些地址在 o66 上逐条核实后，Manager 页在 v8 上才有打开的可能。 已核实并修正（commit `6f6bd8f`）：`lv_obj_add_event_cb 0xc2b7c40`、`lv_img_create 0xc22d4f4`、`lv_bar_create 0xc341d04`；`lv_image_set_src 0xc34ce10`、`lv_label_set_text 0xc34db6c`、`lv_bar_set_range/set_value 0xc341e18/0xc341da4` 经实体断言/调用点确认无误。**仍缺的一环**：o66 的 `FW_ACTIVITY_NAVIGATE 0xc44d504` 其实是 `show_app`（pagemanager 的 show_app/prelaunch 检查），不是 page_goto；用打包 key `0x00CA0000` 调它返回 page == NULL（日志可见），设备不再复位但页面不会打开——说明还差 v8 的页面 key/切换 API（`off_20081420[19]` 的 page lookup + `sub_C4E0208`/`sub_C44B3A4` 的 push，或 launcher 点击时用的 44 字节过渡描述符 `sub_C44EB9C`）。
- p65 `.043`（REDMI Watch 6）：**Mirage 端到端跑通，仍是 emulator 证据**（2026-10-25，`EVID-EMU-65043-001/002/003/004`；无真机、无批准，全部记录保持 restricted / PENDING / not_probed）。安装器 Run 五步全部 result 5，cave stage0 → stage1 → stage2 → Supervisor；`/dev/canopus`、`/canopus/install`、`/canopus/registry` 注册成功，Manager 应用出现在 Launcher 并能打开原生页面（系统信息 / target / 固件版本）。模块生命周期已闭环：CPC2 INSTALL → ENABLE(REBOOT_REQUIRED) → RESTORE_AFTER_BOOT 加载，Manager 显示"运行中"；inbox 扫描（opendir/readdir/closedir）把第二个包导入为已禁用槽位。关键修复：`close` 为 `0x1c154d9c`（0x1c154c40 会复位设备）、cave exec 前不编程 MPU（`stage0_mpu = false`）、常驻镜像整段 RW+exec MPU lease、本 VFS 的 `O_RDONLY` 必须为 1、`device_error()` 读满 88 字节 CQD1、消息框按钮 stride 按 5 word。持久化已打通（`EVID-EMU-65043-004`）：`write` 的 VFS 条目更正为 `0x1c159a2c`（分发表 `0xc4a96c4` 的 write 槽；旧 `0x1c158690` 无任何代码引用，每次写都返回 EINVAL），`O_CREAT` 更正为 `4`（`vfs_open 0x1c157d24` 只在 flag bit2 才取 mode 参数）。修正后 Supervisor 自己完成 tmp 创建 → 写入 → rename：CPC2 DISABLE 返回 REBOOT_REQUIRED(8)，QUERY_DEVICE 显示无失败 stage、1 次成功事务，`registry.bin` 当场变化、不留 tmp；安装器表盘不再需要代为发布。

## Band 11 `4.100.179` 与全局 safe mode（2026-10-20）

- `.179`（q66tc，build `user-4.100.179-cn-202609302100`）由已审核的 `.155` 包移植：270 条符号记录全部 `restricted` / `PENDING` / `not_probed`，
  没有任何批准、真机证明或能力状态从 `.155` 带过来（`generated_stability.rs` 的 `band11_179_carries_no_approval_from_155` 钉住）。
  当前 280 条记录中有 227 条 `STATIC_RECOVERED`、52 条 `CANDIDATE`、1 条 `FORBIDDEN`。2026-10-24 revision 2 以完整 `.177` / `.179` 函数体和 IDA ABI 复核独立恢复 `lv_obj_area_is_visible` 的精确入口 `0x0c381d4c` / Thumb `0x0c381d4d`；原 `.155` 比较的函数体差异仍保留在来源记录。见 `EVID-RESOURCE-VISIBILITY-4179-001`。仅该入口增加静态恢复证据，全部符号仍为 restricted / PENDING / not_probed，无新增 public veneer、promotion 或设备批准。
- 2026-10-24：`.179` 原生 UI 输入/滚动/图片适配/字体绑定在精确 `.179` IDB 上独立恢复并接线（15 个宏绑定 + 生成器 ABI 常量）：`lv_event_get_indev 0x0c37fc70`、`lv_indev_get_point 0x0c399b90`、`lv_obj_get_scroll_y 0x0c37ff8c`、`native_font_create 0x0c4942e0`、`native_font_release 0x0c915854`、`native_font_wait 0x0c3998a8` 新恢复；`lv_obj_scroll_to_y` / `lv_obj_update_layout` / `lvx_image_set_scale` / `lv_image_decoder_get_info` / 5 个 font_reload 身份沿用已审核 port 记录。事件码、样式属性 ID、对象坐标偏移、display/layer/draw_unit 结构偏移与 font manager 间接全部在 `.179` 反汇编中逐项核实（值与 `.155` 相同但独立成立），证据 `EVID-UI-4179-LAYOUT-INPUT-001` / `EVID-UI-4179-FONT-LIFECYCLE-001`。Supervisor 重新构建 133,068 / 133,120 字节、常驻 183,370 / 184,320，verifier PASS；host 能力位从此覆盖 Corona 需要的 0x39ff6。全部记录仍为 restricted / PENDING / not_probed，**仅 host/static 证据，无真机验证、无能力状态提升**；`.179` 的 `style-card` 与三项输入 gate 自此解除（上文 2026-10-23 的 gate 描述为当时状态）。
- host / emulator 证据：Unicorn 里真实 `.179` 镜像执行 stage1 → stage2 → Supervisor（两种重定位基址）、Manager 注册、原生 UI 指令与通知路径；
  Mirage 里真实 `.179` 固件经生产安装器 Run → 加密 L2 `.canopus` 安装 → SCAN，services 测试 26/27（仅振动跳过）。
- 全局 safe mode：自动进入（连续 3 次未完成的启动、3 次模块隔离而没有一次好启动、注册表不可读）、重启保持、`EXIT_SAFE_MODE`（保持 / 全部停用 / 全部回滚）退出并试运行一次、
  试运行没有稳定就回到 safe mode；Manager 在 safe mode 里提供退出入口。host 测试，未上真机。
- `.043` Supervisor 123,348 / 131,072 字节（2026-10-23 复审修复版构建）；`supervisor_max_size` 已由 114,688 提到 131,072（构建期预算，非常驻 RAM 上限，
  常驻 161,958 / 163,840 未放宽）。

## 原生 UI ABI 2.0 与模块生命周期（2026-10-23）

- 公共 ABI 直接演进到 2.0：`ctx->ui_ext` 并入 `ctx->ui` 并删除，事件改为 `canopus_ui_event_v1`，新增显式能力协商、
  `node_set_visual`、`control`（滚动/位移/捕获）、`post`。**host implemented**，见 ADR-CAN-013。
- 能力按 target 判定，不支持显式返回 `ERR_UNSUPPORTED`；`.043` / `.155` 图片适配（`NATIVE/CONTAIN/COVER/STRETCH`）与任意非零 16-bit 字号（`CANOPUS_UI_CAP_FONT`，按字号缓存 managed wrapper + 原生标签 `DELETE` 绑定 + 渲染队列/VG wait 退役）现已在 host 接入，其它目标仅 `NATIVE` 与 `0/24/28`。
  能力矩阵、生命周期证明、防御性代码消融、五目标预算（`supervisor_max_size` 构建阈值 `131072 -> 133120`，非已证硬件上限，常驻上限未改）、静态证据与双后端四变体回归见 [verification](native-ui-image-font-verification.md)、[font lifecycle ABI](native-ui-font-lifecycle-abi.md)、[ablation](native-ui-defensive-ablation.md)、ADR-CAN-010 / refactor-plan P7.13；只新增 host/static 证据，无新增真机验证或生产批准。
- 模块生命周期走既有 bus（`"canopus/module/state"` + `query_module`），UI 更新经 `ctx->ui->post`，无线程无轮询。
- host 回归（含双后端 ASan/UBSan）、Rust 215 项、生成物检查、五个 active target 构建与 ELF 校验通过。
  隔离 Mirage 中 `.043` / `.155` 完整 Lab 通过（选中第 40 / 46 项并返回恢复），`.179` 支持路径通过，显式 gated 虚拟滚动/指针拖动/捕获。
  证据与构建哈希见 [`evidence/native-ui-abi2-2026-10-23`](evidence/native-ui-abi2-2026-10-23/README.md)。
  2026-10-23 复审 R1–R14（1 high + 13 medium）已修复并重新验收：正式回归进 CI，隔离 Mirage review7 复跑显示/隐藏/重显/到期 Toast 四态、
  标题/重置卡片与超 32 项列表恢复；`.043`/`.179` 因样式属性 ID 未审计 gate `style-card`（`stylefallback` 显式降级），`.179` 另 gate 三项输入能力。
  逐项结果与红绿日志见 [`evidence/native-ui-abi2-review-fixes-2026-10-23`](evidence/native-ui-abi2-review-fixes-2026-10-23/README.md)。
  二次复审回归 N1/N2（多 Toast staged 所有权、CANCEL 销毁后二次删除）已修复并补正式回归（含 ASan/UBSan 与红现场）；staging 所有权/清理按节点类型收口（audit-findings F1–F7）。重新验收见 [`evidence/native-ui-n1n2-fixes-2026-10-23`](evidence/native-ui-n1n2-fixes-2026-10-23/README.md)。
  三次复审 M1（别名 Toast 样式写共享 owner）/L1（尾随隐藏 Toast 压掉可见 Toast）已修复并补正式回归；Toast 驱动规则收口为"最近可见 Toast 驱动、无可见时隐藏"。重新验收见 [`evidence/native-ui-m1l1-fixes-2026-10-23`](evidence/native-ui-m1l1-fixes-2026-10-23/README.md)。
  四次复审 M2（驱动切换不重绘共享 owner 样式）已修复（切换走替换路径全量重绘）并补正式回归；写入面矩阵定稿无残留缺口。重新验收见 [`evidence/native-ui-m2-fixes-2026-10-23`](evidence/native-ui-m2-fixes-2026-10-23/README.md)。
  四次复审 P1/P2（隐藏期样式写残留/隐藏 fresh 节点替换 owner——样式重绘与文本重绘脱钩）按根因修复（不变量：owner 文本被重写 ⇒ 样式同步重绘，隐藏节点不写不替换）、T1（双 header 争 title 改 first-HEADER 驱动）、D1（矩阵补 hidden 触发源）已完成并补正式回归；重新验收见 [`evidence/native-ui-p1p2-fixes-2026-10-23`](evidence/native-ui-p1p2-fixes-2026-10-23/README.md)。能力状态未提升。
- **能力状态未提升**：新增能力仍为 **host implemented**，模拟器只增加 host/emulator 证据；没有新的真机验证或 production approval。

## 固化启动与可见的故障恢复（2026-10-21，host / 模拟器证据）

- **固化启动**（ADR-CAN-012，Band 10 Pro `.043` 与 Band 11 `.155` / `.179`）：默认不固化；AstroBox 的 Canopus 页面确认后，Supervisor 经 `/dev/bes_flash` 把 `rc.sysinit` 的一段等长差异尾段改成
  `sh /data/canopus/b &`，重启后不依赖当前表盘自动加载 Supervisor 并注册 Manager；解除固化写回原内容。Mirage：Band 10 Pro `.043` A 段闪存读写/解除通过；B 段用离线打补丁的 AP 镜像冷启动，
  不手动加载，Supervisor 自己起来，点亮屏幕一次、熄屏后序列走完。亮屏注册 Manager 会使模拟设备复位，所以状态机只在熄屏时推进。**没有真机运行，物理启动验收未做。**
  Band 11：为 `.155` 和 `.179` 实现独立原生引导载荷（`boot.lua` 内嵌 target 原生 loader 及 profile，加载 stage1 → stage2 → supervisor 至 Umem 并发 `START_BOOT`，由熄屏状态机推进完成 Manager 注册并清除 `boot.pending`）；Mirage 冷启动冒烟测试（`scripts/emu/boot_smoke.py 155` / `179`）全通。`.139` 无后台泵，不支持。安装器页面的延时 Auto-run 已删除。
- **故障恢复可见**：Manager 首页写明安全模式原因与“模块仍已安装，本次暂不加载”；被自动停用/回退的模块留在列表里并说明原因；启动后一条通知（安全模式含原因，或哪个模块被自动处理；正常则“Canopus 已加载，管理器可用”）。
  AstroBox 同步显示，且退出安全模式改为先确认。
- **退出安全模式的真实缺陷**：触发阈值的崩溃计数不清零，退出后下次启动立刻又进 safe mode；批量回退逐模块写盘并吞错误。已修，host 测试覆盖真实阈值路径。
- **稳定窗口熄屏也走**：稳定监视同时挂在 owner 后台泵上，熄屏超过 60 s 后启动标记照常清除。
- **体积**：Band 10 的 Supervisor 构建去掉每个函数一条的 `.ARM.exidx` 表（C 模块里无用，连同重定位约 8 KiB），`.043` 现为 109,180 / 114,688 字节（此前 112,304）。`.155` 127,940 / 131,072。

## 平台硬件服务边界（2026-10-18）

- 原先的占位实现已移除：亮度恒 100、息屏状态恒亮、振动忽略时长、电量恒 85、蓝牙恒已连接、唤醒锁无归属、
  `l2_register_rx` 只保存不派发、通知忽略标题。现在每个钩子要么调用 exact-target 的固件入口，要么为空并返回
  `UNSUPPORTED` / 文档化的"未知"值。
- 文件服务补全：绝对路径任意读写，只保护 Canopus 自己的状态（`/data/canopus`、`/dev/canopus`、`/canopus`，不区分大小写、先规整再判断）；
  相对路径落在 `/data/canopus/m.<id>/`（原先的 `m.<id>.<name>` 扁平命名会让 `mod.a`/`b.x` 与 `mod.a.b`/`x` 撞名，已改为每模块一个目录）；
  `mkdir`、`fsync`、`stat`（含 mtime）、`APPEND`/`READ|WRITE`、固件 errno 的如实映射（缺失文件为 `NOTFOUND`）。
- 2026-10-24：FS 组版本 2 追加 `rmdir`（C/Rust），仅删除空目录，非空返回 `NOTEMPTY(-39)`，沿用路径规整与状态目录保护。
  `.043/.139/.155/.179` 的固件入口已静态恢复（`EVID-RMDIR-*`，restricted / PENDING / not_probed）；`.036` 无入口证据，保持 FS 版本 1。
  C host ASan/UBSan、Rust ABI/runtime、五目标服务模块与 Supervisor 构建/verifier、建模 Thumb 适配器测试通过；未做真实文件系统或真机验证，不提升能力状态。
- 系统事件广播（亮灭屏、电量、蓝牙状态）由定时器泵每 500 ms 采样发布，模块无法伪造这三个主题。
- Band 11 `.139/.155` 普通 UI 构建补齐 10 ms LVGL timer pump，入口来自已有精确 target 私有符号，
  沿用现有软件 timer 调度；headless 不支持。LVGL 暂停时不保证后台触发，UI 重建后的自动恢复仍不支持；
  这些 timer 入口保持 `restricted / STATIC_RECOVERED / PENDING / not_probed`，不属于上述新硬件入口的缺失项。
- host implemented：上述服务逻辑与 C host 测试（含 ASan/UBSan）。
- static recovered（`restricted` / `PENDING`，`device: not_probed`）：Band 10 Pro .036/.043 与 Band 11 .139/.155 的 PowerManager、
  MiWearScreen、vibrator、property、`mkdir` 入口，证据 `EVID-PLATFORM-HW-*`。.036/.139 由 .043/.155 的函数体匹配得到。
- 未提升任何能力状态：没有 device verified，也没有 production approved。services 测试已在 Mirage 运行（仅 emulator 证据）：Band 10 Pro `.043` 22/26（4 项显式跳过：timer、振动、screen-on、通知）、Band 11 `.155` 25/26（仅振动跳过，模拟器里没有可达的 vibrator）；.036/.139 没有模拟器运行。
- `get_rssi` 与仅 CPU 的唤醒锁已从 ABI 移除；`is_connected` 改读固件 `system_all_state` 的 MiWear 链路状态（.036/.043/.139/.155）；通知图标改为调用方传入的绝对路径（`post_with_icon` 的 `icon_path`）。

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
- Band 10 Pro 的 `supervisor_max_size` 98,304 → 114,688（构建预算，不是实测的固件上限，未在真机验证）。
- Mirage 模拟器端到端（`scripts/emu/ui_pages.py 043|155`，仅 emulator 证据）：真实 AstroBox core 装生产安装器表盘 → Run → 手机装 `ui-demo` →
  触摸走完启动器 > Manager > 模块 > 启用 > 立即激活 > 打开界面 > 页面（开关、点击、第二层、标题返回、退出），`.043` 与 `.155` 均通过
  （断言模块探针序列、运行状态与每步截图）。
- 两个家族的控件、渲染与操作方式对齐：Band 10 Pro 现在也画真正的文本标签（MiSans-Medium）、图片、进度条，隐藏而不是删除不用的行，内容放得下时锁住滚动；
  标题返回在两个家族都先交给模块的 `on_back`；对话框两边都用固件 msgbox（Band 10 Pro mode 2，Band 11 mode 4）。滑块已移除。
  Band 10 Pro 的三处根因：标签缺 CJK 字体样式、`.043` 的 `lv_bar_create` 记录指错、隐藏用错了库入口（`lv_obj_set_hidden` 记录实为 `lv_obj_remove_state`，已在 `.036/.043` 改名并降为 restricted/PENDING，见 ADR-CAN-010）。
- Mirage 控件演示 `ui-gallery`（表单、进度条、文本、图片、对话框、样式、自关页、标题返回）在 `.043` 与 `.155` 上端到端通过（仅 emulator 证据）；
  Band 11 上还修掉单选不显示选中、重绘丢滚动位置、短页面被拖到底、提交失败后页面永久卡死（框架现在回收没完成的树）。
- 新静态证据（restricted / PENDING / not_probed）：`EVID-UI-1036-HOST-001`、`EVID-UI-1043-HOST-001`、`EVID-UI-4139-MSGBOX-001`、`EVID-UI-4155-MSGBOX-001`；
  更正了 `.043` `lv_bar_create` 与 Band 11 `.139/.155` 的 msgbox / object_delete 候选（原先指向别的函数）。
- **真机验证与批准（2026-10-19）**：维护者在 `.043`（Band 10 Pro）与 `.155`（Band 11）真机上运行 UI 宿主，反馈正常，并授权提升为生产可用：
  UI 宿主调用的固件入口（`.043` 28 个、`.155` 30 个）改为 `DEVICE_PROVEN` / `APPROVED`（证据 `EVID-UI-1043-DEVICE-001`、`EVID-UI-4155-DEVICE-001`，清单钉死在 `generated_stability.rs`），
  策略仍是 `restricted`（目标私有，不对模块开放）。UI 宿主在这两个 target 上的能力状态：**device verified / production approved**；证据是维护者反馈，没有设备日志；
  不含 Supervisor 安装路径与注销生命周期，§15 的 CAN-P0-007 等条目未改；`.036/.139` 状态不变。

## 生产就绪整改（2026-10-20，仅 host/emulator 证据）

- **常驻内存**：KV 不再缓存（原 `s_kv` 545 KiB 静态表），Supervisor `.bss` 由 627–647 KB 降到 69–90 KB；`canopus verify --max-resident` 与家族 `supervisor_max_resident`
  是新的构建门槛（`.043` 上限 163,840，`.155` 上限 184,320）。这是构建预算，不是实测的固件上限。
- **崩溃循环防护**：开机启动计数持久化，稳定 60 s 才清零；连续两次未稳定则回退旧代或禁用。global safe mode 自动升级与退出路径已在 K19 实现（见下）。
- **portable 打包**：只给能传服务上下文的 loader（Band 11）生成 portable 产物。
- **模块启动器入口**（ADR-CAN-011）：`ctx->launcher`，两步注册、无注销；Band 11 固件仿真覆盖；CAN-P0-007 仍为 BLOCKED-EVIDENCE。
- ~~Auto-run~~（已被固化启动取代，见下）、**后台 timer**（`.043`/`.155`，time 组版本 2）、**商店清单 `canopus` 兼容元数据**（AstroBox 客户端，本地提交）。
- 以上均无真机运行；`tests/hardware/gates.md` 新增了待验证的设备项。

## Host CLI（2026-10-19）

- `canopus` 命令行体验修整，见 [`cli.md`](cli.md)：仓库与 targets 目录统一定位（任意目录可用，包括装到别处的二进制）、target 版本后缀（`--target 155`）、
  `new` 先校验再创建并带 `--ui` 模板、`package` 拒绝过期 ELF、新增 `clean` / `doctor` / `-v`、可操作的错误信息；`cli/canopus/tests/cli.rs` 3 项集成测试。
  `--ui` 模板在 C 工程里按 5 个 target 的 `-Wall -Wextra -Werror` 编译并过 verifier，但没有在 Mirage 里跑过页面。

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
| 2026-10-20 | 生产就绪整改（见上）：KV 流式化与常驻体积门槛、崩溃循环防护、portable 打包、启动器入口、Auto-run、后台 timer、商店清单兼容元数据 | 全量 CI 全绿；host/emulator 证据，无真机 |
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

