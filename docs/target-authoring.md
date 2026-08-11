# Canopus 新 Target 适配手册（面向 AI Agent）

> 文档对象：负责分析、实现、审查和验证 Canopus exact-target 支持的 AI Agent。
> 本文描述当前仓库已经落地的架构和工作流；宏观设计见 [architecture.md](architecture.md)，证据规则见 [symbol-evidence.md](symbol-evidence.md)。
> **核心原则：一个 `target_id` 只对应一个确定的固件；相同型号的不同固件也必须是独立 target。**

## 1. Agent 的任务目标

适配一个 target，不是“让代码编译”，而是建立一条可审计链路：

```text
精确固件身份
→ 只读逆向证据
→ target/symbol/type records
→ 自动生成 Rust/C 机械绑定
→ 手写 target-private 语义适配
→ loader 分支
→ supervisor / Manager / module 构建
→ ELF verifier
→ 主机与静态 gates
→ 按风险递增的设备 gates
```

最终应满足：

1. 模块源码不包含该固件的绝对地址；
2. `canopus-target-private` 不拥有固件地址，只组合 generated primitives；
3. C veneer、Rust bindings、C target config 都从 target records 生成；
4. 每个构建产物绑定唯一 target、固件版本、build string 和 SHA-256；
5. stock `insmod` 和 custom loader 是两个明确的 loader profile，不互相兜底；
6. 静态恢复、构建通过和实机证明必须分别表述；
7. 无证据的能力保持 unavailable/fail-closed，而不是复制相似 target 的实现。

## 2. 不可违反的约束

### 2.1 禁止跨 target 推断地址

以下做法全部禁止：

- 把 `xiaomi-band-10-pro-3.101.030` 的地址复制到 `.036`；
- 因为两个函数反编译结果相似，就假设其全局变量或 callback slot 相同；
- 用 Band 10 的地址、对象布局或线程模型推断 Band 9；
- 用相邻固件中的固定 offset 修正所有地址；
- 把 byte pattern 命中直接升级为可调用 symbol；
- 在设备运行时扫描“最像的函数”并跳转执行。

允许使用旧 target 的内容仅限于**定位线索**：

- 独有字符串；
- callers/callees 形状；
- 常量组合；
- CFG 和数据流特征；
- 语义问题清单；
- 已知 ABI 的验证项目。

候选地址必须在新固件中独立验证并写入新 target 的 evidence/symbol records。

### 2.2 不得伪造证据等级

Agent 必须区分：

| 表述 | 实际含义 |
|---|---|
| 静态恢复 | 反编译、xref、调用图或数据流支持结论 |
| 主机测试通过 | 与固件无关的逻辑、格式、状态机通过测试 |
| target build PASS | ARM 目标编译/链接成功 |
| verifier PASS | ELF 结构、重定位、绝对地址等通过静态检查 |
| 设备探针通过 | 最小操作在精确设备固件上执行过 |
| 设备证明 | 功能、错误路径、生命周期和恢复边界均有设备证据 |

禁止把以下结果描述为“设备可用”或“万无一失”：

- IDA 反编译正确；
- Rust/C 编译成功；
- verifier PASS；
- 同型号另一固件实机通过；
- 模拟器或 host fake 通过。

### 2.3 禁止在手写代码中重新引入固件地址

普通 target backend 中禁止：

```rust
core::mem::transmute(0x0C...)
canopus_thumb_callable(0x0C...)
core::ptr::read_volatile(0x20... as *const ...)
```

正确形式：

```rust
core::mem::transmute(
    canopus_target_generated::CANOPUS_FW_BT_BUFFER_NEW_CALLABLE,
)

core::ptr::read_volatile(
    canopus_target_generated::canopus_fw_bt_l2cap_owner
        as *const *mut core::ffi::c_void,
)
```

协议值、ABI offset 和 ARM 架构寄存器不是固件 symbol，可在有说明时保留，例如：

- AVDTP PSM `0x0019`；
- callback slot `5`；
- struct 字段 offset；
- MPU `RNR/RBAR/RLAR` 系统寄存器；
- NuttX open flags；
- protocol magic 和 command ID。

不要用“十六进制常量都要生成”这种机械规则替代语义分类。

## 3. 当前权威目录与职责

