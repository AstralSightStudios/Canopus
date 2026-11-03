# Windows + WSL2 的 Mirage 开发流程

入口是 `scripts/emu/mirage.ps1`：Mirage/QEMU 原生运行于 Windows，Canopus CLI、模块与
Supervisor 在 WSL2 中构建。模拟器结果只算 host/emulator 证据，不提升真机能力或批准状态。

## 环境

仓库按同级目录放置：

```text
D:\Canopus\
  Canopus\
  Mirage\
  AstroBox-NG\
  Facetory\
  .tools\mirage\
    swift\       Swift 6.1 工具链与运行库（回退）
    swift-gui\   Swift 6.3 工具链与运行库（GUI/CLI 优先共用）
    qemu-fixed\  修复关机后的 QEMU 与依赖 DLL
```

需要 MSVC Build Tools、Windows SDK、Python、Rust，以及已有的 Ubuntu 24.04 WSL2。
Windows 入口优先为 GUI 与 CLI 共用 Swift 6.3，避免测试重建 CLI 后加载旧版运行库而报入口点缺失。
便携 Swift 根目录内含 `Toolchains/` 和 `Platforms/`；也可通过 `SWIFT_ROOT` 指定安装位置。
AstroBox-NG 用 `repos.xml` 管理 25 个独立组件仓库，clone 主仓库后还需用其 `abtools` 同步组件。
Mirage Bridge 引用同级 AstroBox-NG 的 `core`、`bluetooth`、`pb`，不能只检出主仓库。

固件通过 Mirage 导入完整 OTA；固件、设备资料库、绑定凭据和构建产物均不提交。
Windows 开发资料库默认 `.local/mirage/Library`；冒烟设备放在独立资料库，结束后删除。
脚本不修改 Mirage 的用户资料库设置，macOS/Linux 旧脚本仍由 Mirage 选择资料库。

## 常用命令

在 Canopus 目录的 PowerShell 执行：

```powershell
.\scripts\emu\mirage.ps1 doctor --full
.\scripts\emu\mirage.ps1 canopus doctor

# 原生 Windows 工具与 GUI
.\scripts\emu\mirage.ps1 build
.\scripts\emu\mirage.ps1 facetory-build
.\scripts\emu\mirage.ps1 companion-build
.\scripts\emu\mirage.ps1 gui-build
.\scripts\emu\mirage.ps1 gui

# .043 固件启动、截图、NSH、QEMU 退出与 NOR 写回
.\scripts\emu\mirage.ps1 boot-smoke --firmware-source PATH_TO_FULL_OTA
.\scripts\emu\mirage.ps1 screenshot DEVICE .local/mirage/screen.png --wake

# WSL 构建后，在 Windows 上检查离线 inbox / NSH 模块流程
.\scripts\emu\mirage.ps1 supervisor xiaomi-band-10-pro-3.101.043 --headless --min-size
.\scripts\emu\mirage.ps1 canopus package --build --manifest modules/examples/emu-probe/Canopus.toml
.\scripts\emu\mirage.ps1 module-smoke --package modules/examples/emu-probe/dist/emu_probe-1.0.0.canopus

# 恢复生产 Supervisor 后，检查生产安装器与加密蓝牙投递
.\scripts\emu\mirage.ps1 supervisor xiaomi-band-10-pro-3.101.043 --min-size
.\scripts\emu\mirage.ps1 companion-smoke
```

GUI 也可通过本机 `D:\Canopus\Start-Mirage.cmd` 打开。选择设备后 Boot，双击屏幕唤醒，
拖动模拟滑动；Apps 页可连接 companion。关闭 GUI 后 QEMU 仍可运行，关机用 Shut Down。
只需设备预览时可用 `gui-build --preview` 禁用 companion。
GUI 打开后才由 CLI 开机时，点击 Boot 接管现有进程。旧会话退出仅清理自身 PID 的
运行记录，避免交替重启时误删新记录。

