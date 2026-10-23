# Mirage 模拟器工作流与需求清单

## 当前原生投递边界（2026-10-17）

模块不再使用 RPK 或快应用安装。当前 smoke 脚本以 `.canopus` 打包，通过桥的
`install kind=canopus` 使用与 AstroBox 相同的 core 客户端发送加密 CPC2 请求。
旧快应用模拟器结果只是历史，不是新数据面通过的证据。

普通构建自动包含已实现的 target L2 后端（.043 / .155），smoke 不再单独启用通信功能。
这不批准任何私有符号，也不升级真机能力状态。脚本默认使用独立设备名，
可用 `CANOPUS_SMOKE_DEVICE` 指定专用测试实例；该实例会被重建，请勿指定个人设备。

## 工具链前置条件（2026-10-22 复核）

端到端跑通前有四处**树外**工具链要求，缺任一处都会在很后面才失败，且报错离根因很远：

1. **Facetory 打包器必须打 `scripts/emu/band11/facetory-lua-layer.patch`。**
   未打补丁的 `package_lua_watchface` 只把入口 `main.lua` 当 File 资源塞进包，
   没有布局层指向它，固件的 `render_file_create` 就不会调用 `miwear_lua_run`。
   表现是 `setWatchface` 成功、`watchfaces` 里能看到表盘，但屏幕只显示 `preview.png`
   （`ui_pages.preview_png` 的纯色底图），Lua 界面完全不出现，触摸也点不亮屏幕。
   打补丁后重新 `cargo build --example package_lua_watchface`。
2. **`MAB_SMOKE` 必须用 Mirage 分支 `feat/canopus-native-l2-20261017`（提交 `19b74e8`）构建。**
   `main` 上的 `AstroBoxBridge/src/ops.rs` 只有 `InstallKind::{Watchface,Quickapp}`，
   `install kind=canopus` 会报 `invalid params: unknown variant 'canopus'`。
   建议用该分支的 git worktree 构建，不动日常 checkout：
   `git worktree add <dir> origin/feat/canopus-native-l2-20261017` →
   `git submodule update --init --recursive External/AstroBox` →
   `AstroBoxBridge/examples/build-smoke.sh`，然后把 `MAB_SMOKE` 指到该 worktree 的
   `AstroBoxBridge/target/smoke`。
3. **QEMU 必须由含 `3e05ca7` 的 qemu-miwear 构建。** 该提交补上 BES I/D 缓存控制器使能字
   （0x07ffa000 / 0x07ffc000，bit0 复位为 1）和 8 个 MPU region。`Mirage.app` 里预编译的
   `Helpers/qemu/bin/qemu-system-arm` 可能早于该提交；缺了它安装器在「同步缓存并启动原生
   加载器」一步报 `unexpected BES cache state`，Supervisor 不加载，随后 `install kind=canopus`
   报 `refused L2CAP channel for PSM 0x0011`。用 `Mirage/Scripts/build-qemu.sh` 重建，
   产物在 `External/qemu-miwear/build/qemu-system-arm`，再让它顶掉实际被加载的那份二进制。
4. **AstroBox 的 `canopus.rs` / `package.rs` 自带一份 ABI 版本号，必须跟上 `sdk/c/canopus_abi.h`。**
   `External/AstroBox/modules/core/src/device/canopus.rs` 的 `CANOPUS_ABI_MAJOR`/`CANOPUS_ABI_MINOR` 和
   `canopus/package.rs` 的 `manifest.canopus_abi != "<n>"` 都是硬编码副本，与本仓库的头文件没有共享。
   不一致时 Supervisor 的 `canopus_transport_v2_decode_request` fail-closed 拒收，表现是桥侧
   `Canopus request timed out`（不是报错码），同时日志里混着一条无关的
   `refused L2CAP channel for PSM 0x0011` —— 后者是 AstroBox 模拟 L2CAP 只放行 `PSM_SDP` 的正常结果，
   Canopus 流量其实走 SAR 通道的 `L2Packet::canopus_write`，与 L2CAP PSM 无关，不要照着它查。
   改完 `AstroBoxBridge/examples/build-smoke.sh` 重编 `MAB_SMOKE`。

## UI ABI 2.0 隔离验收（2026-10-23）

通用入口 `scripts/emu/native_ui_v2.py` 使用已构建的生产安装器和 `native-ui-lab`，经加密 CPC2 投递后用触摸驱动卡片、100 项虚拟列表、拖动、选择、Toast、标题与关页。
先为目标构建 Supervisor，再运行：