```text
targets/<target-id>/
├── target.toml                  target 身份、loader、capability、重定位
├── evidence/                    结论所依赖的证据 bundle
├── symbols/                     函数、全局地址、prototype、policy、状态
├── types/                       精确布局、alignment、字段 offset
├── generated/
│   ├── canopus_veneer.h         自动生成 C veneer
│   └── canopus_target_config.h  自动生成 supervisor/Manager target config
└── ...                          target-specific 调研文档或签名资料

sdk/rust/
├── canopus-target-generated/    自动生成的机械 Rust bindings/constants
└── canopus-target-private/      手写语义 facade、ownership 和 ABI 翻译

manager/target/
├── lvgl_v9/                     当前 Band 10 UI backend
└── lvgl_v8/                     当前 Band 9 UI backend

runtime/loader/                  portable ELF32 loader/relocation core
manager/target/band9/            Band 9 early bootstrap/stage adapter
watchfaces/canopus-installer/    Lua bootstrap、staged resources、控制 UI
```

职责边界：

| 层 | 应包含 | 不应包含 |
|---|---|---|
| evidence/symbol/type | 地址、原型、布局、来源、状态 | 高层事务代码 |
| generated | callable/global constants、typed wrapper、C config | 目标业务状态机 |
| target-private | ownership、rollback、callback 翻译、语义统一 | 裸固件地址 |
| public SDK/module | target-neutral API 和业务逻辑 | target-specific ABI |
| loader adapter | heap/MPU/cache/filesystem/bootstrap | Bluetooth/UI 业务逻辑 |

## 4. 开始适配前的输入清单

Agent 在修改代码前必须确认：

- 固件二进制的绝对路径；
- 固件 SHA-256；
- firmware version string；
- firmware build string；
- 型号和 board revision；
- CPU、指令集、端序和 float ABI；
- IDA/Ghidra 数据库是否对应这个 SHA；
- 当前设备可用的 bootstrap/控制渠道，例如：
  - stock `insmod`/`rmmod`；
  - NSH `mw`/`exec`；
  - Lua resource read/write；
  - 已知可写 SRAM cave；
- 已有相似 target 只能作为参考，不能作为新 target 的证据。

如果固件文件、数据库和声明的 SHA 无法对应，立即停止地址迁移。不要在身份不确定时创建 target pack。

## 5. 新 target 的分类决策

### 5.1 同型号、不同固件版本

例如：

```text
xiaomi-band-10-pro-3.101.030
xiaomi-band-10-pro-3.101.036
```

策略：

- 创建独立目录和 `target_id`；
- 独立记录完整 SHA、version 和 build；
- 独立恢复每个 symbol/global/callback table；
- 允许共享公共 Rust/C API 和 target-private facade 形状；
- 不允许共享地址记录；
- 对 layout 完全相同时也应在新 target type records 中明确记录；
- 对相同 prototype、不同地址，分别生成产物；
- 对同名函数的行为差异，必须在 target-private 中形成显式分支或独立 backend。

推荐迁移顺序：

```text
旧 target 已知 symbol
→ 在新固件用字符串/xref/callgraph 定位候选
→ 验证函数边界、参数来源和返回值
→ 验证使用的 globals/table/slot
→ 为新 target 创建独立 evidence
→ 创建 symbol/type record
→ regenerate
→ 比较 facade 行为，而不是比较地址差值
```

特别注意：小版本升级也可能改变：

- callback table base；
- callback slot 的 stock function；
- singleton/registration handle 地址；
- 同一入口的参数数量；
- owner queue/global；
- UI helper 的真实入口；
- 一个函数是否被 inline、拆分或删除。

### 5.2 跨型号 target

跨型号时默认假设以下全部可能不同：

- Bluetooth stack family；
- UI major version；
- app/Launcher descriptor；
- filesystem/NuttX wrapper；
- heap allocator domain；
- timer和work queue模型；
- loader 和重定位行为；
- cache/MPU 所有权；
- callback 参数和事件 payload；
- native app 生命周期。

正确做法不是复制一份 Band 10 backend 后换地址，而是：

1. 先识别 subsystem family；
2. 为目标建立最小 capability map；
3. 逐能力决定：直接 generated wrapper、target-private ABI translation，或 unavailable；
4. 只有 facade 的**语义接口**与已有目标保持一致；
5. 如果固件不存在等价能力，返回明确错误，不搭建第二套 host protocol stack 来伪造固件能力。

Band 9 是典型例子：Bluelet GAP、LVGL v8 和 custom loader 与 Band 10 不同，但模块仍消费同一个 target-neutral facade。

