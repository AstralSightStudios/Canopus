# Canopus 重构计划与进度追踪

创建：2026-10-14；第 2 版：2026-10-14（事实核查 + 决策 K7–K10）。本文档是重构工作的唯一进度来源；能力状态
（host implemented / static recovered / device verified / production approved）仍以 `progress.md` 与
`native-manager-ui-plan.md` §15 为准，重构不得提升任何能力状态。P3.7 完成后进度来源合并为一处。

状态标记：`[ ]` 未开始　`[~]` 进行中　`[x]` 完成（附证据/commit）　`[!]` 阻塞（写明原因）

## 1. 背景与痛点（2026-10-14 已核查）

开发体验：
- D1 地址、target 管理混乱
  - 同一份 `targets/*/symbols/*.json` 被多条生成链消费：Rust `canopus target generate-veneer` /
    `generate-rust-bindings`、`tools/generate_target_private_symbols.py`（CI 调用）、
    `scripts/generate_band11_native_config.py`、`scripts/generate_band9_loader_config.py`。
  - 另有不经 symbol 记录的地址来源：`loader/lua-recovery.toml` 中的裸地址（pmain/os_execute/io_open…）、
    `build_band11_installer.py` 构建时从固件镜像读取的指纹字、手写的 `sdk/rust/canopus-target-private/src/targets/*.rs`。
  - 55 个 target 目录以外的文件硬编码 target id；family 级差异（Manager 后端、Supervisor 体积上限
    73728/131072/98304、编译宏、loader 源）写在 `build_canopus_supervisor.sh` 的 `case` 里。
  - symbol 文件命名不一致：036/043/Band 9 为 `<target>.<domain>.<name>.json`，Band 11 为 `<target>.<name>.json`。
  - 生成物与生成器已不一致：2026-10-14 运行 `scripts/ci.sh`，第 1 步即失败（`generated_stability` 4 项：
    036 veneer 与 Rust bindings 过期，疑自 `4f87d57`；`calendar_text_color was unexpectedly promoted`
    与近期 calendar 批准提交冲突）。由于 `set -e`，第 2–7 步未执行。
- D2 ABI 不稳定、Rust/C API 不统一、SDK 混乱
  - 根因：模块是 zero-import ELF，直接绑定固件地址，因此每个模块 × 每个固件都要单独构建、发布，固件一升级全部失效。
    `canopus_context_v1` 只有前向声明，Supervisor 调用 `activate(0)` 传空指针，模块得不到任何 Supervisor 服务。
  - 线协议：CPC1（16B 命令）/CPS1（384B 状态）与 CPC2 并存（CPRT 只是内存中的 envelope，不是第三套线协议）；
    命令枚举两套（`CANOPUS_CMD_*` 与 `CANOPUS_SUP_CMD_*` 0x4351xxxx）；另有 CMI1/CMR1/CQD1/CQM1/CAP1 等 magic。
  - 两个 Cargo workspace；`target-generated` / `target-private` 两层；每 target 一个 feature，外加手写的互斥 `cfg` 矩阵；
    各 target 的 Rust 公开 API 不一致（036 有 84 个 `pub fn`，139 有 95 个）；Band 11 三个固件共用一个手写文件。
  - Rust `canopus-runtime` / `canopus-host-fake` / `canopus-ui-core` 是 C 实现的第二份实现；Manager 有
    lvgl_v9（1451 行）与 band11（1302 行）两套 UI 绑定。
- D3 无统一打包流程
  - Rust `package-builder` 产出 `.canopus`（tar + 签名），但设备端从不读取它；设备只认 CMI1 回执 + ELF
    （`scripts/build-module-installer-receipt.py`）。实际出包靠 Python 生成按设备分目录的 Lua 安装表盘
    （`build_module_installer_prod.py` 中硬编码 `PRODUCTS` / `SUPPORTED` / `PRODUCT_TARGETS`）。
  - `hello` 的 `build.sh` 写死 036，`package.json` 的 sha256 为全 0 占位。
  - 模块身份有四套语法：商店 `^[a-zA-Z0-9][a-zA-Z0-9_-]*$`（AstroBoxCreatorConsole `canopus-id.ts`）、
    CMI1 `[a-z0-9_.-]{1,31}`、insmod `^[a-zA-Z0-9_]+$`、示例 `org.example.hello`；版本有三套：
    semver 字符串、CMI1 的 u32、商店 `versionCode`。
  - Band 11 安装器构建依赖本地固件镜像；8 个测试直接读 `fwbins/`；evidence 中含 `/Volumes/EXT0/...` 绝对路径。
- D4 文档与进度分散：architecture.md 1771 行、native-manager-ui-plan.md 1461 行、firmware-symbol-porting-plan.md
  1271 行；进度权威分在 architecture §23、ui-plan §15 与 progress.md 三处；仓库没有 AGENTS.md / CLAUDE.md。

用户体验：
- U1 Manager 的"更新"和"回滚"按钮都没有意义：二者走同一分支，只记 intent=ENABLED、state=UPDATE_STAGED，
  设备上既没有新包也没有旧版本。更新之所以要"移除→重启→安装→启用→重启"，是因为 `sup_stage_package`
  对已存在的 module_id 返回 `STAGE_DUPLICATE`（-105）、安装后默认禁用、inbox 路径 `<token>.elf/.receipt` 固定。
- U2 只能在手环上管理，无法更新提醒/批量更新。AstroBox 目前把 Canopus 当表盘推送（Mass 16）；表盘 ID 超过
  16 字节会被随机化，更新匹配因此失效；商店按设备型号而非固件选择下载项，不匹配时回退到 default 或任意条目
  （`provider/.../officialv2.rs:1259-1264`），存在装到错误固件的风险。
- U3 Supervisor 不开机自启（`persistent_autoload=false`）：每次重启后都要打开安装表盘点 Run，才会加载 Supervisor
  并恢复模块。所以"重启生效"实际是"重启 + 手动 Run"，在此之前手机端也无法与设备端通信。

## 2. 已确认决策

