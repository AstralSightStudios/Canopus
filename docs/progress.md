# Canopus 实施进度

权威进度追踪位于两份文档：

- 任务进度追踪区：[`architecture.md`](architecture.md) §23。
- P0/P1/P2 强制整改状态（每个 CAN-P?-??? ID 的 OPEN / BLOCKED-EVIDENCE /
  HOST-FIXED / CLOSED-GATED / CLOSED）：[`native-manager-ui-plan.md`](native-manager-ui-plan.md) §15。

本文档是轻量索引，不与上述权威表重复。任何"预计通过"不得写成"已通过"。

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