Facetory 打包器保留默认 feature：当前生成绑定无条件引用 MCP 类型，禁用默认 feature 会编译失败。
`companion-build` 复用 Mirage 的 Rust 构建脚本，并按 Rust 输出的 native-static-libs 链接 Windows
命令行客户端；请求格式与上游 `smoke` 一致（`method=JSON` / `--stdin`）。

`module-smoke` 复用既有 .043 用例，验证安装、启用、重启与 restore，不经过蓝牙。
`companion-smoke` 使用生产 staging 和已构建的 `emu_probe` 包，在每次运行的独立资料库中完成
表盘打包/安装 → Run 加载 Supervisor → 加密 CPC2 安装/启用 → 冷启动后再次 Run → ACTIVE 与探针检查。
恢复依赖重新加载 Supervisor，不代表开机固化。截图、日志与 `run.json` 留在 `.local/mirage/runs/`。

`--min-size` 显式传入 `-Oz`，不改变 target 体积预算。Canopus `af4f9b30` 的生产构建为
120,892 / 133,120 B，通过 ELF verifier（165,960 B resident、0 undefined）；默认 `-Os` 构建超过门槛。
Supervisor 构建会同步 staging，切换测试配置后应恢复生产配置。

## 桌面分发

Windows 上游 CI 用 `swift build -c release --product Mirage` 构建，将 `Mirage.exe`、
`.resources`、Swift DLL、`qemu/` 与 Windows App Runtime 1.5 安装器一起分发。
EXE 需要同目录依赖，不能单独复制；仓库没有 MSI/MSIX 或其他安装器工程。
可在这个目录包外再制作安装器。

本机便携包为 `.local/mirage/dist/Mirage-windows-x64-local.zip`，解压后运行 `Mirage.exe`。
这是含本地兼容补丁的 Debug 构建，未签名；附带补丁与许可证，不含固件、设备资料库或配对凭据。
清除开发工具 PATH、`SWIFT_ROOT` 与 `MIRAGE_QEMU` 后，已验证 GUI 启动、包内 QEMU 启动设备及
GUI 选择模块包安装。运行不需要 WSL 或 Swift SDK；Canopus 构建仍用 WSL。
Windows App Runtime 1.5 需已安装，包内附官方安装器；尚未在全新 Windows 机器验证。

macOS 使用 `App/` 中的原生 SwiftUI 前端，CI 输出 `Mirage-macOS.zip`，解压后打开 `Mirage.app`。
当前 `project.yml`、Xcode 工程和 CI 要求 macOS 26；README 的 macOS 14+ 要求已与工程不一致。
源码构建需 Xcode、Rust，以及 `brew install glib pixman meson ninja pkgconf`：

```sh
git submodule update --init --recursive
make qemu
xcodebuild -project Mirage.xcodeproj -scheme Mirage -configuration Release \
  -derivedDataPath .build/xcode -destination 'platform=macOS,arch=arm64' \
  ARCHS=arm64 CODE_SIGN_IDENTITY=- CODE_SIGN_STYLE=Manual DEVELOPMENT_TEAM= build
open .build/xcode/Build/Products/Release/Mirage.app
```

构建脚本自动处理 Rust companion，将 QEMU 与非系统 dylib 放进 `.app` 并做本地签名。
以上 macOS 流程依据工程和 CI，未在本机实测。

## 环境变量

工具发现顺序为显式环境变量、同级仓库构建产物、PATH。

| 变量 | 用途 / 默认值 |
|---|---|
| `MIRAGE_ROOT` | 同级 `Mirage` 仓库 |
| `MIRAGECTL` | 控制工具路径，也可用 PATH 命令名 |
| `MIRAGE_QEMU` | Windows 优先 `.tools/mirage/qemu-fixed/`，其次 `qemu/` |
| `SWIFT_ROOT` | Windows Swift 根目录；显式指定时，匹配的运行库须在 PATH 中 |
| `CANOPUS_WSL_DISTRO` | `Ubuntu-24.04` |
| `CANOPUS_EMU_HOME` | `.local/mirage`，存放 Windows 资料库、日志和运行报告 |
| `MIRAGE_LIBRARY` | Windows 默认 `CANOPUS_EMU_HOME/Library`；显式设置优先 |
| `CANOPUS_FW_OTA_DIR` | .043 解包固件目录，默认库中的已导入固件 |
| `MAB_SMOKE` | AstroBox 命令行桥 |
| `FACETORY_PACKAGER` | Facetory Lua 表盘打包器 |
| `CANOPUS_BIN` | 旧脚本使用的 Canopus CLI；Windows 构建用 WSL 入口 |