| # | 决策 | 日期 |
|---|---|---|
| K1 | 允许破坏性变更（协议升版本、旧 Supervisor 须重装、Rust feature 命名可改） | 2026-10-14 |
| K2 | 更新采用"提交新包，重启生效"，不追求热更新 | 2026-10-14 |
| K3 | P65 完全移除（target、installer、SDK feature、脚本、文档、测试），事故记录仅在 ADR/历史中留一句；P65 期间产出的通用机制保留（见 P1.1） | 2026-10-14 |
| K4 | 手机端直接修改 AstroBox-NG，不走 WASM 插件：设备逻辑放 `core`（包名 `corelib`，按其 AGENTS.md 做成 LogicComponent），`app` 只暴露 Tauri 命令，前端在 AstroBox-NG-WebUI | 2026-10-14（第 2 版更正：原写"app crate"） |
| K5 | 手环端入口不走快应用壳/Corona 方案，另开通道（见 O1、O5） | 2026-10-14 |
| K6 | Mirage 固件来源使用其固件仓（`miragectl firmware archive/download`），不在 Canopus 内保存固件 | 2026-10-14 |
| K7 | 引入 Supervisor 服务表 `canopus_services_v1`（经 `canopus_context_v1` 传入）：通用能力每个固件只在 Supervisor 内实现一次，模块只依赖 Canopus ABI 版本；`target-private` 保留为 full-trust 逃生口 | 2026-10-14 |
| K8 | Band 9 / 9 Pro 冻结，随后（用户决定：除 Band 10 Pro 与 Band 11 外的固件地址一律不可信、需走自定义 loader 推翻重来）已从 `targets/` 删除，数据可由 tag `band9-frozen` 找回 | 2026-10-14 |
| K9 | 移除全部签名与验签：Ed25519/monocypher、密钥与角色、吊销、`.canopus` 签名、回执签名、target-pack revision 签名。保留非签名的安全检查：artifact SHA-256 完整性、target/固件绑定、模块 ID 合法性、版本单调（保留项为本文解读，如需一并删除请修改） | 2026-10-14 |
| K10 | 开机自动加载 Supervisor 纳入调查，作为 P4/P5 的前置（P4.0） | 2026-10-14 |
| K11 | 旧 Supervisor 不做迁移，要求用户重装（原 O2）：不迁移 `registry.bin`；新 Supervisor 遇到旧格式 registry 或旧 inbox 文件时须安全忽略并提示重装，不得崩溃或误加载 | 2026-10-14 |
| K12 | `calendar_text_color` 保留提升（原 O6）：被提升的是 .139 记录（.043 记录仍为 PENDING），依据为 2026-10-08 的用户授权与 `targets/xiaomi-band-11-4.100.139/background-calendar-port-approval.md`。失败的 `private_abi_records_have_exact_thumb_callables` 早于该授权，改为仿照 `band11_177_calendar_approval_is_exact_and_private_only`：`private_abi` 记录必须保持 restricted；APPROVED 仅限钉死的授权清单，且须满足 STATIC_RECOVERED、not_probed、promotion 固件 SHA 匹配 | 2026-10-14 |
| K13 | **已撤销（2026-10-17）**：旧快应用容器投递方案由 K15 / ADR-CAN-008 原生投递替代；旧模拟器结果仅保留为历史 | 2026-10-14 |
| K14 | 确立 WearPacket Layer 2 0x43 通道，与 Pb 同级承载 CPC2；模块仅用 `.canopus`，不映射为快应用（K15） | 2026-10-14 |
| K15 | 用户要求彻底移除 RPK 包装与快应用列表依赖，只保留 `.canopus`；通过加密 L2 CPC2 原生分块投递，固件私有 API 仍遵守批准门槛 | 2026-10-17 |
| K16 | 所选 target 已实现的功能随普通构建默认包含，不再要求额外实验启用参数；L2 后端由 exact-target 配置自动选择，构建默认值不提升符号或真机能力状态 | 2026-10-17 |
| K18 | 生产就绪整改（2026-10-20，真机验证除外）：Supervisor 常驻内存由 KV 缓存（545 KiB）改为按文件流式并加常驻体积门槛；模块开机启动计数与崩溃循环隔离；portable 按 loader 能力打包；模块启动器入口（ADR-CAN-011）；安装器 Auto-run（默认关闭）；后台 timer（time 组版本 2）；商店清单 `canopus` 兼容元数据（P5.7 客户端）。均只算 host/emulator 级证据，不提升任何能力状态 | 2026-10-20 |
| K19 | 收尾整改（2026-10-20，真机验证除外）：适配 Band 11 `4.100.179`（以已审核的 `.155` 包为基线，匹配器 + 反编译复核 + 调用关系/位置核对，人工纠正 1 处误匹配；全部记录 restricted/PENDING，不继承任何批准或真机证明，见 `targets/xiaomi-band-11-4.100.179/loader/README.md`）；全局 safe mode（启动标记写进注册表头，自动进入、重启保持、`EXIT_SAFE_MODE` 退出与 trial 试运行，Manager 提供退出入口）；CreatorConsole 与 AstroBox-NG 的商店 `canopus` 元数据（P5.7）；043 冒烟完整通过（含手机端 phone flow，P5.2 的超时不再复现）。均只算 host/emulator 级证据 | 2026-10-20 |
| K20 | 固化启动取代页面延时 Auto-run（ADR-CAN-012）：默认不固化，AstroBox Canopus 页确认后 Supervisor 经 `/dev/bes_flash` 改写 `rc.sysinit` 的一段等长差异尾段，覆盖 Band 10 Pro `.043` 与 Band 11 `.155` / `.179`（Mirage 冷启动验证，无真机）；安全模式与自动停用/回退对用户可见（管理器、启动通知、AstroBox）；修复退出安全模式不清崩溃计数；Band 10 构建去掉 `.ARM.exidx`；线程/工作队列的评估结论见 progress.md | 2026-10-21 |
| K17 | 放弃 Band 11 `4.100.177`（缺固件镜像、适配不完整），整体移除其 target、Rust 绑定、测试与脚本；Band 11 改为适配 `4.100.179`（q66tc 完整包）。177 上的授权清单（calendar alpha 等）随目录删除，179 须重新取得用户授权，不继承；历史行与证据出处文字保留，可由 git 历史找回 | 2026-10-20 |
| K21 | 快速静态适配四款 Mirage 支持的固件：Xiaomi Band 9 `3.1.32`（n66）、Band 9 Pro `3.1.190`（n67cn）、Band 10 `3.1.28`（o66tc）、REDMI Watch 6 `3.100.043`（p65）。Band 9 由 tag `band9-frozen` 恢复且全部批准降为 restricted/PENDING；n67 由 3.1.175 包移植（108/123 STATIC_RECOVERED），o66 由 Band 9 包移植（47/142），p65 由 Band 10 Pro `.043` 包移植。四者都标注为 Band-11 风格 Lua-owned staged loader（无 insmod、无 Lua `os.execute` 恢复），但 stage1/stage2、installer、模拟器与真机验证均未实现；n66/n67/o66 为 LVGL v8，需要专门的 v8 UI 后端，p65 为 v9。只新增静态记录，不提升任何能力状态 | 2026-10-24 |

## Toast API 移除

- [x] 按用户要求移除 C/Rust SDK 的 Toast 入口、Manager 渲染与计时、示例及模拟器场景；节点编号 11 与能力位 16 保留停用，其余 ABI 编号不变。保留标题更新、输入事件时钟和字体回收。下文既有 Toast 验证记录仅表示移除前的历史，不代表当前可用能力。
  验证：C host 与双家族输入测试通过 ASan/UBSan；四组原生后端配置通过 ASan/UBSan；Rust UI core 22 个单元测试与 2 个文档测试通过；`.043` / `.155` Supervisor 构建与 ELF 校验通过；native-ui-lab 五目标构建、ELF 校验与打包通过。未新增模拟器或真机验证，不提升能力状态。

## 3. 待定项（阻塞对应任务，回答后移入 §2）

- O1（现由 K15/ADR-CAN-008 替代旧 K13）手机↔设备端控制通道（原阻塞 P5.1 及之后）。
  - 前提更正：AstroBox 的 `transport.send` 只存在于 WASM 插件 host API，只接受 Xiaomi `WearPacket` protobuf，
    且只走 Pb 通道（`pluginsystem/src/api/host/transport.rs:21-50`），不能发原始字节。无论选哪种方案，都要在 `core` 中新写代码。
  - 候选（按现有证据强弱排序）：
    - a. interconnect：手环端以保留包名注册 connection。`interconnect_connect/send/close` 与
      `thirdparty_submit_message_content` 在 036/043 已是 STATIC_RECOVERED/APPROVED，在 139/155/177 为 CANDIDATE
      （路由图见 `sdk/rust/canopus-target-private/src/targets/xiaomi_band_10_pro_3_101_036.rs:1221`）；手机端走
      ThirdpartyApp `SEND_PHONE_MESSAGE`。待证：miwear-server 是否要求该包名是已安装的快应用、是否校验 fingerprint
      （`CONN_STATUS_UNINSTALLED=7` 暗示会检查）；AstroBox 侧现从已安装快应用列表查 fingerprint。
    - b. hook protobuf 分发，处理自定义 WearPacket 类型。
    - c. hook 一个 L2 通道。
  - 数据面另行确定：
    - D-a 复用 Mass 16 投递，由 Supervisor 按 token 拾取（Supervisor 已在调用 `delete_watchface(token)`；ID 须 ≤16 字节）。
    - D-b 经控制通道分块传输（可借鉴 `core/src/device/res_pack.rs` 的 P/F/A/C 续传设计）。
- O3 商店清单字段。归属已查明：schema 在 `provider` 的 `ManifestV2`，发布校验在 AstroBoxCreatorConsole，
  `manifest_v2.json` 在各条目自己的仓库，AstroBox-Repo 只放索引。待定的是字段设计，默认提案见 P5.7。
- O4 固件可用性（部分已查明）：
  - Mirage 只有 Band 10 Pro 3.101.043 能启动到 UI；Band 11 4.100.177 只到开机向导/配对二维码；036 和 139 在 Mirage 中没有记录。
  - Mirage 当前固件库为空，配置的库路径不存在。
  - `fwbins/` 只有 `vela_ap.bin`，p67 还需要 `vela_resource.bin`。
  - 待查：MiWearFirmwareArchives 是否有 036/043/139/155/177 的完整包。
- O5（已结案 → K13，无需锚点快应用）如果 O1-a 证实需要已安装的快应用，是否接受一个无 UI 的"路由锚点"快应用（与 K5 的边界）？P5.0 出证据后再定。

（O2 → K11，O6 → K12，已结案。）

## 4. 总览

| 阶段 | 内容 | 状态 | 依赖 |
|---|---|---|---|
| P0 | 基线、ADR、AGENTS.md、CI 恢复 | [x] | — |
| P1 | Target/地址单一来源；移除 P65；冻结 Band 9 | [x] | P0 |
| P2 | ABI/SDK：服务表、协议收敛、移除签名、Rust 整理 | [x] | P0.1–P0.4；P2.4 起依赖 P1.3 |
| P3 | 统一打包 + 文档 | [x] | P1、P2.1 |
| P4 | 设备端更新语义 + 开机自启调查 | [x]（P4.0：固化启动（`.043` 与 Band 11 `.155` / `.179`，用户确认才写闪存），待真机验证） | P0.1、P0.2（不依赖 P5） |
| P5 | 手机端管理（AstroBox-NG） | [x]（.043/.155/.179 后端已默认内置；商店元数据与发布校验已完成；真机验证待做） | P5.0（O1）、P2.5、P3.3 |
| P6 | Mirage 模拟器调试 | [x]（043 冒烟 + Band 11 155/179 端到端全通） | P6.0、P6.2、P3 |

里程碑达成情况：
- M1（让用户尽快受益）：P0 → P1.1 移除 P65、P1.8 冻结 Band 9 → P2.7 移除签名 → P4.1–P4.5（同 ID 新版本一步更新）。已达成。
- M2（开发体验）：P1 单一来源、P2 服务表与统一协议、P3 统一打包与构建工具链。已达成。
- M3（手机端与模拟器）：按 K15 重验原生 `.canopus` / L2 数据面；旧快应用闭环不计入此项，固件私有 API 的生产批准另行处理。

## 5. 任务清单

### P0 边界与基线
- [x] P0.1 ADR-CAN-003：模块身份、版本与包格式
  - module_id 取各方语法的交集，默认 `^[a-z][a-z0-9_]{0,15}$`，同时用作商店 id、回执 module_id、insmod 名和 inbox token。
    取 ≤16 字节，是因为它在 Mass 16 路径上也是表盘 ID，而 AstroBox 会随机化超过 16 字节的 ID；若不再走 Mass 16，可放宽到 31。
  - 版本：`versionCode`（u32，单调递增）= 回执 `module_version` = 商店 `versionCode`；`version` 字符串仅用于展示。
  - `.canopus` 内容：manifest、portable 产物（`artifacts/abi-<major>-thumbv8m/`）或 per-target 产物（`artifacts/<target_id>/`），
    每个产物附一份无签名的安装回执；安装器表盘和商店条目都是它的派生物。