```sh
CANOPUS_TARGET=xiaomi-band-11-4.100.155 sh scripts/build_canopus_supervisor.sh
python3 -u scripts/emu/native_ui_v2.py 155 \
  --library "$CANOPUS_UI_TEST_LIBRARY" \
  --installer-dir watchfaces/canopus-installer-prod/xiaomi-band-11 \
  --work "$CANOPUS_UI_EVIDENCE_DIR"
```

`CANOPUS_UI_TEST_LIBRARY` 必须是独立的测试目录；脚本拒绝日常资料库，以及未带 `.canopus-native-ui-v2` 标记的既有非空目录。
每次创建新设备，默认完成后删除；`--keep-device` 保留实例用于调试。Band 10 Pro 用 `043` 和 `watchfaces/canopus-installer-prod/xiaomi-band-10-pro`。
设置 `MIRAGECTL`、`MAB_SMOKE`、`FACETORY_PACKAGER`，工具链要求见上节。`run.json` 保存固件、能力掩码、gated 项、Supervisor/模块 SHA-256 和截图哈希。
Band 11 没有 NSH，驱动完成后先关机，再从 `nand_data` 离线提取模块测试记录；记录不承担应用状态存储。

`.155` 最终构建已通过完整场景：虚拟窗口移动到 44，选中第 46 项，返回后恢复相同位置与选中态；捕获拖动、Toast 显示/到期、标题返回态与关页通过。
`.179` 的导航、选择、Toast、标题和关页通过，缺少独立恢复的输入/滚动原语，明确 gated `virtual-list` / `pointer-drag` / `pointer-capture`。
`.043` 完整场景通过：窗口到 37、选中第 40 项并返回恢复，卡片/图片、捕获拖动、选择、Toast、标题与关页通过。
首次失败定位到私有 UI 入口的加载地址/运行地址混淆；运行地址、图片 ABI 与绝对滚动入口修正后，滑动起点还需位于列表视口内。
驱动按面板高度减 80 选起点（`.043` y=400，Band 11 y=440），避免点在列表下方。
构建/运行哈希、截图、事件与完整 CI 日志见 [`evidence/native-ui-abi2-2026-10-23`](evidence/native-ui-abi2-2026-10-23/README.md)。
所有结果只算 host/emulator 证据，不表示真机性能或 production approval。

### 复审修复后的重新验收（2026-10-23，review7）

R1–R14 修复后重跑隔离实例（包 `f2869e65…`，每目标新设备）。Lab 的卡片更新拆成两步：
通用部分（标题/几何/颜色）先提交，扩展样式（圆角/边框/透明度/圆角裁剪）单独尝试，目标缺少对应
样式属性 ID 时以 `stylefallback 1/8` 降级并在 `run.json` 里 gate `style-card`——`.155` 属性齐全
无降级，`.043`/`.179` 各恰好一次，三者都没有 `renderfail`。
Toast 演示时长加到 10 秒，让显示/隐藏/重显/到期四帧落在无歧义窗口；面板靠已选 radio 的重复点击
保持点亮（重渲染不重启计时、到期后不重播）。四态按截图像素验收：显示/重显有“Selection saved”
文本像素（.155/.179 872、.043 1613），隐藏/到期为 0，且隐藏帧出现在原 Toast 仍在期内的时刻
（提前消失而非等到期）。
结果：`.155` passed 无 gate；`.043` partial（`style-card`）；`.179` partial（`virtual-list` /
`pointer-drag` / `pointer-capture` / `style-card`）。`.043` 窗口到 37、选中第 40 项，`.155` 到 44、
第 46 项，返回恢复一致；虚拟列表/拖动的缺失在 `.179` 显示为“unavailable”占位页而不是假成功。
证据见 [`evidence/native-ui-abi2-review-fixes-2026-10-23`](evidence/native-ui-abi2-review-fixes-2026-10-23/README.md)。

### N1/N2 整改后的复跑（2026-10-23，包 `7347d369…`）

