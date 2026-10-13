# ADR-CAN-004：设备协议

状态：已采纳（2026-10-14，refactor-plan P0.2；实现见 P2.5）

## 决定

单一线格式：CPC2（`abi/schema/transport-v2.json`，36 字节头 + payload），配统一的命令枚举（`CANOPUS_CMD_*`）。
CPC2 在不兼容改动时升 `abi_major`；旧 Supervisor 须重装（K1/K11）。

## 各 magic 的去留

| magic | 含义 | 去留 |
|---|---|---|
| CPC2 | v2 请求/响应信封 | 保留，唯一线格式 |
| CPC1 | 16 字节旧命令 | 删除（P2.5） |
| CPS1 | 384 字节旧状态 | 删除（P2.5）；状态改由 QUERY 响应承载 |
| CPRT | 内存中的 envelope（不是线协议） | 保留为内部名，与线格式无关 |
| CMI1 | 带签名的 256 字节回执 | 已删除，被 CMI2 取代（P2.7） |
| CMI2 | 无签名的 176 字节回执 | 保留 |
| CMR1 | 模块向 Supervisor 的注册帧 | 保留 |
| CQD1 / CQM1 | 设备/模块查询 payload | 并入统一的查询 payload（P2.5） |
| CAP1 | capability 描述 | 并入服务表（ADR-CAN-006） |
| CRD1 | registry 文件 | 保留；version 2（A/B 代标志位） |

命令枚举：`CANOPUS_SUP_CMD_*`（0x4351xxxx）并入 `CANOPUS_CMD_*`；已删除的 `UPDATE` 操作码保留不复用。

## 约束

新 Supervisor 遇到旧格式 registry 或旧 inbox 文件时必须安全忽略并提示重装，不得崩溃或误加载（K11；P2.9 加测试）。