- [x] P0.2 ADR-CAN-004：设备协议
  - 单一线格式（CPC2 升版本）+ 统一的命令枚举。
  - 逐项写明所有 magic 的去留：CPC1/CPS1 删除；CMI1 改为无签名回执；CMR1 保留；CQD1/CQM1 并入查询 payload；CAP1 并入服务表。
- [x] P0.3 ADR-CAN-005（见 `adr/ADR-CAN-005-phone-delivery-and-management.md`）：手机端管理协议边界。由常驻的 Supervisor 服务层应答（不是 Manager UI），与 P0.2 使用同一套 opcode；
  O1 只决定 transport。依赖 O1。
- [x] P0.4 ADR-CAN-006：Supervisor 服务表（K7）
  - `canopus_context_v1` 的定义：struct_size、abi、services 指针。
  - 服务分组与各组版本；能力缺失时的语义。
  - portable 模块的 verifier 规则：不允许出现任何固件地址。
- [x] P0.5 恢复 CI（`CANOPUS_CI_KEEP_GOING=1 scripts/ci.sh` 全绿；Band 11 安装器测试在无本地种子/未构建固件产物时显式跳过）
  - 给 `ci.sh` 加 keep-going 模式，记录完整的红/绿表。
  - 在单独提交中重新生成 036 生成物，并按 K12 改写 `private_abi_records_have_exact_thumb_callables`
    （.139/.155 的已授权清单同样钉死，任何扩大都让测试失败）。
  - 重构从绿色开始。
- [x] P0.6 冻结基线（`scripts/baseline-hashes.sh` → `docs/baseline/hashes.txt`，两次运行确定；.177 Supervisor 因缺固件跳过）（在 P0.5 之后），记录三类哈希：
  - (a) 已提交的生成物；
  - (b) 生成器当前的输出；
  - (c) 每个 active target 的 Supervisor ELF 与示例模块 ELF。
  - 重构后须字节一致，或对差异逐项说明。
- [x] P0.7 `AGENTS.md`（原 P3.6，前移）：目录结构、常用命令、不可触碰项、证据门槛、"改 symbols 后必须重新生成"。

### P1 Target / 地址单一来源
- [x] P1.1 移除 P65（K3；ADR-CAN-007；同时移除仅 P65 使用的 `module_initialize` 入口风格、模板与 verifier 检查）。删除范围：
  - 目录：`targets/xiaomi-p65-*`、`manager/target/p65`、`watchfaces/canopus-installer-prod/xiaomi-p65`。
  - 脚本与测试：`scripts/*p65*`、`scripts/lua/test_p65_installer.lua`、`scripts/tests/test_p65_*`、`tests/host/test_p65_*`。
  - Rust：`target-xiaomi-p65-*` feature、`generated_p65.rs`、`canopus-target-private/build.rs` 的 P65 分支。
  - 共享代码中的 `CANOPUS_SUP_P65_*` 条件编译（`canopus_supervisor_module.c`、`canopus_supervisor_platform.c`）。
  - `tools/canopus-core/src/template.rs` 的 P65 `module_initialize` 脚手架。
  - verifier / fixture / stability 测试中的 P65 用例；README 与 progress 中的相关段落。
  - 保留 P65 期间产出的通用机制：CMR1 注册帧、registry 错误码细分与诊断、注入式文件 IO 加固。
  - 完成标准：CI 全绿。
- [x] P1.2 target 数据模型（判断结案：`native-bindings.json` 已是 symbol 名→宏的声明式表，由 native_config.rs 消费；`lua-recovery.toml` 的地址是 Lua VM 内部点，不属于 Canopus ABI symbol 模型，原地保留；symbol 文件名不影响功能（按 symbol_id 加载），不做 3000+ 文件的重命名；固件指纹已预生成进 `native-bindings.json` 的 `fingerprint_words`，Band 11 安装器构建不再读取固件镜像；已完成：family 层 `targets/_families/*.toml`（Manager 后端、体积上限、loader 源、编译宏、prod 目录、生成选项）、`status`、`rust_module`、build_canopus_supervisor.sh 的 case 改读 `target info --shell`；未完成：`native-bindings.json` 并入声明式绑定表、`lua-recovery.toml` 裸地址转 symbol、指纹预生成、symbol 文件命名统一）：`target.toml` + `symbols/*.json` 为唯一权威
  - 新增 family 层（如 `targets/_families/<family>.toml`），承载 Manager 后端、Supervisor 体积上限、编译宏、loader 源、安装器布局。
  - `native-bindings.json` 并入为声明式绑定表；`lua-recovery.toml` 中的裸地址改为 symbol 记录。
  - 固件指纹预先生成进 target 包，构建不再读取固件。
  - 统一 symbol 文件命名；target 增加 `status`（active / frozen）。
- [x] P1.3 `canopus target generate <id|--all> [--check]`（Rust）统一产出 C 头、target config、Rust 绑定、SDK private-symbol 元数据、Band 11 native 地址头（Lua/loader 配置待 P1.2 一并并入）。另有 `canopus target info <id> --shell|--json`。
- [x] P1.4 迁移并删除 Python 生成脚本（`generate_band9_loader_config.py` 随 Band 9 删除；其余两个已移植到 Rust 并删除；CI 只跑 `target generate --all --check`；pre-commit 钩子未做）（`generate_target_private_symbols.py`、`generate_band11_native_config.py`、
  `generate_band9_loader_config.py`）；CI 与可选的 pre-commit 钩子只做"生成物未过期"检查，防止再次出现 `4f87d57` 式的过期。
- [x] P1.5 target 注册表（`target list --active --names`；ci.sh 与 build_prod_all.sh 不再硬编码清单）：CLI / CI / 打包脚本从 `targets/` 发现 active target，删除硬编码清单。
- [x] P1.6 保持证据门槛（`generated_stability` 中 .139/.177 授权清单测试、FORBIDDEN/restricted 不出 wrapper 的断言原样保留并通过）：FORBIDDEN / CANDIDATE / restricted 状态与 evidence 链迁移后语义不变（测试覆盖）。
- [x] P1.7 固件定位（K6）（`scripts/fwlocate.py`：`$CANOPUS_FW_DIR`/`fwbins/`；测试与构建脚本改用之，缺固件显式跳过；150 个文件里的 `/Volumes/EXT0/...` 绝对路径改写为仓库相对路径或 `fw://<target_id>/<file>`；Mirage 固件库查找待 P6.0）：需要固件的测试和工具经统一定位器查找（`CANOPUS_FW_DIR` 或 Mirage 固件库），缺失时显式跳过；
  evidence URI 改为 `fw://<target_id>/<file>` 相对形式。
- [x] P1.8 冻结 Band 9 / 9 Pro（K8；tag `band9-frozen`；target.toml `status = "frozen"`；同时清理了 Rust 互斥 cfg 矩阵，改由 build.rs 校验）
  - target 标记为 frozen；退出 `build_prod_all`、CI 和 Rust feature。
  - 仅 Band 9 使用的代码（lvgl_v8 后端、stage1/2、NSH bootstrap 脚本）移出构建；默认删除，并打 tag `band9-frozen` 以便找回。
  - 共享的 `runtime/loader` 保留。
- [x] P1.9 与 P0.6 的基线对比通过（committed==generator；差异均已解释：P65/Band 9 删除、生成物头注释、hello/Rust ELF 链接顺序与 crate 路径）。

### P2 ABI / SDK 整理
- [x] P2.1 实现服务表（K7、P0.4）（判断结案：mem 受 restricted 策略限制、NOTIFY 组已完成（Band 11 经 unicorn 加真实固件验证 `canopus_manager_native_notify`，Band 10 Pro 仅 host 测试，模拟器冒烟为 headless 不走通知）；ui/bt/interconnect 按 ADR-CAN-006「实现状态」不实现，待有具体模块需求再立项；已完成：`sdk/c/canopus_services.h`、`manager/service/canopus_supervisor_services.c`；module/log/fs 三组，per-module context 经 activate/publish 回调与自定义 loader 的 init 入口 r0 传入；fs 为模块私有扁平命名空间；TIME 组已完成（`time_monotonic_ms`，经 clock_gettime；Supervisor 平台钩子 `monotonic_ms`，仅 Band 10 Pro）；MEM 组已完成（`mem_alloc`/`mem_free`，无固定块数/字节上限，固件堆为唯一约束，卸载时 Supervisor 回收；经 heap_malloc/free，只由 Supervisor 调用，不向模块暴露地址，策略 restricted 不变；Band 11 为 STATIC_RECOVERED 未验证），notify/ui 与 BT/interconnect 未做；Band 10 stock modlib 的 ctor 无参数，portable 加载需等自定义 loader 迁移）
  - Supervisor 按 target 填表，并经 context 传给模块回调（目前是 `activate(0)`）。
  - 第一批服务：日志/状态、时间/定时器、内存、文件、通知、`canopus_ui` 语义树渲染（复用 `sdk/c/canopus_ui.h`（原 `app-sdk/ui`））。
  - 2026-10-18：Band 11 `.139/.155/.177` 普通 UI 构建接入 exact-target 私有 LVGL timer pump，复用现有服务调度；headless 仍不支持，息屏暂停与 UI 重建恢复边界见服务规范，不提升符号或设备验证状态。
  - 2026-10-24：FS 版本 2 追加空目录删除 `rmdir`（C/Rust），保留旧对象成员偏移，非空目录返回 `NOTEMPTY(-39)`；
    `.043/.139/.155/.179` 接入静态恢复的固件入口，`.036` 缺少入口证据而保持版本 1。C host ASan/UBSan、Rust ABI/runtime、
    五目标服务模块及 Supervisor 构建/verifier、建模 Thumb 目录适配器及 generated_stability 18 项通过。证据 `EVID-RMDIR-*`；未做真实文件系统或真机验证，符号状态不提升。
  - BT 与 interconnect 放到后续批次。
