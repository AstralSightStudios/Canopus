# ADR-CAN-012: 固化启动（pinned start）

状态：已实现于 Band 10 Pro `.043`（host / 模拟器证据，未上真机）。Band 11 `.155` / `.179`：第一版写入器（经 `/dev/bes_flash`）2026-11-03
在真机上把闪存写坏、手环循环重启；同日换成经固件 flash HAL 的写入器（见“写入”），Band 11 仍默认关闭（`boot-persistence.json` 的
`device: failed`），只有 `CANOPUS_BOOT_DEVICE_TEST=1` 的构建打开，等真机复测。`.043` 也换成了新写入器，同样没有真机证据。
同日查到开机路径本身的第二个问题（见“开机脚本里的 `lua`”）：`boot.lua` 退出后固件仍拿着它已释放的 Lua 状态，下一次亮灭屏就复位，
`.043` 与 Band 11 都有；`boot.lua` 现在先归还它。
`.139` 不支持：原因原是没有后台泵；2026-10-24 起 `.139` 有了 owner 泵（后台 timer 用），但 `scripts/emu/boot_smoke.py 139` 里
Supervisor 加载后启动序列不完成（`boot.pending` 不被清除），所以 `generate_boot_config.py` 只给同时有 L2 后端的 target 打开固化启动。

## 背景

Supervisor 不随开机加载：每次重启后都要打开安装器表盘点一次 Run。曾经的 Auto-run 只是安装器页面初始化后延时执行 Run，换成别的表盘就失效，已删除。

启动脚本在固件内嵌的 ROMFS（`/etc/init.d/rc.sysinit`）里，没有读取 `/data` 脚本的入口。ROMFS 在 XIP 运行的 AP 镜像里，
只能擦写它所在的 8 KiB 擦除块；只清位的改法（不擦除）写不出任何指向 `/data` 的命令（`rc.sysinit` 与 `rcS` 的每一行都试过）。

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

## 写入

第一版经 `/dev/bes_flash` 写（`mtd_proxy` + FTL + BCH）。静态分析找到的问题：

- 擦除块里不只有脚本：`.155` 的块在 ROMFS 之后还有 203 字节在用的只读数据（一张驱动 `file_operations` 表和 `gpiod_list_chips` 等函数名），
  `.043` 有 1006 字节（NAND 分区表、带指针的颜色表）；`.179` 只有 ROMFS 和补零。`rcS` 与 `rc.sysinit` 也在同一块里。
- MTD 的擦除（`up_progmem_eraseblock`）在 BASEPRI 0x80 里擦完后，出临界区调一次 `usleep(1)`（Band 11 仅在中断/idle 时改用 `udelay`，
  `.043` 无条件），随后 FTL 才调 `bwrite` 编程。这段空隙里别的任务照常运行，读到已擦空的表就会跳到 0xFFFFFFFF；
  此时复位，块就停在空白状态，`rcS`/`rc.sysinit` 和那张驱动表都没有了。
- 尾段跨 512 字节扇区边界，BCH 分两次写，同一块要读改写两遍，空隙也是两次；写完只比对尾段，读回不对时不恢复。

现在的写入器（`manager/service/canopus_supervisor_boot.c`）：

- 直接调 MTD 驱动用的 BES flash HAL：`app_flash_erase` / `app_flash_program` / `app_flash_flush`（每个 target 三条 `flash.*` 符号记录，
  STATIC_RECOVERED / restricted；三个镜像里的 HAL 代码除跳转偏移外逐字节相同）。擦除与编程放在**同一个** BASEPRI 0x80 临界区里连续完成，
  中间不让出 CPU；HAL 在这一级本身就是同步的（`norflash_api_erase/write` 当场完成，bank 0 上没有排队的操作，`flush` 直接返回）。
- 块的状态由整块 CRC32 区分（`generate_boot_config.py --recover` 从固件镜像算出，`boot_config.h` 里四个常量）：原样、已探测、已固化、已注释。
  读回走非缓存别名 `0x28000000`。
- 第一次擦除前先做**只清位的探测**：把要删掉的那行 `echo` 的 `ec` 改成 ` #`（注释），读回必须是“已探测”，否则不擦除直接失败。
  这一步确认 HAL 能在这一块编程、读回看得到，也排除了写保护（包括只保护双芯片之一）。
- 固化：从当前块复制 8 KiB，换上固化尾段，CRC 必须等于“已固化”才写；写后读回，最多 3 次；仍不对就用同一份副本换回原尾段再写（最多 3 次）。
- 解除固化不擦除：把 `sh` 的 `s` 清成 `#`，启动行变成注释（“已注释”）；读回不对才擦写回原脚本。再次固化从“已注释”直接擦写。
- 擦除前要求电量 ≥ 30% 或在充电；每一步写进 `/data/canopus/pin.log`（`<步骤> <值>`，逐行 fsync），复测时用它定位。
- NSH 把以 `#` 开头的记号当注释，探测行与注释行都由 Mirage 上的 `sh` 验证（`boot_smoke.py 043`）。

## 开机脚本里的 `lua`