## 6. 创建 target pack

### 6.1 复制结构，不复制结论

可从最近似 target 复制：

- 文件夹结构；
- schema 字段；
- build profile 形状；
- capability checklist；
- 测试模板。

必须删除或重新证明：

- 所有 entry/callable 地址；
- 所有 globals；
- firmware hash/version/build；
- loader 假设；
- `DEVICE_PROBED/DEVICE_PROVEN` 状态；
- target-specific notes；
- callback slot/table 结论；
- type layout 和 ownership 结论。

### 6.2 `target.toml` 最低要求

必须准确填写：

```toml
schema = 1
target_id = "vendor-model-firmware-version"
device_family = "..."
device_model = "..."
board_revision = "..."
os_family = "..."
architecture = "..."
cpu = "..."
instruction_set = "..."
endianness = "..."
float_abi = "..."
loader = "..."
firmware_sha256 = "..."
firmware_version = "..."
firmware_build = "..."
module_abi = 1
relocation_profile = "..."
revision = 1
```

还要定义：

- `firmware_address_ranges`：只覆盖已恢复的真实固件地址范围；
- `capabilities`：只声明已经有 target 实现边界的能力；
- `[loader_profile]`：从目标 loader 实际行为恢复；
- relocation allowlist：不能因为编译器可能生成就自动允许。

### 6.3 Runtime identity

每个 target 至少应有：

- `firmware_version_string` symbol；
- `firmware_build_string` symbol；
- generated identity guard。

identity guard 失败必须停止初始化，不得尝试邻近版本或 runtime signature fallback。

## 7. Symbol、type 和 evidence 的创建规则

### 7.1 Function symbol

每个 function record 应包含：

- `symbol_id` 和 `target_id`；
- 稳定的语义 `name`；
- even `entry_address`；
- Thumb 目标上的 odd `callable_address`；
- 完整 prototype；
- calling convention；
- allowed contexts；
- blocking 属性；
- argument/return/callback ownership；
- side effects；
- evidence IDs；
- policy、status、approval state；
- exact firmware SHA provenance。

Thumb 约束：

```text
entry & 1 == 0
callable == entry | 1
```

不要把反编译器显示的 odd function pointer 当作 entry，也不要在手写代码里补 `+1`。

### 7.2 Global/data symbol

固件全局也必须记录，例如：

- singleton slot；
- callback table；
- registration handle；
- owner queue pointer；
- allocator heap pointer；
- stock style object；
- callback function-pointer slot。

必须区分：

```text
全局变量本身的地址
全局变量保存的指针值
指针指向的对象
```

`prototype` 应表达生成 constant 被解释成何种数据，而 target-private 负责正确的 dereference 次数和 volatile 语义。

### 7.3 Restricted private ABI

只有静态恢复、但仍由 target-private 使用的 symbol，应采用类似：

```json
{
  "policy": "restricted",
  "status": "STATIC_RECOVERED",
  "approval_state": "PENDING",
  "proof": {
    "static": "recovered",
    "device": "not_probed",
    "evidence_ids": ["..."]
  }
}
```

当前 Rust generator 对这类 function 生成：

```rust
pub const CANOPUS_FW_<NAME>_CALLABLE: usize = ...;
```

但不生成 public typed wrapper。target-private 可以用自身受审查的 ABI adapter 消费该常量。

### 7.4 不确定原型时怎么办

如果以下任一项不确定，不要创建可调用 wrapper：

- 参数数量；
- 32/64 位宽度；
- pointer const/mut；
- variadic；
- callback ABI；
- struct by-value/by-pointer；
- 返回值是否为 handle、status 或 ownership token。

可保留 candidate 或 restricted constant，并让 facade fail-closed。编译需求不能作为猜原型的理由。

## 8. Generated 层与 target-private 层

### 8.1 生成命令

当前 C 命令同时生成 veneer 和 target config：

```sh
cargo run -p canopus-cli -- \
  target generate-veneer <target-id> --targets-dir targets
```

Rust bindings：

```sh
cargo run -p canopus-cli -- \
  target generate-rust-bindings <target-id> \
  --targets-dir targets \
  --output sdk/rust/canopus-target-generated/src/<target-file>.rs
```

新增 target 时不要依赖 Rust 命令的默认 `generated.rs` 路径；为非默认 target 显式指定输出，避免覆盖另一个 target。

