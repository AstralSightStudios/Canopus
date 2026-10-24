# `canopus` 命令行指南

`canopus` 负责创建、构建、校验、打包模块工程，以及管理 target 包与生成物。源码在 `cli/canopus`，核心逻辑在 `tools/canopus-core`。

```bash
cargo install canopus-cli           # 或在仓库里 cargo build -p canopus-cli / cargo install --path cli/canopus
canopus sdk update                  # 下载 SDK、模板与 target 包（仓库 main 的最新 commit）
canopus doctor                      # 检查工具链、SDK 与 target
```

只支持 macOS 与 Linux（Windows 请在 WSL2 里用，原生 Windows 编译时直接报错）。工具链：macOS `brew install llvm lld`；
Debian/Ubuntu `apt install clang lld llvm`（Fedora/Arch 同名包）；Rust 模块另需 `rustup target add thumbv8m.main-none-eabi`。

## SDK 与 target 包

所有命令都需要 Canopus 仓库里的 `sdk/`、`templates/` 与 `targets/`。查找顺序：`$CANOPUS_ROOT` → 当前目录及其祖先 →
可执行文件及其祖先 → `canopus sdk update` 下载的快照（`~/.canopus/sdk/current`，`$CANOPUS_HOME` 可改根目录）→
编译这个二进制时所在的仓库（`cargo install --path`）。`canopus doctor` 与 `canopus sdk status` 会打印实际用到的是哪个。

```bash
canopus sdk update             # 切到 main 的最新 commit，打印新增/修订/移除的 target
canopus sdk update --rev <x>   # 切到某个分支、tag 或 commit（回退用）
canopus sdk status             # 在用的 SDK、快照 commit、main 领先几个 commit、CLI 是否有新版
```

- 快照是整个仓库在该 commit 的内容（约 2.4 MB 下载，需要 `curl`），下载到 `~/.canopus/sdk/<commit>/` 后原子切换 `current`，
  旧快照随即删除。Rust 工程的路径依赖写的是 `current/sdk/rust`，更新后自动跟上。
- 兼容闸门：快照里的 `sdk/sdk.toml` 写着 `min_cli`，比当前 CLI 新就拒绝切换（保留旧快照）并提示 `cargo install canopus-cli`。
  改了 CLI 解析的格式（`target.toml`、`Canopus.toml`、模板约定）时要同时提高 `min_cli` 与 CLI 版本。
- 在快照上运行 `new` / `build` / `package` 时每天最多检查一次：main 有新 commit 或 crates.io 有新版 CLI 就在 stderr 提示一行，
  从不自动更新（target 包变化会改变构建与校验结果），离线时静默跳过。
- `canopus package` 把 CLI 版本与 SDK commit 写进包 manifest 的 `reproducibility.build_environment`。

`--targets-dir` 缺省为 `<仓库>/targets`；所有接受 target 的参数（`--target`、`target info <id>`、`verify --target`……）既可写完整 id，
也可写唯一的版本后缀：`155` 即 `xiaomi-band-11-4.100.155`，`043` 即 `xiaomi-band-10-pro-3.101.043`。重名会报歧义并列出候选，不存在会列出已注册的 target。

## 模块工程

```bash
canopus new hello                          # portable C 模块：只编译一次，对所有 Band 11 固件通用（Band 10 Pro 须用 --target 绑定）
canopus new hello --target 155             # 绑定精确固件；--target 可重复
canopus new hello --target 036 --target 043 --ui       # 带一个 UI 页面的模板（C）
canopus new hello --lang rust                          # Rust 模块（portable 或 --target 绑定都行）
canopus new hello --template rust-ui                   # 按名字选模板；canopus new --list 列出全部

cd hello
canopus build                  # 编译、链接并过 verifier；target 模块按 [targets].include 逐个构建
canopus build --target 155     # 只构建一个
canopus package                # 写 dist/<id>-<version>.canopus（每个 target 一份 CMI2 收据）
canopus package --build        # 先构建再打包
canopus package --installer    # 另外写 dist/installer/<设备族>/（旧的安装器表盘流程）
canopus clean                  # 删除 build/ 与 dist/
```

