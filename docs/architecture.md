# Canopus 架构与实施路线

> 文档状态：Draft v1  
> 框架状态：架构设计阶段  
> 初始参考目标：小米手环 10 Pro（Xiaomi Band 10 Pro），固件 `3.101.030`  
> 初始参考固件 SHA-256：`f701a84ffcafa67f4d4603ad8cd66a11e5442f27140f5af0982e0975dccd225b`  
> 目标 ID：`xiaomi-band-10-pro-3.101.030`（见 ADR-CAN-001/002：target id 人类可读且携带固件版本，一个 target_id 精确对应一个固件；firmware SHA-256 仍为权威安全身份）

---

## 1. 文档目的

Canopus 是一个面向嵌入式设备固件扩展的原生模块框架。它允许开发者：

1. 使用 C 或 Rust 编写功能模块；
2. 将模块编译成设备原生内核模块；
3. 使用从目标固件中逆向恢复、经过证据审核的函数、全局对象和 ABI；
4. 针对不同设备、不同固件版本生成不同目标产物；
5. 将多个目标产物打包为同一个可安装模块；
6. 由模块制作并注册真正的原生应用，使其像固件内置应用一样出现在 launcher 中，使用原生 UI、生命周期和系统接口；
7. 让原生应用与模块逻辑组成同一个产品，也允许应用只是模块管理页，或让模块仅作为独立原生应用的逻辑载体与安装入口；
8. 在设备端安装、启用、禁用、更新、回滚和删除模块及其原生应用；
9. 通过类似 KernelSU 管理应用的界面查看设备、框架、模块、应用、兼容性和错误状态；
10. 使用 LLM、IDA MCP 和其他分析工具加速固件逆向，但不允许未经验证的模型输出直接进入可执行符号表。

本文定义 Canopus 的总体架构、数据模型、安全边界、模块 ABI、C/Rust SDK、构建与打包流程、设备端管理器、逆向工程流水线、测试策略、实施阶段以及完整任务进度追踪区。

---

## 2. 目标与非目标

### 2.1 核心目标

- **精确固件绑定**：每个目标产物默认绑定完整固件哈希，不使用“相近版本大概可用”的策略。
- **多目标构建**：同一模块源码可为多个设备/固件符号表生成多个产物。
- **证据化符号管理**：地址、函数原型、结构布局、调用上下文、所有权和验证状态均可追溯。
- **安全生命周期**：明确区分可卸载模块、逻辑可禁用模块和必须驻留到重启的模块。
- **语言无关 ABI**：C 是首个 SDK，Rust 通过 `no_std` 和 C ABI 接入。
- **强静态验证**：拒绝未知重定位、未定义导入、未批准绝对地址和不匹配的目标身份。
- **设备端管理**：用户可以管理模块及原生应用，并理解“当前生效”“下次启动生效”“需要重启”等真实状态。
- **原生应用扩展**：将 launcher 注册、原生页面、UI 线程、资源、路由和应用生命周期作为一等 target capability，而不是把 JavaScript 快应用当作唯一前端。
- **模块/应用一体化**：同一包可以同时提供内核逻辑、launcher 入口和原生管理页，也可以提供 headless 模块或独立原生应用。
- **可扩展逆向平台**：符号库、目标适配器、构建后端、管理 UI 和分析工作流均可独立扩展。
- **可复现发布**：构建产物、目标包、符号库版本、工具链和签名全部可复现、可审计。

### 2.2 明确非目标

Canopus 不承诺：

- 让任意固件或任意函数地址自动变得安全；
- 对同一内核地址空间中的任意原生机器码提供真正的安全沙箱；
- 自动推断并执行 LLM 发现的函数；
- 默认允许模糊签名扫描后直接调用候选地址；
- 在保留 callback、timer、worker 或 hook 后仍强制卸载模块；
- 把模拟器、相似设备 SDK 或参考固件地址当作目标设备证据；
- 通过文件扩展名判断载荷类型或安全性；
- 将“请求已入队”描述为异步操作已经成功完成。

---

## 3. 设计原则

### 3.1 证据优先

每个目标能力必须经过以下状态链，而不是从反编译结果直接进入 SDK：

```text
假设
→ 候选符号
→ 静态证据
→ ABI 审核
→ 主机测试
→ 最小设备探针
→ 设备验证
→ 能力批准
→ SDK/模块可用
```

### 3.2 精确目标优先于便利性

目标身份默认包含：

- 设备型号与板级修订；
- SoC/CPU；
- 指令集、端序、浮点 ABI；
- OS/模块加载器类型；
- 完整固件 SHA-256；
- 固件版本和 build string；
- 模块 ABI 与允许的重定位集合。

管理器必须选择完全匹配的产物。任何兼容范围都必须由目标维护者显式声明和验证。

### 3.3 生命周期先于功能

在调用固件功能前，模块必须先说明：

- 谁拥有传入和返回的内存；
- callback 是否会被固件长期保存；
- timer cancel 是否释放参数；
- worker 是否可能在模块禁用后运行；
- 资源是否可以撤销；
- 禁用是否等同于卸载；
- 错误后的恢复边界是回滚、卸载还是重启。

### 3.4 默认拒绝

以下行为默认不批准：

- 未登记的绝对函数地址；
- 未登记的 MMIO 或共享内存写入；
- 未知函数原型；
- 未知 allocator 域之间的内存释放；
- 未知线程/中断上下文中的阻塞调用；
- 未知 callback 事件；
- 未验证的重定位；
- 未签名目标包和模块包。

### 3.5 共享 SDK 不泄漏目标私有 ABI

公共 SDK 只能包含稳定的 Canopus 类型与接口。固件私有结构、地址和调用约束只能存在于目标包或生成代码中。

---

## 4. 当前原型提供的基础证据

现有仓库不是 Canopus 本身，但提供了第一套目标适配参考：

- Cortex-M33、Thumb-2、ARM EABI5、soft-float；
- stock modlib 可加载 ELF32 relocatable object；
- 目标模块符号表为空，因此当前产物不能依赖普通未定义 ELF 导入；
- 已验证的重定位包络包括：
  - `R_ARM_NONE`
  - `R_ARM_ABS32`
  - `R_ARM_REL32`
  - `R_ARM_TARGET1`
  - `R_ARM_PREL31`
  - `R_ARM_THM_MOVW_ABS_NC`
  - `R_ARM_THM_MOVT_ABS`
- 已验证 constructor/destructor 模式；
- 已验证字符设备、固定宽度控制协议、sequence snapshot 和资源追踪思路；
- 已证明可卸载模块与 boot-resident 模块必须采用不同生命周期；
- 已通过精确固件地址实现并实机验证 Classic 配对、AVDTP、媒体 L2CAP、RTP/SBC；
- 已证明固件身份、函数原型、callback 事件和资源所有权不能从相似固件直接复制。

这些内容将作为首个 target pack 的输入，不应直接成为跨设备公共 API。

---

## 5. 总体架构

```text
┌──────────────────────────────────────────────────────────────┐
│                         Host / CI                            │
│                                                              │
│  canopus CLI ─ build planner ─ C SDK ─ Rust SDK              │
│       │              │           │        │                  │
│       ├─ package/sign/verify      └─ target-generated FFI    │
│       │                                                      │
│       ├─ target registry ─ symbol/type/evidence database     │
│       │                                                      │
│       └─ RE orchestrator ─ LLM ─ IDA/Ghidra/MCP ─ reviewers │
└──────────────────────────────┬───────────────────────────────┘
                               │ signed .canopus package
┌──────────────────────────────▼───────────────────────────────┐
│                      Device control plane                    │
│                                                              │
│  Canopus Manager native app                                   │
│       │                                                      │
│  manager service / supervisor                                │
│       ├─ device identity                                     │
│       ├─ package and native-app registry                     │
│       ├─ lifecycle state                                     │
│       ├─ load/register/disable/remove/rollback               │
│       ├─ safe mode/quarantine                                │
│       └─ diagnostics/event log                               │
│                                                              │
│  target loader adapter ─ stock modlib / native loader        │
│  native-app adapter ─ launcher/app registry/UI dispatcher    │
└──────────────────────────────┬───────────────────────────────┘
                               │
┌──────────────────────────────▼───────────────────────────────┐
│                         Module plane                         │
│                                                              │
│  generated constructor glue                                 │
│  Canopus module runtime                                     │
│  C or Rust module logic                                     │
│  optional native-app descriptor, pages, UI callbacks        │
│  target-specific typed veneers                              │
│  resource tracker / callback guards / diagnostics           │
└──────────────────────────────────────────────────────────────┘
```

### 5.1 三个独立边界

1. **Host Tooling**：分析、构建、验证、打包和签名；
2. **Device Manager**：身份识别、包管理、生命周期和 UI；
3. **Module Runtime**：实际在固件/内核地址空间执行的代码。

三者使用版本化协议连接，不能通过隐式目录约定或 UI 文本耦合。

---

## 6. 推荐仓库结构