生成后必须运行 byte-for-byte stability tests。任何直接编辑 generated 文件的修改都应被丢弃并回到 generator/records 修复。

### 8.2 target-private 应保留什么

合理的手写内容：

- 把 Band 9 handle-based API 翻译为统一 adapter API；
- callback table mirror 的分配、注册、回滚；
- callback payload 翻译；
- timer/queue 生命周期协调；
- allocator domain pairing；
- app/Launcher 分阶段事务；
- LVGL v8/v9 语义统一；
- unproven capability 的 fail-closed 实现。

不合理的手写内容：

- firmware callable/global 地址；
- generated 已表达的机械 prototype wrapper；
- 从另一 target 复制的 callback table；
- 为通过编译而假设的 dummy address；
- public module 中的 target-specific `cfg` 和地址。

### 8.3 增加 Rust target feature

新增 target 通常需要同步：

- `sdk/rust/canopus-target-generated/Cargo.toml` feature；
- `sdk/rust/canopus-target-generated/src/lib.rs` 的 mutually-exclusive include；
- `sdk/rust/canopus-target-private/Cargo.toml` feature；
- `sdk/rust/canopus-target-private/src/lib.rs` backend selection；
- 新的 target-private backend 或明确共享的 backend module；
- 外部模块的 target feature/build matrix。

如果两个 firmware backend 语义完全一致，也不要让它们共享 generated 地址文件；最多共享不含地址的 facade helper。

## 9. Loader 决策树

```text
目标是否存在已证明可加载 zero-import ELF32 ET_REL 的 stock modlib？
├─ 是
│  ├─ insmod 是否在所需进程/上下文可调用？
│  │  ├─ 是 → stock modlib profile
│  │  └─ 否 → 先解决调用上下文；不可直接假设 custom loader 必需
│  └─ rmmod 是否安全？
│     ├─ 已证明 → 仍遵守 Canopus 当前 next-boot 生命周期策略
│     └─ 未证明 → boot-resident/reboot cleanup
└─ 否
   ├─ 是否有受控内存写入和执行入口（例如 NSH mw/exec）？
   │  ├─ 否 → target loader BLOCKED，停止更高层适配声明
   │  └─ 是
   │     ├─ 能否注入小型 stage-1？
   │     ├─ 能否从文件读取较大的 stage-2/supervisor？
   │     ├─ heap、MPU、cache、filesystem 是否可恢复？
   │     └─ 全部满足 → custom staged loader profile
   └─ 禁止把完整 supervisor 长期用 mw 逐 word 注入作为正式方案
```

## 10. 支持 stock `insmod` 的 target

必须独立证明：

- loader 接受 ELF32 ARM `ET_REL`；
- zero-import 或 symbol import 行为；
- section allocation/alignment；
- constructor/destructor discovery；
- relocation types；
- module name/path规则；
- module size限制；
- return value 和 errno；
- `insmod` 所在进程是否拥有模块需要的事件循环/服务上下文；
- `rmmod` 是否会等待 callback/worker，以及是否真实安全。

当前 Band 10 路径的关键原则：

- supervisor 是 exact-target zero-import ELF；
- generated veneer 内嵌 callable；
- verifier 拒绝未知绝对地址和重定位；
- native app 注册等依赖 miwear 上下文的操作，通过 `/dev/canopus` 从正确进程触发，不能在 `system -c insmod` constructor 中直接完成；
- 即使 stock 存在 `rmmod`，当前 Canopus 生命周期仍采用 next-boot enable/disable/remove，不自动恢复热卸载。

新增 stock-modlib target 时，应扩展 target profile 和 build selection，而不是在共享 supervisor 代码中散布 target ID 判断。

## 11. 不支持 stock `insmod` 的 target

### 11.1 推荐 staged loader

```text
Lua/NSH bootstrap
→ compare-before-write SRAM cave
→ tiny stage-1
→ 从文件读取 flat PIC stage-2
→ stage-2 读取 supervisor ELF32 ET_REL
→ portable ELF loader 分配、重定位、映射、调用 constructor
```

分层职责：

| 层 | 职责 |
|---|---|
| Lua/NSH | 资源检查、cave 原字节验证、注入/exec、mailbox 观察、恢复 |
| stage-1 | 极小启动桥，定位 stage-2 文件并移交执行 |
| stage-2 | exact-target filesystem/heap/MPU/cache adapter + portable loader 调用 |
| portable loader | ELF parse、section layout、symbol/relocation、constructors |
| supervisor | 与 stock-modlib target 尽量相同的控制面和模块生命周期 |