`b` 用 NSH 的 `lua` 跑 `boot.lua`。这个解释器启动时打开固件的 miwear 库，其中 `luaopen_screen` 把解释器的 `lua_State` 存进一张
三格的全局表（符号 `lua.screen_lua_states`：`.043` 0x20140D60、`.155` 0x200D3C5C、`.179` 0x200D2B4C），并注册 `screen_state_cb`；
之后每次亮屏/熄屏，miwear 任务都对表里每个非空的状态做 `lua_getglobal(L, "ScreenStateChangedCB")`。只有应用框架自己的
`lua_module_miwear_close` 会清掉一格，`lua` 进程退出时不会，于是表里留下已释放的内存。

- Mirage `.155`：`boot.lua` 之后第一次亮灭屏，miwear 任务在 `screen_state_cb` 下的 `luaT_gettm` 里 MemManage 错误（或卡死），手环复位。
  `b` 每次开机都会启动 `lua`，`boot.lua` 的 3 次上限只让它提前返回，状态照样留下，所以固化后的 Band 11 会一直这样重启。
  这很可能就是真机上看到的循环重启（闪存是否也被写坏，要复测时看 `pin.log` 与重启现象）。
- Mirage `.043`：`lua -e` 退出后第 0 格仍是它的状态。那里没有出错只是因为那块内存还没被复用；真机上固化后任何一次亮灭屏都可能出错。
- 修复：生成的 `boot.lua` 第一件事是用 `tostring(coroutine.running())` 得到自己的状态地址，读三格（`mw` 读），哪一格等于它就用一次
  对齐的 `mw` 写 0。之后 miwear 不再碰这个解释器。`boot_smoke.py` 放上设备的 `driver.lua` 开头也有同一段。

## 模拟器里确定的约束（Mirage，仅模拟器证据）

- 在 `rc.sysinit` 里直接启动 `lua` 会使系统复位，所以由 `b` 先延迟。
- 亮屏时在 UI owner 上注册 Manager 会使模拟设备复位，熄屏时在 owner 定时器里做则正常；所以状态机只在屏幕熄灭时推进（平台 `screen_state`）。
- owner 定时器要靠 LVGL 跑一次来引导，所以 Manager 的注册发生在**开机后第一次点亮屏幕、随后熄屏时**，不是开机即刻。
- 从 NSH 任务直接发 `NATIVE_STAGE` 会卡死，所以 `boot.lua` 不自己执行注册步骤。
- 没有后台泵（没有 L2 后端）的目标没有地方推进状态机，`QUERY_BOOT` 报告不支持（`.139`）。

## Band 11（等待真机复测）

2026-11-03：第一版写入器在真机上固化后闪存被写坏、手环循环重启（模拟器只测过离线打补丁镜像的冷启动，没有跑过 Band 11 的运行时写入）。
两个已修的原因都可能导致它：“写入”一节的空隙（只在 `.155` 的块里有在用数据，`.179` 只有 ROMFS），和“开机脚本里的 `lua`”
（`.155`、`.179` 都有，而且不论写入是否成功，每次开机都会复位）。是哪一个，要看复测时的 `pin.log` 与重启现象。

- Band 11 NuttX 内核无 `insmod`，开机由 `/data/canopus/b` 延时调用 `boot.lua`。
- `boot.lua` 内嵌 target 专用原生加载器（`native_loader.lua`）与 loader profile，独立加载 `/data/canopus/stage1.bin`、`stage2.bin`、`supervisor.elf` 到 Umem 执行重定位与初始化构造函数。
- 加载完成后，`boot.lua` 向 `/dev/canopus` 发送 `CANOPUS_CMD_START_BOOT`（25）。
- Supervisor 的后台定时器（`canopus_sup_l2_background`）在开机后首次点亮屏幕时引导，并在屏幕熄灭时依次推进 5 个启动阶段，完成 Manager 注册、启动器入口发布、清除 `boot.pending`、恢复已启用模块及模块应用发布。
- Mirage 冷启动冒烟测试（`scripts/emu/boot_smoke.py 155` 与 `179`）2026-10-21 通过。2026-11-02 Mirage 的触摸模型改为熄屏时一按就能唤醒后，
  亮灭屏真的发生了，测试随即因上面那份残留的 Lua 状态复位；`boot.lua` 交还状态后再次通过，并且由开机脚本里的一个 `lua`
  依次解除固化（只清位）、查询、再固化（整块擦写）、查询（`boot_smoke.py` B 段）。`.139` 无后台定时器泵，不支持固化启动。

## 风险与边界

- 写入的是固件闪存区：擦除到编程完成（约几十到几百毫秒）之间断电或复位，手环无法启动；没有在真机上验证过，确认对话框如实写明。
- 脚本连续 3 次没有走到 Manager 注册就不再自动尝试，手环按原样启动（前提是 `boot.lua` 交还了它的 Lua 状态，见上）。
- `START_BOOT` 在 NSH 的 `lua` 任务里调 `lv_timer_create` 引导 owner 定时器；固件的 LVGL 没有锁，这次调用还会经 resume 回调
  （`lv_nuttx_uv_timer_resume`）在 UI 的 libuv 循环上 `uv_timer_start`。这是跨任务、不加锁的修改，模拟器里没有观察到问题，但不是严格安全的。
- 闪存读写只在 Supervisor 内，作用于证据里记录的那一段；手机与公共 SDK 看不到任意闪存地址。
- 旧的 `autorun` 标记不代表对固化的授权。