```text
canopus/
├── README.md
├── docs/
│   ├── architecture.md
│   ├── module-lifecycle.md
│   ├── symbol-evidence.md
│   ├── security-model.md
│   ├── target-authoring.md
│   └── progress.md
├── schemas/
│   ├── target.schema.json
│   ├── symbol.schema.json
│   ├── type.schema.json
│   ├── evidence.schema.json
│   ├── module.schema.json
│   └── package.schema.json
├── cli/
│   └── canopus/
├── sdk/
│   ├── c/
│   ├── rust/
│   └── abi/
├── runtime/
│   ├── module/
│   ├── lifecycle/
│   ├── resources/
│   ├── diagnostics/
│   └── control/
├── manager/
│   ├── service/
│   ├── native-app/
│   ├── protocol/
│   └── storage/
├── app-sdk/
│   ├── c/
│   ├── rust/
│   ├── ui/
│   ├── launcher/
│   └── resources/
├── targets/
│   └── xiaomi-band-10-pro-3.101.030/
│       ├── target.toml
│       ├── symbols/
│       ├── types/
│       ├── capabilities.toml
│       ├── loader.toml
│       ├── evidence/
│       ├── probes/
│       └── generated/
├── modules/
│   ├── examples/
│   └── reference/
├── tools/
│   ├── re-orchestrator/
│   ├── symbol-generator/
│   ├── elf-verifier/
│   ├── package-builder/
│   └── reproducibility/
└── tests/
    ├── host/
    ├── integration/
    ├── fixtures/
    └── hardware/
```

现有固件研究仓库可以暂时保留，首个 target pack 通过有来源记录的导入任务建立；不要一开始重写或移动所有研究材料。

---

## 7. 目标包 Target Pack

### 7.1 目标身份

`target.toml` 示例：

```toml
schema = 1
target_id = "xiaomi-band-10-pro-3.101.030"
device_family = "xiaomi-smart-band"
device_model = "target-model"
board_revision = "unknown"
os_family = "nuttx-derived"
architecture = "armv8-m.main"
cpu = "cortex-m33"
instruction_set = "thumb2"
endianness = "little"
float_abi = "soft"
loader = "nuttx-modlib-elf32-rel"
firmware_sha256 = "f701a84ffcafa67f4d4603ad8cd66a11e5442f27140f5af0982e0975dccd225b"
firmware_version = "3.101.030"
firmware_build = "CONBINE_LTALM078_T3.101.030_06011854"
module_abi = 1
relocation_profile = "best1503-v1"
```

### 7.2 Runtime identity guard

离线完整哈希绑定是主要身份依据。模块还必须包含低风险 runtime guard，例如：

- 版本字符串；
- build string；
- 若干只读代码区指纹；
- 关键 loader ABI 指纹。

runtime guard 必须在第一次目标函数调用前完成。guard 失败时只能发布错误并退出初始化，不得尝试“兼容模式”。

### 7.3 Loader profile

Loader profile 描述：

- ELF class/machine/type；
- constructor/destructor 发现方式；
- 支持的重定位；
- 未定义符号策略；
- section 限制；
- 对齐和最大尺寸；
- 模块名称规则；
- 加载/卸载命令或系统 API；
- 是否支持符号导入；
- 是否支持持久化自动加载；
- native app registry、launcher entry 和 UI dispatcher profile；
- 原生应用描述符、页面、资源和生命周期的加载方式。

---

## 8. 符号、类型和证据数据库

### 8.1 符号记录

```yaml
schema: 1
symbol_id: xiaomi-band-10-pro-3.101.030.bt.timer.cancel
name: bt_timer_cancel
kind: function
entry_address: 0x0C7D2CCC
callable_address: 0x0C7D2CCD
instruction_set: thumb
prototype_id: proto.bt_timer_cancel.v1
calling_convention: arm-aapcs
contexts:
  allowed: [bluetooth_owner, module_callback]
blocking: false
ownership:
  argument: borrowed_handle_pointer
  callback_argument: freed_only_if_pending_timer_removed
side_effects:
  - removes_timer
  - may_free_timer_argument
proof:
  static: confirmed
  device: proven
policy: resident-only
status: exported-internal
provenance:
  firmware_sha256: f701a84f...
  evidence_ids: [EVID-TIMER-001, EVID-TIMER-002]
```

### 8.2 必填字段

每个函数至少需要：

- 唯一 symbol ID；
- target ID；
- entry 和 callable 地址；
- ARM/Thumb 状态；
- 完整原型 ID；
- calling convention；
- 允许调用上下文；
- 阻塞/锁行为；
- 输入、输出和释放所有权；
- callback 行为；
- 可能副作用；
- 证据状态；
- 发布策略；
- 来源和撤销记录。

### 8.3 类型与布局记录

结构体不能只保存反编译器中的名称，必须保存：

- size/alignment；
- 字段 offset、width、signedness；
- 指针目标与所有权；
- 固定数组长度；
- bitfield 编码；
- 读取和写入是否被证明；
- 不同固件版本之间的 layout diff。

### 8.4 证据等级

```text
CANDIDATE          仅为候选
STATIC_RECOVERED   反编译和 xref 支持
STATIC_CONFIRMED   多条静态证据一致
HOST_TESTED        纯逻辑或格式已主机测试
DEVICE_PROBED      最小设备探针执行过
DEVICE_PROVEN      目标功能和生命周期已实机证明
RESTRICTED         只能在指定模块/上下文使用
FORBIDDEN          禁止生成可调用 veneer
WITHDRAWN          历史结论已撤销，永久保留记录
```

“静态可信”与“允许调用”是两个字段。一个地址可以高度可信，但因为所有权、锁或副作用不明确而保持 `FORBIDDEN`。

### 8.5 签名和跨版本移植

函数签名用于 host 侧寻找新固件中的候选位置，不能直接成为 runtime fallback：

```text
旧固件已证明符号
→ 生成若干 xref/字节/CFG 特征
→ 新固件候选匹配
→ 调用图与字符串交叉验证
→ 原型和布局复核
→ 新 target 独立证据
→ 新 target pack 发布
```

禁止模块在设备上扫描到“最像”的地址后直接执行。

---

## 9. LLM/MCP 固件逆向流水线

### 9.1 Orchestrator

`canopus re` 负责：

- 选择目标固件与只读 IDA 数据库；
- 将问题分解为函数、类型、调用链、状态机和 ABI 任务；
- 调用 IDA/Ghidra/二进制分析 MCP；
- 保存所有工具输入、输出和地址来源；
- 生成结构化 evidence bundle；
- 发起独立验证和人工审核；
- 仅在审批后生成 target pack 变更。

### 9.2 默认允许的 MCP 操作

- decompile/disassemble；
- callers/callees/callgraph；
- xrefs；
- strings/immediates/bytes；
- CFG 和 data flow；
- type inspection；
- signatures；
- 只读 binary survey。

### 9.3 默认禁止的自动操作

- patch binary；
- patch assembly；
- 写入目标设备内存；
- 自动调用新发现函数；
- 自动修改并保存权威 IDA 数据库；
- 自动把候选标为 `DEVICE_PROVEN`；
- 自动发布 target pack；
- 自动签名生产模块。

### 9.4 Evidence bundle

```yaml
question: "timer cancel 是否释放 callback argument"
target_id: xiaomi-band-10-pro-3.101.030
candidate_symbols: [...]
prototype_hypothesis: ...
callsite_evidence: ...
control_flow_evidence: ...
ownership_analysis: ...
unsafe_assumptions: ...
recommended_probe: ...
reviewers: [...]
verdict: STATIC_CONFIRMED
artifacts:
  - uri: ida://...
  - uri: capture://...
```

LLM 输出本身只能是 `hypothesis` 或 `evidence-linked candidate`。状态晋升由 schema validator、验证任务和审核者完成。

---

## 10. Canopus 模块模型

### 10.1 模块描述符

```c
struct canopus_module_descriptor_v1 {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint8_t module_id[32];
    uint8_t module_version[16];
    uint8_t build_id[32];
    uint8_t target_id[32];

    int32_t (*prepare)(const struct canopus_context_v1 *context);
    int32_t (*activate)(const struct canopus_context_v1 *context);
    int32_t (*deactivate)(const struct canopus_context_v1 *context);
    int32_t (*stop)(const struct canopus_context_v1 *context);
    int32_t (*query)(struct canopus_status_writer_v1 *writer);
};
```

该结构是逻辑 ABI。对于当前 stock loader，由生成的 C constructor/destructor glue 调用它；不能假设 loader 会按符号名称查找描述符。

### 10.2 生命周期类别

> 现行实现（next-boot 语义）：enable/disable/remove 对**所有**类别统一为 boot intent，返回 `REBOOT_REQUIRED`。lifecycle class 不再区分“可热卸载”与“必须重启”，只作为元数据（风险显示、boot 加载后的状态命名、安全模式策略）。REMOVABLE 不再是即时 unload 的许可，见 §16.3。

#### REMOVABLE

历史上可停止 admission、排空 callback、撤销资源并 `rmmod`。现在：与其它类别一样 next-boot；boot 加载后状态为 `ACTIVE`。

#### RESIDENT_AFTER_ACTIVATION

激活前可能可卸载；第一次注册 retained callback、timer、worker、hook、listener 或 SDP 后进入 boot-resident barrier。现在：与其它类别一样 next-boot；boot 加载后状态为 `BOOT_RESIDENT`。

#### ALWAYS_RESIDENT

加载后驻留到重启。现在：与其它类别一样 next-boot；boot 加载后状态为 `BOOT_RESIDENT`。

#### PATCH_REBOOT_REQUIRED

涉及无法在所有状态安全恢复的 hook/patch。现在：与其它类别一样 next-boot；boot 加载后状态为 `BOOT_RESIDENT`。

建议 descriptor flags 还包括：

```text
HAS_NATIVE_APP
NATIVE_APP_INTEGRATED
NATIVE_APP_STANDALONE
REGISTERS_LAUNCHER_ENTRY
REQUIRES_UI_DISPATCHER
APP_UNREGISTER_REBOOT_REQUIRED
```

模块声明 native app 后，target verifier 必须拒绝缺少 app/launcher capabilities 或错误声明为普通 removable 的包。

### 10.3 状态机

