# Canopus 协作须知

重构计划与进度唯一来源：`docs/refactor-plan.md`。能力状态（host implemented / static recovered /
device verified / production approved）以 `docs/progress.md` 与 `docs/native-manager-ui-plan.md` §15 为准，
重构不得提升任何能力状态。

## 目录
- `cli/`、`tools/canopus-core/`：Rust CLI 与核心库（target 生成器、verifier、package、re）
- `targets/<target_id>/`：每个固件一个目录（`target.toml`、`symbols/*.json`、`evidence/`、`generated/`）
- `sdk/`：公开 SDK（`sdk/c` 头文件是模块作者的唯一 include 目录；`sdk/rust/` 是独立的 Rust SDK workspace）；`runtime/`、`manager/`：C 运行时实现与设备端 Supervisor/Manager（原 `app-sdk/` 已并入 `sdk/c` 与 `runtime/`）
- `templates/`：`canopus new` 的模板（随 SDK commit 分发，约定见 `templates/README.md`）
- `modules/`、`watchfaces/`、`scripts/`、`tests/host/`：示例模块、安装器表盘、构建脚本、C host 测试

## 常用命令
- 全量 CI：`scripts/ci.sh`；不中断并输出红/绿表：`CANOPUS_CI_KEEP_GOING=1 scripts/ci.sh`
- 构建 Supervisor：`CANOPUS_TARGET=<id> sh scripts/build_canopus_supervisor.sh`
- 重新生成 target 产物：`canopus target generate <id>|--all`（`--check` 只检查是否过期）
- 模块工程：`canopus new <id> [--target 155] [--ui]`、`canopus build`、`canopus package`、`canopus clean`；工具链自检 `canopus doctor`（详见 `docs/cli.md`，示例 `modules/examples/hello-portable`）
- 模拟器端到端（需 Mirage + 固件包，约 5 分钟）：`python3 scripts/emu/smoke.py`（缺环境时退出码 77）
- 基线哈希：`scripts/baseline-hashes.sh`（输出对比 `docs/baseline/hashes.txt`）

## 规则
- **改 `targets/*/symbols/*.json` 后必须重新生成**并一起提交生成物；`generated_stability` 测试会检查过期。
- 不可触碰：FORBIDDEN / CANDIDATE / restricted 状态不得擅自提升为 APPROVED；批准须有 evidence 与用户授权
  （例如 `.139` 已授权清单钉死在 `generated_stability.rs` 的 `APPROVED_139_PRIVATE`）。
- 证据门槛：假设 → 静态证据 → ABI 审核 → host 测试 → 设备探针 → 设备验证；模拟器结果只算 host/emulator 级证据。
- 固件镜像（`fwbins/`、`*.bin`）不得提交；缺固件的测试应显式跳过。
- 不做防御性编程，不写冗余测试。

## 发布与升级顺序
CLI 已发布到 crates.io（`canopus-cli` 及 core/elf/package/re 四个库，其余 crate `publish = false`；已发布的版本删不掉，只能 yank）。
装好的 CLI 运行 `canopus sdk update` 会切到 **main 的最新 commit**，所以 push 到 main 就等于发给所有用户。按改动类型选顺序：

1. **旧 CLI 照样能读的 SDK 改动**（新增 target、改 symbols 后重新生成、改 `sdk/`/`runtime/` 源码、新增符合现有约定的模板）：直接 push，不动版本。
2. **只改 CLI**：提版本 → commit + push → 发布。
3. **新 CLI 依赖 SDK 里新增的东西、旧 CLI 会忽略它**（新目录、新文件）：先 push 这部分 SDK 内容，再提版本发布 CLI。
   反了的话，新装的 CLI 拿到的快照里还没有它要的东西。`min_cli` 不动。
4. **旧 CLI 读不懂的格式变更**（`target.toml`、`Canopus.toml`、模板约定、`sdk/sdk.toml` 本身的破坏性变化）：
   1. 先让 CLI 新旧两种格式都能读，提版本，commit + push（这时 main 还是旧格式，什么都不会坏）；
   2. 发布这个版本的 CLI；
   3. 最后在同一个 push 里提交格式变更，并把 `sdk/sdk.toml` 的 `min_cli` 提到刚发布的版本。

   顺序反了的后果：先 push 格式变更并提 `min_cli`，旧 CLI 被挡住、提示 `cargo install canopus-cli`，可 crates.io 上还没有新版，用户无路可走；
   不提 `min_cli`，旧 CLI 会切到读不懂的快照，直接报错。新 CLI 实在没法兼容旧格式时，2 和 3 之间要紧挨着做。

发布步骤：版本号要改两处，`[workspace.package] version` 和 `[workspace.dependencies]` 里 canopus-core/elf/package/re 的 `version`；
漏了后者，内部依赖的版本要求就对不上，发布会失败。然后先 dry-run，再正式发布：
`cargo publish [--dry-run] -p canopus-core -p canopus-elf -p canopus-package -p canopus-re -p canopus-cli`。
要发布的 commit 必须已经 push 到 GitHub：crate 里记着它的 commit。
发布后做一次干净测试：新的 `CARGO_HOME`/`CANOPUS_HOME` 下 `cargo install canopus-cli && canopus sdk update && canopus doctor`，
再把每个模板按 portable 和 `--target 043 --target 155` 各建一次，`canopus package --build` 加 `package verify` 都要通过。
