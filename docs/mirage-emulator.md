# Mirage 模拟器工作流与需求清单

## 当前原生投递边界（2026-10-17）

模块不再使用 RPK 或快应用安装。当前 smoke 脚本以 `.canopus` 打包，通过桥的
`install kind=canopus` 使用与 AstroBox 相同的 core 客户端发送加密 CPC2 请求。
旧快应用模拟器结果只是历史，不是新数据面通过的证据。

普通构建自动包含已实现的 target L2 后端（.043 / .155），smoke 不再单独启用通信功能。
这不批准任何私有符号，也不升级真机能力状态。脚本默认使用独立设备名，
可用 `CANOPUS_SMOKE_DEVICE` 指定专用测试实例；该实例会被重建，请勿指定个人设备。

## 2026-10-20 重跑

`miragectl` 在 `/Volumes/EXT0/Mirage/.build/out/Products/Debug/miragectl`（脚本默认值已改到这里）。资料库在 `Mirage/.scratch/Library`，
`.179` 固件用 `miragectl firmware import ~/Downloads/miwear.watch.q66tc_v4.100.179_full_a93678a9` 导入。

- 安装器页面加了 Auto-run 之后「Run」按钮上移：Band 11 `(106, 355)`，Band 10 Pro `(168, 168)`，`band11_smoke.py` / `ui_pages.py` 已更新（旧坐标会点到状态栏，Supervisor 不加载，L2 报 `refused L2CAP channel for PSM 0x0011`）。
- `scripts/emu/smoke.py`（043）：收件箱流程与手机端 phone flow 全部通过，P5.2 的超时没有复现。
- `scripts/emu/run_services_test.py`（043）：22/27，KV 在重启后保留（流式 KV）。
- `scripts/emu/band11_smoke.py 155|179`：通过；`--services`：`.179` 26/27（仅振动跳过）。
- `scripts/emu/ui_pages.py 043|155|179`：通过（启动器 > Manager > 模块页面 > 返回）。
- `scripts/emu/module_smoke.py --module modules/examples/hello-portable 155|179`：通过。
- Auto-run（G18）在 Mirage 里测不了：设备重启后 Mirage 不会再次启动 Lua 表盘；手机安装并设为当前时，表盘看起来启动了两次，后一次读到了 `autorun.pending` 而把 Auto-run 关掉（观察所得，原因未查）；
  熄屏后 LVGL 定时器不走，模拟器里熄屏约 8 秒，所以延迟从 10 秒缩到 4 秒（两个安装器）。Auto-run 只有 host Lua 测试（真实 C 栈帧）证据。

### 本轮实际结果

- 043 收件箱安装、升级、降级拒绝、坏版本回退与回滚：通过。
- 155 生产安装器原生 bootstrap：`result=0`；本地 `/dev/canopus` 查询与 SCAN：通过。
- 155 加密 L2 原生 `.canopus` 安装与模块列表查询：**通过**。真实 AstroBox core 在约 453 ms 内完成 `hello_portable` 安装，列表返回 slot0/state3/version1/activationError0；本地 SCAN result=5/count=1。
- 043 独立加密 L2 原生安装已通过；完整 fresh-device smoke 的 phone flow 仍存在超时，尚未全部闭环。
- 旧 LVGL heartbeat 在熄屏后停止，现只用于引导独立 UI libuv timer；回复通过第二个 Bluetooth libuv timer 发送，避免 UI task-group 写蓝牙任务 fd 的 EBADF。
- `.155` 待打包目录此前曾是无 L2 后端的构建：本地 bootstrap result=0，但 AstroBox 超时。现在普通构建已自动包含后端，并同步重建 Supervisor、CRC profile 和 stage1/stage2；不再需要单独开启通信。
- 编译后的 ARM SRAM shim 在 Unicorn 模型中通过；它不覆盖真实持续调度行为。

## 工作流（P6.1/P6.2/P6.5）
- `canopus emu smoke`（= `python3 -u scripts/emu/smoke.py`）：Band 10 Pro 3.101.043 上的安装→启用→重启→更新→降级被拒→加载失败回退→回滚，以及手机端原生 `.canopus` / 加密 CPC2 流程（真实 AstroBox 核心经模拟器蓝牙）。缺 Mirage/固件时退出码 77。
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

补充（Band 11，2026-10-14，更正前文）：
- 已绑定的 q66 设备屏幕是正常的，之前"全黑"和"配对被拒"是我用错了坐标：Band 11 屏幕 212×520，配对确认键在 (106, 478)，不是 043 的 (250, 435)。
  按此点击后 AstroBox 桥对 Band 11 认证成功（`authenticated with Xiaomi Smart Band 11`，固件 4.100.177），即 Band 11 的手机侧链路在模拟器里可用。
  （未绑定设备能显示开机向导并完成蓝牙配对，但缺少绑定数据，认证返回 account error 4。）