```text
DISCOVERED
→ VERIFIED
→ INSTALLED            ← 安装即禁用（intent DISABLED），绝不自动加载
→ DISABLED             ← ENABLE 之前的状态
→ ENABLED              ← next-boot：已记录 intent ENABLED，等待下次加载
→ LOADING              ← 仅在 boot restore 时发生（insmod）
→ PREPARING
→ READY
→ ACTIVE
   └─ BOOT_RESIDENT     ← 所有类别：启动时加载，重启前不卸载

命令（全部 next-boot，返回 REBOOT_REQUIRED）：
ENABLE   → intent ENABLED, 状态 ENABLED
DISABLE  → intent DISABLED; 已加载 → DISABLED_NEXT_BOOT, 未加载 → DISABLED
REMOVE   → intent REMOVE,   状态 REMOVE_PENDING（boot 时删文件并回收 slot）
UPDATE/ROLLBACK → intent ENABLED, 状态 UPDATE_STAGED

任意初始化失败：FAILED
运行期失败：FAIL_STOP → QUARANTINED_NEXT_BOOT
```

没有热插拔：stop/drain/unload 路径已从 supervisor 移除。所有 enable/disable/remove 都是 boot intent，由下一次 supervisor 加载（boot restore）执行。状态机描述的是运行时可见状态；持久化的是 intent。

### 10.4 Resource tracker

统一追踪：

- 字符/块设备；
- heap allocation 及 allocator domain；
- callback table；
- timer；
- worker；
- service；
- protocol registration；
- file descriptor；
- hook/patch ledger；
- module open references；
- inflight callback；
- generation/cookie。

资源状态必须支持 `ACTIVE`、`DRAINING`、`DETACHED`、`RELEASED`、`RETAINED_UNTIL_REBOOT`。例如 unregister 返回 `EBUSY` 但 namespace 已 unlink 时，应标记为 `DETACHED`，不能盲目重试。

### 10.5 Callback 规则

每个 retained callback 必须：

- 位于确定会持续映射的代码中；
- cookie/state 不依赖 UI 生命周期；
- 验证 target、generation、CID/handle、事件和当前状态；
- 对旧 generation 变成无害 no-op；
- 遵守已证明的 packet/free ownership；
- 不阻塞、不调用 UI、不执行未证明的回滚；
- 在进入 retained registration 前发布 resident barrier。

---

## 11. C SDK

### 11.1 编译模型

初始 profile：

```text
--target=arm-none-eabi
-mcpu=cortex-m33
-mthumb
-mfloat-abi=soft
-ffreestanding
-fno-common
-fno-builtin
-fno-stack-protector
-fno-unwind-tables
-fno-asynchronous-unwind-tables
-fdata-sections
-Os
-Wall -Wextra -Werror
```

真实参数来自 target pack，不写死在模块工程中。

### 11.2 公共 API

- 固定宽度 ABI 类型；
- module descriptor；
- command/status protocol；
- sequence snapshot；
- lifecycle transition；
- resource tracker；
- callback generation guard；
- bounded buffer/string helper；
- diagnostics/event writer；
- target capability query；
- host fake target。

### 11.3 固件调用

生成代码：

```c
typedef int (*canopus_fw_clock_gettime_fn)(uint32_t, void *);

static inline int canopus_fw_clock_gettime(uint32_t id, void *value)
{
    return ((canopus_fw_clock_gettime_fn)(uintptr_t)
        CANOPUS_TARGET_CLOCK_GETTIME_CALLABLE)(id, value);
}
```

生成器负责 Thumb callable bit、原型、地址和 policy。模块作者不应手写十六进制地址。

### 11.4 两种权限级别

- **Managed API module**：只使用已批准的 Canopus API/veneer；适合普通开发者。
- **Native full-trust module**：允许显式 raw target API；必须使用高信任签名，并在 UI 中持续显示风险。

由于两者最终都运行 native code，Managed API 主要提供审核和误用防护，不构成恶意代码沙箱。

---

## 12. Rust SDK

### 12.1 初始约束

```rust
#![no_std]
```

- target：`thumbv8m.main-none-eabi` 或 target pack 指定的等价目标；
- `panic = "abort"`；
- 禁止 unwind；
- 默认无 allocator；
- 默认无线程、TLS 和 async executor；
- callback 使用 `extern "C"`；
- panic 不得跨越 FFI；
- 固件函数调用集中在生成的 `unsafe` target crate；
- 安全 wrapper 只有在 ABI 和所有权足够明确时提供。

### 12.2 Rust 工程结构

```text
canopus-sdk-rs/
├── canopus-abi
├── canopus-runtime
├── canopus-target-generated
├── canopus-macros
├── canopus-host-fake
└── examples/no-heap-counter
```

### 12.3 输出方式

建议流程：

```text
Rust no_std staticlib/object
+ generated C constructor/destructor shim
+ target adapter
→ clang/lld relocatable link (-r)
→ final ELF32 ET_REL
→ Canopus ELF verifier
```

必须验证最终对象，而不是假设 Rust 编译器生成的 section、重定位和 personality symbol 被 stock loader 支持。

### 12.4 初始 Rust 示例

第一个 Rust 真机模块应只做：

- identity guard；
- fixed status record；
- 无 heap 状态机；
- 可安全 load/read/stop/unload。

在 Rust relocations、panic、callback 和静态内存模型被证明前，不直接迁移 Bluetooth resident 模块。

---

## 13. 构建系统与多目标产物

### 13.1 模块工程

```toml
# Canopus.toml
[module]
id = "org.example.a2dp-source"
version = "0.1.0"
language = "c"
lifecycle = "resident-after-activation"
canopus_abi = "1"

[targets]
include = [
  "xiaomi-band-10-pro-3.101.030",
  "xiaomi-band-future-version"
]

[capabilities]
required = [
  "classic.adapter",
  "classic.l2cap",
  "classic.sdp",
  "bt.timer",
  "native-app.register",
  "launcher.entry",
  "ui.dispatch"
]

[native_app]
enabled = true
app_id = "org.example.a2dp-source.manager"
name = "A2DP Source"
icon = "assets/icon.bin"
entry = "a2dp_app_create"
role = "module-manager"
```

`native_app` 是可选项。省略时模块是 headless；存在时 build planner 必须同时验证 app registry/UI target capabilities 和 resident lifecycle。

### 13.2 构建矩阵

```text
module source × target pack × feature set × toolchain profile
```

每个组合生成：

- target-specific ELF；
- ELF verifier report；
- 使用符号清单；
- capabilities 清单；
- 固件身份；
- artifact SHA-256；
- SBOM；
- reproducibility metadata；
- 测试结果。

### 13.3 两种多版本策略

#### 静态特化（MVP，默认）

每个固件生成一个完全独立的 ELF。优点是简单、可审计、适应当前 zero-import loader。

#### Runtime indirection（未来）

同一个模块通过 Core 提供的已验证 API table 适配多个 target。只有在目标能安全发现 Core、传递 context 并保持 Core 驻留后才能启用，不能作为首版假设。

### 13.4 ELF verifier

必须检查：

- ELF class、machine、type、endianness；
- ARM EABI 和 Thumb callable bits；
- undefined symbols；
- relocation allowlist；
- constructor/destructor 数量；
- `.canopus.meta`；
- section 权限、大小、对齐；
- unexpected `.got`、TLS、unwind、exception section；
- 绝对地址是否全部来自当前 target pack；
- forbidden symbol；
- 大栈帧和可疑无限循环；
- writable executable section；
- staged payload 与审核 ELF 字节一致。

---

## 14. 模块包格式

建议扩展名：`.canopus`。扩展名不参与安全判断。

```text
module.canopus
├── manifest.cbor
├── manifest.json              # 可选的人类可读镜像
├── artifacts/
│   ├── xiaomi-band-10-pro-3.101.030/module.elf
│   └── other-target/module.elf
├── apps/
│   ├── launcher-entry.cbor
│   ├── pages.cbor
│   └── target-specific/
├── reports/
│   ├── elf-verifier.json
│   ├── symbols-used.json
│   └── sbom.json
├── resources/
├── signature.ed25519
└── certificate-chain/
```

manifest 包含：

- package/module ID；
- 版本与 build generation；
- Canopus ABI；
- lifecycle class；
- 每个 artifact 对应的 exact target；
- artifact hash；
- required/optional capabilities；
- control/data ABI；
- 可选 native app ID、launcher metadata、UI ABI、页面 entry、资源 hash；
- 模块和应用是一体、伴生还是独立载荷；
- reboot/update/remove 语义；
- target pack revision；
- 签名 key ID；
- 最低管理器版本。

签名覆盖 canonical manifest 和所有 payload hash。

---

## 15. 原生应用、Launcher 与 UI 子系统

### 15.1 一等架构能力

Canopus 不能把设备端 UI 永久限制为 JavaScript 快应用。对支持原生应用框架的目标，模块应当能够制作并注册真正的 native app，使它与固件内置应用一样：

- 具有稳定 app ID、名称、图标和 launcher 入口；
- 使用目标固件的原生 UI toolkit、页面栈、路由和事件循环；
- 收到 create/start/resume/pause/stop/destroy 等真实生命周期；
- 调用经过 target pack 批准的固件服务；
- 与模块内核逻辑通过明确 ABI 交互；
- 被 Canopus Manager 安装、启用、禁用、更新和删除；
- 在 launcher 中由用户主动启动，而不是依赖打开某个 JS 表盘或快应用页面。

Canopus Manager 本身的长期形态也应是一个 Canopus native app；JS/Lua UI 只作为 bootstrap 和早期 target adapter。

### 15.2 三种产品形态

#### Headless module

只有后台/内核逻辑，不注册 launcher 应用。适用于协议扩展、驱动、hook 和系统服务。

#### Integrated module app

