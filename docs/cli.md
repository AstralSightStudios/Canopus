# `canopus` 命令行指南

`canopus` 负责创建、构建、校验、打包模块工程，以及管理 target 包与生成物。源码在 `cli/canopus`，核心逻辑在 `tools/canopus-core`。

```bash
cargo build -p canopus-cli          # 产物：target/debug/canopus；也可 cargo install --path cli/canopus
canopus doctor                      # 检查工具链、仓库与 target
```

## 仓库与 target 的定位

所有命令都需要 Canopus 仓库里的 `sdk/c` 与 `targets/`。查找顺序：`$CANOPUS_ROOT` → 当前目录及其祖先 → 可执行文件及其祖先 →
编译这个二进制时所在的仓库（所以 `cargo install` 装出来的 `canopus` 在任何目录都能用）。`canopus doctor` 会打印实际用到的仓库。

`--targets-dir` 缺省为 `<仓库>/targets`；所有接受 target 的参数（`--target`、`target info <id>`、`verify --target`……）既可写完整 id，
也可写唯一的版本后缀：`155` 即 `xiaomi-band-11-4.100.155`，`043` 即 `xiaomi-band-10-pro-3.101.043`。重名会报歧义并列出候选，不存在会列出已注册的 target。

## 模块工程

```bash
canopus new hello                          # portable C 模块：只编译一次，对所有 target 通用
canopus new hello --target 155             # 绑定精确固件；--target 可重复
canopus new hello --target 036 --target 043 --ui       # 带一个 UI 页面的模板（C）
canopus new hello --lang rust --target 155             # Rust 模块（必须绑定 target）

cd hello
canopus build                  # 编译、链接并过 verifier；target 模块按 [targets].include 逐个构建
canopus build --target 155     # 只构建一个
canopus package                # 写 dist/<id>-<version>.canopus（每个 target 一份 CMI2 收据）
canopus package --build        # 先构建再打包
canopus package --installer    # 另外写 dist/installer/<设备族>/（旧的安装器表盘流程）
canopus clean                  # 删除 build/ 与 dist/
```

- `new` 先校验再创建：模块 id、语言、target 有误时不会留下任何目录。模块 id 需匹配 `^[a-z][a-z0-9_]{0,15}$`（ADR-CAN-003）。
  工程里有 `Canopus.toml`、`src/<id>.c`（Rust 为 `Cargo.toml` 与 `src/lib.rs`）和 `.gitignore`。
  Rust 工程建在仓库内时 SDK 依赖写相对路径，建在别处时写绝对路径。
- `--ui` 模板注册一个页面入口（计数与开关），在 Manager 的模块详情页以「打开界面」出现；UI 头文件 `canopus_ui.h` 在 `sdk/c`，不需要额外的 include 目录。页面 API 见 `canopus-services-specification.md` §2.12，完整示例在
  `modules/examples/ui-demo` 与 `ui-gallery`。
- `package` 会拒绝比源码旧的 ELF（工程里任何 `.c/.h/.rs/.toml/.ld/.s` 比 ELF 新就报错），避免改了代码忘了 `build` 而打出旧包。
  工程目录之外的头文件（`include_dirs` 指向别处）不在检查范围内。
- 旧的 `Canopus.toml` 若还写着 `${CANOPUS_ROOT}/app-sdk/...`（该目录已并入 `sdk/c` 与 `runtime/`），`build` 会忽略不存在的 include 目录、把 `canopus_ui.c` / `canopus_app.c` 的旧路径指向新位置，并打印警告，请尽快改掉。
- 构建用的工具来自环境变量：`CC`（默认 `clang`）、`LD`（默认 `ld.lld`）、`CARGO`（默认 `cargo`）。缺工具时报错会说明是哪一步、缺哪个；
  编译器自己的输出原样保留，`-v` 在前面打印完整命令行。

## target、校验与生成物

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
