# Xiaomi Band 11 4.100.139 实机测试包

打包目录为 `watchfaces/canopus-installer-prod/xiaomi-band-11/`。根层严格只有
`main.lua` 和五个 `.bin` 文件；`src/`、`docs/`、`build/` 都不加入 watchface。
沿用 Band 9 的打包/安装方式，**不要把资源 ZIP 当作已封装的 watchface 固件刷入**。

```sh
CANOPUS_TARGET=xiaomi-band-11-4.100.139 scripts/build_canopus_supervisor.sh
```

根层资源：

- `main.lua`
- `manager_icon.bin`
- `canopus_loader_profile-xiaomi-band-11-4.100.139.bin`
- `canopus_stage1-xiaomi-band-11-4.100.139.bin`
- `canopus_stage2-xiaomi-band-11-4.100.139.bin`
- `canopus_supervisor-xiaomi-band-11-4.100.139.bin`

UI 与 Band 9 一致：同样的页面结构、标题、状态区和 Run / Clear Env 按钮。
分辨率从设备读取。进入页面时恢复 os.execute；不会自动执行 shell 命令或加载模块。
点击 Run 后同步加载 Supervisor，并通过 /dev/canopus 执行 RESTORE、INSTALL 0/1/2，
注册 Canopus 管理器及其三个原生页面，再发布应用列表入口。Clear Env 需要两次点击。

首次测试先重启。若有旧的 `/data/canopus/registry.bin`，可先双击 Clear Env 再重启，
避免旧模块的启动意图中断此次安装。只支持准确的 `.139` 版本和 build；其他身份不给 Run。
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