模块代码、原生应用页面和管理逻辑位于同一个 target artifact，或位于同一包内且共享明确 ABI。应用既是用户功能，也是模块管理页。这是推荐形态：

```text
launcher
→ native app lifecycle
→ module UI/controller layer
→ module service/state machine
→ typed firmware capabilities
```

#### Standalone native app carried by a module

模块主要承担安装、注册、固件 veneer 和必要的特权逻辑；原生应用拥有独立功能。即使应用不需要持续后台逻辑，模块仍可以作为目标固件不具备通用 native-app installer 时的注册载体。

### 15.3 当前证据边界

当前固件已经证明可以安装基于 JS 的快应用，也证明 stock modlib 可以载入 native ELF；但“第三方 native app 的注册表、launcher descriptor、页面工厂和卸载 ABI”尚未完成 exact-target 证明。

因此首版架构必须把以下内容作为独立逆向和真机 gate，而不能假定已有 Lua/LVGL 页面等价于原生应用注册：

1. launcher 如何枚举应用；
2. app descriptor 的结构、所有者和存储位置；
3. 注册/注销函数及返回语义；
4. app ID 冲突和持久化规则；
5. icon、名称、本地化和资源加载格式；
6. 页面/窗口创建工厂；
7. UI thread/dispatcher；
8. 生命周期 callback 及参数所有权；
9. launcher 是否保存函数指针；
10. 应用关闭后 descriptor 是否仍被引用；
11. 原生应用能否跨 reboot 持久注册，或必须由 Canopus 每次启动恢复；
12. 注销是否真正同步，还是需要 reboot。

在这些 ABI 被证明前，Canopus 只能提供 bootstrap manager UI，不能宣称已经支持通用 native app 安装。

### 15.4 Native app descriptor

公共逻辑模型如下，目标 adapter 可以生成不同的实际布局：

```c
struct canopus_native_app_descriptor_v1 {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint8_t app_id[48];
    uint8_t display_name[48];
    uint32_t icon_resource_id;

    int32_t (*create)(const struct canopus_app_context_v1 *context);
    int32_t (*start)(void);
    int32_t (*resume)(void);
    int32_t (*pause)(void);
    int32_t (*stop)(void);
    int32_t (*destroy)(void);
};
```

该描述符不是对目标固件布局的猜测。target generator 负责把公共模型转换为 exact-target launcher/app descriptor，并生成 constructor registration glue。

### 15.5 App lifecycle 与 module lifecycle

原生应用注册会把模块推入更严格的生命周期：

```text
module PREPARING
→ publish resident barrier
→ register native app descriptor
→ launcher 可见
→ APP_REGISTERED / BOOT_RESIDENT
```

只要 launcher、app manager 或 UI router 可能保存模块中的 descriptor、字符串、icon 指针或 callback，模块就不能卸载。

禁用/删除顺序必须是：

```text
阻止新 launch
→ 关闭或等待所有 app instance
→ drain UI event/callback
→ 从 launcher/app registry 注销
→ 证明 descriptor 和资源无引用
→ 再判断模块是否可卸载
```

如果任一步缺少同步注销证据，则操作只能标记为 `DISABLED_NEXT_BOOT` 或 `REMOVE_PENDING`，不得调用 `rmmod`。

### 15.6 UI 线程和后台模块隔离

- Bluetooth、timer、ISR、worker callback 不得直接创建或更新 UI；
- callback 只写入 bounded event/state，并投递到已证明的 UI dispatcher；
- app 页面只能通过 versioned module API 控制后台逻辑；
- UI 被销毁不能释放 resident callback 仍使用的数据；
- app instance generation 与 module generation 分开；
- 页面重复打开、后台/前台切换和 launcher 强制关闭必须可重入；
- 模块功能不应因 UI 页面暂时不存在而失效，除非 manifest 明确声明 app-scoped service。

### 15.7 Module/App 内部通信

Integrated module app 可以在同一 ELF 内直接调用，但仍建议使用显式接口：

```c
struct canopus_module_app_api_v1 {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    int32_t (*get_status)(void *record, uint32_t capacity);
    int32_t (*submit)(const void *command, uint32_t size);
    int32_t (*subscribe)(canopus_app_event_fn callback, void *cookie);
    int32_t (*unsubscribe)(uint32_t subscription_id);
};
```

这样可以：

- 对 UI 和后台状态机分别做 host tests；
- 将来把 app 和 module 拆成不同载荷；
- 防止 UI 直接修改 module private state；
- 正确处理 async accepted/completed；
- 为 Rust UI 提供稳定 C ABI。

### 15.8 原生 UI SDK

App SDK 应提供：

- app descriptor 和 lifecycle wrapper；
- launcher metadata；
- UI thread assertion/dispatcher；
- page/view 生命周期；
- bounded event subscription；
- resources、icon、字体、本地化；
- navigation/router；
- module status/command binding；
- target capability detection；
- host-side fake UI runtime；
- C API 和 Rust wrapper。

公共 SDK 不规定目标必须使用 LVGL、ArkUI 或某个 Vela 私有框架。target pack 声明并生成具体 UI backend。

### 15.9 Rust 原生应用

Rust app 初期采用：

- Rust 业务和状态逻辑；
- generated C ABI lifecycle entry；
- C/C++ target UI shim；
- `no_std`；
- UI 对象由 target shim 拥有，Rust 使用 opaque handle；
- 所有 callback `extern "C"` 且不得 unwind；
- retained closure 必须被转换为 resident、generation-checked cookie。

只有目标 UI allocator、异常模型和 C++ ABI 被证明后，才允许 Rust 直接绑定复杂 UI object。

### 15.10 资源和安装

包中的 app 资源必须参与签名：

- icon；
- 本地化名称；
- 图片/字体；
- 页面描述；
- app/launcher metadata；
- target-specific compiled resources。

Manager 安装时检查：

- app ID 是否冲突；
- app/module 签名者是否一致；
- target 是否支持所需 UI backend；
- launcher slot/resource 限制；
- native app 是否要求 resident module；
- 删除是否需要 reboot。

### 15.11 安全问题

原生应用额外引入：

- launcher/UI spoofing；
- app ID 抢占；
- 假冒系统设置页；
- UI thread deadlock；
- 恶意全屏或无法退出页面；
- 图标/资源 parser 攻击；
- app callback 在模块删除后执行；
- 普通 UI 触发高风险固件操作。

Manager 必须显示签名者和“系统/Canopus/第三方”来源，不允许第三方使用保留 app ID 或伪装系统应用。高风险模块命令需要独立确认，而不能仅依赖应用自身 UI。

---

## 16. 设备端 Manager

### 16.1 组成

```text
Canopus Manager UI
        ↓ versioned control protocol
Canopus supervisor/service
        ↓
target loader adapter + module control endpoints
```

MVP 可使用当前已证明的 Lua/NSH/字符设备能力，但 UI 只能是 client。最终 authoritative 状态必须由 supervisor 的持久化状态和模块状态提供。

### 16.2 UI 页面

#### 设备

- 型号、board、CPU、架构；
- 固件版本、build string、完整哈希验证状态；
- target ID、target pack revision；
- loader profile；
- framework ABI；
- safe mode、reboot required、last boot status。

#### 模块列表

- 名称、版本、签名者；
- installed/enabled/active；
- removable/resident；
- 当前 target artifact；
- capabilities；
- update/rollback/quarantine；
- 错误和日志。

#### 模块详情

- manifest；
- artifact hash；
- exact firmware compatibility；
- 资源、callback、timer、worker、hook；
- 当前生命周期；
- enable/disable/update/remove 的实际生效时间；
- 风险等级和恢复方式。

#### 框架状态

- supervisor 状态；
- package store 一致性；
- 已加载模块；
- resident modules；
- pending operations；
- event log；
- safe-mode controls。

### 16.3 操作语义

#### Install

```text
读取包
→ 验签
→ schema 校验
→ exact target 匹配
→ artifact hash/ELF verifier
→ capability/policy 审核
→ 临时目录
→ 原子 rename
→ INSTALLED/DISABLED
```

#### Enable（next-boot，所有 lifecycle class）

所有类别统一为 next-boot 语义：`ENABLE` 只记录 boot intent `ENABLED`、把状态改为 `ENABLED`（“下次启动启用”），并返回 `REBOOT_REQUIRED`。当前 boot 绝不 insmod；模块只在下次 supervisor 加载时由 boot restore 实际加载。safe mode 下拒绝（激活类操作）。

#### Disable（next-boot，所有 lifecycle class）

统一 next-boot：`DISABLE` 记录 intent `DISABLED`。模块已加载（ACTIVE/READY/BOOT_RESIDENT）时状态改为 `DISABLED_NEXT_BOOT`（代码仍驻留到重启）；未加载时改为 `DISABLED`。一律返回 `REBOOT_REQUIRED`，不做 stop/drain/unload（热插拔路径已移除）。safe mode 允许（只读、不执行第三方代码）。

#### Remove（next-boot，所有 lifecycle class）

统一 next-boot：`REMOVE` 记录 intent `REMOVE`、状态改为 `REMOVE_PENDING`，返回 `REBOOT_REQUIRED`。代码驻留到重启；下次 boot restore 删除 inbox 的 `.cmi`/`.ko` 并回收 slot。safe mode 允许。

#### Update/Rollback

active resident module 永不原地替换。新版本进入 staged slot，下次启动切换；保留 previous slot 用于回滚。操作记录 intent `ENABLED` 并返回 `REBOOT_REQUIRED`。

#### 持久化（模块注册表）