- Band 11 是 user 构建，没有 NSH，Canopus 在它上面只能经安装器表盘（Lua）启动。打包工具其实有：Facetory 仓库
  （`/Volumes/EXT0/GitHub/facetory/rust`）的 `cargo build --example package_lua_watchface`，用法
  `package_lua_watchface <源目录: main.lua + .bin + preview.png> --name N --package-id 12位字母数字 --width 212 --height 520 -o out.bin`
  （没有 preview.png 会警告"可能无法选中"）。用它把 177 的安装器资源打成 `.bin`，经 AstroBox 桥 `install kind=watchface` 装进模拟的 Band 11 成功，
  `watchfaces` 里能看到它，`setWatchface` 能切为当前表盘。
- 但屏幕上只显示预览图（纯色），Lua 界面没有出现；换成只有一个 Label 的最小 Lua 表盘结果相同。因此不是安装器的问题，
  而是这条"facetory 打包 + 模拟器里的 Band 11 表盘引擎"组合下 Lua 不执行/不绘制（可能需要不同的协议字或 Band 11 专用的表盘结构），
  同一个最小 Lua 表盘装进 043 模拟器也一样（只显示预览图，重启后屏幕全黑），所以这不是 Band 11 特有的问题：在 Mirage 里，
Facetory 打包的 Lua 表盘界面都不出现（Lua 入口如何被固件启动取决于设备的表盘运行时约定，见 Facetory 的 docs/LUA_WATCHFACE_PACKAGING.md）。待在 Facetory / qemu-miwear 一侧确认。Band 11 的 Supervisor/SCAN 因此仍不能端到端验证。
  已验证手机投递这一半：经桥把 hello-portable 的 .rpk 装进模拟的 Band 11，数据分区里出现 `/data/quickapp/app/com.canopus.pkg.hello_portable/canopus/{portable.ko, 五个 target 的 .cmi}`；
  由此发现并修正了 Supervisor 的包根目录（Band 11 是 `/data/quickapp/app/`，不是 Band 10 Pro 的 `/data/app/`）。

补充（Lua 启动点，043 IDB）：固件里的 Lua 运行入口是 `sub_CAB2D20`（pmain：取脚本路径、设置 `SCRIPT_PATH`/`package.path`、`luaL_loadfile`、`pcall`，
并打印 `[lua] pmain: script path: …`）。把 Facetory 打包的 Lua 表盘设为当前表盘后，模拟器控制台里没有出现这条日志，说明 pmain 根本没被启动——
Lua 页面的触发条件（不是"设为当前表盘"这么简单）还没弄清，需要从调用 pmain 的上层（表盘/页面管理）继续追，或向 Facetory 作者确认。

再补充（Lua 触发点，续）：调用 `miwear_lua_run`（0xcab2fc0）的是表盘渲染器里的 `render_file_create`（0xca886a0，日志标签 `wf`）：
它从资源管理器取一个 **File 元素**，扩展名为 `lua` 才会启动 Lua（`riv` 会报 not support rive）。也就是说，表盘**布局里必须有一个指向该 .lua 的
File 元素**，仅仅把 main.lua 作为 File 资源放进包里（Facetory 的 `package_lua_watchface` 示例就是这样）不会启动 Lua。
Facetory 的编辑器导出路径里有对应概念（`facetory.editor.file` 的 `fileRef = layout` 生成 `JsFile` 元素，`widget` 则生成跳转 `luaApplication` 的 Widget），
我在一份拷贝里给示例的入口资源加了 `fileRef=layout` 再打包安装，屏幕仍只显示预览图——`.bin` 导出路径看来没有把它转成布局元素。
要让 Lua 表盘在模拟器里跑起来，需要 Facetory 一侧提供带布局元素的 Lua 表盘导出，或手工构造包含 File 元素的布局。运行期日志在启动后不再写入 console.log，无法旁证。


## 历史 Band 11 端到端（2026-10-14，旧投递方案已撤销）
`python3 scripts/emu/band11_smoke.py 155`：真实 AstroBox 核心经模拟器蓝牙装入**未改动的生产安装器表盘** → 点 Run →
原生加载器加载 Supervisor（`bootstrap-result.txt: result=0`）→ 经桥投递 hello-portable 的 `.rpk` → 第二个表盘（`scripts/emu/band11/e2e_scan.lua`）
通过 `/dev/canopus` 发 SCAN（结果 5=COMPLETED），`count=1`、模块进入 slot0（state 3），inbox 里出现 `hello_portable.cmi/.ko`。仅模拟器级证据。
为此需要两处外部改动，补丁放在 `scripts/emu/band11/`：
- qemu-miwear 提交 `3e05ca7`（`dp-1700-exp`，已推送；Mirage 已指向它）：BES 缓存控制器字（0x07ffa000/0x07ffc000，bit0=1）和 8 个 MPU region（真机如此，QEMU 默认 16；
  安装器与原生加载器都会校验，否则分别报 `unexpected BES cache state` / rc=-302、-307）。
- `facetory-lua-layer.patch`：Facetory 的 `package_lua_watchface` 示例没有给入口 .lua 生成布局元素，固件的 `render_file_create` 就不会启动 Lua；
  补丁给入口资源加一个 layout 层。（用户平时用 /Users/searchstars/Downloads/小米表盘工具.app 打包 Lua 表盘，这个官方工具是 GUI，脚本里没有用它。）
