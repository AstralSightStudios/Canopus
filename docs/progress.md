# Canopus 实施进度

权威进度追踪位于 [`docs/architecture.md`](architecture.md) §23（任务进度追踪区）。

本文档是机器可读的轻量索引，与 architecture.md §23 保持一致。

## Phase 状态

| Phase | 状态 | 备注 |
|---|---|---|
| 0 项目边界与 schema | READY | 待独立仓库确认（BLK-001） |
| 1 Host CLI 与 target registry | BACKLOG | |
| 2 C ABI 与 portable runtime | BACKLOG | |
| 3 首个 target adapter | BACKLOG | xiaomi-band-10-pro-3.101.030 |
| 4 C module build/package | BACKLOG | |
| 5 Device supervisor MVP | BACKLOG | |
| 6 Native App 与 Launcher adapter | BACKLOG | 受 BLK-006 阻塞，依赖 IDA MCP 逆向 |
| 7-12 | BACKLOG | 见 architecture.md |

## 变更日志

| 日期 | 变更 | 证据/备注 |
|---|---|---|
| 2026-08-05 | 建立独立仓库 /Volumes/EXT0/Canopus | AGPL-3.0；CLI 采用 Rust；target id 重命名见 ADR-CAN-001 |