模块 slot（module_id、lifecycle class、version、flags、boot intent）在每次变更时原子写入 `/data/canopus/registry.bin`（固定 784 字节格式，tmp + rename）。重启 / 重装 canopus 后，supervisor 构造函数读取注册表并恢复 slot：intent `ENABLED` 的模块立即加载，intent `DISABLED` 的保持未加载，intent `REMOVE` 的删除 inbox 文件并丢弃 slot。安装默认记 intent `DISABLED`，因此“安装即禁用、重启生效”。注册表缺失或 magic 非法视为全新安装（非错误）。

### 16.4 Package store

```text
/data/canopus/
├── state.cbor
├── registry.bin        # 模块注册表（固定 784 字节：16 slot x {id[32],class,version,flags,intent} + header）
├── registry.tmp        # 原子写临时文件
├── inbox/              # 安装接收收件箱：<token>.cmi（签名回执）+ <token>.ko（ELF），持久化
│   ├── manager.ko      # （split 落地后）Manager core 模块
├── targets/
├── packages/
│   └── package-id/
│       ├── active/
│       ├── previous/
│       ├── staged/
│       └── quarantined/
├── logs/
└── recovery/
```

实际路径由 target adapter 决定。所有状态写入采用 write-temp、flush、atomic rename；不直接覆盖 active manifest。`registry.bin` 由 supervisor 在每次 slot 变更（INSTALL/ENABLE/DISABLE/REMOVE/UPDATE）时原子写入；boot restore 在 supervisor 构造函数中读取并应用 intent。

### 16.5 Safe mode

触发条件可以包括：

- 上一 boot 未达到 READY；
- resident module fail-stop；
- crash/watchdog counter；
- 用户在启动窗口请求 safe mode；
- package state 校验失败。

Safe mode 默认不加载第三方模块，只启动最小 supervisor 和恢复 UI。

---

## 17. Control ABI 与诊断

### 17.1 控制头

```c
struct canopus_control_header_v1 {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t command;
    uint32_t request_id;
    uint32_t payload_size;
    uint32_t flags;
};
```

响应必须区分：

```text
REJECTED
ACCEPTED
QUEUED
RUNNING
COMPLETED
FAILED
DISALLOWED
REBOOT_REQUIRED
```

### 17.2 Status record

沿用已验证的 sequence snapshot 思路：

1. writer 将 sequence 置为奇数；
2. 写入固定宽度字段；
3. 发布相同偶数 begin/end sequence；
4. reader 只有在 begin == end 且为偶数时接受。

ABI 采用 append-only record 或 TLV，不复用旧字段表达新含义。

### 17.3 Event log

每条事件至少包含：

- monotonic sequence；
- timestamp（若 capability 可用）；
- boot ID；
- module ID/build ID；
- target ID；
- lifecycle before/after；
- command/request ID；
- result/error；
- reboot required；
- 可选 evidence/capture ID。

### 17.4 Control plane 与 data plane

控制面只传递小型命令、状态和 bounded event。音频、抓包、大型 snapshot 和 bulk log 必须使用独立 bounded stream，避免阻塞 control path。

---

## 18. Hook 与 Patch 子系统

Hook/patch 不是 MVP 的默认功能，应作为高风险 target capability：

- patch site 必须有原始字节 hash；
- compare-before-write；
- 明确 Thumb instruction boundary；
- trampoline 和 cache maintenance 必须经目标证明；
- 记录 patch ledger；
- 支持幂等检测；
- 声明是否可撤销；
- callback inflight 时禁止撤销；
- 多模块 patch 冲突检测；
- 未知原始字节立即拒绝；
- 默认只允许生产签名 key；
- LLM 不得直接生成并部署 patch。

无法证明安全撤销的 patch 一律是 `PATCH_REBOOT_REQUIRED`。

---

## 19. 安全与信任模型

### 19.1 事实边界

原生模块与内核/固件运行在同一地址空间时，Canopus 不能阻止恶意代码：

- 访问其他模块内存；
- 跳转到任意地址；
- 绕过 capability table；
- 破坏 manager 状态；
- 触发 watchdog 或永久 callback。

因此 capability system 是开发、审核、签名和 UI 风险边界，不是硬件沙箱。

### 19.2 信任等级

- **Production trusted**：官方/设备维护者签名；
- **Reviewed third-party**：通过源码、符号和 artifact 审核；
- **Developer**：设备明确开启开发模式后允许；
- **Unsigned**：默认拒绝。

开发模式必须持续显示、可撤销，并禁止悄悄混入 production package store。

### 19.3 签名

分别管理：

- target pack signing key；
- module package signing key；
- development key；
- revocation metadata key。

建议使用 Ed25519 或等价成熟方案。设备每次 load 前重新验证签名和 payload hash，而非只在安装时验证。

### 19.4 防回滚

如设备存在可信 monotonic storage，则保存最低 generation。否则至少：

- manifest 记录版本/generation；
- UI 显示 downgrade；
- production policy 默认拒绝已撤销版本；
- signed revocation list；
- previous slot 只允许已知良好版本。

---

## 20. 测试与验证

### 20.1 Host tests

- ABI size/alignment；
- lifecycle state machine；
- resource tracker/double free；
- callback generation；
- sequence snapshot；
- manifest canonicalization；
- signature and revocation；
- target selection；
- symbol/type schema；
- build graph；
- C/Rust FFI；
- protocol parser fuzzing；
- package rollback transaction。

### 20.2 Artifact tests

- zero unexpected undefined symbols；
- relocation allowlist；
- target identity embedded；
- absolute address allowlist；
- no forbidden symbol；
- no unwind/TLS/GOT surprises；
- constructor/destructor；
- section and storage limits；
- byte-identical staged payload；
- manifest hash/signature；
- reproducible build。

### 20.3 Device gate 顺序

```text
G0  zero-import load/unload
G1  wrong-firmware rejection
G2  status/control ABI
G3  clock/read-only capability
G4  character-device lifecycle
G5  removable drain/unload
G6  resident barrier
G7  callback/timer/worker ownership
G8  native app/launcher 注册、启动、关闭和注销
G9  subsystem registration
G10 protocol functionality
G11 data plane
G12 reboot disable/update/rollback
G13 safe mode and quarantine
```

不能在较低 gate 失败时继续更高风险测试。

### 20.4 Hardware evidence

每次真机测试保存：

- target identity；
- target pack revision；
- package/module hash；
- manager/core version；
- 启用模块集合；
- status/event records；
- boot/reboot 边界；
- 系统日志和相关抓包；
- 最后成功 gate；
- 恢复方法；
- reviewer verdict。

---

## 21. CI/CD

```text
schema/format
→ host C tests
→ host Rust tests
→ fuzz/property tests
→ ABI compatibility
→ target database validation
→ evidence/provenance validation
→ build matrix
→ ELF verification
→ package assembly
→ signature verification
→ reproducibility
→ docs/progress consistency
→ optional hardware-in-loop
→ release approval
```

以下条件必须使 CI 失败：

- target artifact 没有完整 firmware hash；
- symbol 没有 prototype 或 provenance；
- `DEVICE_PROVEN` 没有设备证据；
- withdrawn/forbidden symbol 被生成 veneer；
- resident package 暴露立即 unload；
- staged payload 与 ELF 不一致；
- Rust 产物包含未知 unwind/TLS/relocation；
- LLM candidate 未审核便进入 exported capability；
- 包签名没有覆盖 payload hash；
- 文档声称的 target 支持没有对应测试结果。

---

## 22. 实施阶段

### Phase 0：项目边界和 schema

定义独立仓库、术语、target/symbol/type/evidence/module/package schema，并只读导入当前研究证据。

### Phase 1：Host CLI 与 target registry

实现 `canopus target`、`symbol`、`build-plan`、`verify` 基础命令和生成器。

### Phase 2：C ABI 与 portable runtime

提取 sequence snapshot、resource tracker、lifecycle、diagnostics，完成 host tests。

### Phase 3：首个 target adapter

编码当前 Cortex-M33/Thumb/soft-float/zero-import loader profile，生成 typed veneers 和 verifier。

### Phase 4：C module build/package

实现 `Canopus.toml`、多 target 构建矩阵、`.canopus` 包、签名和可复现报告。

### Phase 5：Device supervisor MVP

先基于已证明的字符设备和显式 loader 操作实现，不假设尚未证明的 daemon/thread API。

### Phase 6：Native App 与 Launcher adapter

逆向并验证 exact-target app registry、launcher descriptor、UI dispatcher 和应用生命周期；先注册无特权原生示例应用，再让 Canopus Manager 转为 native app。

### Phase 7：Manager UI

实现设备页、模块列表、详情、安装/启用/禁用/删除/更新/回滚/safe mode。

### Phase 8：Rust SDK

完成 `no_std` ABI、C shim、relocatable link、ELF verifier 和首个可卸载 Rust 示例。

### Phase 9：RE orchestrator

整合 MCP、evidence bundle、review workflow、symbol promotion 和 withdrawn history。

### Phase 10：迁移参考模块

先迁移 harmless probe，再迁移 control/lifecycle，最后迁移 boot-resident Bluetooth/A2DP 参考模块。

### Phase 11：第二目标

必须引入第二设备或第二固件版本，以识别公共 SDK 中隐藏的单目标假设。

### Phase 12：安全、发布与生态

生产/开发 key、revocation、包仓库、SDK 文档、模板、HIL 和 release policy。

---

# 23. 完整任务进度追踪区

## 23.1 状态定义

| 状态 | 含义 |
|---|---|
| `BACKLOG` | 已记录，尚未满足启动条件 |
| `READY` | 依赖已满足，可以开始 |
| `IN_PROGRESS` | 正在实现 |
| `BLOCKED` | 被明确依赖或证据阻塞 |
| `REVIEW` | 等待代码/架构/证据审核 |
| `DEVICE_TEST` | 等待或正在真机验证 |
| `DONE` | 验收标准和证据均满足 |
| `FAILED` | 本次实现/假设失败，保留记录 |
| `WITHDRAWN` | 结论撤销，禁止删除历史 |
| `DEFERRED` | 有意延期 |

