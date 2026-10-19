# Canopus 文档导航与技术规范

**重构进度权威来源：[`refactor-plan.md`](refactor-plan.md)**  
能力状态依据：[`progress.md`](progress.md) 与 [`native-manager-ui-plan.md`](native-manager-ui-plan.md) §15。

---

## 核心技术规范与开发指引

| 开发主题 | 权威文档 | 核心内容摘要 |
|---|---|---|
| **服务对象与 API 规范** | **[`canopus-services-specification.md`](canopus-services-specification.md)** | **微内核服务对象架构（FS 完备文件系统、128K 动态堆内存、通用动态 Hook 引擎、时钟/定时、结构化日志、通知、硬件状态与 L2 蓝牙）** |
| **Rust SDK 架构与指引** | **[`../sdk/rust/README.md`](../sdk/rust/README.md)** | **Rust 核心 Crate 分工、设备型号对应表、Portable vs Target 模块开发流** |
| **命令行工具** | **[`cli.md`](cli.md)** | **`canopus` 的 new / build / package / clean / doctor、target 短名与仓库定位、退出码** |
| **仓库协作规范** | **[`../AGENTS.md`](../AGENTS.md)** | 目录拓扑、不可触碰底线、证据链门槛、常用命令与重构红线 |
| **新增与维护 Target** | [`target-authoring.md`](target-authoring.md) | Target 目录结构、符号表 JSON 规范、生成器使用指引 |
| **安装器与更新 IO** | [`module-installer-io.md`](module-installer-io.md) | A/B 世代原子步进切换、防回滚语义、设备持久化 |
| **安全与无签名模型** | [`security-model.md`](security-model.md) | 移除签名后的安全性校验（SHA-256 完整性、Target 绑定、单调版本） |
| **外部模块迁移指南** | [`external-module-migration.md`](external-module-migration.md) | CMI2 无签名收据、CPC2 协议升级、Portable 迁移 |
| **模拟器端到端验证** | [`mirage-emulator.md`](mirage-emulator.md) | Mirage + QEMU 自动化端到端冒烟与配对调试 |
| **重构基线哈希表** | [`baseline/hashes.txt`](baseline/hashes.txt) | 产物 SHA-256 基线与生成一致性守卫 |

---

## 架构决策记录 (Architecture Decision Records)

- [`ADR-CAN-003: 模块身份、版本与包格式`](../adr/ADR-CAN-003-module-identity-version-package.md)
- [`ADR-CAN-004: 设备通信协议 CPC2 收敛`](../adr/ADR-CAN-004-device-protocol.md)
- [`ADR-CAN-005: 手机端管理与安装通道边界`](../adr/ADR-CAN-005-phone-delivery-and-management.md)
- [`ADR-CAN-006: Supervisor 服务表微内核设计`](../adr/ADR-CAN-006-supervisor-service-table.md)
- [`ADR-CAN-007: 移除 P65 Target 事故记录`](../adr/ADR-CAN-007-p65-removed.md)
- [`ADR-CAN-008: WearPacket Layer 2 通道 (0x43) 架构`](../adr/ADR-CAN-008-wearpacket-l2-channel.md)
- [`ADR-CAN-009: CanopusConnect 模块与 AstroBox 插件消息通道`](../adr/ADR-CAN-009-canopus-connect.md)
- [`ADR-CAN-010: 原生 UI 宿主后端与模块页面`](../adr/ADR-CAN-010-ui-host.md)

---

## 历史归档（仅供只读参考）

归档于 `docs/archive/`：
- [`architecture.md`](archive/architecture.md)：项目初期全景长文档（历史版本）。
- [`function-signatures.md`](archive/function-signatures.md)：早期符号签名探索草稿。
- [`native-manager-ui-plan.md`](native-manager-ui-plan.md)：早期 Manager UI 改造计划。
- [`firmware-symbol-porting-plan.md`](firmware-symbol-porting-plan.md)：早期移植计划草稿。
