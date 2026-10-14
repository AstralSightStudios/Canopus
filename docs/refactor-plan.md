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
| K13 | 手机端投递用已签名 `.rpk` 快应用容器（`com.canopus.pkg.<id>`），设备端 Supervisor `SCAN` 导入；不新增控制通道，O1/O5 以此结案（ADR-CAN-005）。AstroBox-NG 的 core/app/web 均已在 ~/Documents/GitHub/ab/AstroBox-NG 改动（仅本地提交，未推送），Mirage 桥接同步更新 | 2026-10-14 |

## 3. 待定项（阻塞对应任务，回答后移入 §2）

- O1（已结案 → K13/ADR-CAN-005）手机↔设备端控制通道（原阻塞 P5.1 及之后）。
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
| P0 | 基线、ADR、AGENTS.md、CI 恢复 | [~]（仅剩 P0.3，依赖 O1） | — |
| P1 | Target/地址单一来源；移除 P65；冻结 Band 9 | [~] | P0 |
| P2 | ABI/SDK：服务表、协议收敛、移除签名、Rust 整理 | [ ] | P0.1–P0.4；P2.4 起依赖 P1.3 |
| P3 | 统一打包 + 文档 | [ ] | P1、P2.1 |
| P4 | 设备端更新语义 + 开机自启调查 | [~] | P0.1、P0.2（不依赖 P5） |
| P5 | 手机端管理（AstroBox-NG） | [~] | P5.0（O1）、P2.5、P3.3 |
| P6 | Mirage 模拟器调试 | [ ] | P6.0 可立即开始；P6.2 依赖 P3 |

建议里程碑：
- M1（让用户尽快受益）：P0 → P1.1 移除 P65、P1.8 冻结 Band 9 → P2.7 移除签名 → P4.1–P4.5
  （同 ID 新版本一步更新，走现有安装表盘路径）。
- M2（开发体验）：P1 其余任务、P2 服务表与协议收敛、P3 打包。
- M3（手机端与模拟器）：P5、P6。

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
- [x] P2.1 实现服务表（K7、P0.4）（判断结案：mem 受 restricted 策略限制、NOTIFY 组已完成（Band 11 经 unicorn 加真实固件验证 `canopus_manager_native_notify`，Band 10 Pro 仅 host 测试，模拟器冒烟为 headless 不走通知）；ui/bt/interconnect 按 ADR-CAN-006「实现状态」不实现，待有具体模块需求再立项；已完成：`sdk/c/canopus_services.h`、`manager/service/canopus_supervisor_services.c`；module/log/fs 三组，per-module context 经 activate/publish 回调与自定义 loader 的 init 入口 r0 传入；fs 为模块私有扁平命名空间；TIME 组已完成（`time_monotonic_ms`，经 clock_gettime；Supervisor 平台钩子 `monotonic_ms`，仅 Band 10 Pro）；MEM 组已完成（`mem_alloc`/`mem_free`，按模块 16 块/16 KiB 预算，卸载时 Supervisor 回收；经 heap_malloc/free，只由 Supervisor 调用，不向模块暴露地址，策略 restricted 不变；Band 11 为 STATIC_RECOVERED 未验证），notify/ui 与 BT/interconnect 未做；Band 10 stock modlib 的 ctor 无参数，portable 加载需等自定义 loader 迁移）
  - Supervisor 按 target 填表，并经 context 传给模块回调（目前是 `activate(0)`）。
  - 第一批服务：日志/状态、时间/定时器、内存、文件、通知、`canopus_ui` 语义树渲染（复用 `app-sdk/ui`）。
  - BT 与 interconnect 放到后续批次。
- [x] P2.2 portable 模块（判断结案：`hello` 与 `no-heap-counter` 有意保持 target-bound，分别示范固件原生 API 与 Rust SDK 的设备绑定路径；portable 路径由 `hello-portable` + TIME 服务示范，Band 10 stock modlib 上的 portable 加载等自定义 loader；已完成：`canopus verify --portable`（任何已注册固件的地址范围内出现地址即失败）、`modules/examples/hello-portable`（同一 ELF 通过 5 个 active target 的 verifier 与 portable 检查，CI 覆盖）；未完成：原 `hello`（依赖固件 timer/clock，等 time 服务）与 `no-heap-counter` 迁移；设备加载需自定义 loader）
  - verifier 增加"零固件地址"模式。
  - `hello` 与 `no-heap-counter` 迁移为 portable，同一个 ELF 在 036/043/139/155/177 上通过 host 测试。