## 23.2 更新规则

每个任务变更必须同步：

- 状态；
- owner；
- dependency；
- artifact/commit hash（如适用）；
- host/device test；
- evidence URI；
- reviewer；
- next action；
- reboot/recovery 约束。

`DONE` 不等于“代码已写”，而是验收标准全部满足。失败或撤销任务不得从表中删除。

## 23.3 总览

| 范围 | DONE | 进行中 | 未开始/阻塞 | 说明 |
|---|---:|---:|---:|---|
| 参考证据 | 4 | 0 | 0 | 来自当前固件研究原型 |
| 架构与治理 | 2 | 0 | 6 | 本文完成首版架构；仓库/许可证已确定（AGPL-3.0，ADR-CAN-001） |
| Host/Schema/CLI | 9 | 1 | 2 | SCH-001..006/CLI-001..003 DONE；CLI-004 进行中；CLI-005/006 未开始 |
| C SDK/Runtime | 9 | 1 | 0 | C-001..009 DONE；C-010 host 部分完成，真机 G0 待测 |
| Target/Build/Package | 11 | 1 | 2 | TGT-001..006、BLD-001..004、PKG-001..003 DONE；PKG-004 进行中 |
| Device Manager | 2 | 1 | 6 | DEV-002/003、UI-001..004 DONE；DEV-001 进行中；DEV-004..009 待真机 |
| Native App/Launcher/UI | 2 | 4 | 9 | APP-008/011 DONE；APP-001/002/003/004 逆向中（枚举源+descriptor+register/unregister 静态恢复）；其余待真机 |
| Rust SDK | 8 | 1 | 0 | RUST-001..006/008/009 DONE；RUST-007 host+verifier PASS，真机 G0 待测 |
| RE/LLM/MCP | 9 | 0 | 0 | RE-001..009 DONE（canopus-re crate + `canopus re` CLI） |
| 迁移/多目标/发布 | 5 | 1 | 7 | REL-001/002/004、MULTI-003/004 DONE；REL-003 进行中 |

> 注：参考原型的完成度不能计作 Canopus framework 已实现。

## 23.4 参考证据基线

| ID | 状态 | 任务 | 验收/证据 |
|---|---|---|---|
| `CAN-BASE-001` | `DONE` | 固件 exact identity 基线 | 完整 SHA-256、version、build string 已记录 |
| `CAN-BASE-002` | `DONE` | stock ELF module loader 基线 | load/unload、constructor/destructor、relocation envelope 已探测 |
| `CAN-BASE-003` | `DONE` | removable/resident 生命周期原型 | control reference、drain、rollback、boot-resident 边界已有代码与设备证据 |
| `CAN-BASE-004` | `DONE` | 复杂固件功能参考模块 | Classic pairing、AVDTP、media L2CAP、345 RTP/1725 SBC、SUSPEND 已实机证明；实时 pacing 仍是模块后续项 |

## 23.5 架构与治理任务

| ID | 状态 | 任务 | 依赖 | 验收标准 |
|---|---|---|---|---|
| `CAN-ARCH-001` | `DONE` | Canopus 完整架构文档 v1 | 无 | 本文存在并含进度区 |
| `CAN-ARCH-002` | `DONE` | 决定独立仓库和许可证 | ARCH-001 | 仓库、license、贡献策略确定 |
| `CAN-ARCH-003` | `BACKLOG` | 术语和 ABI version policy | ARCH-002 | glossary 与兼容规则审核通过 |
| `CAN-ARCH-004` | `BACKLOG` | Security threat model | ARCH-001 | native trust boundary、key、rollback、resident 风险审核 |
| `CAN-ARCH-005` | `BACKLOG` | Target maintainer policy | ARCH-002 | target pack owner/reviewer/promotion 权限明确 |
| `CAN-ARCH-006` | `BACKLOG` | Module signing policy | ARCH-004 | production/developer/revocation policy |
| `CAN-ARCH-007` | `BACKLOG` | Release/versioning policy | ARCH-003,006 | SDK/ABI/package/target 独立版本规则 |
| `CAN-ARCH-008` | `BACKLOG` | Evidence retention policy | ARCH-005 | failed/withdrawn evidence 永久保留规范 |

## 23.6 Schema、Host CLI 和 Registry

| ID | 状态 | 任务 | 依赖 | 验收标准 |
|---|---|---|---|---|
| `CAN-SCH-001` | `DONE` | target schema | ARCH-003 | JSON Schema + valid/invalid fixtures |
| `CAN-SCH-002` | `DONE` | symbol schema | SCH-001 | 地址、原型、上下文、所有权、policy、proof 完整；含 WITHDRAWN 条件约束 |
| `CAN-SCH-003` | `DONE` | type/layout schema | SCH-001 | size/alignment/field/bitfield/layout diff |
| `CAN-SCH-004` | `DONE` | evidence schema | ARCH-008 | provenance、verdict、review、artifact hash |
| `CAN-SCH-005` | `DONE` | module manifest schema | ARCH-003 | lifecycle/capabilities/targets/control ABI |
| `CAN-SCH-006` | `DONE` | package manifest schema | SCH-005,ARCH-006 | multi-artifact hash/signature/revocation |
| `CAN-CLI-001` | `DONE` | CLI skeleton | ARCH-002 | Rust clap 多子命令；version/error model 就绪 |
| `CAN-CLI-002` | `DONE` | `canopus target validate` | SCH-001,CLI-001 | fixture tests 通过 |
| `CAN-CLI-003` | `DONE` | `canopus symbol validate` | SCH-002,003,004 | schema + policy/provenance checks |
| `CAN-CLI-004` | `IN_PROGRESS` | Registry revision model | SCH-001..004 | targets/ 目录 registry 已实现；revision 签名未完成 |
| `CAN-CLI-005` | `BACKLOG` | Target diff command | CLI-002,003 | firmware/symbol/layout/capability diff |
| `CAN-CLI-006` | `BACKLOG` | Evidence query/report | CLI-003 | symbol→证据→测试双向查询 |

## 23.7 C ABI 与 Runtime

| ID | 状态 | 任务 | 依赖 | 验收标准 |
|---|---|---|---|---|
| `CAN-C-001` | `DONE` | 固定宽度 module descriptor | ARCH-003,SCH-005 | C static asserts + ABI fixture |
| `CAN-C-002` | `DONE` | Context/capability ABI | C-001 | versioned tables，无 target private type |
| `CAN-C-003` | `DONE` | Command/status ABI | C-001 | request ID、async states、unknown version tests |
| `CAN-C-004` | `DONE` | Sequence snapshot library | C-003 | race/partial snapshot host tests |
| `CAN-C-005` | `DONE` | Lifecycle state machine | C-001 | illegal transition/property tests |
| `CAN-C-006` | `DONE` | General resource tracker | C-005 | reverse rollback、detached、retained states |
| `CAN-C-007` | `DONE` | Callback generation guards | C-005 | stale callback/timer tests |
| `CAN-C-008` | `DONE` | Diagnostics/event writer | C-003,004 | bounded append + dropped counter |
| `CAN-C-009` | `DONE` | Host fake target | C-002 | allocator/timer/callback/driver fakes |
| `CAN-C-010` | `IN_PROGRESS` | C example removable module | C-001..009 | host tests 通过；真机 load/unload 待 G0 gate |

## 23.8 首个 Target、Build 与 Package

| ID | 状态 | 任务 | 依赖 | 验收标准 |
|---|---|---|---|---|
| `CAN-TGT-001` | `DONE` | 创建 xiaomi-band-10-pro-3.101.030 target pack | SCH-001..004 | identity/loader/38 symbols/types/evidence 完整 |
| `CAN-TGT-002` | `DONE` | 导入已证明 symbols/types | TGT-001,BASE-* | 38 symbols + 3 types + 4 evidence bundles，状态不自动提升 |
| `CAN-TGT-003` | `DONE` | Loader profile | TGT-001 | best1503-v1 relocation envelope 已编码进 target.toml |
| `CAN-TGT-004` | `DONE` | Runtime identity generator | TGT-001 | 生成 canopus_identity_guard（版本+build 字符串） |
| `CAN-TGT-005` | `DONE` | C typed veneer generator | TGT-002,C-002 | generate-veneer；managed 符号生成 Thumb veneer，FORBIDDEN/restricted 只留审计注释 |
| `CAN-TGT-006` | `DONE` | Rust binding generator | TGT-002,RUST-002 | `generate-rust-bindings`；managed 生成 unsafe binding，FORBIDDEN/restricted 只留审计注释 |
| `CAN-BLD-001` | `DONE` | Declarative build planner | SCH-005,TGT-003 | build-plan 展开 module×target 并校验 capabilities |
| `CAN-BLD-002` | `DONE` | C cross-build backend | BLD-001,C-010 | clang+ld.lld -r 产出 ET_REL，verifier PASS（sha256 a0f73378） |
| `CAN-BLD-003` | `DONE` | Generic ELF verifier | TGT-003 | canopus-elf；真机 probe sha256=0f47e2e1 PASS；undefined/relo/ctor 检查 |
| `CAN-BLD-004` | `DONE` | Absolute-address policy scan | TGT-002,BLD-003 | verifier 拒绝 SHN_ABS 目标不在 allowlist 的 relo |
| `CAN-PKG-001` | `DONE` | Canonical package format | SCH-006 | deterministic tar（sorted+mtime=0），重建 byte-identical |
| `CAN-PKG-002` | `DONE` | Ed25519 sign/verify | PKG-001,ARCH-006 | tamper/wrong-key 拒绝；签名覆盖 manifest+payload 规范摘要 |
| `CAN-PKG-003` | `DONE` | Multi-target artifact bundle | BLD-002,PKG-001 | manifest 多 artifact 逐项 hash 校验后嵌入 |
| `CAN-PKG-004` | `IN_PROGRESS` | SBOM/reproducibility report | BLD-002,PKG-001 | determinism 已证明；正式 SBOM/报告未生成 |