- [x] P2.2 portable 模块（判断结案：`hello` 与 `no-heap-counter` 有意保持 target-bound，分别示范固件原生 API 与 Rust SDK 的设备绑定路径；portable 路径由 `hello-portable` + TIME 服务示范，Band 10 stock modlib 上的 portable 加载等自定义 loader；已完成：`canopus verify --portable`（任何已注册固件的地址范围内出现地址即失败）、`modules/examples/hello-portable`（同一 ELF 通过 5 个 active target 的 verifier 与 portable 检查，CI 覆盖）；未完成：原 `hello`（依赖固件 timer/clock，等 time 服务）与 `no-heap-counter` 迁移；设备加载需自定义 loader）
  - verifier 增加"零固件地址"模式。
  - `hello` 与 `no-heap-counter` 迁移为 portable，同一个 ELF 在 036/043/139/155/177 上通过 host 测试。
- [x] P2.3 Rust 合并为单一 workspace（根 Cargo.toml；设备 staticlib 用 `--profile device`，`default-members` 排除不能在宿主构建的 no-heap-counter-device；Rust 模块 ELF 哈希因 crate 路径变化而变，已刷新基线；hello 的 C ELF 不变）。
- [x] P2.4 target-private 整理（三个后端现在函数集一致：缺的能力是返回 `ERR_UNSUPPORTED`(-38) 的桩（036/043 补了 `lvx_object_clear_flag`、`capabilities()`、`ERR_UNSUPPORTED`、`SELECTED_TARGET_ID`），`tests/api_parity.rs` 在 CI 里强制三者函数集相等；互斥 cfg 矩阵由 build.rs 校验取代）
  - 所有 target 由生成代码提供同一函数集，缺失的能力显式返回 `Unsupported`；手写部分只保留真实差异。
  - 删除手写的互斥 `cfg` 矩阵（`build.rs` 已保证恰好选中一个 target）。
- [x] P2.5 协议收敛（P0.2）（设备线协议只剩 CPC2：新增 `NATIVE_STAGE`/`RESTORE_AFTER_BOOT`/`SCAN` 操作码，QUERY_DEVICE 载荷 36 字节含 scan 摘要；CPC1/CPS1 不再被设备端接受（`handle_command`/`render_status` 仅留作进程内 host 测试接口）；Band 10 Pro / Band 11 框架安装器、模块安装器、hello 安装表盘、模拟器 driver 全部改为 CPC2；旧的独立开发安装表盘删除；Band 11 受保护的 main.lua 已用本地种子重新生成；`scripts/tests/band11_*.py` 的固件仿真脚本已全部用 unicorn（venv）跑通（重建 155/177 的暂存资源后；private_backend 的 workspace 路径已修）；CQD1/CQM1/CAP1 并入未做）：删除 CPC1/CPS1 与第二套命令枚举；更新 `abi/schema`；Lua 安装器与外部模块安装器同步改造。
- [x] P2.6 C/Rust 同源（`sdk/rust/canopus-abi/tests/c_header_parity.rs` 在 CI 中逐项校验 Rust 常量与 C 头一致，布局由 C 的静态断言与 Rust 的大小断言守住；`canopus-runtime`/`host-fake`/`ui-core` 判断为保留双实现：Rust 侧是宿主可测的实现，设备侧是 C，不绑定 C）
  - ABI 类型由一份清单生成两侧代码。
  - `canopus-runtime` / `host-fake` / `ui-core` 二选一：改为绑定 C 实现，或保留双实现并加一致性测试。
- [x] P2.7 移除签名（K9；回执改为无签名 CMI2/176B/version 2，旧 CMI1 被拒绝；`package verify` 改为结构+哈希校验；`re revision-sign/verify` 与 `key` 子命令整体删除；monocypher 删除，Band 10 Pro Supervisor 52 KiB）
  - 设备端：删除 monocypher、`s_installer_public_key` 与回执验签；回执改为无签名格式，继续校验 target/固件、
    artifact SHA-256、module_id 与 module_version。
  - 工具：删除 `package sign/verify/keygen`、`key` 子命令、`re revision-sign/verify`、`keyroles.rs`、
    `build-module-installer-receipt.py` 的签名步骤、`build_module_installer_prod.py` 的 `verify_signature`、schema 中的签名字段。
  - 文档：`security-model.md`、README 中"未签名包默认不批准"的原则、CAN-P1-013。
  - 测试：删除签名用例；新增"哈希 / target / 固件不匹配仍被拒绝"的用例。
- [x] P2.8 Manager 的 UI 后端合并（`manager/target/canopus_manager_target.c` 单文件，Band 11 由 family 的 `-DCANOPUS_MGR_BAND11=1` 选择；用 `diff -D` 机械合并并手工修了一处被注释截断的分支。验证：036/043 的 Supervisor ELF 字节完全一致，Band 11 的 139/177 所有加载段内容一致（仅符号表里的源文件名不同），unicorn 的 Band 11 固件测试与 CI 全绿。仍有约 100 处 `#ifdef` 分支（页面/应用/通知描述符布局、身份校验、少量 UI 常量），后续可再逐步收敛成按目标的头文件）
- [x] P2.9 Supervisor / Manager / client / host tests 同步改造（本阶段范围内：ASan/UBSan 纳入 CI 保持干净、旧格式 registry 拒绝测试、family 体积上限由 build 脚本强制；随 P2.5 协议收敛再复查）；ASan/UBSan 保持干净；各 family 的 Supervisor 体积不超过上限；
  新增"遇到旧格式 registry.bin / 旧 inbox 文件时安全忽略"的测试（K11）。

### P3 统一打包流程
- [x] P3.1 `canopus new <id> [--target T]...`（C；Rust 工程用 `module new`，构建走 sdk/rust/examples/no-heap-counter 的 build.sh 路径；`canopus build` 已支持 Rust：`canopus new --lang rust --target <id>` 脚手架，`[build] sources = ["Cargo.toml"]`，cargo（thumbv8m、panic=abort、按目标选 canopus-target-generated 特性）→ C ctor shim → `ld.lld -r`（不用 --whole-archive，避免把 compiler_builtins 整个拉进来）→ verifier；仅 stock modlib 目标（Band 10 Pro），Band 11 需要 Rust 侧的服务表入口，portable Rust 同理未支持；CI gate_5 覆盖）：生成含 `Canopus.toml`、构建配置、示例源码的模块工程（默认 portable）。
- [x] P3.2 `canopus build [--target X]`（C；portable 构建一次，target 模块按 `[targets].include` 逐个构建，产物过 verifier；Rust 见 P3.1）：portable 模块只构建一次，target-private 模块按矩阵构建；产物均须过 verifier。
- [x] P3.3 `canopus package`（Canopus.toml → `dist/<id>-<ver>.canopus`，含 manifest、产物与逐 target 的 CMI2 回执；校验沿用 `canopus package verify`）：产出确定性的 `.canopus`（P0.1 格式，含无签名回执）；`canopus verify` 只做结构 / 哈希 / target 校验。
- [x] P3.4 安装器表盘：`canopus package --installer` 产出 `dist/installer/<family>/`（main.lua + 载荷 + 回执，仅有安装传输模板的设备族：Band 10 Pro、Band 11）；`build_module_installer_prod.py` 的 SUPPORTED 改为从 targets/ 动态推导（外部产品的 PRODUCTS/PRODUCT_TARGETS 表仍保留，因其 payload 不是 Canopus 工程）。
- [x] P3.5 `hello` 示例改用 `canopus build` 构建（`modules/examples/hello/Canopus.toml`，旧 build.sh/package.json 删除；hello 的 C ELF 哈希因链接顺序变化已刷新基线）；`hello-portable` 同样。
- [x] P3.6 （已前移为 P0.7）
- [x] P3.7 文档收口（已完成：architecture.md、function-signatures.md 已移入 docs/archive/；其余历史文档被 evidence 的 URI 引用，原地保留；`docs/README.md` 入口页、三份大文档加归档提示、progress.md 指向本文；未做：物理搬入 archive/）
  - `docs/` 入口页（不超过一屏）。
  - 进度来源合并为一处（architecture §23、ui-plan §15、progress.md、本文件）。
  - 过时的大文档归档。
- [x] P3.8 外部模块仓库（指南：`docs/external-module-migration.md`）（Bluetooth Audio、Lyra、Resource Hook、Data Quota Lift、ReFetch）迁移指南：
  去签名、新回执、协议变更，以及能否转为 portable。