`boot-smoke` / `module-smoke` 使用 `CANOPUS_EMU_HOME/BootSmokeLibrary`。
`boot-smoke --keep-device` 保留运行中设备并跳过关机检查；检查它时需显式设置 `MIRAGE_LIBRARY`。

## 兼容补丁

- `mirage-windows-gui.patch`：Windows 可选 companion、Swift 6.3 依赖约束与栈空间，macOS 文件 API
  条件编译，以及 WinUI 画面对象生命周期与触摸事件排序；支持按官方 `<hash>_upd_<product>`
  文件名匹配 AP 镜像中的产品。锁文件保留原有格式和上游 URL。
- `swift-winui-runtime.patch`：WinUI 主线程 STA；仅在 bootstrapper 返回包族错误 `0x80670016` 时，
  用 Windows 原生动态依赖 API 加载对应 runtime，保留原有安装与错误处理流程。
- `swift-cross-ui-windows.patch`：每个窗口分别创建菜单控件，避免打开 Firmware 等窗口时，
  同一个菜单项被挂到多个菜单栏而导致 WinUI 闪退；导入结果提示临时隐藏并恢复新建表单，
  避免同一窗口同时打开两个 ContentDialog。
- `qemu-nor-shutdown.patch`：shutdown notifier 的 `opaque` 是 `ShutdownCause`，应从 notifier
  所属结构取得设备。旧实现会在退出时崩溃，影响 NOR 写回。

`gui-build` 使用锁文件建立本地浅镜像，解析依赖后应用补丁；已应用时可重复构建。
Windows 的输出捕获使用临时文件，避免后台 QEMU 继承管道后使 Python 一直等待 EOF。
双击唤醒使用同一 QMP 连接，截图检查实际 RGB 像素，避免把黑屏当成成功。

QEMU 在 MSYS2 UCRT64 shell 中按 Mirage 的依赖要求构建：

```sh
sh /d/Canopus/Canopus/scripts/emu/build-qemu-windows.sh
```

脚本复用上游构建流程，将产物放入 `.tools/mirage/qemu-fixed`。
冒烟检查实际 QEMU 退出码和 NOR 写回，不能仅凭 `miragectl shutdown` 成功判断正常退出。

## 验证记录（2026-11-01）

本机基于 Mirage `4da71b80`、QEMU 子模块 `42b97e2c`、AstroBox-NG `5b4742bb`、Facetory `d8e17415`。

- CLI 构建、帮助、型号/固件/设备查询与独立 .043 设备的启动、截图、NSH、退出码 0、NOR 写回通过：
  `.local/mirage/cli-retest.json`。`qemu` 诊断的 `boards` 列表仍为空，实际 `profiles` 查询正常。
- Canopus 分支 `ds-newdevice-supports` 已同步至 `af4f9b30`；重建 CLI、生产 Supervisor、
  `emu_probe` 与 `ui_demo`，两个模块包的全部 target 均通过 verifier。
- 最新生产安装器、加密 CPC2、冷启动后 ACTIVE（state=9、activationError=0）、实际 `probe.txt=1`：
  `.local/mirage/runs/af4f9b30-companion/run.json`；两次 QEMU 退出码 0、NOR 写回，测试设备保留。
- 最新页面返回回归 12 项、timer pump 回归 3 项实际通过，无 skip：
  `.local/mirage/host-af4f9b30-tests.log`。