- 模板在 SDK 的 `templates/<name>/`（约定见 `templates/README.md`），随 SDK commit 更新：`c`、`c-ui`、`rust`、`rust-ui`。
  `--lang` / `--ui` 是选模板的简写（`<lang>` 或 `<lang>-ui`），不能和 `--template` 同时用。
- `new` 先校验再创建：模块 id、模板、target 有误时不会留下任何目录。模块 id 需匹配 `^[a-z][a-z0-9_]{0,15}$`（ADR-CAN-003）。
  工程里有 `Canopus.toml`、`src/<id>.c`（Rust 为 `Cargo.toml` 与 `src/lib.rs`）和 `.gitignore`。
  Rust 工程建在仓库内时 SDK 依赖写相对路径，建在别处时写绝对路径；`Cargo.toml` 自带空的 `[workspace]`，建在仓库里也不会被当成仓库 workspace 的成员。
- `-ui` 模板注册一个页面入口（计数与开关），在 Manager 的模块详情页以「打开界面」出现；UI 头文件 `canopus_ui.h` 在 `sdk/c`，不需要额外的 include 目录。页面 API 见 `canopus-services-specification.md` §2.12，完整示例在
  `modules/examples/ui-demo` 与 `ui-gallery`。Rust 侧的服务对象、`services_has` 与 UI props 在 `canopus-abi`。
- `package` 会拒绝比源码旧的 ELF（工程里任何 `.c/.h/.rs/.toml/.ld/.s` 比 ELF 新就报错），避免改了代码忘了 `build` 而打出旧包。
  工程目录之外的头文件（`include_dirs` 指向别处）不在检查范围内。
- 旧的 `Canopus.toml` 若还写着 `${CANOPUS_ROOT}/app-sdk/...`（该目录已并入 `sdk/c` 与 `runtime/`），`build` 会忽略不存在的 include 目录、把 `canopus_ui.c` / `canopus_app.c` 的旧路径指向新位置，并打印警告，请尽快改掉。
- 构建用的工具来自环境变量：`CC`（默认 `clang`）、`LD`（默认 `ld.lld`）、`AR`（默认 `llvm-ar`）、`CARGO`（默认 `cargo`）。缺工具时报错会说明是哪一步、缺哪个；
  编译器自己的输出原样保留，`-v` 在前面打印完整命令行。
- 模块按 `-Wall -Wextra` 编译；`CANOPUS_WERROR=1` 再加 `-Werror`（`scripts/ci.sh` 会设，仓库里的示例与模板必须零警告）。
  默认不加是因为更新的 clang 会引入新警告，不应让用户的模块因此编不过。

## target、校验与生成物

维护者命令（`target generate`/`validate`、`build-plan`、`symbol`、`type`、`evidence`、`module`、`re`、`emu`、`package build`/`validate`）
不出现在 `--help` 里，用法不变。

```bash
canopus target list [--active] [--names]      # 已注册的 target（--names 只打印 id，脚本用）
canopus target info 155 [--json|--shell]      # 单个 target 及其设备族构建数据
canopus target generate 155 | --all [--check] # 重新生成 veneer、配置头、Rust 绑定、Band 11 原生地址头；--check 只检查是否过期
canopus verify module.elf --target 155        # 对 target 包校验 ELF；--portable 校验 portable 模块
canopus build-plan Canopus.toml               # 展开模块的 target 矩阵
canopus package verify x.canopus              # 校验包结构与哈希
```

改了 `targets/*/symbols/*.json` 必须重新 `target generate` 并一起提交生成物（见 AGENTS.md）。

## 退出码

成功为 0，任何错误为 1，错误信息前缀 `error:`，原因链用 `: ` 连接。`verify` 校验不通过也是 1；`canopus emu smoke` 缺 Mirage 或固件时退出码 77（跳过）。