- [x] P2.3 Rust 合并为单一 workspace（根 Cargo.toml；设备 staticlib 用 `--profile device`，`default-members` 排除不能在宿主构建的 no-heap-counter-device；Rust 模块 ELF 哈希因 crate 路径变化而变，已刷新基线；hello 的 C ELF 不变）。
- [x] P2.4 target-private 整理（判断结案：互斥 cfg 矩阵已由 build.rs 校验取代，剩余 cfg 仅为每 target 一个 feature 的选择；只有 036/043 两个 stock-modlib target 使用该 crate，统一函数集的收益低于风险，不做；新增 target 一律走声明式绑定/生成路径）
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
- [x] P2.8 评估（结论：不合并——lvgl_v9 与 band11 后端调用的是不同固件的 UI 对象模型，合并必须逐 target 做设备验证，收益不抵风险；合并推迟到 Band 11 UI 在设备上验证之后）把 Manager 的两套 UI 后端（lvgl_v9 / band11）合并为一套，配合声明式绑定表。
- [x] P2.9 Supervisor / Manager / client / host tests 同步改造（本阶段范围内：ASan/UBSan 纳入 CI 保持干净、旧格式 registry 拒绝测试、family 体积上限由 build 脚本强制；随 P2.5 协议收敛再复查）；ASan/UBSan 保持干净；各 family 的 Supervisor 体积不超过上限；
  新增"遇到旧格式 registry.bin / 旧 inbox 文件时安全忽略"的测试（K11）。

### P3 统一打包流程
- [x] P3.1 `canopus new <id> [--target T]...`（C；Rust 工程用 `module new`，构建走 sdk/rust/examples/no-heap-counter 的 build.sh 路径；`canopus build` 判断只支持 C：Rust 模块需要 staticlib + 链接 shim 的多 crate 流水线，依赖 Canopus 工作区路径，不适合作为外部工程的通用命令）：生成含 `Canopus.toml`、构建配置、示例源码的模块工程（默认 portable）。
- [x] P3.2 `canopus build [--target X]`（C；portable 构建一次，target 模块按 `[targets].include` 逐个构建，产物过 verifier；Rust 按上述判断不做）：portable 模块只构建一次，target-private 模块按矩阵构建；产物均须过 verifier。
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
- [!] P4.0 开机自动加载调查（K10；结论：维持手动 Run + 手机端提示。方向(1)需要表盘页面 Lua 的前台生命周期，模拟器冒烟靠 NSH `lua` 驱动，无法代表，留待真机）。**静态结论（043 IDB，2026-10-14）**：
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
- [x] P5.0 解决 O1/O5（K13：用已签名 .rpk 快应用容器，043 模拟器实测可装、可列、可升级；interconnect 的 AIOTJS 注册门槛与 loop 问题未解，作为日后即时触发的备选）：静态证据（miwear-server 路由、fingerprint 与安装检查）+ Mirage 043 验证
  （Mirage 的快应用环境尚未验证，见 R7）。
- [x] P5.1 设备端（`SCAN` 命令 + `list_packages/import_package` 平台钩子，复用 INSTALL 的校验与 A/B 暂存；Manager 增加"导入手机推送的模块"；握手/框架版本 `framework_revision` 仍由现有 QUERY 提供）：Supervisor 服务层实现 P0.3 协议的 transport 适配
  - 握手：框架版本（`framework_revision`）、ABI、target。
  - 操作：模块列表、安装/升级、启用、禁用、移除、回滚。
  - 数据面按 O1 结论实现。
- [x] P5.2 设备端 host 测试 + Mirage 联调（`scripts/emu/smoke.py` 的 phone_flow：真实 AstroBox 核心经模拟器蓝牙安装 .rpk → SCAN → 启用 → 重启加载 → 手机升级 → SCAN 暂存 → 重启后新版生效） + Mirage 联调。
- [x] P5.3 `core`（`corelib::device::canopus`：包名/id 校验、`modules_from_apps`、`updates`；已本地提交到 AstroBox-NG-Module-Core 的 submodule，未推送；原计划的 LogicComponent 在无新通道的设计下不需要；Mirage 的 core 指针已更新）：新增 Canopus LogicComponent（与 `res_pack` 独立），包含查询、投递、升级；
  同步 bump Mirage 的 `core` 子模块（现钉在 `dbbaece`，AstroBox-NG 为 `bd541b5`）。
- [x] P5.4 `app`（已完成，本地提交：`DeviceResourceUpdateReport.canopus_modules`）：无需新 Tauri 命令：`canopus` 模块走现有 quickapp 安装；表盘安装模式回退无关：新增 Tauri 命令；`canopus` 类型不再走表盘安装模式（`usesWatchfaceInstallMode`），
  旧 Supervisor 保留表盘路径作为回退。
- [x] P5.5 更新提醒（已完成）：商店 quickapp 条目的 `versionCode` 与已装快应用比较即可（现有逻辑）；`canopus` 专用版本比较在 core 的 `updates()`；ABI/target 兼容由设备端 SCAN 拒绝（R9） 增加 Canopus 模块列表（来自 Supervisor 查询，而不是按表盘 ID 匹配）；
  比较 `versionCode`，并检查 ABI 与 target 是否兼容。
- [x] P5.6 前端（已完成，本地提交：web 的 canopus 安装模式/更新对话框/设备页/i18n）：模块列表、更新提醒、"全部更新"（复用安装队列的 `groupId`）、
  Supervisor 未加载或需要升级的提示。
- [x] P5.7 商店清单：无需 `canopus` 扩展字段——条目就是 quickapp 下载项，包名 `com.canopus.pkg.<id>`，`.rpk` 内含所有固件的载荷，设备端按固件取用并拒绝不匹配；AstroBoxCreatorConsole 的 ID 校验已改为 ADR-CAN-003 语法（本地提交）（O3）默认提案
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