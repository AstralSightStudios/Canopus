# Mirage 模拟器工作流与需求清单

## 现状（P6.1/P6.2/P6.5）
- `canopus emu smoke`（= `python3 -u scripts/emu/smoke.py`）：Band 10 Pro 3.101.043 上的安装→启用→重启→更新→降级被拒→加载失败回退→回滚，以及手机端 `.rpk` 流程（真实 AstroBox 核心经模拟器蓝牙）。缺 Mirage/固件时退出码 77。
- 环境变量：`MIRAGECTL`、`CANOPUS_FW_OTA_DIR`、`MAB_SMOKE`（AstroBox 桥）。结果只算 host/emulator 级证据。
- 日志在设备目录 `Logs/` 下（console.log、uart*.log、qemu.log）；`miragectl sh` 与 GUI 互斥。

## 调试器联调（P6.3）
- 本机没有 gdb，用 lldb 的 gdb-remote：`scripts/emu/lldb.sh <设备 id 前缀> [module.elf <.text 加载地址>]`。
  已在 043 冒烟设备上验证能连上并停在固件代码（Cortex-M 线程，`register read pc` 可用），退出后设备继续运行。
- gdb.sock 只在设备运行时存在；模块的加载地址要自己给（Supervisor 尚不上报各模块基址，没有做自动化）。

## 向 Mirage 提出的需求（P6.4，本地已改的桥接见 Mirage 仓库提交 `ab23d33`）
1. `miragectl hmp`（库内已有 `hmp()`）暴露为命令。
2. 全部命令的 `--json` 输出。
3. 快照/恢复（缩短 15–20 分钟的冒烟耗时）。
4. `log` 子命令（读设备日志而不是自己找文件）。
5. 控制台多路复用（`sh` 与 GUI/console 并存）。
6. `fs put live` 支持大文件（现在走 NSH printf，50 KB 以上不可用）。
7. AstroBox 桥：按手环地址连接（已绑定设备不响应 inquiry）已在本地实现，希望合入上游。

## Band 11（q66）验证尝试（2026-10-14）
导入 4.100.177 OTA 并创建 q66 设备可以启动 QEMU，但 3 分钟以上 AP 控制台（console.log/uart0.log）无任何输出、屏幕全黑，
`miragectl sh` 超时；仅 uart1 有 bootloader 横幅。当前 Mirage 构建上 Band 11 没有跑起来，因此 Band 11 的安装器/SCAN
只有 unicorn 固件仿真（`scripts/tests/band11_*.py`）级别的证据。需要先在 Mirage/qemu-miwear 侧排查 q66 启动。
更正（后续用 `-d in_asm` 复核）：AP 并没有卡在早期初始化——运行 15 秒后 CPU 在 0x0c6c6a6c 一带的 OS 定时器/等待代码里循环，
说明固件已启动进入系统。真正的现象是：4.100.177 是 user 构建，没有 NSH 控制台（`miragectl sh` 必然超时），
且设备已绑定（跳过向导）后截图仍是全黑（所有像素为 0），点击无反应。早先"卡在 bes.periph 轮询"的判断只是日志前段，不成立。
因此 Band 11 的验证路径不能沿用 043 的 NSH + Lua 驱动，需要经手机侧（AstroBox 桥）安装安装器表盘并看屏幕；
屏幕为何全黑要在 qemu-miwear 一侧查（q66 的 TE/面板初始化）。