## 23.9 Device Supervisor 与 Manager

| ID | 状态 | 任务 | 依赖 | 验收标准 |
|---|---|---|---|---|
| `CAN-DEV-001` | `IN_PROGRESS` | Device identity reader | TGT-004 | veneer identity guard 已生成；supervisor 读取逻辑待接线 |
| `CAN-DEV-002` | `DONE` | Versioned supervisor protocol | C-003 | canopus_protocol；malformed/old-ABI/async-state 测试通过 |
| `CAN-DEV-003` | `DONE` | Package store transaction | PKG-001,002 | canopus_store；temp+fsync+rename、previous/staged/quarantine 测试通过 |
| `CAN-DEV-004` | `BACKLOG` | Target loader adapter | TGT-003,DEV-001 | exact payload load/status |
| `CAN-DEV-005` | `BACKLOG` | Module state persistence | DEV-002,003 | enabled/active/pending 不混淆 |
| `CAN-DEV-006` | `BACKLOG` | Removable disable/unload | C-005,006,DEV-004 | drain 后 rmmod，busy 语义正确 |
| `CAN-DEV-007` | `BACKLOG` | Resident barrier enforcement | C-005,007,DEV-004 | 激活后无 unload path |
| `CAN-DEV-008` | `BACKLOG` | Next-boot disable/remove/update | DEV-003,005,007 | reboot 后正确切换 slot |
| `CAN-DEV-009` | `BACKLOG` | Quarantine/safe mode | DEV-005,008 | fail boot 后最小恢复启动 |
| `CAN-UI-001` | `DONE` | Manager device page | DEV-001,002 | canopus_manager 设备页：identity/target/framework/safe-mode |
| `CAN-UI-002` | `DONE` | Module list/detail | DEV-005 | 列表/详情：lifecycle/class/signature/risk |
| `CAN-UI-003` | `DONE` | Install/update/rollback UI | DEV-003,008 | op 走 versioned protocol + transport 抽象 |
| `CAN-UI-004` | `DONE` | Disable/remove/reboot UI | DEV-006..009 | lifecycle-aware：resident 只显示 [disable-next-boot]/[remove+reboot]，无虚假 unload |

## 23.10 Native App、Launcher 与 UI

| ID | 状态 | 任务 | 依赖 | 验收标准 |
|---|---|---|---|---|
| `CAN-APP-001` | `IN_PROGRESS` | 调查 exact-target launcher 应用枚举链 | RE-002 | 枚举源已证明；descriptor/register/unregister 已恢复（EVID-APP-004）；线程上下文待补 |
| `CAN-APP-002` | `IN_PROGRESS` | 恢复 native app descriptor/layout | APP-001,SCH-003 | STATIC_RECOVERED：launcher_app_descriptor（name@8/icon@12/app_id u16@16/flags@18/resolver@28/hidden@60）+ launcher_app_record（u16 id@0/icon@4/name@8/icon_name@12/flags@20）；icon 格式待补 |
| `CAN-APP-003` | `IN_PROGRESS` | 恢复 app register/unregister ABI | APP-001,002 | STATIC_RECOVERED：register=app_launcher_add(desc)、unregister=app_launcher_del(u16 appid)（也是 msg26 hide 路径）；返回值/冲突/释放语义交叉验证待补 |
| `CAN-APP-004` | `BACKLOG` | 恢复 launcher icon/name/resource 格式 | APP-001 | host parser/serializer fixtures |
| `CAN-APP-005` | `BACKLOG` | 恢复 UI toolkit 与 dispatcher ABI | APP-001,RE-004 | UI thread 投递与对象所有权明确 |
| `CAN-APP-006` | `BACKLOG` | 恢复 create/resume/pause/destroy 生命周期 | APP-002,005 | stock app callsites 和状态机证据 |
| `CAN-APP-007` | `BACKLOG` | Native app target capability/profile | APP-002..006,TGT-001 | target pack 可生成但默认 restricted |
| `CAN-APP-008` | `BACKLOG` | 公共 native app descriptor/App SDK C API | APP-007,C-001..003 | 不泄漏 target private layout |
| `CAN-APP-009` | `BACKLOG` | 无特权 Hello Native App probe | APP-007,008 | launcher 可见、可打开/关闭、无 retained leak |
| `CAN-APP-010` | `BACKLOG` | 注册后 resident/unregister 真机 gate | APP-009,C-005..007 | active view/drain/reboot 语义证明 |
| `CAN-APP-011` | `BACKLOG` | Module/App versioned internal API | APP-008,C-003 | status/command/event host tests |
| `CAN-APP-012` | `BACKLOG` | App resource packaging/signing | APP-004,PKG-001,002 | icon/page/resource hash 全部被签名 |
| `CAN-APP-013` | `BACKLOG` | Rust native app wrapper | APP-008,RUST-007 | C UI shim + opaque handles + no unwind |
| `CAN-APP-014` | `BACKLOG` | Canopus Manager 迁移为 native app | APP-009..012,UI-* | launcher 启动、管理功能和 safe mode 可用 |
| `CAN-APP-015` | `BACKLOG` | 独立 native app 模块模板 | APP-011..014 | C/Rust 模板、headless/integrated/standalone 三模式 |

## 23.11 Rust SDK

| ID | 状态 | 任务 | 依赖 | 验收标准 |
|---|---|---|---|---|
| `CAN-RUST-001` | `DONE` | `canopus-abi` no_std crate | C-001..003 | repr(C) 布局与 C header 逐字段对齐（64/32-bit 断言） |
| `CAN-RUST-002` | `DONE` | Generated target crate | TGT-006,RUST-001 | `generate-rust-bindings` + callable/policy/prototype/布局回归测试 |
| `CAN-RUST-003` | `DONE` | Panic-abort runtime | RUST-001 | 设备构建 `panic=abort`；最终 ELF 0 undefined（无 unwind/personality） |
| `CAN-RUST-004` | `DONE` | C constructor/destructor shim | RUST-001,C-001 | canopus_ctor.c；最终 ELF 1 ctor + 1 dtor |
| `CAN-RUST-005` | `DONE` | Relocatable link backend | RUST-003,004,BLD-003 | `ld.lld -r` 产出 ELF32 ET_REL，verifier PASS（sha256 50641841，0 undefined）→ **BLK-003 已解除** |
| `CAN-RUST-006` | `DONE` | Rust host fake target | C-009,RUST-001 | allocator/timer wheel/driver/harness 测试通过 |
| `CAN-RUST-007` | `IN_PROGRESS` | no-heap removable example | RUST-001..006 | host 全生命周期测试 + 交叉构建 verifier PASS；真机 load/status/stop/unload 待 G0 gate |
| `CAN-RUST-008` | `DONE` | Optional allocator API | RUST-007 + allocator proof | `BumpArena` LIFO arena + 5 项 allocator 域测试 |
| `CAN-RUST-009` | `DONE` | Rust callback/resident policy | RUST-007,C-007 | stale-callback 端到端测试 + resident 无 unload path + 设备 panic handler |

## 23.12 RE、LLM 和 MCP

| ID | 状态 | 任务 | 依赖 | 验收标准 |
|---|---|---|---|---|
| `CAN-RE-001` | `DONE` | RE task/evidence store | SCH-004,CLI-006 | `canopus re` store；forward-only 状态机 + append-only audit + JSON 持久化 |
| `CAN-RE-002` | `DONE` | Read-only IDA MCP adapter | RE-001 | 只读 tool allowlist + fail-closed + per-call audit（§9.3 强制） |
| `CAN-RE-003` | `DONE` | Function evidence workflow | RE-002 | decompile+xref+callgraph bundle 装配与渲染 |
| `CAN-RE-004` | `DONE` | Type/layout workflow | RE-002,SCH-003 | TypeEvidence bundle（size/alignment/fields/used_by） |
| `CAN-RE-005` | `DONE` | Signature candidate workflow | RE-003 | decompile→候选原型；只产候选（confidence=10 不自动提升） |
| `CAN-RE-006` | `DONE` | Independent verifier stage | RE-003,004 | Review 记录 + refute/withdraw 支持 |
| `CAN-RE-007` | `DONE` | Human promotion gate | RE-006,ARCH-005 | `evaluate_gate`/`apply_gate`；需人工审批，单个 refute 阻塞 |
| `CAN-RE-008` | `DONE` | Minimal probe generator | RE-007,C-010 | `canopus re probe`；dry probe（只 identity+status），FORBIDDEN 拒绝 |
| `CAN-RE-009` | `DONE` | Target pack revision signer | RE-007,PKG-002 | `canopus re revision-sign/verify`；Ed25519 签名，篡改/wrong-key 拒绝 |

## 23.13 迁移、多目标与发布