### 11.2 Custom loader 必须证明的目标能力

- 可用 allocator 及其匹配 free；
- allocation alignment；
- 文件 open/read/close；
- executable memory policy；
- MPU region allocate/configure/release；
- cache clean/invalidate，或经证据选择 coherent/non-cacheable mapping；
- DSB/ISB 边界；
- constructor 调用 ABI；
- 失败路径清理；
- module unload 或 reboot-only 语义；
- stage/cave 幂等性和恢复。

### 11.3 MPU 和 cache 约束

Agent 不得只做到“内存可写然后跳转”。必须回答：

- 该 region 是否 executable；
- executable 和 writable 是否同时存在；
- region base/length 如何对齐；
- AttrIndx/MAIR 属性是什么；
- 配置前后是否需要清 cache；
- release 前是否清除 `RLAR`；
- region ownership token 如何保存；
- 任一步失败时释放顺序是什么。

Band 9 当前使用 tracked MPU region ownership 和 normal non-cacheable dynamic executable mapping。新 target 不得直接继承这些参数，必须验证其 MPU/MAIR 实现。

### 11.4 Early bootstrap 地址例外

在 generated runtime 尚未运行前，stage-1/Lua 可能必须持有：

- SRAM cave；
- allocator；
- MPU allocate/configure；
- cleanup callables；
- mailbox 地址；
- 原始 trampoline words。

这些是 loader-specific bootstrap metadata，不应伪装成普通模块 API symbol。

长期目标仍应是从 target records 生成专用 bootstrap constants/header/resource，确保 Lua、stage-1 和 stage-2 不形成多份人工地址表。无论是否自动生成，都必须有：

- exact target identity；
- compare-before-write；
- known original bytes；
- restoration path；
- failure mailbox；
- 不跨 target fallback。

## 12. Build 系统集成

新增 target 时检查所有显式 target 列表。当前仓库至少包括：

- `scripts/build_canopus_supervisor.sh` target case；
- Manager UI backend 选择；
- stock/custom loader source selection；
- supervisor size budget；
- Rust generated/private Cargo features；
- generated stability target matrix；
- installer resource staging；
- module build scripts和外部模块 feature；
- package manifest target include；
- CI target matrix。

构建脚本中允许根据 loader/UI family 选择 backend，但避免重复目标地址。目标地址必须来自 generated headers/bindings。

### 12.1 UI backend 决策

先识别 UI ABI family：

```text
LVGL v9-compatible → manager/target/lvgl_v9
LVGL v8/BES ABI    → manager/target/lvgl_v8
其他 UI family     → 新 backend，不能硬套现有布局
```

需要验证：

- widget factory prototype；
- event object layout；
- user data/code offset；
- page descriptor；
- navigation/popup ownership；
- timer callback type；
- style/font对象；
- trailing-kind、alignment、screen size等协议常量。

UI“长得相同”不代表 ABI 相同。

## 13. 测试和验证矩阵

### 13.1 每次 target 迁移至少运行

```sh
# target records/schema
cargo test -p canopus-core

# generated byte stability
cargo test -p canopus-core --test generated_stability

# Rust backend，每个 feature 独立检查
cargo check --manifest-path sdk/rust/Cargo.toml \
  -p canopus-target-private --no-default-features \
  --features <target-feature>

# 全仓 gates
scripts/ci.sh

# exact target supervisor
CANOPUS_TARGET=<target-id> scripts/build_canopus_supervisor.sh
```

如果修改共享 generator、public ABI、portable loader 或 Manager，必须重新运行**所有已支持 target**，不能只测新 target。

### 13.2 Generated stability 应保证

- 所有 generated Rust 文件可逐字节重建；
- 所有 C veneers 可逐字节重建；
- 所有 `canopus_target_config.h` 可逐字节重建；
- private ABI function 满足 even entry/odd callable；
- restricted/PENDING symbol 不生成 public typed wrapper；
- target-private 没有 active firmware literal；
- 每个 backend 只引用当前 feature 对应 generated module 中存在的 symbol。

### 13.3 ELF verifier 必须 PASS

至少检查：

- ARM ELF class/machine/type；
- zero unexpected undefined symbols；
- relocation allowlist；
- constructors/destructors；
- section flags/alignment/size；
- firmware-range absolute address全部在当前 target allowlist；
- 无 forbidden symbol；
- 无意外 unwind/TLS/GOT；
- staged payload 与已验证 ELF 一致。

