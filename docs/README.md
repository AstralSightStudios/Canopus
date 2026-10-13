# Canopus 文档入口

**进度唯一来源：[`refactor-plan.md`](refactor-plan.md)**（任务状态、决策 K1–K12、待定项、风险、变更日志）。
能力状态（host implemented / static recovered / device verified / production approved）仍以
[`progress.md`](progress.md) 与 [`native-manager-ui-plan.md`](native-manager-ui-plan.md) §15 为准，重构不得提升任何能力状态。

## 先读这些

| 想做的事 | 文档 |
|---|---|
| 仓库协作规则、常用命令 | [`../AGENTS.md`](../AGENTS.md) |
| 新增/维护 target | [`target-authoring.md`](target-authoring.md)、[`target-private-codegen.md`](target-private-codegen.md)、[`symbol-evidence.md`](symbol-evidence.md) |
| 写一个模块（`canopus new/build/package`） | `modules/examples/hello-portable`、[`../adr/ADR-CAN-003-module-identity-version-package.md`](../adr/ADR-CAN-003-module-identity-version-package.md) |
| 设备协议与服务表 | [`../adr/ADR-CAN-004-device-protocol.md`](../adr/ADR-CAN-004-device-protocol.md)、[`../adr/ADR-CAN-006-supervisor-service-table.md`](../adr/ADR-CAN-006-supervisor-service-table.md) |
| 安装器表盘、更新与回滚 | [`module-installer-io.md`](module-installer-io.md)、[`module-lifecycle.md`](module-lifecycle.md) |
| 安全模型（无签名） | [`security-model.md`](security-model.md) |
| 重构基线哈希 | [`baseline/hashes.txt`](baseline/hashes.txt)（`scripts/baseline-hashes.sh`） |

## 历史文档（只读参考，可能与现状不符）

[`architecture.md`](architecture.md)（原始设计）、[`native-manager-ui-plan.md`](native-manager-ui-plan.md)、
[`firmware-symbol-porting-plan.md`](firmware-symbol-porting-plan.md)、[`function-signatures.md`](function-signatures.md)、
[`band11-bluetooth-139.md`](band11-bluetooth-139.md)。其中签名、CPC1/CPS1、P65、Band 9 的内容已被重构取代。
