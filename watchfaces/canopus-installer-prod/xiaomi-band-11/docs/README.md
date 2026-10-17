# Xiaomi Band 11 4.100.139 / 4.100.155 统一实机测试包

2026-09-15：已修复“注册管理器”后的通知崩溃。`message+92` 不再为空，
两条通知分别持有驻留的普通通知上下文；新增真实消费者回归能重现旧故障 PC，
并验证修复路径。见 [crash3 审计](../../../../targets/xiaomi-band-11-4.100.139/loader/notification-crash3.md)。
替换前重启；修复后的真实屏幕效果仍需复测。139 和 155 共用本目录的一个
`main.lua`，入口读取 `/etc/build.prop`，按 version + build 精确选择恢复参数和资源。

打包目录为 `watchfaces/canopus-installer-prod/xiaomi-band-11/`。根层严格只有
`main.lua` 和九个 `.bin` 文件；`src/`、`docs/`、`build/` 都不加入 watchface。
沿用 Band 9 的打包/安装方式，**不要把资源 ZIP 当作已封装的 watchface 固件刷入**。

```sh
python3 scripts/build_band11_installer.py
```

## 加密通信后端（4.100.155，实验构建）

Band 11 `.155` 的通信后端复用 `manager/service/canopus_supervisor_l2.c`，
在 MiWear L2 `0x43` 通道仅受理 `WriteEnc`，按配对会话解密 CPC2。
Bluetooth owner 收包入队，UI owner 处理 Supervisor 命令和文件操作，
响应再由 Bluetooth owner 的独立原生定时器发送；不能从 UI 任务直接写蓝牙任务的 fd。
两条循环的地址及 `.155` 专属的 88 字节定时器布局来自
`targets/xiaomi-band-11-4.100.155/evidence/l2-sram-probe.json`。

打包前须明确构建带通信后端的 `.155` 资源，而不是只重新打包旧 `.bin`：

```sh
CANOPUS_TARGET=xiaomi-band-11-4.100.155 CANOPUS_EXPERIMENTAL_L2=1 \
  sh scripts/build_canopus_supervisor.sh
```

此命令同步更新本目录的 `.155` Supervisor、stage1/stage2、CRC profile 及资源 ZIP。
打包本目录根层的 `main.lua` 与全部 `.bin`；安装并切换到该表盘后，仍须点击 **Run**，
直至显示 `Run completed`。仅设为当前表盘不会自动加载 Supervisor；重启后也须重新 Run。
已驻留的旧 Supervisor 不会被新资源热替换，更新安装器后先重启再 Run。

不设置实验开关的默认构建仍不包含 L2 后端，启动接口返回 `UNSUPPORTED`，
即使本地管理器注册成功也不能与 AstroBox 通信。`.139` / `.177` 没有本次已审计的
L2 配置，不能复用 `.155` 地址。相关符号保持 `restricted / PENDING / not_probed`；
此开关不是生产批准，模拟器验证也不等同真机验证。

根层资源：

- `main.lua`
- `manager_icon.bin`
- `canopus_loader_profile-xiaomi-band-11-4.100.139.bin`
- `canopus_stage1-xiaomi-band-11-4.100.139.bin`
- `canopus_stage2-xiaomi-band-11-4.100.139.bin`
- `canopus_supervisor-xiaomi-band-11-4.100.139.bin`
- `canopus_loader_profile-xiaomi-band-11-4.100.155.bin`
- `canopus_stage1-xiaomi-band-11-4.100.155.bin`
- `canopus_stage2-xiaomi-band-11-4.100.155.bin`
- `canopus_supervisor-xiaomi-band-11-4.100.155.bin`

统一 ZIP：`build/canopus-installer-prod-xiaomi-band-11.zip`。默认命令重建两版；
`--target <target-id>` 可只更新一版资源，仍使用统一入口与打包目录。

UI 与 Band 9 一致：同样的页面结构、标题、状态区和 Run / Clear Env 按钮。
分辨率从设备读取。进入页面时恢复 os.execute；不会自动执行 shell 命令或加载模块。
点击 Run 后按步骤显示进度、加载 Supervisor，并通过 /dev/canopus 执行 RESTORE、INSTALL 0/1/2，
注册 Canopus 管理器及其三个原生页面，再发布应用列表入口。Clear Env 需要两次点击。

2026-09-11：进度区显示步骤序号、当前操作和上一项已完成操作；错误会保留失败步骤。
每个步骤先更新文字，经过120ms的LuaLVGL定时器回调后才继续执行，让界面循环有机会刷新。
资源读取/CRC校验/写入回读、MPU与固件核验、加载器准备、图标、注册和清理均有对应文字。
进行操作时禁用两个按钮，离开页面会删除定时器并取消尚未执行的操作。
布局仍为原来的标题、状态区、Run / Clear Env，根层为一个Lua和九个.bin。