### 13.4 Device gate 顺序

建议新 target 按以下顺序推进：

```text
T0 identity guard 拒绝错误固件
T1 loader 最小执行/清理
T2 /dev/canopus 注册和固定 status/control ABI
T3 read-only clock/version capability
T4 persistence 和 reboot restore
T5 Manager 注册但不发布复杂 UI
T6 Launcher entry 和基础页面
T7 UI event/timer/navigation lifecycle
T8 subsystem adapter（例如 Bluetooth discovery）
T9 retained callback/queue/timer ownership
T10 protocol/data plane
T11 reboot disable/remove/update/rollback
T12 safe mode、失败注入和恢复
```

若 loader 路径本身未设备执行，不得跳到 Bluetooth 或 native app 功能验证。

## 14. Fail-closed 设计

当目标缺少对应能力时，backend 应明确返回：

- `None`；
- `false`；
- `-1`；
- `ENOSYS`；
- typed unavailable/error enum。

选择哪一种取决于既有 facade contract，但必须满足：

- 不触发错误地址；
- 不伪造成功状态；
- 不返回会让上层继续危险流程的 handle；
- diagnostics 能区分 unavailable、policy rejection 和运行失败。

示例：某 target 没有证明 HCI receive hook，就应让安装返回 false，而不是选择一个“看起来像 callback slot”的全局覆盖。

## 15. 常见失败模式和诊断顺序

### 15.1 LOAD 后立即崩溃

按顺序检查：

1. artifact 是否确实为当前 target；
2. identity guard 是否在第一次 firmware call 前执行；
3. Thumb callable 是否为 odd；
4. constructor context 是否允许该操作；
5. generated 文件是否过期；
6. verifier 是否允许了错误 target 的地址；
7. callback/global/table 是否来自另一固件；
8. UI/app 注册是否错误地在 loader 进程执行。

### 15.2 `/dev/canopus` 缺失

检查：

- `register_driver` prototype（不同 target 可能是 3/4 参数）；
- generated target config 是否选择正确 wrapper；
- constructor 是否执行；
- `CANOPUS_SUP_PLATFORM_COMPLETE` 是否由 capability生成；
- fops layout 和 pointer width；
- target identity 是否失败；
- custom loader 是否调用 init array。

### 15.3 编译通过但功能超时/remote error

不要先增加 timeout。检查：

- 上层错误码到底表示同步拒绝、异步 remote reject 还是 timeout；
- callback 是否被另一个 stock client过滤；
- registration handle/table/slot 是否精确；
- callback 参数和 address byte order；
- owner thread/queue 是否正确；
- 请求被 accepted 不等于完成；
- target-specific state enum 是否不同。

### 15.4 Custom loader 执行失败

分层诊断：

```text
bootstrap 未执行
stage-1 未读到 stage-2
stage-2 文件/heap失败
ELF parse/layout失败
relocation失败
MPU/cache finalize失败
constructor失败
supervisor registration失败
```

每层使用独立 mailbox/status，不要把 shell `exec` 返回值直接当作 loader 成功，因为某些 NSH 实现会把被调函数返回值作为 shell status。

## 16. Agent 推荐工作方式

### 16.1 先建立差异表

对同型号新固件，建立：

| 能力 | 旧 target | 新 target 候选 | 新 target 证据 | ABI 是否相同 | 状态 |
|---|---|---|---|---|---|
| identity | ... | ... | string/xref | 是/否 | ... |
| loader | ... | ... | call graph/probe | 是/否 | ... |
| register_driver | ... | ... | ... | ... | ... |
| UI | ... | ... | ... | ... | ... |
| Bluetooth | ... | ... | ... | ... | ... |

对跨型号 target，先按 subsystem family 建表，不要逐地址“平移”。

### 16.2 搜索策略

优先级：

1. 独有字符串及其 xref；
2. 已知 caller/callee；
3. global 数据流；
4. vtable/interface slot；
5. 函数大小、CFG 和常量组合；
6. byte signature 仅作为候选；
7. 不同搜索路径交叉确认。

对关键函数至少回答：

- 谁调用它；
- 参数从哪里来；
- 返回值如何使用；
- 是否保存 callback/pointer；
- 使用哪些 globals/locks/queues；
- 哪个线程或事件循环执行；
- 失败路径释放什么。

### 16.3 修改顺序