### P4 设备端更新语义
- [~] P4.0 开机自动加载（K10；2026-10-21 的 Auto-run 页面延时方案已删除，换成**固化启动**，ADR-CAN-012：Band 10 Pro `.043` 与 Band 11 `.155` / `.179`，用户在 AstroBox 确认后才改写启动脚本，Mirage 冷启动验证，无真机证据；`.139` 无后台泵不启用）。**静态结论（043 IDB，2026-10-14）**：
  - 启动脚本固化在固件 romfs（`/etc/init.d/rc.sysinit` 挂载 /data 等，`/etc/init.d/rcS` 启动 miwear 等服务，仅在存在 `/data/nobusiness` 目录时跳过业务服务），
    没有任何读取 `/data` 下脚本的钩子，因此无法通过写文件让 Supervisor 随开机加载。
  - 剩余可行方向（均未验证）：(1) 安装表盘 Lua 在页面初始化时自动执行 Run（需该表盘是开机后的当前表盘，且要防止重复加载/崩溃循环——可用"上次加载是否完成"标记）；
    (2) 分析 `miwear_product_load` 是否读取可写配置；(3) 经 interconnect/快应用路由由手机触发加载（依赖 O1）。
  - 在 Mirage 上冷启动验证仍待做；在此之前维持手动 Run，手机端检测"Supervisor 未加载"并提示。
  原计划：
  - 在固件中寻找开机时可挂接的入口：启动脚本或服务、当前表盘 Lua 的自动执行、其他自启机制。
  - 优先在 Mirage 043 上反复冷启动验证。
  - 若无可行方案，则维持手动 Run，由手机端检测"Supervisor 未加载"并提示用户。
- [x] P4.1 INSTALL 支持升级（实现为 A/B 双代而非 `.next`→current 搬移：更新写入非活动代，registry 位切换；设计见 module-installer-io.md）
  - 同 module_id 且 `module_version` 更高：写入 `<token>.next.*`，保留原启用状态，返回 REBOOT_REQUIRED。
  - 同版本或更低版本：拒绝（防回滚）。
- [x] P4.2 开机切换
  - Supervisor 加载时原子地把 next 切为 current，旧版保留为 `<token>.prev.*`。
  - 加载失败时自动退回 prev，并进入可诊断状态。
- [x] P4.3 Manager 按钮
  - 删除无意义的"更新"按钮；"回滚"只在存在 prev 时显示，作用是切回 prev。
  - `CANOPUS_CMD_UPDATE` / `ROLLBACK` 按 P4.1/P4.2 重新定义或删除。
- [x] P4.4 现有外部安装表盘路径（Lua → inbox → `/canopus/install`）即可投递新版本，无需等待 P5。
- [x] P4.5 host 测试：升级成功、同版本/降级被拒、哈希/target 不匹配、写入中断后恢复、加载失败退回 prev。

### P5 手机端管理（AstroBox-NG，K4）
- [x] P5.0 通道设计：WearPacket L2 0x43 / CPC2（ADR-CAN-008），旧快应用方案撤销。普通构建按 exact-target 配置自动包含已实现的后端，不要求额外启用参数，也不提升符号状态。
- [x] P5.1 设备端原生 staging RPC（15..18）、INSTALL、目录快照式 SCAN；不再有 import_package 或快应用目录。
  - 握手：框架版本（`framework_revision`）、ABI、target。
  - 操作：模块列表、安装/升级、启用、禁用、移除、回滚。
  - 数据面按 O1 结论实现。
- [x] P5.2 原生数据面重验：host 上传顺序/短写/中断/错误 target 与 ARM shim 测试通过；043 收件箱升级/回滚全流程通过。双 owner 后端分离 UI 命令处理与 Bluetooth 响应发送，解决跨 NuttX task-group 写 fd 的 EBADF。2026-10-17 Band 11 `.155` 新建 Mirage 设备经生产安装器 Run，真实 AstroBox core 加密 L2 原生安装/列表查询、本地 SCAN 全部通过；实验 `.155` 资源已同步到待打包目录，普通构建也默认包含该后端。2026-10-20 在本机 Mirage 上重跑 `scripts/emu/smoke.py`：收件箱流程（安装/升级/降级拒绝/坏版本回退/回滚）与手机端 phone flow（真实 AstroBox core 经加密 L2 安装 v1，重启后升级 v2）全部通过，之前的 phone flow 超时没有复现；Band 11 `.155` 与 `.179` 的 `band11_smoke.py` 通过。仅 emulator 证据，符号保持 restricted/PENDING/not_probed；不使用 FORBIDDEN 的线程/工作队列。证据：各 target 的 evidence/l2-sram-probe.json。旧 RPK 冒烟结果不能替代。
- [x] P5.3 core：CPC2 RPC 查询列表/版本，原生 tar/manifest/receipt 校验、按精确 target 选择、逐块应答上传；移除 modules_from_apps 与包名映射。
- [x] P5.4 `app`（已完成：`DeviceResourceUpdateReport.canopus_modules`，新增 `canopus_send_request`、`canopus_query_device`、`canopus_trigger_scan`、`canopus_set_module_enabled`、`canopus_remove_module` 等 Tauri 命令，直通 L2 通道）。
- [x] P5.5 更新提醒：模块列表和已装版本只来自 Supervisor；原生商店清单/版本比较独立适配，不借用 quickapp 项。
  比较 `versionCode`，并按下载项的 `canopus` 元数据检查 ABI、框架版本与固件 target（2026-10-20，AstroBox-NG `provider`/`app`/`privprovider` 本地提交，未推送）。
- [x] P5.6 前端：Canopus 专属卡片与管理页；原生安装队列独立调用 canopus_install_module，不再用 Mass 64/快应用安装。
- [x] P5.7 商店清单：旧 quickapp 下载项设计撤销，原生 Canopus 条目需独立类型与 target/ABI 元数据。**客户端**（`ManifestCanopusV2`、更新检查按手环上报拒绝不兼容下载项、单元测试）、
    **AstroBoxCreatorConsole**（导入 `.canopus` 时读包内 manifest：版本、versionCode、模块 ID、`canopus {abi, targets?}`；读不出的包不能进入下一步；PR 审核规则逐项核对条目声明与包体，`~/GitHub/AstroBoxCreatorConsole` 本地提交 0d936e8）
    与 **AstroBox-NG web 商店安装**（下载任务先用 `canopus_check_download` 问手环自己的固件 target / 框架 / ABI，Canopus 资源不再回退到别的设备的下载项）均已完成；设备端回执校验仍是最终防线。
  - `downloads[deviceId]` 下增加 `canopus: { abi, targets?: [target_id], minFramework? }`。
  - portable 模块只写 abi；target-private 模块必须列出 target。
  - 客户端安装前比对手环固件，不匹配就拒绝（修复按型号回退到 default 或任意条目的问题）。
  - CreatorConsole 的 ID 校验改为 P0.1 的语法。
- [x] P5.8 Supervisor 自身升级（设计结案：Supervisor 不常驻——每次开机都要重新运行安装器表盘才会加载，所以"升级"就是用新版安装器表盘替换旧表盘后重启再 Run，不存在旧 Supervisor 与新版并存的窗口；手机端检测落后版本直接复用现有的表盘更新流程（商店表盘条目 versionCode 与设备上 `watchfaces` 列表里的 versionCode 比较，AstroBox 的 DeviceResourceUpdateReport 已覆盖表盘）。因此不需要新的框架版本通道；Manager 仍显示 framework_revision 供用户核对）
  明确告知需重装模块（K11），并支持在手机端一键重装此前记录的模块列表。

### P6 Mirage 模拟器调试
- [x] P6.0 O4（Mirage 库实际在 `/Volumes/EXT0/Mirage/.scratch/Library`；043 完整 OTA 目录 `/Volumes/EXT0/miwear.watch.p67tc_v3.101.043_full_a4ce8564` 的 vela_ap.bin SHA-256 与 target 一致，`firmware import` 后可启动；Band 11 的 139/155(tc)/177 在 MiWearFirmwareArchives 上有包，未下载）：把 043 完整固件（含 `vela_resource.bin`）导入 Mirage 固件库并修正库路径；查 MiWearFirmwareArchives 对 036/139/155/177 的覆盖。
- [x] P6.1 `canopus emu`（`canopus emu smoke` 封装 scripts/emu/smoke.py，细节见 docs/mirage-emulator.md；实际用 `scripts/emu/smoke.py` + `driver.lua` 直接包装 miragectl：create/shutdown/`fs put nand_data`/boot/`sh`；`fs put live` 走 NSH printf 对 50KB 文件不可用，故离线写分区。NSH 内置 `lua`，driver.lua 用它按安装表盘同样的 CPC2/CPC1 IO 驱动 Supervisor）：包装 `miragectl` 的 create / boot / fs put / sh / screenshot
  - 日志：读 `<device>/Logs/` 下的 console.log、uart*.log、qemu.log（`miragectl` 没有 `log` 子命令）。
  - `sh` 与 GUI 互斥，CLI 须独占运行。
- [x] P6.2 冒烟测试（在 043 上）（`python3 scripts/emu/smoke.py`：安装→启用→重启加载→更新 v2（REBOOT_REQUIRED）→降级被拒→v3 加载失败回退 v2→v4→回滚到 v2，全部通过；仅 host/emulator 级证据）。发现并修复两个真实问题：加载失败的 stock-modlib 模块未 rmmod 导致回退重试 EEXIST；模拟器里由 NSH 任务驱动 INSTALL 时通知/删表盘调用会阻塞，加了仅测试用的 `CANOPUS_SUP_HEADLESS` 构建开关
  - 流程：`fs put` Supervisor → `sh` 执行 insmod 或等效于 Run 的流程 → 安装 hello → 重启 → 验证。
  - 无固件时跳过并明示；不进入 GitHub CI（固件不能公开）。
- [x] P6.3 调试器联调（改用 lldb 的 gdb-remote：`scripts/emu/lldb.sh`，已在 043 设备上验证连接；Supervisor 不上报模块加载基址，加载地址需手工提供）
  - gdb.sock 仅在开启 shell 选项时存在；此时 QEMU 以 `-S` 暂停启动，注入器在启动期占用后才 detach。
  - Supervisor 查询需报告各模块的加载基址，脚本据此执行 `add-symbol-file`。