| ID | 状态 | 任务 | 依赖 | 验收标准 |
|---|---|---|---|---|
| `CAN-MIG-001` | `BACKLOG` | 迁移 harmless probe | C-010,TGT-001..005 | Canopus build/package/device gates G0-G4 |
| `CAN-MIG-002` | `BACKLOG` | 迁移 removable control module | MIG-001,DEV-006 | G5 通过 |
| `CAN-MIG-003` | `BACKLOG` | 迁移 resident lifecycle skeleton | MIG-002,DEV-007 | G6-G7 通过 |
| `CAN-MIG-004` | `BACKLOG` | 迁移 AVDTP/SBC reference module | MIG-003 | 功能不回退、pacing 单独验收 |
| `CAN-MIG-005` | `BACKLOG` | A2DP real-time pacing 修正 | MIG-004 | 五秒 RTP 时间轴与 on-air 时长容差达标 |
| `CAN-MULTI-001` | `BACKLOG` | 选择第二固件/设备 | TGT-001 | 获得合法固件和身份信息 |
| `CAN-MULTI-002` | `BACKLOG` | 第二 target pack | MULTI-001,RE-* | 独立证据与 loader profile |
| `CAN-MULTI-003` | `DONE` | 同模块双 target 构建 | MULTI-002,PKG-003 | `planner::expand` 矩阵展开（include/exclude/capability 校验）；CLI build-plan 复用；未注册 target 拒绝 |
| `CAN-MULTI-004` | `DONE` | SDK 单目标假设审计 | MULTI-003 | sdk_hygiene 测试：公共 SDK 无 target-private 标记；launcher adapter 层显式排除 |
| `CAN-REL-001` | `DONE` | Production/dev key ceremony | ARCH-006,PKG-002 | KeyRole cert + 签名 revocation list；`canopus key role-cert/revoke/check`；篡改/wrong-role 拒绝 |
| `CAN-REL-002` | `DONE` | CI pipeline | 各 host/build 任务 | scripts/ci.sh 6 gates + GitHub Actions（macos/thumbv8m/lld） |
| `CAN-REL-003` | `IN_PROGRESS` | Hardware-in-loop harness | DEV-009,MIG-001 | tests/hardware/gates.md（G0-G13 定义+归档格式）+ scripts/device-gates.sh 检查清单；真机执行待硬件 |
| `CAN-REL-004` | `DONE` | SDK templates/docs | C-010,RUST-007 | `canopus module new` C/Rust 模板；C 模板产出 verifier-PASS ELF（1 ctor/1 dtor） |

## 23.14 当前阻塞项

| Blocker ID | 影响任务 | 内容 | 解除条件 |
|---|---|---|---|
| `BLK-001` | ARCH-002 及全部实现 | 尚未建立独立 Canopus 仓库和 license | **已解除 2026-08-05**：仓库 `/Volumes/EXT0/Canopus` 建立，license AGPL-3.0，ADR-CAN-001 |
| `BLK-002` | DEV native service 后端 | 目标 daemon/thread/service ABI 尚未通用证明 | MVP 使用已证明 control/loader；另立 probe |
| `BLK-003` | RUST-005 | Rust 最终 ET_REL relocation envelope 未验证 | **已解除 2026-08-05**：no-heap-counter 交叉构建 ELF32 ET_REL，verifier PASS（sha256 50641841，0 undefined，1 ctor/1 dtor） |
| `BLK-004` | MULTI-* | 尚未选定第二 exact target | 获得第二固件、hash、合法分析资料 |
| `BLK-005` | MIG-005 | 当前 RTP 五秒内容在 Pixel HCI 时间轴占 6.528 秒 | 单独设计并验证 pacing 策略 |
| `BLK-006` | APP-002..015 | exact-target native app registry、launcher descriptor、UI dispatcher 和注销同步语义尚未证明 | **部分解除 2026-08-05**：枚举源（EVID-APP-001/002）+ descriptor/register/unregister（EVID-APP-004）已静态恢复；UI dispatcher 与注销同步语义待真机 |

## 23.15 决策记录模板

```markdown
### ADR-CAN-XXXX：标题

- 状态：Proposed / Accepted / Rejected / Superseded
- 日期：
- 决策者：
- 背景：
- 决策：
- 替代方案：
- 安全影响：
- 生命周期影响：
- 兼容性影响：
- 验证证据：
- 后续任务：
```

## 23.16 进度变更日志

| 日期 | 变更 | 任务 | 证据/备注 |
|---|---|---|---|
| 2026-08-05 | 创建 Canopus 架构文档 v1 | `CAN-ARCH-001` | 基于现有 module loader、control ABI、resident lifecycle 和 Phase 6 媒体实验证据 |
| 2026-08-05 | 建立独立 Canopus 仓库并确定许可证 | `CAN-ARCH-002` | 仓库 `/Volumes/EXT0/Canopus`；AGPL-3.0；host 工具链选用 Rust；初始提交 `2711f51` |
| 2026-08-05 | Target ID 重命名为 `xiaomi-band-10-pro` | `ADR-CAN-001` | 人类可读命名；完整 firmware SHA-256 仍为精确身份约束 |
| 2026-08-05 | Target ID 携带固件版本：`xiaomi-band-10-pro-3.101.030` | `ADR-CAN-002` | 设备存在多固件版本；一个 target_id 精确对应一个固件；ADR-CAN-001 相应部分被取代 |
| 2026-08-05 | Phase 1：六个 schema + Rust CLI + target registry | `CAN-SCH-001..006`,`CAN-CLI-001..003` | 14 个 fixtures；6 项 schema 集成测试通过；`canopus target/symbol/... validate` 可用 |
| 2026-08-05 | 首个 target pack + ELF verifier | `CAN-TGT-001..003`,`CAN-BLD-003` | target.toml + 38 symbols 导入；verifier 对真机 probe（sha256 0f47e2e1）PASS；提交 `ad1e14e` |
| 2026-08-05 | Phase 2：portable C runtime + host tests | `CAN-C-001..010` | 155 项 host checks；runtime 对 Cortex-M33 Thumb 交叉编译通过；提交 `270389b` |
| 2026-08-05 | Phase 3/4：veneer+identity 生成、目标构建、打包签名 | `CAN-TGT-004/005`,`CAN-BLD-001..004`,`CAN-PKG-001..003` | hello ET_REL verifier PASS（a0f73378）；Ed25519 签名/tamper 检测通过；提交 `cf59743` |
| 2026-08-05 | Phase 5：supervisor protocol + package store | `CAN-DEV-002/003` | 7 项 host 测试；提交 `d9b11ec` |
| 2026-08-05 | Phase 6：launcher 应用枚举链逆向 | `CAN-APP-001`（EVID-APP-001/002） | launcher.db+orderlist_v001/layout_v001+protobuf ordered list+msg 26/27 hide/show（u16 appid）；提交 `e41f66a` |
| 2026-08-05 | Phase 8：Rust SDK 全链路 | `CAN-RUST-001..009`,`CAN-TGT-006`,`BLK-003` | no_std abi/runtime/host-fake/generated-crate/no-heap 模块；55 项 Rust 测试；交叉构建 verifier PASS（0 undefined）；提交 `62c337d` |
| 2026-08-05 | Phase 9：RE orchestrator | `CAN-RE-001..009` | canopus-re（store/IDA allowlist/workflow/verify/revision）+ `canopus re` CLI；25 项测试；提交 `0329dd8` |
| 2026-08-05 | Phase 7：Manager UI | `CAN-UI-001..004` | canopus_manager 页面+生命周期感知操作；13 host 测试/67 检查；提交 `1e60be9` |
| 2026-08-05 | Phase 6：App SDK host 侧 + launcher descriptor/register RE | `CAN-APP-002/003/004/008/011` | canopus_app.h + ordered-list parser/serializer + EVID-APP-004（descriptor 布局 + app_launcher_add/del ABI）；提交 `3ccfe42`、`7372e22` |
| 2026-08-05 | Phase 12：key roles、CI、模板 | `CAN-REL-001/002/004` | `canopus key` + revocation list；scripts/ci.sh 6 gates + GitHub Actions；`canopus module new`；提交 `d10d722`、`1de0c97` |
| 2026-08-05 | Phase 10/11 脚手架：planner、SDK 审计、G-gate harness | `CAN-MULTI-003/004`,`CAN-REL-003` | planner::expand + sdk_hygiene 审计 + tests/hardware/gates.md + device-gates.sh |
| 2026-08-05 | 安装器表盘 + 设备 supervisor 模块 | `CAN-DEV-002/004`（部分） | watchfaces/canopus-installer（btpatch_phase5 结构）+ manager/service/canopus_supervisor（384B status/16B command ABI，12 host 测试）；.bin verifier PASS（ec28819e）但 /dev/canopus 注册与加载器待 G0/G4 设备 RE |

---

## 24. 第一轮建议执行顺序

在不立即重构现有功能模块的前提下，建议按以下顺序开始：

1. 建立独立 Canopus 仓库、许可证和 ADR；
2. 完成六个 schema；
3. 导入首个 exact target pack，但暂时只生成 read-only/clock/control 等低风险能力；
4. 提取 portable C ABI、sequence snapshot、resource tracker 和 lifecycle；
5. 构建最小 C removable example；
6. 建立通用 ELF verifier 和 package signer；
7. 在当前设备上依次通过 G0-G5；
8. 实现 supervisor MVP，并以现有 Lua/JS 页面作为临时 bootstrap client；
9. 只读逆向 native app registry、launcher descriptor、UI dispatcher 和生命周期；
10. 注册最小无特权 Hello Native App，通过 launcher、关闭、注销和 resident gate；
11. 将 Canopus Manager 实现为 native app，并保留 safe-mode bootstrap；
12. 再实现 Rust no_std 最小模块和 Rust native-app wrapper，验证重定位；
13. 引入第二 target 后才冻结 Canopus ABI v1；
14. 最后迁移 Bluetooth/A2DP resident reference module。

这样可以避免把当前单固件实验中的地址、状态字段、Bluetooth 生命周期和 `.bin` 打包细节固化为错误的跨设备公共框架。