推荐一次迁移一个完整垂直切片：

```text
evidence
→ symbol/type record
→ generate
→ target-private semantic adapter
→ tests
→ target build/verifier
```

不要先在 backend 写几十个裸地址，最后再“补 records”；这样最容易出现地址漂移和错误证据状态。

### 16.4 独立审查

对高风险迁移安排只读复核，至少检查：

- 新旧 target 地址没有交叉；
- entry/callable oddness；
- prototype 和手写 `extern "C" fn` 一致；
- global dereference 层数；
- allocator/free 配对；
- callback slot/table；
- public codegen approval gate；
- loader cleanup 和 MPU ownership；
- 文档没有把静态结果写成设备证明。

## 17. Definition of Done

### 17.1 静态适配完成

只有满足以下全部条件，才能称为“静态适配完成”：

- [ ] target identity 和 SHA 独立确认；
- [ ] target.toml/schema 通过；
- [ ] 所用 symbol/type 均有 exact-target record；
- [ ] generated Rust/C/config 可重建；
- [ ] target-private 无固件裸地址；
- [ ] 不可用能力 fail-closed；
- [ ] Rust/C backend 编译；
- [ ] supervisor 构建并 verifier PASS；
- [ ] 所有既有 target 未回归；
- [ ] 文档明确设备验证仍 pending。

### 17.2 Loader 静态完成

还必须：

- [ ] stock loader profile 或 custom loader 分支明确；
- [ ] relocation、section、constructor 路径有测试；
- [ ] custom loader 的 heap/MPU/cache/cleanup 已编码并静态验证；
- [ ] installer 正确 staging exact-target resources；
- [ ] 没有跨 loader fallback。

这仍不等于 loader device complete。

### 17.3 设备支持完成

还必须按 gate 保存：

- [ ] exact target/version/build/hash；
- [ ] artifact SHA；
- [ ] loader 实机结果；
- [ ] `/dev/canopus` 状态；
- [ ] Manager/Launcher/UI 生命周期；
- [ ] 模块功能和错误路径；
- [ ] reboot restore/disable/remove；
- [ ] crash log、event/status 和恢复方法；
- [ ] 最后成功 gate；
- [ ] reviewer verdict。

某个 subsystem 未通过时，应声明“target core supported, capability X pending”，而不是把整个 target 标为完全支持。

## 18. Agent 最终报告模板

```markdown
## Target
- target_id:
- model/board:
- firmware version/build:
- firmware SHA-256:
- IDB/binary source:

## Target classification
- same-model firmware / cross-model:
- UI family:
- Bluetooth/runtime family:
- loader: stock modlib / custom staged / blocked:

## Evidence added
- evidence bundles:
- symbols:
- types/layout diffs:
- unproven assumptions:

## Generated artifacts
- C veneer:
- C target config:
- Rust bindings:
- target-private facade:

## Loader
- selected path and why:
- relocations:
- heap/MPU/cache ownership:
- bootstrap resources:
- failure cleanup:

## Validation
- schema:
- generated stability:
- Rust features:
- full CI:
- supervisor/module verifier:
- device gates:

## Remaining limits
- fail-closed capabilities:
- device proof pending:
- recovery/reboot requirements:

## Claims
- STATIC_RECOVERED:
- HOST/BUILD VERIFIED:
- DEVICE_PROBED:
- DEVICE_PROVEN:
```

## 19. 最后检查：Agent 必须能回答的问题

提交一个新 target 前，Agent 必须能明确回答：

1. 为什么这是独立 target，而不是兼容范围？
2. 固件 SHA、version 和 build 从哪里取得？
3. 每个执行地址是否来自该 target 自己的 evidence record？
4. Thumb entry/callable 是否分别正确？
5. 哪些能力是 generated wrapper，哪些只能由 target-private 使用？
6. target-private 是否仍有固件裸地址？
7. UI、Bluetooth、timer、queue、allocator 属于什么 ABI family？
8. loader 为什么选择 stock modlib 或 custom path？
9. custom loader 如何管理 heap、MPU、cache 和失败清理？
10. 不可用能力如何 fail-closed？
11. 所有已支持 target 是否重新构建和验证？
12. 当前结论最高是静态、主机构建、设备探针还是设备证明？
13. 用户遇到崩溃时，可靠恢复边界是什么？

只要其中任一项依赖“应该一样”“大概可用”或“编译通过所以没问题”，适配就尚未完成。