Lab 的 shown 提交改成携带成对 TOAST 节点（可见文本仍为 "Selection saved"，由最近可见的后节点驱动），
hide 提交移除其中一个（N1 移除形态）；`toastpair` probe 三目标各 2 次。重跑结果与 review7 逐项一致：
`.155` passed 无 gate，`.043` 仅 gate `style-card`（`stylefallback 1/8` 恰一次），`.179` gate 三项输入 +
`style-card`，均无 `renderfail`，窗口/选中 37/40（`.043`）与 44/46（`.155`）与 review7 相同。
四态近白文本像素 `.155`/`.179` 872/0/872/0，`.043` 862/0/862/0（同口径对 review7 批次量测结果相同，
无回归）。N2 的捕获取消销毁时序无法用顺序点击可靠驱动，由 host ASan/UBSan 回归覆盖。
证据见 [`evidence/native-ui-n1n2-fixes-2026-10-23`](evidence/native-ui-n1n2-fixes-2026-10-23/README.md)。

M1/L1 整改（别名样式守卫 + "最近可见 Toast 驱动"规则）后再复跑一轮：三目标结果、四态像素与
toastpair/stylefallback 计数与前两批完全一致（`.155` 无 gate，`.043`/`.179` 各一次 `stylefallback`）。
证据见 [`evidence/native-ui-m1l1-fixes-2026-10-23`](evidence/native-ui-m1l1-fixes-2026-10-23/README.md)。

M2 整改（驱动切换走替换路径重绘样式）后再复跑一轮：三目标结果、四态像素与计数仍与前三批完全一致。
证据见 [`evidence/native-ui-m2-fixes-2026-10-23`](evidence/native-ui-m2-fixes-2026-10-23/README.md)。

P1/P2 整改（不变量「owner 文本被重写 ⇒ 样式同步重绘」+ 隐藏节点不写不替换、T1 first-HEADER 标题驱动）后再复跑：
三目标 gates、toastpair 计数与四态像素与前四批同口径一致（.155/.179 = 872/0/872/0；.043 同法 866/2/866/2，含 418 行静态 chrome 2px）。
证据见 [`evidence/native-ui-p1p2-fixes-2026-10-23`](evidence/native-ui-p1p2-fixes-2026-10-23/README.md)。

调试 `.043` 时发现：启动代码把部分 flash LMA 拷贝到 SRAM/PSRAM，必须调用对应 RAMX/PSRAMX 的 VMA。
例如样式 dispatcher 的加载地址 `0x0c107790` 对应执行地址 `0x002bd924`；从加载地址执行会让相对调用跳进 AP 头部空白区。
这无需给 Mirage 增加 ROM helper 或修改固件镜像，详见 target 的 `EVID-UI-1043-LAYOUT-INPUT-001`。

## 2026-10-20 重跑

`miragectl` 在 `/Volumes/EXT0/Mirage/.build/out/Products/Debug/miragectl`（脚本默认值已改到这里）。资料库在 `Mirage/.scratch/Library`，
`.179` 固件用 `miragectl firmware import ~/Downloads/miwear.watch.q66tc_v4.100.179_full_a93678a9` 导入。

- 安装器页面加了 Auto-run 之后「Run」按钮上移：Band 11 `(106, 355)`，Band 10 Pro `(168, 168)`，`band11_smoke.py` / `ui_pages.py` 已更新（旧坐标会点到状态栏，Supervisor 不加载，L2 报 `refused L2CAP channel for PSM 0x0011`）。
- `scripts/emu/smoke.py`（043）：收件箱流程与手机端 phone flow 全部通过，P5.2 的超时没有复现。
- `scripts/emu/run_services_test.py`（043）：22/27，KV 在重启后保留（流式 KV）。
- `scripts/emu/band11_smoke.py 155|179`：通过；`--services`：`.179` 26/27（仅振动跳过）。
- `scripts/emu/ui_pages.py 043|155|179`：通过（启动器 > Manager > 模块页面 > 返回）。
- `scripts/emu/module_smoke.py --module modules/examples/hello-portable 155|179`：通过。
- `scripts/emu/boot_smoke.py`（043，G18）：A 段在原版固件上经 `/dev/bes_flash` 查询、固化、读回脚本、解除；B 段用离线改好启动脚本的 AP 镜像副本冷启动
  （Mirage 每次启动都从固件包重装 AP 镜像，运行时写入跨重启保不住），不手动加载，Supervisor 自己起来、点亮一次屏幕熄灭后序列走完、`boot.pending` 被清除。
  导入补丁包时必须用硬链接或复制：Mirage 不跟随符号链接，会把链接本身当成镜像。亮屏时注册 Manager 会使模拟设备复位，熄屏时没问题（ADR-CAN-012）。
  Band 11 `.155`：`boot.lua` 的原生加载器自己跑通，但 Manager 注册序列没走完，未启用。

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