- [x] P6.4 向 Mirage 提需求（清单写入 docs/mirage-emulator.md；本地桥接改动未推送上游）
  - `miragectl hmp`（库里已有 `hmp()`）、`--json` 输出、快照、`log` 子命令、控制台多路复用。
  - AstroBoxBridge 增加 Canopus 方法（已做：`canopusModules`；另加按地址添加 band、等待进行中的 inquiry，已在本地 Mirage 仓库提交，未推送）。
- [x] P6.5 P5 端到端（AstroBoxBridge 路径：`scripts/emu/smoke.py` 的 phone_flow，需要在模拟器上点击一次配对确认，脚本自动点；AstroBox-NG 的 emu bridge 路径未跑）。原两条路径：
  - AstroBoxBridge（进程内 JSON-RPC）；
  - AstroBox-NG 的 emu bridge（HCI over TCP 连 qemu-miwear 的 BT 控制器；端口按设备配置，不是固定的 5566）。
  - QEMU 可能只接受单个客户端，两条路径不能同时连接（待验证）。
- [x] P6.6 文档（本文与 ADR-CAN-005 明确：模拟器结果只算 host/emulator 级证据，不得写成 device verified）：模拟器结果只算 host/emulator 级证据，不得写成 device verified。

### P7 生产就绪整改（2026-10-20）
- [x] P7.1 Supervisor 常驻内存：`s_kv` 由 545 KiB 静态表改为按模块文件逐条流式读写（`.bss` 627–647 KB → 69–90 KB）；`canopus verify` 报告常驻大小并接受 `--max-resident`，各家族 `supervisor_max_resident` 成为构建门槛；KV 键值超长改为拒绝而不是截断。Band 11 Unicorn 引导测试不再 NOMEM。
- [x] P7.2 模块崩溃循环防护：开机启动前把启动计数写入注册表，稳定 60 s 后清零；连续两次未稳定则退回更老的一代或禁用（`CANOPUS_SUP_ERR_UNSTABLE`）。全局 safe mode 的自动升级、重启保持与 `EXIT_SAFE_MODE` 退出路径随 K19 补齐（docs/security-model.md §7）。
- [x] P7.3 portable 按 loader 能力打包：规划器只把 portable 模块展开到向构造函数传服务上下文的 loader（Band 11），Band 10 Pro 须绑定 target。
- [x] P7.4 模块启动器入口（ADR-CAN-011，CAN-P0-007 状态不变）：`ctx->launcher`，两步注册，无注销；host 测试与 Band 11 固件仿真。
- [x] P7.5 安装器 Auto-run（P4.0）——已由固化启动取代并删除（K20）。
- [x] P7.6 后台 timer：time 组版本 2，复用 L2 owner timer；仅 `.043`/`.155`；线程与工作队列仍 FORBIDDEN。
- [x] P7.7 文档与状态表对齐（限额、链接、README 的能力状态说法、§15 的 CAN-P0-004/006、外部模块迁移说明）。
- [ ] P7.8 仍需真机：上述各项以及 G0–G13 都没有归档过一次设备运行；`tests/hardware/gates.md` 列出新增的待验证项。
- [ ] P7.9 Supervisor 文件预算（2026-10-23，UI ABI 2.0 + Lyra 符号合并收尾，5 个 active target 全部构建 + ELF 校验通过，0 未定义符号）：
  `supervisor_max_size`（Band 10 Pro）由 114,688 提到 131,072，与 Band 11 对齐——这是构建期预算，不是实测固件上限；
  Band 11 与 Band 10 Pro 都裁掉无用符号和 C unwind 表，保留重定位、导出和构造函数（`--strip-all` 会移除 ELF loader 需要的符号表）。
  常驻上限（真正的 RAM 槽约束）未放宽；再加功能需重新评估预算。合并构建的精确字节/常驻见 `targets/*/evidence` 与构建日志。
  P5.2 的 phone flow 超时在本机 Mirage 重跑时没有复现。
- [x] P7.10 `.179` 适配与全局 safe mode（K19）。
- [x] P7.11 Native UI incremental snapshot application (2026-10-22): compare committed content before native setters, preserve unchanged placement, refresh event bindings, repair dirty switches and retry after partial apply failures. `scripts/tests/test_ui_backend.py` runs both real backend variants with counted firmware substitutes; identical sample commits decrease from 14/15 native mutations to 0/0 (Band 10/Band 11). Host ASan/UBSan, cross-thumb and all five target Supervisor builds/verifiers pass. No new device validation or capability promotion; see ADR-CAN-010.
- [x] P7.12 UI ABI 2.0 与模块生命周期通知 (2026-10-23, host implemented + review fixes): 公共 ABI 直接演进到 2.0（`ctx->ui_ext` 并入 `ctx->ui` 并删除，旧 major descriptor 不再发布）；
  显式能力协商（不支持显式 `ERR_UNSUPPORTED`，不静默忽略；样式属性 ID 按 target 审计，未审计属性整包拒绝，`FLAG_CLIP` 缺裁剪 ID 同样显式拒绝）；真实布局/样式/视觉（含受控 RGB、圆角、裁剪、位移）；
  完整输入事件（按下/长按/移动/松开/取消、视口坐标与时间、捕获与滚动锁定、跨虚拟窗口迁移）；
  滚动查询/定位/通知与 `content_height` 虚拟列表（数据留在模块模型，树只放可见窗口，可超 32 项）；
  Toast（最近替换、超时、同内容不重启、隐藏→显示重显、到期不重播、关页/卸载不悬空）；导航标题随路由更新且过期标题事件被丢弃；
  模块生命周期 `"canopus/module/state"` + `query_module`（首订阅语义）与 `ctx->ui->post` 最小 UI 调度（仅前台心跳排空）。
  证据：全量 CI 9 gate（含双后端 ASan/UBSan、Rust 215 项）、`canopus target generate --all --check`、五个 active target Supervisor 构建与 ELF 校验（0 未定义符号）。
  隔离 Mirage `.043` / `.155` 完整 Lab 通过（窗口分别到 37 / 44，选中第 40 / 46 项并返回恢复）；`.179` 支持路径通过（虚拟滚动/指针/捕获明确 gated）。`.043` 已修正 UI 入口的加载地址/运行地址误用、图片 ABI 与绝对滚动入口，驱动滑动起点改到真实列表视口内。
  构建哈希、截图、事件记录、CI 日志及修复前失败现场归档于 [`evidence/native-ui-abi2-2026-10-23`](evidence/native-ui-abi2-2026-10-23/README.md)。
  **host/emulator 证据，未做真机验证，能力状态未提升**。详见 ADR-CAN-013。
  2026-10-23 复审（1 high + 13 medium，R1–R14）已全部修复并纳入正式回归（隐藏 Toast 空指针、显式样式残留、标签几何、stretch 后代、section 标题、checkbox padding、stock row padding、Toast 重显、捕获失效 CANCEL、失败提交原子性、捕获转移 RELEASE、post 调度、SCAN/publish 漏通知），另修复活动捕获相同快照零写入与 `FLAG_CLIP` 静默忽略两项自查发现。
  重新验收：CI 9 gate 全绿、五目标构建、隔离 Mirage review7（`.155` 无 gate，`.043` gate `style-card`，`.179` gate 三项输入 + `style-card`；Toast 四态截图像素 872/0/872/0 量测，隐藏为提前消失）。证据归档 [`evidence/native-ui-abi2-review-fixes-2026-10-23`](evidence/native-ui-abi2-review-fixes-2026-10-23/README.md)。
  2026-10-23 二次复审发现整改引入两个 high 回归（N1 多 Toast 批次 staged 所有权错乱、N2 CANCEL 销毁父节点后二次删除 staged 候选），已修复并补正式回归（`test_two_toasts_removal_keeps_shared_owner`、`test_cancel_destroying_root_reclaims_staged_candidates`，双 family + ASan/UBSan，红现场归档）；
  所有权/清理审计按节点类型/所有权收口（F1/F3/F4/F7 修复含 F3 独立红绿，F2/F5/F6 报告项）。Toast 语义补多节点规则（最近可见 Toast 驱动共享 owner，尾随隐藏占位不压掉可见 Toast）。
  重新验收：CI 9 gate 全绿（Rust 215 项）、五目标构建（`.155` 文件余量 648 B）、隔离 Mirage review7 复跑含成对 Toast 移除形态；捕获取消销毁由 host ASan/UBSan 覆盖（模拟器无法可靠驱动该时序）。证据归档 [`evidence/native-ui-n1n2-fixes-2026-10-23`](evidence/native-ui-n1n2-fixes-2026-10-23/README.md)。
  三次复审发现 N1 修复引入 M1（medium，别名 Toast 样式写共享 owner）与 L1（low，尾随隐藏 Toast 压掉可见 Toast 契约缺口），已修复并补正式回归（`test_alias_toast_style_stays_off_shared_owner`、`test_mixed_toast_visibility_pair`，双 family + ASan/UBSan，含复审 repro 红转绿）；
  Toast 驱动规则收口为"最近可见 Toast 驱动、无可见时隐藏"（`host_feedback_toast_driver`），别名写入面扫描确认共享 owner 的唯一通用写入点（样式）已按驱动归属收口。CI 全绿、五目标构建、隔离 Mirage review7 复跑无回归。证据归档 [`evidence/native-ui-m1l1-fixes-2026-10-23`](evidence/native-ui-m1l1-fixes-2026-10-23/README.md)。
  四次复审发现 M2（medium，驱动切换时共享 owner 样式不重绘——自 diff 门控漏写新驱动、残留旧驱动），已修复（驱动身份变化走替换路径全量重绘）并补正式回归 `test_driver_switch_repaints_shared_owner`（双方向 + keep-alive，双 family + ASan/UBSan，复审 repro 红转绿）；
  Toast 共享 owner 写入面矩阵与"自 diff 写共享对象"审计定稿（无残留缺口，见 evidence 目录 `write-surface-matrix.md`）。CI 全绿、五目标构建、隔离 Mirage review7 复跑无回归。证据归档 [`evidence/native-ui-m2-fixes-2026-10-23`](evidence/native-ui-m2-fixes-2026-10-23/README.md)。
  四次复审 P1/P2（medium，Toast owner 样式重绘三处与文本重绘脱钩——隐藏期 hide-driver 自 diff 样式写残留、隐藏 fresh 节点替换 owner 带入隐藏样式）按根因修复：不变量「owner 文本被重写 ⇒ 样式同步重绘」（`host_feedback_toast` 返回重绘标志强制 `style_changed`）+ 隐藏节点不写不替换（VISIBLE 门槛）；T1（low，双 NAVIGATION_HEADER 争夺共享 page_title/binding）改为 first-HEADER 驱动单写者；D1 补矩阵 hidden 触发源（`set_interactive(0)`）。补正式回归 p1/p2/t1（双 family + ASan/UBSan，复审 gap-repro 双家族红转绿）；写入面矩阵定稿更新。CI 全绿、五目标构建、隔离 Mirage review7 复跑四态像素同口径无回归。证据归档 [`evidence/native-ui-p1p2-fixes-2026-10-23`](evidence/native-ui-p1p2-fixes-2026-10-23/README.md)。
  P7.12 收尾时字体仍仅 24/28、图片仅 NATIVE；图片适配与任意字号现由 P7.13 补齐 `.043` / `.155`，其它目标仍 gated。样式属性 ID 仅 `.155` 全量、`.179` 三项输入能力 gated。
