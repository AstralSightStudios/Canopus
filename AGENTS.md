# Canopus 协作须知

重构计划与进度唯一来源：`docs/refactor-plan.md`。能力状态（host implemented / static recovered /
device verified / production approved）以 `docs/progress.md` 与 `docs/native-manager-ui-plan.md` §15 为准，
重构不得提升任何能力状态。

## 目录
- `cli/`、`tools/canopus-core/`：Rust CLI 与核心库（target 生成器、verifier、package、re）
- `targets/<target_id>/`：每个固件一个目录（`target.toml`、`symbols/*.json`、`evidence/`、`generated/`）
- `sdk/rust/`：独立的 Rust SDK workspace；`runtime/`、`manager/`：C 运行时与设备端 Supervisor/Manager
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
