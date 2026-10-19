# 外部模块迁移指南（P3.8，2026-10-14）

适用对象：Bluetooth Audio、Lyra、Resource Hook、Data Quota Lift、ReFetch 等位于本仓库之外的模块仓库。
这些变更是有意的破坏性变更（K1/K9/K11）：**旧 Supervisor 须重装，旧 registry / 旧 inbox 文件会被拒绝**。

## 1. 去签名，换新回执（必做）

- 签名、验签、密钥、吊销全部移除；不要再保存或传递私钥，也不要再调用 `--private-key`。
- 回执从 `CMI1`（256 字节，含 Ed25519 签名）换成 **`CMI2`（176 字节，无签名）**：
  `magic "CMI2"`、`version 2`、`header_size 176`，字段到 `artifact_sha256` 为止，布局同旧回执的前 176 字节。
- 生成方式：`canopus package`（在模块工程里）会为每个 target 写出 `receipts/<target_id>.cmi`；
  脚本 `scripts/build-module-installer-receipt.py` 已删除。需要裸回执时：
  `tar -xOf dist/<id>-<ver>.canopus receipts/<target_id>.cmi > receipt.bin`。
- 校验项不变：target、固件 SHA-256、artifact 大小与 SHA-256、模块 ID、生命周期类别、版本。

## 2. inbox 路径与安装结果

- 安装器只写暂存区：`/data/canopus/inbox/<token>.cmi.next` 与 `<token>.ko.next`（不再写 `<token>.cmi/.ko`）。
- CPC2 `INSTALL` 的结果除 `COMPLETED(5)` 外，**升级会返回 `REBOOT_REQUIRED(8)`，应视为成功**，
  并提示用户"重启后生效"。同版本返回 `STAGE_DUPLICATE(-105)`，更低版本返回 `STAGE_DOWNGRADE(-109)`。
- 更新直接用同一 module_id、更高的 `versionCode` 再安装一次即可，不再需要"移除→重启→安装→启用"。

## 3. 模块身份与版本

- `module_id`：`^[a-z][a-z0-9_]{0,15}$`（ADR-CAN-003）；`org.example.xxx` 这类反向域名不再合法。
- 版本比较用 `versionCode`（u32，单调递增）；`version` 字符串仅展示。

## 4. 能否转为 portable

portable 模块不含任何固件地址，只依赖 Supervisor 服务表（ADR-CAN-006、`docs/canopus-services-specification.md`），一个 ELF 可装在所有实现所需服务组的固件上。
服务表现有 `module`、`log`、`fs`、`time`（含定时器）、`mem`、`notify`、`hook`、`system`、`kv`、`haptics`、`display`、`bus`、`connect`、`ui`/`ui_ext`、
`launcher`，以及（经 `system` 的蓝牙状态）`bt`；这些服务够用的模块可以转为 portable。
上述五个外部模块（蓝牙音频、Lyra 等）还用到服务表之外的东西——经典蓝牙 A2DP/SDP 注册、字体/资源 hook、驱动注册等固件私有入口——
所以继续作为 target 模块，用 `canopus new <id> --target <exact-target-id>` 生成工程，或把现有构建接入 `Canopus.toml` 的 `[build]`。
portable 模块只能由向构造函数传服务上下文的 loader 加载，即 Band 11（自定义 loader）；Band 10 Pro 的原厂 modlib 不传，所以
`canopus build` / `canopus package` 不会为 Band 10 Pro 生成 portable 产物，要支持它请绑定 target。

## 5. 其他

- Manager 里的"更新"按钮已删除；"回滚"只在存在上一代时显示，重启后生效。
- CPC1/CPS1 在 P2.5 之前仍可用；P2.5 会统一到 CPC2，届时再给出安装表盘的改造说明。
- Band 9 / 9 Pro 已冻结（git tag `band9-frozen`），P65 已移除。