- [x] P7.13 Native image fit、arbitrary `font_px` 与防御性代码消融（2026-10-23，host/static 证据）：先测五目标基线（`.155` 文件 130,696/131,072，常驻 182,014/184,320）；仅 UI 后端改用 `-Oz` 回收空间。
  - **Image fit**：`.043` / `.155` 接入 `lvx_image_set_scale` 独立 Q8 X/Y 与原图尺寸查询，实现 CONTAIN / COVER / STRETCH，COVER 用原生父容器裁剪；缺少独立证据的 `.036/.139/.179` 返回 UNSUPPORTED、能力位关闭。`.043` 缩放/查询记录纠正为 PSRAM 运行地址。
  - **Arbitrary `font_px`（`CANOPUS_UI_CAP_FONT` bit 17）**：`.043` / `.155` 私有绑定 `native_font_create(manager, &key)`（`0x0c85f6cd` / `0x0c494381`）、`native_font_release(manager, font)`（`0x0c85f8fd` / `0x0c917385`）、`native_font_wait(vg_unit)`（`0x0c15c061` / `0x0c399941`）及 `*(uikit_root + 28)`，所有新符号保持 `restricted` / `STATIC_RECOVERED` / `PENDING` / `not_probed`，无模块公开 veneer。按字号缓存 managed wrapper，引用归属独立于 view 的每原生标签 `DELETE`（event 36，userdata `event + 12`）堆记录与提交前事务预检引用；仅在检查无 display 渲染中（`byte 58 & 2`）、无 layer 待执行任务（`layer + 100`）、无活动 draw unit（`unit + 32`）并执行 `native_font_wait` 排空 CPU 待释放字形引用后释放零引用缓存项，覆盖 show/hide/discard、对话框保留/释放、页面重载、view 重置、先删根/先 detach 与重入 CANCEL。
  - **经验性消融与构建预算**：8 变体隔离消融实验（`EVID-UI-DEFENSIVE-ABLATION-001`，[ablation](native-ui-defensive-ablation.md)）证明正图片宽高检查为 load-bearing（保留），移除 4 处已由 SDK/控制流保证的重复检查与 3 处新字体集成冗余检查后，`.155` 由消融前 131,440 / 182,062 B 降至 **131,012 / 181,642 B**（净省 428 / 420 B，较图片版 +1,276 / +996 B；`.043` **124,740 / 162,546 B**）。双家族 `supervisor_max_size` 统一由 `131072` 调整为 `133120`（+2,048 B 构建余量；**显式声明为构建期阈值而非已证硬件 loader 上限，真机加载仍未验证 / `not_probed`**），`supervisor_max_resident`（`163840` / `184320`）未放宽。
  - **验证**：五目标构建/ELF 均 PASS、0 undefined；`generate --all --check`、18 项 `generated_stability`、双后端带/不带绑定四变体 ASan/UBSan 回归、显式 `gate_2` 与完整 CI 九 gate 全绿。详见 [verification](native-ui-image-font-verification.md)、[font lifecycle ABI](native-ui-font-lifecycle-abi.md)、ADR-CAN-010；不提升任何能力状态。

## 6. 风险

- R1 重构期间地址生成物意外漂移 → P0.6 基线 + P1.9 对比。
- R2 协议升版本使旧设备端不可用，用户须重装模块 → K1、K11 已接受；新 Supervisor 对旧 registry/inbox
  安全忽略（P2.9 加测试），升级说明写入 P3.8。
- R3 O1 通道需要固件证据，可能拖慢 P5 → P5.0 先行；P4 不依赖 P5，用户可以先获得一步更新。
- R4 Mirage 无快照、控制台只能有一个使用者 → CLI 独占运行；P6.4 提需求。
- R5 移除 P65 后历史事故文档失去上下文 → 在 `adr/` 保留一条简短记录。
- R6 移除签名（K9）后不再有来源认证：Band 11 上任何表盘 Lua 都能经 `/canopus/install` 安装内核代码
  （Band 10 Pro 的 Lua 本来就有 `os.execute`，签名在那里也防不住）。可选的非加密缓解：首次安装或升级时在手环 Manager 上弹确认。
- R7 Mirage 目前只有 043 能启动到 UI，Band 11 只到开机向导，快应用环境未验证 → O1-a 无法在模拟器上做端到端测试，Band 11 联调须用真机。
- R8 服务表会增加 Supervisor 体积，而 Band 10 Pro 的上限是 73728 字节 → 移除 monocypher 腾出空间；P2.9 设体积门槛。
- R9 商店按型号选择下载项，target-private 模块可能装到错误固件上 → P5.7 做固件比对；portable 模块缩小这一风险面。

## 7. 变更日志