两处保持同步：缓存维护至原生exec返回，以及每条Supervisor命令写入至读取结果，
其中不插入UI回调；屏幕保持显示当前步骤，原生调用内部没有伪造子步骤或百分比。
初始os.execute恢复仍在pmain入口同步完成（移到定时器会丢失所需C栈帧），
界面首次可刷新时已完成这一初始化，随后以进度显示检查安装配置和资源。
Lua5.4实C帧测试、延迟绘制/忙碌点击/错误停止/页面销毁测试和加载器跨yield的GC测试已通过。
实际屏幕刷新时序待设备复测；模拟器验证的是事件循环调度和文字更新先于操作。

首次测试先重启。若有旧的 `/data/canopus/registry.bin`，可先双击 Clear Env 再重启，
避免旧模块的启动意图中断此次安装。只接受 `.139` / `.155` 各自匹配的版本和 build；其他身份不给 Run。
成功路径显示 Run completed，然后退出表盘，在应用列表中打开 Canopus 管理器。

失败时保存页面上的错误码，并获取 `/data/canopus/bootstrap-result.txt`、
`bootstrap-exec.txt` 以及 `/dev/canopus` 的 384 字节状态（若节点已存在），重启后再试。
- -302：原生执行上下文或 MPU 属性不匹配。
- -303：Kmem 分配失败；-311/-312：Umem 临时缓冲分配失败。
- -304/-313：stage2 或 Supervisor 文件读取/CRC 校验失败。
- -305：没有可用的 MPU 执行区域。
- -401..-406：ELF 加载、重定位、权限或构造表失败。
- -201..-204：Supervisor 身份/初始化/设备注册失败。
- -65536-n：设备节点打开核验的 errno；-73728-n：注册调用的 errno。

2026-09-10 用户已报告此前版本在真机成功加载，管理器基本功能正常。
随后修复了原生列表和中文字体；**新版 UI 的屏幕效果待复测**，详见
[原生 UI 核验记录](NATIVE_UI_AUDIT.md)。替换新版时先重启，旧Manager驻留时不会被新文件替换。

当前自动验证：真实 Lua 5.4.0 C 栈恢复测试、Lua UI/启动编排测试、ARM Cortex-M33
仿真中的 stage1→stage2→Supervisor→设备节点→INSTALL→Manager 页面创建/生命周期，
以及 ELF verifier。原生LVX列表、样式和字体初始化已执行真实固件指令，
底层VFS、分配器、LVGL样式存储和绘制仍使用模型。当前新构建的整体设备状态仍为NOT_PROBED，
不抹去上述用户对旧构建的实机成功反馈，也不据此声称新UI已验证。

Supervisor 和失败后可能发布了回调的镜像保持驻留，恢复方式为重启。
原生加载不关闭 MPU，不使用未知静态 cave，保守地留出区域7；原生代码原子租用4–6，
一个区域合并保护代码/只读段，Kmem 数据段使用固件已有的特权背景映射。
二次核验直接执行了 `.139` 的 MPU 启动、配置和寄存器保存/恢复代码，权限值与加载器一致。
新增全部已启用区域的重叠检查及 RBAR/RLAR 回读。临时 ELF/工作缓冲改用 Umem，
当前模拟 Kmem 峰值为101376字节。此前“区域7就是调度器栈保护”的说法已纠正：
固件切换上下文时实际使用 MSPLIM/PSPLIM；加载器仍保守保留区域7。
加载器可加载签名校验通过的零导入 ARM ET_REL；通用 Rust SDK 的多数 `.139` 接口仍为
restricted/PENDING，已有 Rust 示例“能编译”不等于其所有设备服务均可用。
未恢复的 watchface 删除接口没有调用，模块安装后可手动删除其安装表盘。

os.execute 恢复在初始 pmain 中重新打开 OS 库，并在清空 execute 之前中断重入。
固件首次创建循环上下文时会弹掉原 root 参数；修复版核对第三槽的错误处理函数地址，
再使用 registry 的 luavgl_key 上下文，并隔离、恢复 dataman.meta，避免覆盖真实 dataman。
每个 Lua 状态最多一次；该中断可能漏掉一次路径 strdup 的释放，尚无设备测量。
本次真机启动报错的固件证据与回归见
`targets/xiaomi-band-11-4.100.139/loader/pmain-startup-fix-2026-09-10.md`。
细节见 `targets/xiaomi-band-11-4.100.139/loader/native-audit.md`。

2026-09-10 再核验补充：真实固件线程初始化会清除 CONTROL.nPRIV；4096 字节
命令栈、应用/页面注册和 Launcher 记录生成均增加了真实 ARM 指令测试。
缓存维护已改用固件固定入口，按 D-cache clean-all → 屏障 → 已启用的 I-cache
clean/invalidate-all → 屏障执行，避免 `mw` 对缓存命令寄存器的额外读取。
这一修改更新了 main.lua 和 loader_profile，必须重新打包完整根目录。
结果见 `targets/xiaomi-band-11-4.100.139/loader/revalidation-2026-09-10.md`。

## Lua 发行格式

根目录 `main.lua` 是标准明文 Lua 5.4 文本入口，由 `scripts/build_band11_installer.py` 从 `src/` 各组件组装生成。所有历史混淆与字节码加密层已全部移除，代码完全开源透明、可审计。开发源仍在 `src/`。
