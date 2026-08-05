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
| 3 首个 target adapter | READY（host） | xiaomi-band-10-pro-3.101.030 target pack、veneer、bindings |
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