| 日期 | 变更 |
|---|---|
| 2026-10-14 | 创建本文档；记录决策 K1–K6 与待定项 O1–O4 |
| 2026-10-14 | 第 2 版：事实核查（CI 基线为红、协议与回执、模块身份、U3 等）；K4 更正为 `core`；新增 K7–K10、O5–O6、R6–R9；P4 不再依赖 P5；任务重新编号 |
| 2026-10-14 | O2 → K11（不迁移，要求重装）；O6 → K12（保留 .139 提升，改写过时测试） |
| 2026-10-14 | P0.5 完成：ci.sh keep-going 模式、036 生成物重新生成、K12 测试改写、Band 11 测试对无种子克隆的处理 |
| 2026-10-14 | P1.1 完成：P65 全部移除；P0.6/P0.7 完成 |
| 2026-10-14 | P1.8 完成：Band 9 冻结 |
| 2026-10-14 | P2.7 完成：移除全部签名/验签 |
| 2026-10-14 | P4.1–P4.5 完成（A/B 双代更新、回滚、加载失败回退；P4.0 开机自启调查未开始） |
| 2026-10-14 | P1.3/P1.4 完成，P1.2 部分完成（family 层）；基线 hashes 刷新（committed==generator；差异仅为 P65/Band 9 删除与生成物头注释） |
| 2026-10-14 | P2.3 完成：Rust 单一 workspace |
| 2026-10-14 | P3.1/P3.2/P3.3/P3.5 完成（C 模块工程：new/build/package；P3.4 安装器表盘仍由旧 Python 脚本生成） |
| 2026-10-14 | K13：O1/O5 结案（.rpk 快应用容器）；P5.0–P5.2、P5.7 完成，P5.3/P5.5 部分，P5.4/P5.6 因私有仓库阻塞；P6.5 端到端用真实 AstroBox 核心在 Mirage 043 上跑通 |
| 2026-10-14 | P5.3–P5.6 完成：AstroBox-NG 的 core/app/web 在 ~/Documents/GitHub/ab/AstroBox-NG 改动（本地提交，未推送） |
| 2026-10-14 | P2.1 TIME 服务组完成（mem 受 restricted 策略限制未开放） |
| 2026-10-14 | P3.4 完成（package --installer；安装器目标列表动态化） |
| 2026-10-14 | Band 9 / 9 Pro target 包删除（地址不可信，重来须走自定义 loader）；仅 Band 10 Pro 与 Band 11 为受支持设备 |
| 2026-10-14 | P2.2/P2.4/P2.6/P2.8 判断结案（新增 C/Rust ABI 常量一致性测试） |
| 2026-10-14 | P1.2 结案：Band 11 指纹预生成，安装器构建不再需要固件镜像 |
| 2026-10-14 | P6.1 `canopus emu smoke`；P6.4 需求清单；P6.3 因无 gdb 未验证 |
| 2026-10-14 | Band 11 固件仿真脚本全部通过（unicorn）；Band 11 SCAN 的 opendir/readdir/closedir 经 043→Band 11 集成匹配核对为错误候选（置信 0.55–0.58，反编译不符），需专门 RE，未做；P4.0/P5.8 记为受限 |
| 2026-10-14 | `canopus build` 不支持 Rust 为判断结案 |
| 2026-10-14 | Band 11 139/155/177 的 opendir/readdir/closedir 由 scandir 调用关系与 104/0xC01/98 常量恢复（177 为 155 函数体的唯一前缀匹配），Supervisor 的 SCAN 钩子在 Band 11 上编译启用（STATIC_RECOVERED，未在运行的 Band 11 上验证） |
| 2026-10-14 | P6.3：scripts/emu/lldb.sh（lldb 经 gdb stub 连 Mirage 设备，已验证） |
| 2026-10-14 | P2.1 MEM 服务组（按模块预算与自动回收） |
| 2026-10-14 | P2.1 NOTIFY 服务组（模块 id 作标题、每次加载 4 条配额）；ADR-CAN-006 记录 ui/bt/interconnect 不实现的理由 |
| 2026-10-14 | Band 11 在 Mirage 上手机投递验证：.rpk 装入后落在 /data/quickapp/app/<pkg>/，Supervisor 的 SCAN 根目录按 Band 11 修正；安装器表盘因缺 facetory 打包工具仍无法装入模拟器 |
| 2026-10-14 | P5.8 设计结案：Supervisor 不常驻，升级=替换安装器表盘并重启，复用现有表盘更新提醒 |
\n| 2026-10-14 | Band 11 模拟器端到端跑通（155）：生产安装器 → Supervisor 加载 → 手机投递 .rpk → SCAN 导入；需 qemu-miwear 与 Facetory 两个补丁（scripts/emu/band11/） |\n
| 2026-10-14 | Band 11 端到端在 177 上同样通过；打补丁后的 qemu 下 043 冒烟 22 项仍全过 |
| 2026-10-14 | `canopus build` 支持 Rust 目标模块；qemu-miwear 补丁改为直接提交（3e05ca7）并推送，补丁文件删除 |
| 2026-10-14 | P2.4 函数集一致（api_parity 测试）；P2.8 两套 Manager UI 后端合并为一个源文件（产物验证一致） |
| 2026-10-14 | 评估 Canopus 专用 L2CAP 通道：手环侧入站 PSM 监听无现成 API，手机侧经典 L2CAP 在 Android/iOS 不可用，建议 RFCOMM 专用 channel（docs/canopus-channel-feasibility.md，未实测） |
| 2026-10-15 | Hook 引擎架构根因重构与 Cortex-M33 边界防御；废除 Flash 槽位硬编码，提供 register_slot_map 运行时映射（Corona 模式）；修复 realloc 溢出、INT32_MIN 格式化 UB 与 KV 存储 VFS 持久化；all-services-test 达成 93 项服务方法 100% 覆盖并于 Mirage 模拟器（043 与 155）全绿通过（18/18 PASS）；新增 CI 强制覆盖门禁 services_coverage |

| 2026-10-17 | K15：移除 RPK 包装、快应用目录与列表依赖；新增原生 CPC2 分块暂存、精确 target 查询与 AstroBox 独立安装命令。全量 CI 9 个 gates 全绿；新增 ARM L2 shim 的 SRAM 替换、明文拒绝、解密失败、队列分发和 session 边界仿真通过（仅 host/emulator 级证据）。.043/.155 实际 Mirage 蓝牙联调失败与持续调度入口阻塞见 P5.2；不能宣称端到端完成。 |
| 2026-10-18 | 原生 UI 宿主后端与模块页面（ADR-CAN-010）：`canopus_ui_host` 为 Band 10 Pro / Band 11 通用后端，Manager 页面接入，`ctx->ui_ext` 与 Manager 承载的模块页面，示例 `ui-demo`；host 测试与 Band 11 固件仿真通过，Mirage `.043`/`.155` 端到端（启动器 > Manager > 模块页面）通过，仅 emulator 证据，未做真机验证 |
| 2026-10-18 | CanopusConnect（ADR-CAN-009）：`ctx->connect` 与 CPC2 opcode 19/20/21，模块 ↔ AstroBox 插件按模块 id 互发文本/二进制消息；管理器状态不受影响，仅 .043/.155；host 测试与 5 个 Supervisor 交叉构建通过，AstroBox 侧见各仓库单元测试，未做 Mirage/真机验证 |
| 2026-10-19 | UI 宿主对齐（62aa5fd）：Band 10 Pro 与 Band 11 同一渲染路径（文本标签、图片、进度条、隐藏、锁滚动），Band 11 对话框改用固件 msgbox、标题返回转给模块 `on_back`，滑块移除；新增静态证据 EVID-UI-1036/1043-HOST-001、EVID-UI-4139/4155-MSGBOX-001（restricted/PENDING）；Mirage `.043`/`.155` 通过。维护者反馈已在 `.043` 与 `.155` 真机上验证 |
| 2026-10-19 | UI 宿主真机验证与批准：`.043`（28 个）、`.155`（30 个）UI 宿主调用的固件入口提升为 `DEVICE_PROVEN` / `APPROVED`（证据 EVID-UI-1043/4155-DEVICE-001，清单钉在 generated_stability.rs），策略仍 restricted（不对模块开放）；能力状态 device verified / production approved（仅 UI 宿主，证据为维护者反馈，无设备日志）；Band 11 原生地址头生成器接受 `DEVICE_PROVEN` |
| 2026-10-19 | CLI 体验修整（docs/cli.md）：`--targets-dir` 缺省为仓库内 targets（`target list/info/generate`、`verify`、`build-plan`、`re probe` 不再依赖当前目录），装到别处的二进制可找到仓库；`new` 先校验再创建、target 可写版本后缀、`--ui` 模板、`.gitignore`、Rust 工程相对 SDK 路径；`package` 拒绝过期 ELF、`--build`；新增 `clean`、`doctor`、`-v`；缺工具与缺清单的报错可操作；管道关闭不再 panic；CLI 集成测试 3 项 |
| 2026-10-18 | 服务组去占位：亮度/屏幕状态/唤醒锁/振动/电量/属性/蓝牙状态改为 exact-target 固件入口（.036/.043/.139/.155 STATIC_RECOVERED，.177 无镜像保持空钩子），文件服务放开任意绝对路径（仅保护 Canopus 自己的状态）、每模块私有目录、mkdir/APPEND/errno，L2 接收派发，通知标题，系统事件广播；host 测试与 5 个 Supervisor 交叉构建通过，未做设备或模拟器验证 |
| 2026-10-19 | CI 恢复与生产级配额全线扩容：修复 `.177` 缺镜像与 `.155` 特批符号比对导致的 CI 阻塞，全量 CI 9 个 gates 恢复全绿；服务层与 SDK 配额全线拉满：KV 单值由 64 扩至 512 字节（支持 JSON 与长 Token）、键值对由 32 扩至 64，循环日志由 8 扩至 64 条、单条长度由 80 扩至 128 字节，定时器由 16 扩至 64 个，总线订阅由 32 扩至 64 个，文件路径由 128 扩至 256 字节、打开句柄由 16 扩至 32 个，堆内存基线预算由 128 扩至 256 KiB、分配块由 64 扩至 128，通知标题由 40 扩至 64 字节、正文由 120 扩至 256 字节、配额由 8 扩至 16；UI 宿主放置节点由 64 扩至 256、行数由 32 扩至 64（升级 uint64_t 行掩码）、标签由 16 扩至 64、图片由 4 扩至 16、进度条由 8 扩至 32、视图由 8 扩至 16；Supervisor 尺寸仍在各 target 上限内（.043 106768/114688, .155 117880/131072），host 测试（含 ASan/UBSan）与长值持久化测试全部通过 |
| 2026-10-20 | K17：移除 Band 11 `4.100.177`（target 包、Rust 绑定、脚本与测试、安装器 profile、基线哈希）；此前各行对 177 的记述仅为历史。179 适配尚未开始 |
| 2026-10-20 | K18：生产就绪整改（P7.1–P7.7）：KV 流式化与常驻体积门槛、模块崩溃循环防护、portable 按 loader 打包、模块启动器入口（ADR-CAN-011）、安装器 Auto-run、后台 timer、商店清单 `canopus` 兼容元数据（客户端）、文档对齐。全量 CI 全绿；仅 host/emulator 证据，未做真机验证 |
| 2026-10-20 | K19：适配 Band 11 `4.100.179`（新增 `tools/fw-match/port_pack.py`、`port_review.py`、`map_data.py`；270 条记录全部 restricted/PENDING，无批准继承；Unicorn 里真实 179 镜像跑通 stage1/stage2/Supervisor、Manager 与通知路径；Mirage 里真实 179 固件经生产安装器 Run → 加密 L2 安装 → SCAN 与 services 26/27 通过）；全局 safe mode 与退出路径（host 测试）；CreatorConsole 与 AstroBox-NG 的 `canopus` 元数据发布/安装校验；043 冒烟完整通过。仅 host/emulator 证据，未做真机验证 |
| 2026-10-21 | K20：固化启动（`.043`，Mirage 冷启动通过，无真机）、可见的故障恢复、退出安全模式修复、AstroBox 固化入口与确认（已推送）；删除页面 Auto-run；`.043` Supervisor 109,180 / 114,688。仅 host/emulator 证据 |
