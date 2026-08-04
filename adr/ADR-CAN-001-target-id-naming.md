### ADR-CAN-001：Target ID 采用人类可读命名

- 状态：Superseded（命名决策由 [[ADR-CAN-002]] 取代：target id 还必须携带固件版本）
- 日期：2026-08-05
- 日期：2026-08-05
- 决策者：Canopus maintainer
- 背景：架构文档早期草案将首个 target 的 `target_id` 命名为 `xiaomi-band-f701a84`，`f701a84` 截取自固件 SHA-256。该命名被截断且对人不友好，无法快速识别设备。
- 决策：
  - 首个 target 的 `target_id` 统一为 `xiaomi-band-10-pro`（对应设备：小米手环 10 Pro / Xiaomi Band 10 Pro）。
  - 精确固件身份不依赖 target_id 字符串，仍然由完整 `firmware_sha256`、`firmware_version`、`firmware_build` 字段承担（架构 §3.2 / §7.1）。
  - target_id 只承担“人类与工具可读的稳定标识符”职责；`firmware_sha256` 承担精确绑定职责。
- 替代方案：保留 `xiaomi-band-f701a84`（拒绝：不友好、截断、无法表达设备身份）；使用品牌名 `xiaomi-smart-band-10-pro`（拒绝：与 device_family 字段职责重叠）。
- 安全影响：无。身份验证仍以完整 SHA-256 和 runtime guard 为准；target_id 不参与任何安全判断。
- 生命周期影响：target pack revision 与 target_id 解耦；未来固件版本使用同一 target_id 的新 target pack revision。
- 兼容性影响：任何使用旧 target_id 的文档/示例均已在本 ADR 生效日改为新命名。
- 验证证据：全仓 `grep -r xiaomi-band-f701a84` 无命中；`docs/architecture.md` 六个引用点已更新。
- 后续任务：导入首个 target pack（CAN-TGT-001）时使用 `xiaomi-band-10-pro`。
