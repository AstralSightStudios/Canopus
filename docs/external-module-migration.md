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
服务表现有 `module`、`log`、`fs`、`time`（含定时器）、`mem`、`notify`、`hook`、`system`、`kv`、`haptics`、`display`、`bus`、`connect`、`ui`、
`launcher`，以及（经 `system` 的蓝牙状态）`bt`；这些服务够用的模块可以转为 portable。
上述五个外部模块（蓝牙音频、Lyra 等）还用到服务表之外的东西——经典蓝牙 A2DP/SDP 注册、字体/资源 hook、驱动注册等固件私有入口——
所以继续作为 target 模块，用 `canopus new <id> --target <exact-target-id>` 生成工程，或把现有构建接入 `Canopus.toml` 的 `[build]`。
portable 模块只能由向构造函数传服务上下文的 loader 加载，即 Band 11（自定义 loader）；Band 10 Pro 的原厂 modlib 不传，所以
`canopus build` / `canopus package` 不会为 Band 10 Pro 生成 portable 产物，要支持它请绑定 target。

## 5. UI 与生命周期 ABI 2.0（2026-10-22）

- 模块与 UI 的公共 ABI 都是 2.0。重新用当前 SDK 构建并打包，`Canopus.toml` 的 `canopus_abi` 改为 `"2"`；旧 major descriptor 会被拒绝，旧 Supervisor 须重装。
- `ctx->ui_ext` 已删除：`node_set_style`、`node_set_layout`、`set_entry`、`close_page` 全部通过 `ctx->ui` 调用。
  UI 事件回调改为接收 `const struct canopus_ui_event_v1 *`；普通点击处理先检查 `ACTIVATE` / `VALUE_CHANGED`，避免把按下/移动当成点击。
- 先查 `ctx->ui->capabilities(ui)` 再请求目标能力；未支持的布局/视觉/控制返回 `CANOPUS_UI_ERR_UNSUPPORTED`。字体只支持 MiSans-Medium 24/28，图片适配目前只有 `NATIVE`。
- 模块生命周期用既有 `ctx->bus` 版本 2：在 owner 任务上先订阅 `CANOPUS_BUS_TOPIC_MODULE` 再 `query_module`，中间不让出；没有初始事件和历史重放。
  负载在回调返回前拷贝，UI 工作通过 `ctx->ui->post` 投递到下一次前台 UI 心跳。`ENABLE` / `DISABLE` / `REMOVE` 仍是下次启动意图。
- Rust 的 `Application` 改用 `decode_event(&Event)`，可将完整事件拷贝到 typed Message；只处理点击的应用按事件类型过滤。
  typed `NavigationHeader` 分开指定 `back` 和 `show_back`，可保留暂时隐藏的返回消息；显示时事件 ID 必须非零。
  指针/滚动消息更新后仅在发出 `Command::Rebuild` 时重建，点击/值变更仍自动重建。`CAbiBackend` 将原生不支持保留为 `UiError::Unsupported`。
  详情见 [服务规范 §2.7、§2.12](canopus-services-specification.md) 与 [ADR-CAN-013](../adr/ADR-CAN-013-ui-abi-2-lifecycle.md)。

## 6. 其他

- Manager 里的"更新"按钮已删除；"回滚"只在存在上一代时显示，重启后生效。
- CPC1/CPS1 在 P2.5 之前仍可用；P2.5 会统一到 CPC2，届时再给出安装表盘的改造说明。
- Band 9 / 9 Pro / Band 10 / REDMI Watch 6 在 2026-10-24 建立了仅静态的 target pack
  （`xiaomi-band-9-3.1.32`、`xiaomi-band-9-pro-3.1.190`、`xiaomi-band-10-3.1.28`、`redmi-watch-6-3.100.043`）。
  Band 9 的记录由 tag `band9-frozen` 恢复并降为 restricted/PENDING；四者的 loader、installer 与设备验证都还未实现，
  其中 Band 9 / 9 Pro / Band 10 是 LVGL v8，需要专门的 v8 UI 后端。P65 的旧部署事故记录仍见 ADR-CAN-007。
