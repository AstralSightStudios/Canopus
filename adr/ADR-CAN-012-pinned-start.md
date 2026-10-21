# ADR-CAN-012: 固化启动（pinned start）

状态：已实现于 Band 10 Pro `.043`（host / 模拟器证据），未上真机。Band 11 未启用。

## 背景

Supervisor 不随开机加载：每次重启后都要打开安装器表盘点一次 Run。曾经的 Auto-run 只是安装器页面初始化后延时执行 Run，换成别的表盘就失效，已删除。

启动脚本在固件内嵌的 ROMFS（`/etc/init.d/rc.sysinit`）里，没有读取 `/data` 脚本的入口。固件把闪存注册为 `/dev/bes_flash`（MTD 驱动，
经 `mtd_proxy` + FTL 读改写 8 KiB 扇区，关闭文件时落盘），可以用普通文件接口读写。

## 决定

- **默认不固化。** 只有用户在 AstroBox 的 Canopus 页面点击“固化”并确认后，Supervisor 才写闪存；“解除固化”写回原内容。
- 写入的是 `rc.sysinit` 里一段**等长**的差异尾段：去掉末尾两条 `echo "mount …"`，在脚本末尾追加 `sh /data/canopus/b &`。文件大小与 ROMFS 头不变。
  差异尾段与偏移由 `scripts/generate_boot_config.py --recover` 从精确的固件镜像恢复，记在 `targets/<id>/evidence/boot-persistence.json`。
  没有这份证据的目标，构建里 `CANOPUS_BOOT_ENABLED 0`，设备报告“不支持”，AstroBox 不提供按钮。
- 状态永远由读回闪存得出（`QUERY_BOOT`：bit0 支持，bit1 已固化，bit8.. 当前启动步骤），不保存“开关”标志。
- 开机路径：`rc.sysinit` → `sh /data/canopus/b &`（先 `sleep 30`，再 `lua /data/canopus/boot.lua`）。
  `boot.lua` 最多尝试 3 次（`boot.pending` 计数）：`insmod` 加载 Supervisor，再发 `START_BOOT`。
- `START_BOOT` 只在 owner 后台泵上挂一个每秒一步的状态机：Manager 注册 → 启动器入口 → **清除 `boot.pending`** → 恢复已启用模块 → 模块应用 → 启动器入口。
  Manager 在第三方模块之前，计数在 Manager 存在后才清零。
- 这些文件由安装器 Run 落到 `/data/canopus/`：`supervisor.elf`、`boot.lua`、`b`、`boot.target`。固化前 Supervisor 检查它们齐全且 `boot.target` 与自身一致。

## 模拟器里确定的约束（Mirage，仅模拟器证据）

- 在 `rc.sysinit` 里直接启动 `lua` 会使系统复位，所以由 `b` 先延迟。
- 亮屏时在 UI owner 上注册 Manager 会使模拟设备复位，熄屏时在 owner 定时器里做则正常；所以状态机只在屏幕熄灭时推进（平台 `screen_state`）。
- owner 定时器要靠 LVGL 跑一次来引导，所以 Manager 的注册发生在**开机后第一次点亮屏幕、随后熄屏时**，不是开机即刻。
- 从 NSH 任务直接发 `NATIVE_STAGE` 会卡死，所以 `boot.lua` 不自己执行注册步骤。
- 没有后台泵（没有 L2 后端）的目标没有地方推进状态机，`QUERY_BOOT` 报告不支持（`.139`）。

## Band 11

`.155` 上 `boot.lua` 的原生加载器在没有安装器表盘的情况下跑通（`result=0`），但 Manager 注册序列在模拟器里没有走完，原因未查明，所以 Band 11 没有写入
`boot-persistence.json`，设备报告不支持。

## 风险与边界

- 写入的是固件闪存区，写入期间断电可能使手环无法启动；没有在真机上验证过，确认对话框如实写明。
- 脚本连续 3 次没有走到 Manager 注册就不再自动尝试，手环按原样启动。
- 闪存读写只在 Supervisor 内，作用于证据里记录的那一段；手机与公共 SDK 看不到任意闪存地址。
- 旧的 `autorun` 标记不代表对固化的授权。
