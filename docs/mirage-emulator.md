# Mirage 模拟器工作流与需求清单

## 现状（P6.1/P6.2/P6.5）
- `canopus emu smoke`（= `python3 -u scripts/emu/smoke.py`）：Band 10 Pro 3.101.043 上的安装→启用→重启→更新→降级被拒→加载失败回退→回滚，以及手机端 `.rpk` 流程（真实 AstroBox 核心经模拟器蓝牙）。缺 Mirage/固件时退出码 77。
- 环境变量：`MIRAGECTL`、`CANOPUS_FW_OTA_DIR`、`MAB_SMOKE`（AstroBox 桥）。结果只算 host/emulator 级证据。
- 日志在设备目录 `Logs/` 下（console.log、uart*.log、qemu.log）；`miragectl sh` 与 GUI 互斥。

## GDB 联调（P6.3，未验证）
- 设备以 shell 选项启动时才有 `gdb.sock`，此时 QEMU 以 `-S` 暂停，注入器占用后 detach。
- 本机没有 gdb / arm-none-eabi-gdb（只有 lldb），故未实际联调。步骤预期：`target remote <gdb.sock>`，
  模块符号用 `add-symbol-file module.elf <load base>`；Supervisor 还不上报各模块加载基址，需先在 QUERY 载荷里补充后才能自动化。

## 向 Mirage 提出的需求（P6.4，本地已改的桥接见 Mirage 仓库提交 `ab23d33`）
1. `miragectl hmp`（库内已有 `hmp()`）暴露为命令。
2. 全部命令的 `--json` 输出。
3. 快照/恢复（缩短 15–20 分钟的冒烟耗时）。
4. `log` 子命令（读设备日志而不是自己找文件）。
5. 控制台多路复用（`sh` 与 GUI/console 并存）。
6. `fs put live` 支持大文件（现在走 NSH printf，50 KB 以上不可用）。
7. AstroBox 桥：按手环地址连接（已绑定设备不响应 inquiry）已在本地实现，希望合入上游。