- 便携 GUI 的 Apps → Choose 文件选择安装 `ui_demo` 通过；独立 AstroBox 查询确认 state=3、
  activationError=0，原 `emu_probe` 仍 ACTIVE。复用同一隔离设备已建立的配对，记录
  `.local/mirage/portable-gui-install.json` 与 `portable-gui-modules.log`。
  恢复运行记录竞争后，重启、重新 Run，再经 AstroBox 启用/激活 GUI 安装的模块，两个模块均为
  ACTIVE（state=9、activationError=0）：`.local/mirage/portable-recovered/run.json`。
- `ui_demo` 已实际完成启用、激活、开关、三次计数、详情页、返回与关闭，最终 state=9、
  activationError=0；严格 UI 回归未通过：探针在交互前连续记录两次 `open 0/0/0`，
  清空探针并冷启动后仍复现，其余事件与预期一致。证据
  `.local/mirage/af4f9b30-ui/clean-ui/run.json`、`.local/mirage/ui-clean.log`。
  页面操作使用持续 QMP 连接；原 CLI 逐次触摸脚本还遇到导航误入录音机与黑屏，不能报全绿。
- GUI companion 配对、设备信息与表盘列表读取通过；双击唤醒、上滑到列表、点击应用、右滑返回通过，
  复查记录 `.local/mirage/gui-review-20261101.json`，构建日志 `.local/mirage/gui-build-review-final.log`。
- 修复 WinUI 复用菜单项造成的 `Element is already the child of another element` 闪退。
  Firmware 产品/版本列表、筛选、导入文件选择器打开与取消、窗口关闭重开，以及主窗口、Firmware、
  Profiles 同时打开通过；便携版目录刷新与菜单展开通过，持续打开 120 秒仍正常响应。构建日志
  `.local/mirage/archive-menu-fix-build.log`，验证记录 `.local/mirage/archive-menu-verification.json`。
- 修复新建表单内导入结束时的 `Only a single ContentDialog can be open at any time` 闪退。
  原名 q65 `4.100.087` ZIP 导入、成功提示关闭后的表单恢复、创建设备与启动至操作引导通过，
  5 个导入镜像与原 ZIP 的 SHA-256 一致；便携版错误导入提示与表单恢复通过。
  q65/q65gl 官方命名回归 2 项通过，记录 `.local/mirage/firmware-import-verification.json`，
  构建日志 `.local/mirage/import-dialog-fix-build.log`。

复查通过 Python/PowerShell/shell 语法、补丁应用检查、缺依赖退出码 77 和启动前失败清理；
WSL 确认非 Windows 导入不覆盖 `MIRAGE_LIBRARY`。AstroBox 全部组件与 Facetory 无代码改动。
再次复查去除 WinUI 补丁的换行差异，收紧官方包名识别并恢复 Windows 空白截图检查；
GUI 重建、包名回归、RGB/RGBA 黑屏与空白页检查通过，记录 `.local/mirage/review-final.json`。

UNIX 对照验证使用 WSL2 / Ubuntu 24.04、Swift 6.3：原版 MirageKit 14 项、修改版 15 项测试通过，
包含依赖补丁的 GTK GUI、Linux CLI 与 QEMU 构建通过；GUI 在 Xvfb/Cairo 下启动 15 秒，独立 .043
设备完成开机、截图、NSH、触摸、退出码 0 与 NOR 写回。Canopus `cargo check` 与 `doctor` 通过。
原版 GTK 构建复现了 macOS 专用文件 API 报错，修改版条件编译后通过。macOS 目标语法解析、
非 Windows 分支与包声明对比通过；未执行 Xcode 构建或 macOS 实机运行。
记录 `.local/mirage/unix-verification.json`，不代表所有 UNIX 系统、工具链版本或真机均已验证。

便携 GUI 部分启动会出现 Tokio I/O driver `ERROR_ABANDONED_WAIT_0` panic；本次固件窗口验证时
再次出现，但窗口操作正常，companion 原因尚未定位。未验证真机蓝牙或长期 GUI 稳定性。
