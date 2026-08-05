# Canopus 原生 Manager、正式 Transport 与原生 UI SDK 实施设计

> 文档状态：Proposed v1  
> 日期：2026-08-05  
> 初始目标：`xiaomi-band-10-pro-3.101.030`  
> 依赖：[architecture.md](architecture.md) §10、§14–17、§19–20  
> 目标：把当前 bootstrap 原型推进为可从 launcher 启动、可真实管理外部模块的原生 Manager，并提供面向 C 与 Rust 的人类可读原生 UI SDK。

---

## 1. 背景与决策

当前项目已经证明：

- boot-resident supervisor 可以注册 `/dev/canopus`；
- 固件 `register_driver` 字符设备 ABI 和完整 `file_operations` 布局可以稳定工作；
- 固定宽度命令和状态记录可以跨 Lua、C 与 native module 传递；
- supervisor 可以在第一次固件调用前执行 exact-target identity guard；
- C 与 Rust 模块可以构建为目标 loader 接受的 ELF32 ARM ET_REL；
- launcher app record/descriptor 已有部分静态证据，但第三方原生应用的完整注册、页面工厂、UI 生命周期和注销语义仍未达到设备批准状态。

因此作出以下方向性决策：

1. **`/dev/canopus` 是 Canopus 唯一正式、稳定、可版本化的设备端控制 transport。**
2. Manager 不再拥有另一套权威控制 ABI；现有 Manager protocol 应迁移到 `/dev/canopus` 的公共 protocol。
3. 文件节点只是 transport，消息格式、能力协商、异步完成和兼容规则由独立的 versioned protocol 定义。
4. Lua/watchface installer 只保留为 bootstrap、恢复和开发诊断工具，不再作为最终 Manager。
5. 最终 Manager 是由 resident Canopus 系统模块承载并注册到 launcher 的原生应用。
6. 原生 UI SDK 不直接暴露未经审核的固件对象，而是导出目标固件中**所有已发现且达到批准等级的原生预制组件**。
7. C ABI 是 UI backend 的稳定语言边界；Rust 在其上提供 `no_std`、类似 SwiftUI 的声明式构建语法。
8. Manager 是 UI SDK、native app SDK、transport 和模块生命周期的首个 dogfood 应用；在 Manager 自身稳定以前，不宣称这些接口可供第三方生产使用。

本文细化上述决策。若与 `architecture.md` 冲突，以更严格的 identity、evidence、lifecycle 和 default-deny 规则为准。

---

## 2. 最终产品形态

```text
launcher
   │
   ▼
Canopus Manager native app
   │  public C/Rust client API
   ▼
/dev/canopus
   │  versioned request / response / event protocol
   ▼
boot-resident Canopus supervisor
   ├── target identity + policy
   ├── package verification + transactional store
   ├── module loader + lifecycle tracker
   ├── native app registry adapter
   ├── safe mode + rollback
   └── bounded event log
```

职责边界：

- **Manager UI**：展示状态、收集确认、发送请求；不是权威状态持有者。
- **Transport client**：编码消息、关联 request ID、轮询/订阅完成事件、处理 ABI 兼容。
- **Supervisor**：唯一权威控制端；验证包、执行策略、推进状态机、持久化操作结果。
- **Target adapter**：封装 exact-target loader、launcher、UI、文件系统和 dispatcher ABI。
- **Package store**：保存 active/previous/staged/quarantined slot；任何 UI 都不能绕过它直接加载任意路径。
- **UI runtime**：把公共语义组件映射到固件预制组件；不拥有模块控制状态。

---

## 3. `/dev/canopus` 正式 Transport

### 3.1 稳定边界

稳定承诺位于 `/dev/canopus` 的字节协议，而不是某个 C struct 的进程内布局：

- 固定小端编码；
- 固定宽度整数；
- header 自描述大小；
- major/minor version；
- 明确 request/response/event 类型；
- payload 长度和上限先验证、后读取；
- 未知 opcode、flag、TLV 默认拒绝或按协议声明跳过；
- 不传递裸指针、函数地址或进程私有 handle；
- 每次成功 `write` 返回完整消费字节数，失败返回负错误；绝不以 `0` 表示成功；
- `read` 只返回完整 record，或返回明确的 buffer-too-small 错误；不发布半写状态。

`/dev/canopus` 名称长期稳定。协议 major 可以升级，但不得依靠更换设备节点来掩盖不兼容变化。

### 3.2 分层

```text
Canopus operation model
    install / enable / disable / update / remove / query
                     │
Canopus control protocol
    framing / versions / request IDs / result states / TLVs
                     │
/dev/canopus transport
    open / read / write / poll-or-bounded-retry
                     │
stock character-driver ABI
```

这样可以同时服务 native Manager、Lua bootstrap、NSH diagnostics 和 host/device test harness，而不会复制业务协议。

### 3.3 协议演进

当前固定 16-byte command 与 384-byte status 作为 **legacy protocol v1** 保留，只承担 bootstrap 和迁移，不继续塞入新语义。正式 Manager 使用 self-describing protocol v2。

建议 v2 envelope：

```c
struct canopus_transport_header_v2 {
    uint32_t magic;          /* "CPC2" */
    uint16_t header_size;
    uint16_t message_kind;   /* REQUEST / RESPONSE / EVENT */
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t total_size;
    uint32_t opcode;
    uint32_t request_id;
    uint32_t flags;
    int32_t  result;
    uint32_t payload_size;
};
```

约束：

- `header_size` 和 `total_size` 必须同时验证；
- `total_size` 必须小于 target pack 声明的硬上限；
- `request_id == 0` 仅用于 unsolicited event；
- response 必须回显 opcode 和 request ID；
- sync transport 成功只表示 record 已被 supervisor 接收，不表示异步操作完成；
- operation state 使用 `REJECTED/ACCEPTED/QUEUED/RUNNING/COMPLETED/FAILED/DISALLOWED/REBOOT_REQUIRED`；
- minor 版本只能 append 可忽略字段；语义改变必须提升 major；
- protocol parser 不直接 cast 未对齐外部 buffer，必须使用 bounded decode；
- CRC 只能用于发现传输/存储损坏，不能替代包签名。

具体数值在实现前冻结到独立 schema，并由生成器同时产出 C、Rust 和 Lua codec。上面的结构用于定义字段，不视为已经冻结的 ABI。

### 3.4 能力协商

Manager 打开设备后的第一笔 v2 操作必须是 `HELLO`：

```text
client supported major/minor + max record size
→ supervisor selected version
→ boot ID + target ID digest + supervisor build ID
→ supported opcode/capability bitmap
→ package/UI/loader capability versions
```

Manager 遇到以下情况必须进入只读恢复页，而不是猜测兼容：

- major 不兼容；
- target identity guard 未通过；
- supervisor 尚未 READY；
- status snapshot 不一致；
- package store 需要恢复；
- safe mode 禁止变更操作。

### 3.5 并发与异步完成

- 每个 open session 最多拥有固定数量的 outstanding requests；
- supervisor 内部队列、response ring 和 event ring 全部有静态上限；
- 队列满返回 `BUSY`，不得覆盖尚未读取的完成结果；
- request ID 在同一 boot/session 内唯一；
- 长操作先返回 `ACCEPTED` 或 `QUEUED`，最终结果通过 response/event 查询；
- Manager 页面销毁不取消 supervisor operation；重开页面后按 request ID 或 operation ID 恢复观察；
- event overflow 必须累计 dropped count，并触发完整状态重同步；
- 不允许 UI 线程无界 busy-poll。

如果 stock driver 的 `open` 无法提供可靠 per-file private state，MVP 可使用全局 bounded queues，但必须增加 client ID、generation 和 boot ID，避免旧响应被新 Manager instance 误收。

### 3.6 控制面与包数据面

`/dev/canopus` 传递小型控制 record，不通过它发送任意大小的 `.cpm` 包。MVP 数据流为：

```text
外部部署/导入
→ target adapter 管理的固定 inbox
→ supervisor 以 basename/token 引用 staged object
→ hash + size + canonical path 验证
→ package verifier
→ transactional package store
```

禁止 Manager 把任意绝对路径交给 supervisor。若未来需要流式导入，应建立独立 bounded data endpoint 或 chunk protocol，并具有：总大小上限、chunk offset、最终 hash、超时清理和原子 commit。

### 3.7 访问控制

当前节点使用宽松 mode 只能视为开发阶段配置。进入生产 gate 前必须：

- 证明固件节点权限、调用者身份或 capability 的可用边界；
- 若无法可靠识别调用者，默认把 `/dev/canopus` 当作本机非隔离控制面，并依靠签名、policy 和物理用户确认限制危险操作；
- unsigned、wrong-target、revoked、rollback 或 verifier-failed 包在 supervisor 内拒绝，不能只由 UI 拒绝；
- 第三方 app 不获得签名信任管理、safe-mode 退出、保留 app ID 或 unrestricted debug 操作；
- 所有 mutating operation 写入事件日志。

---

## 4. Manager 迁移与真实模块管理

### 4.1 Manager 不再拥有私有协议

现有 Manager 和 supervisor 的两套不兼容 wire format 必须收敛：

1. 抽取公共 protocol schema；
2. 由 schema 生成 C/Rust/Lua codec；
3. supervisor 成为唯一 server implementation；
4. native Manager 只链接 client library；
5. Lua installer 通过同一 client 语义发送 legacy 或 v2 请求；
6. 删除 Manager 内重复的权威状态机和“写成功即操作成功”逻辑。

迁移期间可以双栈，但必须满足：

- 通过 magic 明确区分，不做长度猜测；
- legacy v1 只修 bug，不增加功能；
- 所有新操作只进入 v2；
- 发布一个明确移除 legacy 的 protocol major/Manager 最低版本条件。

### 4.2 安装流水线

真正的 `INSTALL` 必须由 supervisor 完成以下事务：

```text
resolve staged token
→ open fixed inbox object
→ bound size and file count
→ parse canonical manifest
→ verify Ed25519 signature and key role
→ check revocation and anti-rollback
→ exact target/build/hash match
→ verify every payload hash
→ run ELF verifier on selected artifact
→ verify capability and lifecycle policy
→ verify native app ID/resource constraints
→ materialize temporary package slot
→ flush files and metadata
→ atomic rename to staged/active slot
→ persist authoritative state
→ report INSTALLED/DISABLED
```

任何一步失败都不能留下可加载的 partial active slot。失败对象可以进入 bounded quarantine，但不得在下次启动被自动加载。

### 4.3 加载、启用与禁用

- `ENABLE` 先检查 target identity、签名状态、artifact hash 和 lifecycle class；
- loader 成功不等于 module READY，必须等待 constructor/init handshake；
- `REMOVABLE` disable 执行 stop → reject new work → drain callbacks/workers/timers → unload；
- `RESIDENT_AFTER_ACTIVATION` 和 `ALWAYS_RESIDENT` 只逻辑禁用并标记下次 boot；
- native app descriptor 仍被 launcher/router 引用时绝不卸载承载代码；
- UI 必须分别展示 desired state、effective state、loaded state 和 reboot-required，不能压成一个 enabled bit；
- slot 在 remove/update/rollback 后必须可回收，不允许固定表最终耗尽。

### 4.4 启动恢复和 safe mode

Supervisor 每次启动：

1. identity guard；
2. package store journal/recovery；
3. 递增 boot ID 并标记 boot-not-ready；
4. 只加载 policy 允许的 built-in/production modules；
5. 恢复 Manager app 注册；
6. 达到 READY 后提交 boot-success；
7. 若 watchdog/crash/未完成事务触发 safe mode，只注册最小恢复 Manager，不加载第三方模块。

Manager 必须提供 safe-mode 原因、上次失败模块、禁用/回滚/删除操作和明确的重启按钮，但不能自行清除 supervisor 的证据记录。

### 4.5 原生 Manager 安装与注册

首个目标采用“resident system module + native app”形态：

```text
bootstrap installer loads trusted supervisor artifact once
→ supervisor stays resident for this boot
→ target launcher adapter registers Canopus Manager descriptor
→ launcher displays system-styled Canopus entry
→ opening entry creates native UI page tree
→ every control action goes through /dev/canopus client
```

跨 reboot 的最终方式取决于 exact-target 证据：

- 若 launcher descriptor 不持久化：supervisor 每次 boot 重新注册；
- 若系统支持受控持久 app registry：只保存数据 descriptor，函数指针仍在模块 READY 后发布；
- 若无法证明同步注销：Manager module 为 always-resident，更新和删除仅在 reboot 边界生效。

在 app registration、lifecycle callback、router ownership 和 descriptor lifetime 全部通过真机 gate 前，不调用“安装原生应用完成”。

---

## 5. 原生 UI SDK 目标

### 5.1 人类可读，而不是泄漏固件细节

公共 UI API 应使开发者表达“页面是什么”，而不是手工拼接目标私有对象、offset、style ID 和 callback cookie。

必须同时做到：

- 默认呈现与系统内置应用一致的组件、间距、字体、交互和动画；
- C 写法直接、可审阅、没有隐藏 allocator/异常依赖；
- Rust 写法接近 SwiftUI 的声明式结构；
- target backend 仍严格绑定 exact firmware；
- 所有对象所有权、UI thread 和销毁顺序明确；
- backend 不支持的组件在构建或运行 capability check 时明确失败，不静默降级成错误样式；
- host fake 可以测试 view tree、事件和状态更新，而不调用固件地址。

### 5.2 “导出所有原生预制组件”的定义

“所有”指目标固件中能够发现并达到发布门槛的系统预制组件，而不是导出所有疑似 UI 符号。

每个 target 维护组件清单：

```text
CANDIDATE
→ STATIC_RECOVERED
→ ABI_REVIEWED
→ HOST_WRAPPED
→ DEVICE_PROBED
→ LIFECYCLE_PROVED
→ APPROVED
→ PUBLIC
```

只有 `APPROVED/PUBLIC` 组件生成到默认 SDK。其余组件保留在 evidence catalog，供继续逆向，但不能从稳定 API 调用。

首轮必须系统枚举以下类别：

- app shell、page、navigation stack、title/header；
- scroll container、list、section、row、card；
- text、rich text、icon、image、divider、spacer；
- primary/secondary/destructive button；
- switch、checkbox、radio、slider、selector；
- progress、loading、status badge；
- dialog、confirmation、action sheet、toast；
- empty/error/offline/safe-mode state；
- system back gesture、crown/touch/keypress input；
- system typography、color、spacing、radius、motion tokens；
- resource/image/font/localization loader；
- accessibility/semantic labels（若目标支持）。

Catalog 还必须记录每个固件内置复合组件及其 variants。公共 API 可以给出语义化跨目标名称，但 target-specific 扩展必须放在显式 namespace/capability 下。

### 5.3 组件证据记录

建议新增 target 数据：

```text
targets/<target>/ui/
├── backend.toml
├── tokens.json
├── components/
│   ├── navigation_page.json
│   ├── settings_row.json
│   └── ...
├── resources/
├── evidence/
└── generated/
```

每个 component record 至少包含：

- semantic component ID 和固件内部名称；
- constructor/factory/destructor 地址与原型；
- object size/alignment（若调用者分配）；
- props、variants、默认值和取值范围；
- child/parent ownership；
- string/image/resource ownership及复制规则；
- event callback 原型、线程和 retention；
- create/update/remove 是否同步；
- page detach/destroy 后是否仍有 callback；
- UI thread/dispatcher 要求；
- allocator domain；
- style/token/resource ID；
- unsupported combinations；
- firmware hash、evidence IDs 和 device probe results；
- approval state 和最低 UI backend revision。

生成器必须拒绝缺失 ownership、callback lifetime 或 thread evidence 的公共 component。

---

## 6. UI 分层与 ABI

```text
Application state + update logic
                │
Rust declarative DSL / C declarative builder
                │
Canopus semantic view tree
                │
versioned canopus_ui C ABI
                │
target-generated UI backend
                │
firmware native prefab components + UI dispatcher
```

### 6.1 公共 C ABI

公共 C API 只暴露 opaque handle 和版本化 value types：

```c
typedef uint32_t canopus_ui_node_id;

struct canopus_ui_context_v1;
struct canopus_ui_tree_v1;

struct canopus_ui_text_props_v1 {
    uint32_t struct_size;
    const char *text;
    uint32_t text_len;
    uint32_t style;
};

int32_t canopus_ui_tree_begin(
    struct canopus_ui_context_v1 *context,
    struct canopus_ui_tree_v1 **out_tree);

int32_t canopus_ui_text(
    struct canopus_ui_tree_v1 *tree,
    canopus_ui_node_id key,
    const struct canopus_ui_text_props_v1 *props);

int32_t canopus_ui_tree_commit(struct canopus_ui_tree_v1 *tree);
```

规则：

- `struct_size + abi_major/minor` 用于可演进参数；
- app 不直接持有固件 widget pointer；
- node key 在一个 app generation 内稳定；
- string/resource 默认在 commit 前复制到 bounded runtime storage，若借用则 API 名称和 lifetime 必须显式；
- 所有 mutating API 检查 UI thread；
- callback 从 firmware backend 转换为 bounded semantic event；
- app update 函数不在 firmware callback 栈内重入；
- tree commit 失败保持上一棵已提交树可用；
- destroy 幂等，generation 不匹配的迟到事件被丢弃并计数。

### 6.2 C 的人类可读写法

C SDK 同时提供 builder 和薄宏层。宏只生成普通 spec，不隐藏控制流或资源所有权：

```c
static int32_t manager_view(struct canopus_ui_context_v1 *ui,
                            const struct manager_model *model)
{
    CANOPUS_UI_BEGIN(ui, view);
    CANOPUS_NAVIGATION_PAGE(view, UI_KEY("root"),
        .title = "Canopus Manager");
    CANOPUS_SECTION(view, UI_KEY("framework"),
        .title = "Framework");
    CANOPUS_STATUS_ROW(view, UI_KEY("supervisor"),
        .label = "Supervisor",
        .value = model->ready ? "Ready" : "Unavailable");
    CANOPUS_BUTTON(view, UI_KEY("install"),
        .label = "Install package",
        .event = MANAGER_EVENT_INSTALL,
        .enabled = model->can_install);
    return CANOPUS_UI_COMMIT(view);
}
```

不支持宏的项目可以调用一一对应的 builder functions，二者生成同一 semantic tree。

### 6.3 Rust 的 SwiftUI-like 写法

Rust API 采用单向状态流：

```rust
struct ManagerModel {
    supervisor_ready: bool,
    can_install: bool,
}

enum Message {
    Refresh,
    InstallPackage,
    OpenModule(ModuleId),
}

fn view(model: &ManagerModel) -> impl View<Message> {
    view! {
        NavigationPage(title: "Canopus Manager") {
            Section(title: "Framework") {
                StatusRow(
                    label: "Supervisor",
                    value: if model.supervisor_ready { "Ready" } else { "Unavailable" },
                )
            }
            Button("Install package",
                enabled: model.can_install,
                action: Message::InstallPackage,
            )
        }
    }
}
```

设计约束：

- `no_std`，默认不要求全局 allocator；
- `view!` 生成 bounded semantic node spec，而不是固件 C++ 对象；
- `Model → view` 是纯构建过程，事件通过 `Message` 进入 `update`；
- 默认不把任意 Rust closure 留存在固件中；
- 需要 subscription 时使用 generation-checked static trampoline 和 runtime-owned cookie；
- callback `extern "C"`、不得 unwind；panic 策略为 abort/fail-stop；
- `State`、`Binding` 和异步 operation handle 有明确 owner，不模仿桌面 SwiftUI 的隐式无限生命周期；
- dynamic list 必须声明最大项数或使用分页/虚拟列表；
- unsupported component/variant 返回 capability error；
- Rust DSL 与 C builder 共用 ABI、host fake 和 conformance tests，不维护第二套 backend。

“类似 SwiftUI”只指声明式组合、状态驱动和可复用 View，不引入运行时反射、GC、隐式堆分配或不受控闭包。

### 6.4 Reconciliation

目标固件很可能采用 retained widget tree，因此公共 runtime 负责有限 diff：

1. app 生成有稳定 key 的 semantic tree；
2. runtime 在 bounded arena 中校验节点、深度和 children 上限；
3. 对比上一代树；
4. 在 UI thread 创建、更新、重排或销毁原生 prefab；
5. 全部成功后发布新 generation；
6. 失败时保留上一棵可用树并报告具体节点错误。

MVP 可先采用“页面级重建 + 稳定事件 ID”，但 API 从一开始保留稳定 key，避免未来切换 diff backend 时破坏应用源码。

---

## 7. Native App SDK

UI SDK 之上需要独立的 app runtime：

- versioned app descriptor；
- constructor registration glue；
- create/start/resume/pause/stop/destroy；
- launcher metadata、icon 和本地化名称；
- page/router adapter；
- UI dispatcher；
- app instance generation；
- module/control client injection；
- resource mount/unmount；
- crash boundary和最后页面状态；
- host fake lifecycle runner。

推荐公共模型：

```text
CanopusApp
├── metadata()
├── init(context) -> Model
├── update(model, Message, Commands)
├── view(model) -> View<Message>
└── lifecycle(model, AppLifecycleEvent)
```

Target glue 可以是 C/C++，但不得让公共 Rust SDK依赖目标 C++ ABI。复杂固件 UI 对象永远由 backend 创建和销毁。

只要 launcher、router 或 firmware UI 保存 module 内的 descriptor、resource 或 callback，该 app 的承载模块就自动提升为 resident lifecycle，直到同步注销得到设备证明。

---

## 8. Manager UI 信息架构

首个原生 Manager 至少包含：

### 8.1 Overview

- supervisor READY/safe mode；
- target ID、固件 version/build/hash 状态；
- framework/control/UI ABI；
- active/resident/pending module 数量；
- reboot required 和上次启动结果；
- package store health。

### 8.2 Modules

- installed、enabled、loaded、active 分别展示；
- signer、版本、target artifact；
- lifecycle class 和 resident 提示；
- staged update、previous slot、quarantine；
- bounded 搜索、分页或虚拟列表。

### 8.3 Module detail

- manifest 与 payload hashes；
- signer/key role/revocation；
- exact firmware compatibility；
- capabilities 与风险；
- lifecycle、resources、pending operations；
- enable/disable/update/rollback/remove；
- 操作预计立即生效还是 reboot 后生效；
- 最近事件和错误。

### 8.4 Install

- 从受控 inbox 选择 staged object；
- 安装前展示 package ID、版本、签名者、capabilities、lifecycle 与风险；
- 高风险 capability 使用 Manager 自己的系统确认页；
- 展示 verify/stage/commit 各阶段；
- 页面关闭后操作继续，返回后可恢复进度；
- 完成后默认 `INSTALLED/DISABLED`，除非 policy 明确允许 install-and-enable。

### 8.5 Recovery

- safe-mode 原因；
- last known failing module/operation；
- disable next boot；
- rollback staged/active slot；
- remove quarantined package；
- export bounded diagnostics；
- reboot；
- protocol 不兼容时只提供不会破坏状态的动作。

---

## 9. 生成器与建议仓库结构

```text
abi/
├── canopus_transport_v2.h
├── canopus_ui_v1.h
└── schema/
    ├── transport-v2.json
    └── ui-v1.json

manager/
├── client/                 # /dev/canopus client + codecs
├── service/                # supervisor server
├── native-app/             # Manager model/update/view
└── target/                 # registration/bootstrap glue

sdk/c/
├── include/canopus/ui.h
├── include/canopus/app.h
└── src/ui_runtime.c

sdk/rust/
├── canopus-ui-core/        # no_std semantic tree
├── canopus-ui-macros/      # optional view! syntax
├── canopus-app/
└── canopus-manager-client/

targets/<target>/
├── ui/
├── types/
├── symbols/
└── generated/
    ├── canopus_ui_target.h
    ├── canopus_ui_target.rs
    └── component-capabilities.json

tools/
├── ui-catalog/             # evidence/catalog validation
└── ui-codegen/             # C/Rust target backend generation

tests/
├── host/ui/
├── host/transport/
└── device/ui-probes/
```

最终路径可适应现有 workspace，但边界必须保持：公共 semantic ABI、target evidence、generated backend、app code 和 supervisor policy 不得混为一层。

---

## 10. 实施阶段与 Gate

### Phase A：冻结 transport v2

交付：

- wire schema、opcode registry、error registry；
- C/Rust/Lua codec generation；
- HELLO、status、event、operation query；
- legacy v1 compatibility adapter；
- fuzz/property tests 与短读写测试。

Gate：Manager client 与 supervisor 对 malformed/truncated/unknown record 一致 fail-closed；write/read 字节语义实机通过，无 busy-loop。

### Phase B：Manager 全面迁移到 `/dev/canopus`

交付：

- 删除/封存私有 Manager wire protocol；
- 所有页面状态来自 supervisor；
- request/operation ID 恢复；
- safe-mode read-only behavior。

Gate：同一组 protocol conformance vectors 被 C、Rust、Lua 和 supervisor 全部通过。

### Phase C：真实 package store 与模块操作

交付：

- inbox token；
- canonical manifest、签名、revocation、target 和 hash verification；
- ELF verifier device integration；
- transactional active/previous/staged/quarantined slots；
- load/init/READY、disable/drain/unload、update/rollback/remove；
- boot recovery 和 slot reclaim。

Gate：断电点 fault injection、损坏包、wrong target、bad signature、resident disable、rollback 和 repeated install/remove 全部通过。

### Phase D：launcher/native app exact-target 证明

交付：

- registration/unregistration ABI evidence；
- descriptor、app ID、resource、page factory 和 lifecycle records；
-最小 native app probe；
- resident lifetime decision。

Gate：至少完成 100 次 open/close、前后台切换、强制返回、重复注册防护和 reboot 恢复，不出现悬挂 callback、descriptor UAF 或 launcher corruption。

### Phase E：原生 prefab 全量 catalog

交付：

- 系统 UI factory/component 多路径枚举；
- component evidence records；
- tokens/resources/localization catalog；
- 每个候选组件状态和未通过原因；
- 第一批 approved public components。

Gate：不允许“发现数量上限”被描述为全量；必须记录搜索覆盖方式、未分析区域和 completeness review。

### Phase F：C UI SDK

交付：

- opaque C ABI；
- semantic tree、bounded arena、event dispatch；
- target backend；
- human-readable builder/macros；
- host fake。

Gate：Manager 核心页面可只使用公共 C UI API 编写，无 target pointer、地址或私有 style ID 泄漏。

### Phase G：Rust declarative UI

交付：

- `canopus-ui-core`；
- `view!` DSL；
- Model/Message/update runtime；
- bounded list、state、binding、commands；
- C ABI backend；
- examples 与 compile-fail tests。

Gate：Rust Manager 页面在 `no_std` ARM target 构建通过；没有未批准 undefined symbols、unwind 或隐式 allocator 依赖；与 C 版本生成等价 semantic trees。

### Phase H：原生 Manager dogfood

交付：

- Overview/Modules/Detail/Install/Recovery；
- 系统样式组件；
- launcher entry；
- end-to-end external module management；
- bootstrap installer 退化为 recovery tool。

Gate：从 clean device 状态完成 install → verify → enable → load → inspect → disable → update → rollback → remove；UI 展示与 supervisor authoritative state 始终一致。

### Phase I：生产硬化

交付：

- node access policy；
- signer/key role/revocation UI；
- event log 与 diagnostics；
- resource exhaustion、fuzz、long-run、watchdog tests；
- protocol/UI compatibility matrix；
- migration和recovery文档。

Gate：所有 security/device gates 通过后，才能将 native app/UI SDK 标记 stable。

---

## 11. 测试矩阵

### Host

- frame encode/decode golden vectors；
- malformed length/TLV/property fuzz；
- request ID 和 async state machine；
- package transaction crash-point simulation；
- semantic view tree snapshots；
- reconciliation create/update/remove；
- stale generation event rejection；
- C builder 与 Rust DSL equivalence；
- lifecycle permutations和resource leak accounting。

### Artifact

- zero undefined symbols；
- relocation allowlist；
- constructor/destructor；
- identity guard；
- no unwind/forbidden sections；
- approved absolute-address ledger；
- generated UI binding stability；
- package canonicalization和signature vectors。

### Device

- `/dev/canopus` partial/invalid/queue-full behavior；
- Manager launch/close/background/reopen；
- every approved prefab create/update/input/destroy；
- locale、长字符串、缺失资源、最大列表；
- UI callback 与 page destroy race；
- package install/load/disable/update/rollback/remove；
- resident module reboot semantics；
- safe mode and failed boot recovery；
- repeated operation endurance；
- watchdog、heap、stack 和 object count baselines。

每个 device test 记录 firmware hash、target pack revision、artifact hash、boot ID、结果和恢复方式。

---

## 12. Definition of Done

只有同时满足以下条件，才能声称“Canopus 已达到可安装原生 Manager 并管理外部模块”的水平：

- launcher 中存在真正的 Canopus native app entry；
- Manager 不依赖 watchface/Lua 才能正常运行；
- Manager 和恢复工具均通过 `/dev/canopus` 的正式 versioned protocol；
- supervisor 完成真实验签、target 匹配、ELF verifier、事务安装和加载；
- enable/disable/update/rollback/remove 遵守 lifecycle class；
- resident 状态和 reboot-required 不被 UI 误报；
- safe mode 可以在第三方模块失败后恢复；
- C UI SDK 可复用全部 approved system prefabs；
- Rust 可以用声明式 `view!` 语法构建同一套系统样式组件；
- 公共 UI 代码不包含目标地址、私有结构布局或裸 firmware widget pointer；
- native app 注册、页面销毁、callback drain 和模块驻留关系经过真机证明；
- host、artifact、device gates 具有可追溯证据。

---

## 13. 明确不接受的捷径

- 继续扩展一套只供 Manager 使用的第二控制协议；
- 把 `/dev/canopus` write 成功当作 install/load 已完成；
- 让 UI 直接调用 loader 或修改 supervisor private state；
- 把任意文件路径从 UI 传给 supervisor；
- 跳过签名、exact-target、hash 或 ELF verifier；
- 通过卸载仍被 launcher/UI callback 引用的模块来“实现删除”；
- 为追求 SwiftUI 外观引入隐式堆、反射、GC 或无界闭包 retention；
- 直接公开反编译出的私有 widget pointer/API；
- 把未经过 ownership/thread/lifecycle 证明的组件标记为 public；
- 用自绘近似组件冒充已复用系统原生 prefab；
- 只完成静态反编译就宣称 native app 或 UI ABI 已稳定。

---

## 14. 首轮执行顺序

1. 冻结 `/dev/canopus` transport v2 schema 与兼容规则；
2. 将现有 Manager client 收敛到公共 transport；
3. 完成 package store、验签、verifier 和真实 load lifecycle；
4. 用 exact-target 逆向确认 launcher/app lifecycle；
5. 建立 UI component evidence schema，并系统枚举固件 prefab；
6. 先以 C backend 启动最小原生 Manager 页面；
7. 在相同 semantic ABI 上实现 Rust `view!`；
8. 用原生 Manager 完整 dogfood 外部模块管理；
9. 完成 safe mode、rollback、权限和长期稳定性 gate；
10. 再将 native app/UI SDK 开放给第三方模块。

该顺序保证 transport、安装事务和生命周期先稳定，再扩大 UI 表面积；同时让 Manager 尽早成为所有新接口的真实使用者，而不是另建无法验证的演示应用。

---

## 15. P0/P1/P2 强制整改总表

本节是实施清单，不是建议列表。这里的 P0/P1/P2 来自当前代码审计与“原生 Manager 能真实管理外部模块”这一交付目标：

- **P0**：可能造成设备崩溃/内存破坏/执行未授权代码/不可恢复状态，或直接阻断核心目标的事项；
- **P1**：不会在最短路径立即造成灾难，但会破坏安全边界、生命周期真实性、持久状态一致性或长期可维护性；
- **P2**：主要影响 API 完整性、可诊断性、扩展性、可复现性或开发者体验；可以排在 P0/P1 之后，但进入 stable SDK/正式发布前必须关闭；
- **CLOSED-GATED**：代码修复已存在，但仍缺真机 gate，不能当作完全关闭；
- **OPEN**：不得在 production build 中绕过；
- **BLOCKED-EVIDENCE**：必须先取得 exact-target 证据，不允许让 LLM 猜 ABI。

| ID | 优先级 | 状态 | 问题 | 首要代码位置 |
|---|---|---|---|---|
| CAN-P0-001 | P0 | CLOSED-GATED | `/dev/canopus` write 返回 0 导致 Lua/stdio 无进展重试和 watchdog reboot | `manager/service/canopus_supervisor_platform.c:41-45` |
| CAN-P0-002 | P0 | HOST-FIXED/DEVICE-PENDING | Manager bounded text renderer 在截断后发生整数/指针下溢，可写到 buffer 前方 | `manager/ui/canopus_manager.c:89-174` |
| CAN-P0-003 | P0 | OPEN | device INSTALL 未接验签、target/hash/verifier，包路径与归档 entry 未形成安全边界 | `manager/service/canopus_supervisor.c:86-97`、`tools/package-builder/src/lib.rs:28-186` |
| CAN-P0-004 | P0 | OPEN | device stage/load/unload 全为 stub，无法真实管理模块 | `manager/service/canopus_supervisor_platform.c:66-82` |
| CAN-P0-005 | P0 | OPEN | removable disable 没有 stop/drain/unload，可能在 callback 活跃时谎报 disabled | `manager/service/canopus_supervisor.c:99-159` |
| CAN-P0-006 | P0 | OPEN | safe mode 只置位，不阻止 install/enable/load，启动恢复也未持久化 | `manager/service/canopus_supervisor.c:86-164` |
| CAN-P0-007 | P0 | BLOCKED-EVIDENCE | native app 注册、页面工厂、UI dispatcher、callback 和注销 lifetime 未实机证明 | `targets/xiaomi-band-10-pro-3.101.030/evidence/EVID-APP-004.json` |
| CAN-P0-008 | P0 | HOST-FIXED/DEVICE-PENDING | CPRT Manager protocol 与 CPC1/CPS1 设备 protocol 未连接，Manager 无正式 transport | `manager/protocol/canopus_protocol.h:19-61`、`manager/service/canopus_supervisor.h:25-32` |
| CAN-P1-001 | P1 | OPEN | `/dev/canopus` mode 为 `0666`，mutating control surface 无调用者边界 | `manager/service/canopus_supervisor_platform.c:17-19` |
| CAN-P1-002 | P1 | HOST-FIXED/DEVICE-PENDING | pending state 可任意跳转，静态 request ID、response 未完整关联验证 | `manager/protocol/canopus_protocol.c:73-149`、`manager/ui/canopus_manager.c:228-324` |
| CAN-P1-003 | P1 | HOST-FIXED/DEVICE-PENDING | supervisor status 无 sequence snapshot，可能读到 torn state | `manager/service/canopus_supervisor.c:177-223` |
| CAN-P1-004 | P1 | HOST-FIXED/DEVICE-PENDING | status writer 加法溢出、NULL source 和 publish protocol 不完整 | `runtime/control/canopus_control.c:23-89` |
| CAN-P1-005 | P1 | HOST-FIXED/DEVICE-PENDING | event ring 覆盖旧记录时 `dropped` 从不增加，count 语义错误 | `runtime/diagnostics/canopus_diagnostics.c:19-48` |
| CAN-P1-006 | P1 | OPEN | package slot rename 事务不可恢复、previous/.old 堵塞、目录持久化不足 | `manager/storage/canopus_store.c:64-260` |
| CAN-P1-007 | P1 | OPEN | remove 后 supervisor slot 不回收，`module_count` 最终与实际不一致 | `manager/service/canopus_supervisor.c:127-137,236-259` |
| CAN-P1-008 | P1 | HOST-FIXED/DEVICE-PENDING | command 末尾无条件清零 `error_code`，失败原因丢失 | `manager/service/canopus_supervisor.c:171-173` |
| CAN-P1-009 | P1 | HOST-FIXED/DEVICE-PENDING | ordered-list parser/serializer 缺少 out/apps NULL 与 bounded-name 完整检查 | `app-sdk/launcher/canopus_ordered_list.c:25-107` |
| CAN-P1-010 | P1 | HOST-FIXED/DEVICE-PENDING | module descriptor validation 只检查 size/major，未检查 flags、identity、callbacks | `runtime/module/canopus_module.c:28-42` |
| CAN-P1-011 | P1 | OPEN | ELF verifier 只检查 relocation 指向的 SHN_ABS，未覆盖代码/数据中的直接绝对地址 | `tools/elf-verifier/src/verifier.rs:155-195` |
| CAN-P1-012 | P1 | OPEN | target veneer 的 evidence/promotion 状态没有成为 codegen 强制 gate | `targets/xiaomi-band-10-pro-3.101.030/generated/canopus_veneer.h:107-153` |
| CAN-P1-013 | P1 | OPEN | native app resources 没有完整进入 build/embed/hash/verify/capability 流程 | `schemas/package.schema.json:30-95`、`tools/package-builder/src/lib.rs:28-58` |
| CAN-P1-014 | P1 | OPEN | package store/root/helper 的截断、partial write、EINTR、fsync-directory 和清理规则不足 | `manager/storage/canopus_store.c:19-103,135-150` |
| CAN-P1-015 | P1 | OPEN | 进度/安全治理文档与实际代码能力不一致，容易让执行 LLM 越过 device gate | `docs/progress.md:7-18`、`docs/security-model.md:1-9` |
| CAN-P2-001 | P2 | OPEN | supervisor/public helpers 对 NULL、损坏模型和初始化失败的防御不一致 | `manager/service/canopus_supervisor.c:25-36,61-72,177-223,237-259` |
| CAN-P2-002 | P2 | OPEN | device fops 使用裸 `uint32_t[12]`，缺 typed layout/static assertion/slot ownership | `manager/service/canopus_supervisor_platform.c:17-57` |
| CAN-P2-003 | P2 | OPEN | protocol minor、flags、reserved 字段和未知 command 的兼容策略没有统一 schema | `manager/protocol/canopus_protocol.c:7-67` |
| CAN-P2-004 | P2 | OPEN | 多处对外部/固定数组使用无界 `canopus_strlen`，字符串 contract 分散 | `sdk/c/canopus_memory.h:32-39` 及调用点 |
| CAN-P2-005 | P2 | OPEN | Manager model 固定 32 项、全量复制，缺分页、增量同步和 stable module generation | `manager/ui/canopus_manager.h:26-100` |
| CAN-P2-006 | P2 | OPEN | Manager operation availability 只看局部 state，未纳入 policy/capability/pending/safe mode | `manager/ui/canopus_manager.c:177-224` |
| CAN-P2-007 | P2 | OPEN | legacy Lua status parser 只看 magic，未完整验证 ABI、长度、状态值和 snapshot | `watchfaces/canopus-installer/main.lua` |
| CAN-P2-008 | P2 | OPEN | package canonical digest 缺显式 domain/version/entry length framing，格式演进困难 | `tools/package-builder/src/lib.rs:133-149` |
| CAN-P2-009 | P2 | OPEN | package build API 注释声称 schema 校验，但函数本身没有 enforce；CLI/library contract 易分叉 | `tools/package-builder/src/lib.rs:25-58` |
| CAN-P2-010 | P2 | OPEN | diagnostics record 缺 timestamp、module ID、request ID、target/build 和 reboot 语义 | `runtime/diagnostics/canopus_diagnostics.c:19-38` |
| CAN-P2-011 | P2 | OPEN | target-generated Rust binding 存在两份提交副本，单一来源和同步边界不清晰 | `targets/xiaomi-band-10-pro-3.101.030/generated/canopus_bindings.rs`、`sdk/rust/canopus-target-generated/src/generated.rs` |
| CAN-P2-012 | P2 | OPEN | 32-bit firmware packed pointer layout 可在 64-bit host 被误用，缺 compile-time usage barrier | `sdk/rust/canopus-target-generated/src/lib.rs:46-131` |
| CAN-P2-013 | P2 | OPEN | public error 仍混用 `-1`、字符串和 target errno，Manager 无稳定可本地化错误模型 | manager/runtime/store 多处 |
| CAN-P2-014 | P2 | OPEN | UI component catalog、host fake、C backend 与 Rust app/UI crates 当前为空或不存在 | `app-sdk/ui/`、`app-sdk/rust/` |
| CAN-P2-015 | P2 | OPEN | CI 缺 protocol schema drift、evidence completeness、文档状态和 device-evidence metadata gate | `scripts/ci.sh`、`tools/canopus-core/tests/generated_stability.rs` |
| CAN-P2-016 | P2 | OPEN | 固定容量表/计数器的 overflow、saturation、pagination 和 boot wrap policy 未统一 | supervisor/protocol/diagnostics/UI bounded tables |
| CAN-P2-017 | P2 | OPEN | store API 通过 cast 修改 `const` 对象，错误状态与并发所有权不清晰 | `manager/storage/canopus_store.c:42-60,153-231` |
| CAN-P2-018 | P2 | OPEN | 构建工具链/target pack/codegen 输入摘要尚未完全进入可复现 provenance | build scripts、package manifest、generated headers |

表中状态只有在对应“完成判据”全部满足后才能修改。单元测试通过但真机 gate 未通过时，最多改为 `HOST-FIXED/DEVICE-PENDING`。

---

## 16. 面向执行 LLM 的统一工作协议

以下协议应用于每一个 P0/P1/P2。

### 16.1 每个问题只能按这个顺序处理

1. **领取一个 ID**：例如 `CAN-P0-002`。同一工作树一次只处理一个 ID，除非两项在表中明确要求原子合并。
2. **读取证据**：先读本节、列出的源码、对应 header、现有 tests 和 architecture 约束。
3. **写出不变量**：在改代码前，用 3–10 条陈述说明修复后永远必须成立的性质。
4. **写失败测试**：至少覆盖表中列出的攻击/故障输入。不能只写 happy path。
5. **实现最小修复**：不顺手重构其他子系统；不改 generated artifact 的源结果而不改 generator。
6. **运行局部测试**：记录完整命令和结果。
7. **运行全量 CI**：至少 `./scripts/ci.sh`、两个 Rust workspace 的 fmt/clippy（若涉及 Rust）和 `git diff --check`。
8. **执行 artifact gate**：涉及 device ELF、veneer、package 时重新生成并运行 verifier/stability tests。
9. **执行 device gate**：仅当任务要求且已有授权设备路径；记录固件 hash、artifact hash、操作步骤、观察结果和恢复结果。
10. **更新文档状态**：只能根据真实证据更新；禁止把“预计通过”写成“已通过”。
11. **一个问题一个 commit**：commit message 带 ID；不要提交 ignored binary、IDA database、日志或 `.claude/`。
12. **停止并报告**：若遇到未知 firmware ABI、所有权、线程、签名 key、设备权限或 destructive operation，停止；不得猜测后继续。

### 16.2 LLM 每次交付必须使用的报告模板

```text
Issue: CAN-P?-???
Invariant(s):
Files read:
Files changed:
Tests added first:
Implementation:
Host commands + exact results:
Artifact commands + exact results:
Device firmware hash:
Device artifact hash:
Device gate result:
Remaining uncertainty:
Recovery path tested:
Commit:
```

缺少任何不适用字段时写 `N/A + 原因`，不能删除字段。

### 16.3 一律禁止的行为

- 不得自行把 `restricted/FORBIDDEN` target symbol 提升为 callable；
- 不得为了让测试通过降低 identity、signature、hash、lifecycle 或 verifier 检查；
- 不得从相近固件复制地址、结构或函数原型；
- 不得把 host fake 成功描述为 device ABI 已证明；
- 不得在默认分支做未经 gate 的大规模跨问题重构；
- 不得使用“返回 0、忽略错误、以后实现”伪装成功路径；
- 不得直接编辑 generated veneer/bindings 后跳过稳定性测试；
- 不得执行会写固件、patch memory、强制 unload resident module 的探针；
- 不得删除 active/previous/recovery 数据来掩盖事务恢复失败。

---

## 17. P0 逐项修复手册

### 17.1 CAN-P0-001：字符设备 write 无进展重试

**当前状态**：源码修复已进入 commit `3e68cf2`；`sup_control_write` 现在转发到 `canopus_supervisor_device_write`，成功返回 16，Lua 同时检查 `write/close` 返回值。仍需目标设备闭环。

**必须保持的不变量**：

- 恰好 16-byte legacy command 被消费时返回 16；
- malformed length 返回负值；
- command 业务失败仍返回 16，并通过 status/result 表达；
- 永不对已消费 command 返回 0；
- Lua 不把 `pcall` 本身成功等同于 write 成功。

**真机关闭流程**：

1. 从 `3e68cf2` 或更新源码运行 `scripts/build_canopus_supervisor.sh`；
2. 记录 `sha256`、ELF verifier report、undefined symbol count；
3. 将新 artifact 放入 bootstrap watchface，不复用旧缓存；
4. reboot 手环，确认固件 version/build 精确匹配；
5. 执行 `LOAD`，读取 `/dev/canopus` 384-byte status；
6. 只点击一次 `INSTALL`；当前 staging stub 应产生 `FAILED`，但 UI 任务不能忙循环、设备不能 reboot；
7. 等待超过旧 watchdog 复现窗口并收集 CPU/watchdog/offlinelog；
8. 连续执行 20 次 QUERY 和 5 次失败 INSTALL；
9. reboot 后再次确认系统正常；
10. 将状态改为 CLOSED 时附 firmware hash、artifact hash、日志位置和实际观察时长。

**禁止**：因为 staging 尚未实现而把 `FAILED` 改成假 `COMPLETED`。

### 17.2 CAN-P0-002：Manager renderer 截断后越界

**根因**：`canopus_buf_copy` 截断返回 `-1`；`snprintf` 截断返回“本应写入的长度”。当前代码把两者直接累加到 `n`，之后计算 `out + n` 和 `cap - (uint32_t)n`。小 buffer 可令 `n < 0` 或 `n > cap`，随后指针落到 buffer 前方或容量无符号下溢。

**修复步骤**：

1. 新增统一 writer：`struct canopus_text_writer { char *buf; uint32_t cap; uint32_t used; uint32_t truncated; }`；
2. `init` 在 `cap > 0` 时立即写 `buf[0] = '\0'`；
3. append string/format 每次只使用 `cap - used`，前提为 `used < cap`；
4. append 返回状态，不把 `-1` 或 would-have length 累加到 offset；
5. 截断时固定 `used = cap - 1`、保持 NUL、设置 `truncated = 1`；
6. 三个 render 函数改用 writer；
7. API 明确返回 `0` 表示完整、专用负错误表示截断，不可仍然声称成功；
8. 调用者对截断显示分页/省略状态，不能把截断内容作为完整 manifest。

**先写测试**：

- cap = 0、1、2；
- 每一个 append 边界的 cap；
- 长 target/build/module ID；
- 16 个最大长度 module；
- guard bytes 放在 buffer 前后并验证不变；
- ASan/UBSan host run；
- fuzz 任意 cap 与任意非 NUL 外部输入（模型字段自身应先保证 NUL）。

**完成判据**：所有路径始终 NUL 终止；guard bytes 不变；无 signed/unsigned conversion underflow；测试明确观察到 truncation error。

### 17.3 CAN-P0-003：包信任与安全解包未接入 device INSTALL

**根因**：host package builder 可以签名，但 supervisor 的 INSTALL 仅调用空 `stage_package(..., 0)`；设备没有 canonical parse、trusted key、target match、payload hash、ELF verifier 和安全 extraction 的闭环。tar entry path 也需要在 build、verify、extract 三处独立校验。

**修复步骤**：

1. 定义受控 inbox token；协议禁止传任意路径；
2. 实现 `PackageReader` 的 bounded streaming interface，不在设备上一次性分配整个 tar；
3. 对 archive 总大小、entry 数、单 entry 大小、总展开大小、路径长度设 target 常量；
4. 拒绝绝对路径、`..` component、`.` 空洞、反斜杠混淆、NUL、重复路径、重复 manifest/signature、symlink、hardlink、device node、FIFO 和未知 tar type；
5. `read_tar`/build 同样拒绝重复 entry，防止“验签看到一个、解包使用另一个”；
6. 先读取并 canonicalize manifest，但在签名验证前不执行 artifact；
7. signature 必须覆盖规范化 path+content，并绑定 key ID/role；
8. 查 trusted key store、revocation、package/version anti-rollback；
9. 精确匹配 target ID、firmware SHA、version/build 和 target pack revision；
10. 对所有 manifest 声明 payload 重新算 hash；拒绝归档中的未声明可执行 payload；
11. 用与 host 同规则的 device verifier 或预验证报告+设备独立关键检查；报告本身必须被签名且 artifact hash 一致；
12. 只解包到新建的 transaction temp directory；使用 `openat`/等价受约束 API，逐 component 验证；
13. flush 文件和目录，写 verified transaction record；
14. 最后 atomic promote；任何错误 quarantine/cleanup，绝不发布 active；
15. 将 `signature_ok` 从 UI flag 改为 supervisor verifier 产生的不可伪造状态。

**攻击测试**：

- `../x`、`/abs`、`a/../../x`、重复 `manifest.json`、两个 signature、symlink escape；
- 超大 size、截断 tar、checksum 错、entry count bomb、压缩炸弹（若未来支持压缩）；
- signature 对但 manifest artifact hash 错；
- hash 对但 wrong firmware；
- revoked key、错误 key role、版本回滚；
- verifier report 与 artifact 不匹配；
- 每个 write/fsync/rename 点故障注入。

**完成判据**：不存在未经 supervisor 完整验证即可进入 load path 的文件；恶意 archive 无法在 store root 外创建/覆盖任何对象。

### 17.4 CAN-P0-004：真实 staging/load/unload adapter

**根因**：`sup_load_module`、`sup_unload_module`、`sup_stage_package` 全部固定返回 `-1`。stock modlib 链虽已静态恢复，但任意 Canopus ELF 的 exact-target 调用和所有权尚未 device gate。

**实施顺序**：

1. 保持 stubs fail-closed，先建立独立 loader probe；
2. 从 `/Volumes/EXT0/firmware_latest/analysis/MODLIB_PHASE1_FINDINGS.md` 提取已证明调用链、relocation 包络和 zero-import 约束；
3. 在 target evidence 新增 loader function、prototype、context、path ownership、return/error 和 unload semantics；
4. probe 只加载最小 no-op module：identity guard、constructor 设置 READY marker、无 callback/worker/UI；
5. 验证路径 lifetime、module handle、重复 load、失败 load cleanup、constructor failure；
6. 再探测 `rmmod/modlib_remove`：只对无资源的 REMOVABLE probe 执行；
7. 验证 destructor、namespace removal、`EBUSY` 语义和重复 unload；
8. 将批准符号加入 target generator，而不是手写地址；
9. platform adapter 接收经过 store 验证的 immutable artifact reference，不接任意 name/path；
10. loader 返回值转换为明确状态：LOADING → PREPARING → READY/FAILED；
11. 只有 READY 才向 Manager 报 active；
12. unload 前必须收到 lifecycle drain proof token；
13. 设备异常后记录 boot failure 并触发 quarantine/safe mode。

**Gate 顺序**：G0 no-op load → constructor → query → destructor/unload → repeated cycles → malformed ELF reject → module with one tracked resource → reboot recovery。任何一步 crash 都回退到 bootstrap recovery，不继续下一 gate。

### 17.5 CAN-P0-005：removable stop/drain/unload

**根因**：当前 DISABLE 直接把 slot state 设为 `DISABLED`，没有调用 module descriptor 的 deactivate/stop、resource tracker、inflight drain 或 platform unload。

**修复步骤**：

1. supervisor slot 保存 module handle、descriptor、runtime lifecycle、generation 和 resource tracker reference；
2. ENABLE 只能通过 runtime transition table推进；
3. DISABLE 首先设置 reject-new-work barrier；
4. 调用 `deactivate`，记录失败但继续进入受控 stop policy；
5. 调用 `stop`，禁止新 timer/worker/subscription；
6. 等待 bounded inflight/open-ref/callback count 归零；
7. 逆序释放 tracked resources；
8. 检查 retained/detached resource；存在即不得 unload，并升级为 fail-stop/reboot-required；
9. 调用 platform unload；成功后才进入 UNLOADED；
10. unload 失败保持真实 loaded state，不能显示 DISABLED/UNLOADED；
11. resident barrier 发布后永远不走 platform unload；
12. REMOVE 复用同一 disable transaction，不能另写捷径。

**竞争测试**：callback 正在执行、timer 同时触发、worker 排队、页面持有 subscription、open FD、stop 返回错误、drain 超时、unload 返回 EBUSY。每种情况验证代码仍驻留且 UI 显示 `FAIL_STOP` 或 `REBOOT_REQUIRED`。

### 17.6 CAN-P0-006：safe mode enforcement 与持久化

**根因**：`ENTER_SAFE_MODE` 仅 `safe_mode = 1`；随后 INSTALL/ENABLE 仍会执行，reboot 后状态丢失。

**修复步骤**：

1. 定义 safe-mode reason enum、trigger boot ID、failing module ID、crash counter；
2. 将 desired safe mode 写入 atomic state/journal；
3. command dispatch 最前面建立 policy matrix；safe mode 下默认只允许 query、diagnostics、disable-next-boot、rollback、remove-pending 和受控 reboot；
4. 拒绝 install/enable/load/update activation；是否允许仅 stage 更新必须由显式 policy 决定；
5. boot 开始写 `BOOTING` marker，READY 后原子写 `BOOT_OK`；
6. 下一 boot 发现未完成 BOOTING、重复 crash 或 store corruption 时自动 safe mode；
7. safe mode 只加载 supervisor、Manager recovery app 和生产恢复依赖；
8. 退出 safe mode 不是简单清 bit：先选择 recovery action，写 next-boot trial，成功 READY 后才清除；
9. trial boot 失败自动回到 safe mode；
10. Manager 始终显示 reason 和哪些操作被 policy 拒绝。

**测试**：safe mode 后发送每个 opcode；重启 persistence；断电在 BOOTING/READY 写入点；连续失败阈值；回滚成功/失败；未知 reason fail-closed。

### 17.7 CAN-P0-007：native app/launcher/UI ABI 证据闭环

**当前证据边界**：`EVID-APP-001/003/004` 提供 launcher list 和静态 descriptor 线索；`app_launcher_add/del` 在 generated veneer 中仍为 restricted。UI dispatcher、icon format、reserved field、返回语义、同步注销和 callback lifetime 未证明。

**严格探针顺序**：

1. **只读枚举**：确认 runtime list、descriptor 实例、stock app ID 和 resource references；
2. **静态交叉验证**：至少两个独立 caller/callee 证明 prototype、调用线程和 descriptor field；
3. **临时 descriptor probe**：使用保留的 Canopus 测试 ID，descriptor 和字符串全部常驻，不执行卸载；
4. **注册 gate**：在已确认 UI thread/dispatcher 上调用 add，记录返回和 launcher 变化；
5. **launch gate**：页面只显示一个系统 Text/Back，记录 create/resume/pause/destroy 序列；
6. **重复 gate**：重复打开/关闭 100 次，检查 heap/object/callback count；
7. **reboot gate**：判断 registry 是持久还是每 boot 重建；
8. **注销 gate**：先阻止新 launch、关闭实例、drain event，再调用 del；观察 launcher 和内部引用；
9. **descriptor lifetime gate**：注销后仍不立即卸载，跨多个 UI loop 验证无引用后才决定是否支持 removable；
10. **失败 gate**：ID 冲突、缺 icon、invalid resource、重复 add/del；
11. 完整证据审核后才将 symbol 从 restricted 提升；
12. 首版 Manager module 默认 always-resident，除非同步注销明确证明。

**停止条件**：任何未知 allocator/free、任何异步持有裸指针、任何必须猜测的 dispatcher context，都应终止 probe 并保留 restricted。

### 17.8 CAN-P0-008：统一 CPRT 与 CPC1/CPS1

**根因**：Manager 使用带 payload/request ID 的 CPRT callback；真实设备只有 16-byte CPC1 write 和 384-byte CPS1 read，两者没有 bridge，pending table 也未进入 supervisor。

**修复步骤**：

1. 写 protocol decision record：采用本文 v2 envelope，legacy CPC1/CPS1 只兼容；
2. 为 v2 编写 schema 与 golden byte vectors；
3. 使用 byte decoder，不从 device buffer 直接 dereference packed C struct；
4. supervisor device write 按 magic 分发 legacy/v2，未知 magic 负错误；
5. 将 `canopus_pending_table_v1` 或替代 operation table集成到 supervisor；
6. read 端提供完整 response/event record；若驱动不支持 blocking/poll，定义 bounded retry interval 和 no-data 返回；
7. native Manager 实现真实 open/read/write client，删除 generic callback 作为 production transport 的地位；
8. Lua codec 从同一 schema 生成或用 golden vectors 验证；
9. legacy status 继续使用固定 384 bytes，v2 不以长度猜协议；
10. 所有 opcode 只在 supervisor 实现一次；
11. 增加 protocol major/minor、capability negotiation 和 minimum Manager version；
12. device gate 证明 partial/malformed write 不破坏下一条 frame。

**完成判据**：native Manager 在目标设备上通过 `/dev/canopus` 完成 HELLO/QUERY，并能观察一个异步 operation 从 ACCEPTED 到 terminal；没有测试专用 transport callback 参与生产路径。

---

## 18. P1 逐项修复手册

### 18.1 CAN-P1-001：设备节点权限

1. 逆向并实测 `register_driver` mode 是否被 VFS enforce；
2. 枚举系统 app/native module 运行身份，确认 uid/gid/capability 是否可区分；
3. 若可区分，把节点改为最小 owner/group mode，并验证 Manager 可用、普通 app 拒绝；
4. 若不可区分，不伪造权限保证：在 threat model 标注“本机同地址空间调用者可达”；
5. 无论 mode 如何，mutating opcode 仍执行签名、role、safe-mode 和 physical-confirm policy；
6. debug opcode 在 production build 移除或要求 production-debug key；
7. 测试 open/read/write/ioctl 对不同调用者和 mode 的实际行为。

### 18.2 CAN-P1-002：pending 状态、request ID 与 response 关联

1. 定义合法边：ACCEPTED→QUEUED→RUNNING→terminal；允许的直接边必须逐项列出；
2. `set_state` 拒绝倒退、terminal→anything、未知 state；
3. `finish` 不得把任意非 terminal 静默改为 COMPLETED；改为显式 terminal result；
4. terminal record 保留到 client ACK/TTL，不立即清 active 导致查询丢失；
5. request ID 由 client 单调生成，0 保留；不能为每个 opcode固定 1–7；
6. 关联 session generation + boot ID，处理 wraparound；
7. Manager 验证 response magic/ABI/request ID/opcode/payload length；
8. transport error 与 operation failed 分开；
9. 测试 duplicate、table full、out-of-order、stale boot、unknown ID、wraparound、page reopen。

### 18.3 CAN-P1-003：supervisor sequence snapshot

1. status v2 header加入 `sequence_begin/sequence_end` 或使用双序列固定布局；
2. writer：发布 odd begin → 写 payload → release fence → 同一 even begin/end；
3. reader：acquire读取 begin → copy → acquire读取 end；只接受相等偶数；
4. C memory model和目标单核/中断模型都要记录；`volatile` 不能代替 ordering；
5. legacy CPS1 若不能改布局，使用 supervisor lock/interrupt-safe copy到 staging buffer，再由 read copy；
6. 并发 host stress 和设备 timer/command/read stress 下不得出现混合 module states。

### 18.4 CAN-P1-004：status writer 边界与发布

1. `status_ensure` 改为 `need <= capacity - used`，前置验证 `used <= capacity`；
2. 所有 public put 函数检查 `w`、`w->buf`；
3. `len > 0 && src == NULL` 必须失败，不能推进 `used`；
4. `dropped` 使用饱和计数或定义 wrap；
5. 明确 begin/build/publish API；publish 前必须处于 writing generation；
6. snapshot sequence 应与实际输出 record 关联，不能只存在 writer sidecar 中却让 reader看不到；
7. 测试 `UINT32_MAX` need、corrupt used、NULL source、zero length、double publish、publish without begin。

### 18.5 CAN-P1-005：event ring dropped/count

1. 增加 `stored_count`，上限为 ring capacity；
2. append 在 `stored_count == capacity` 时覆盖最旧项并 `dropped++`；
3. `next_sequence` 是单调序列，不等于当前存储 count；
4. API 分别提供 stored count、next sequence、dropped；
5. 处理 sequence wrap并在 protocol 中带 boot ID；
6. reader 按 sequence 检测 gap；
7. 修正现有“wrap 后 dropped 仍为 0”的错误测试期望。

### 18.6 CAN-P1-006：package slot transaction 与恢复

1. 设计 journal state：PREPARED、ACTIVE_TO_PREVIOUS、STAGED_TO_ACTIVE、COMMITTED、CLEANUP；
2. 操作开始前处理已有 previous：按 policy 删除旧 previous 或原子轮转，不以“busy”永久阻断更新；
3. 每次 rename 后 fsync parent directory；
4. rollback 的 `.old` 使用 transaction ID，启动时可识别和清理；
5. 任一步失败由 recovery 根据 journal 幂等恢复，不依赖内存状态；
6. remove 实现安全递归删除受控目录，当前 `rmdir` 只能删空目录；
7. active module 若仍映射/引用文件，遵守 loader profile，不提前删除；
8. 同一 package 操作串行化，不同 package 的并发规则明确；
9. 对每个 syscall 注入失败并在“重启”后检查恰好一个有效 active。

### 18.7 CAN-P1-007：slot 回收和权威 module identity

1. slot 加稳定 module/package ID，不再只靠 UI index；
2. REMOVE 成功后清零整个 slot、递减 module_count；
3. selected/pending operation 不保存可被复用的裸 index，保存 ID+generation；
4. resident remove-pending 在下次 boot 完成删除后回收；
5. repeated add/remove 超过 16 次仍可继续；
6. status enumeration跳过 free slot且 count 与实际一致；
7. stale command 指向旧 generation 时 DISALLOWED。

### 18.8 CAN-P1-008：错误码持久语义

1. 每个 platform operation 返回 stable Canopus error + 可选 target errno；
2. command 开始时清本次 error，完成后只在成功时保持 NONE；
3. 失败路径写入 error，不在公共尾部无条件清零；
4. status 将 last operation error 与 framework health error 分开；
5. request ID 对应 terminal response 保存自己的错误；
6. 测试 stage/load/unload失败后 status 仍可读到原因，下一次成功不会错误继承。

### 18.9 CAN-P1-009：ordered-list 输入安全

1. parse：`count > 0` 时要求 `out != NULL`；
2. serialize：`count > 0` 时要求 `apps != NULL`；
3. 对 fixed name array 使用 `strnlen(max)`，不要无界 `strlen`；
4. 没有 NUL 时拒绝，不读取结构外；
5. offset 必须不指入 entry table，除非 evidence 明确允许；
6. 可选拒绝重叠/重复/倒序 name regions，按真实 wire evidence决定；
7. 解析失败不留下半初始化 output，先解析到 temp 或先清零；
8. fuzz arbitrary buffer、offset near `UINT32_MAX`、count/cap组合、NULL矩阵。

### 18.10 CAN-P1-010：module descriptor 完整验证

1. 精确检查 struct size 的支持范围和 append-only minor；
2. 检查 module ID/build ID 指针、长度、NUL/UTF-8 policy；
3. lifecycle class 有效；
4. flags 只允许 known bits，组合必须合法；
5. required callbacks按 lifecycle/capability存在；
6. callback address 必须落在已加载 artifact executable section；
7. descriptor 和字符串 lifetime 至少覆盖 module residency；
8. manifest identity 与 descriptor identity 一致；
9. target identity guard 在 prepare 前通过；
10. unknown minor fields 只按 struct_size忽略，不读取越界；
11. 建立 malformed descriptor table tests。

### 18.11 CAN-P1-011：ELF 直接绝对地址扫描

1. 明确威胁：编译器可能用 MOVW/MOVT、literal pool 或 data word 嵌入固定 firmware address，不一定产生 SHN_ABS relocation；
2. 使用 ARM/Thumb aware disassembler 扫描 executable sections；不能把任意 4 bytes 都当指令；
3. 解析 MOVW/MOVT register pairs、LDR literal target、branch/call immediate；
4. 扫描 allocatable read-only/data sections 中像 target executable/data range 的 words，并结合 relocation排除正常 section-relative值；
5. 每个允许地址必须来自 target symbol/evidence record，并验证 callable Thumb bit规则；
6. 对 address range、MMIO range 和 data globals分别用 policy；
7. 输出每个命中的 section/offset/instruction/decoded address/evidence ID；
8. 编制 positive fixtures：允许 veneer；negative fixtures：手写 inline asm MOVW/MOVT、literal pointer、未知 MMIO；
9. 如果扫描存在无法可靠分类的模式，production verifier fail-closed，dev mode只能显式 override并记录。

### 18.12 CAN-P1-012：evidence promotion 强制 gate

1. codegen 只接受 `approval_state == APPROVED` 且 evidence IDs非空的 callable symbol；
2. `restricted` 仅生成注释/opaque metadata，不生成函数；
3. `FORBIDDEN` 永不生成 callable，即使地址/类型完整；
4. promotion record 包含 reviewer、日期、firmware hash、prototype、ownership、thread、device probe；
5. target pack revision变化导致 generated stability test 更新；
6. CI 测试伪造 restricted→callable 必须失败；
7. generated file header列出生成输入摘要和 approval revision；
8. 禁止直接手改 generated file作为 promotion。

### 18.13 CAN-P1-013：native app resources 完整签名

1. package builder读取 manifest 中每个 icon/localization/image/font/page/compiled resource；
2. 路径执行 CAN-P0-003 的 canonical validation；
3. 每个资源有 size/hash/media type/target backend；
4. build 验证 hash 后嵌入；verify 确认声明与归档一一对应；
5. 拒绝缺失、重复、未声明、wrong backend资源；
6. 设备安装检查总资源预算、launcher slot、decoder capability；
7. resource mount跟随 package slot generation；active resident app仍引用时不删除；
8. icon parser在注册 launcher 前用受控 decoder/probe验证；
9. 本地化 fallback和缺失字符串有确定规则；
10. tamper 每种资源均使 signature/hash verification失败。

### 18.14 CAN-P1-014：store helper 的系统调用正确性

1. `canopus_store_init` 返回状态并检查 root 是否截断；
2. root 必须是 canonical absolute target-configured path，不能来自包；
3. atomic write 使用循环处理 partial write/EINTR；
4. len > `SSIZE_MAX`/target上限先拒绝；
5. temp 文件使用 exclusive transaction name，避免并发互相 truncate；
6. mode、owner按 target policy；
7. fsync file、close、rename、fsync parent directory；
8. `mkdirs` 检查每次错误，只把 `EEXIST + is-directory` 当成功；
9. 检查 `.old` snprintf truncation；
10. 所有 cleanup错误进入 diagnostics，不能覆盖原始错误；
11. 在 NuttX 上验证 rename/fsync真实语义；若不具备 POSIX承诺，target adapter提供替代 journal实现。

### 18.15 CAN-P1-015：治理与进度真实性

1. `architecture.md` §23 为权威任务表，但每个 READY/DONE必须链接测试或 evidence；
2. `progress.md` 自动生成或 CI 对比，禁止手工长期漂移；
3. 展开 `security-model.md`：threat actors、trust roots、key roles、revocation、anti-rollback、local attacker、no-sandbox事实；
4. 展开 module lifecycle和target authoring，不只保留索引；
5. 将本表 ID加入任务追踪；
6. CI 禁止在 OPEN P0 对应 capability上标记 production-ready；
7. README 的能力描述区分“host implemented”“static recovered”“device verified”“production approved”；
8. 每次 device gate更新 firmware/artifact hash和日期。

---

## 19. P0/P1/P2 依赖顺序与提交计划

不得按表格顺序盲目并行。建议拓扑顺序：

```text
CAN-P0-001 device closure
CAN-P0-002 renderer memory safety
CAN-P1-004 status helper safety
CAN-P1-005 event accounting
CAN-P1-009 ordered-list safety
CAN-P1-010 descriptor validation
        ↓
CAN-P0-008 transport unification
CAN-P1-002 request state machine
CAN-P1-003 consistent snapshot
CAN-P1-008 error semantics
        ↓
CAN-P0-003 package trust/extraction
CAN-P1-011 ELF address policy
CAN-P1-012 evidence promotion
CAN-P1-013 resource signing
CAN-P1-014 filesystem helpers
CAN-P1-006 store journal/recovery
        ↓
CAN-P0-004 loader adapter
CAN-P0-005 stop/drain/unload
CAN-P1-007 slot reclaim
CAN-P0-006 safe mode/boot recovery
        ↓
CAN-P0-007 native app/UI evidence
C UI backend
Rust declarative DSL
native Manager dogfood
        ↓
CAN-P1-001 node access hardening
CAN-P1-015 governance closure
production gate
```

建议 commit 粒度：

- 一个输入安全 helper + tests 一个 commit；
- protocol schema/codegen 一个 commit，server integration另一个，Manager migration再一个；
- package parser、trust policy、extractor、store journal分别提交；
- 每个 target symbol promotion独立 evidence commit；
- 每批 UI component先 evidence，再 generated backend，再 public wrapper；
- device gate只提交日志摘要/evidence metadata，不提交大型原始 dump。

---

## 20. 指挥者验收清单

指挥者对任何 LLM 的“已完成”至少追问以下问题；任一回答含糊则退回：

1. 你修的是哪个 CAN-P0/P1 ID？
2. 修复前能稳定触发什么失败？测试是否先失败？
3. 你写出的安全/生命周期不变量是什么？
4. 是否存在未检查的长度加法、截断返回、NULL+非零长度？
5. 是否把 transport 接收成功误当成 operation 完成？
6. 是否有任意路径、重复 archive entry 或未声明 payload 进入 store？
7. signature、target identity、artifact hash 和 verifier 是否都由 supervisor enforce？
8. removable unload 前是否真实 stop/drain/resource release？
9. resident module 是否可能被调用 unload？
10. safe mode 是否实际拒绝 enable/load，而不只是显示一个 bit？
11. callback/thread/ownership来自哪条 exact-target evidence？
12. 是否手改 generated file？generator与稳定性测试是否同步？
13. host test、artifact test、device gate分别是什么？不要混称。
14. 真机固件 hash和测试 artifact hash是什么？
15. 失败后的恢复路径是否实际执行过？
16. `git diff --check`、fmt、clippy、完整 CI 的原始结果是什么？
17. 工作区是否误加 `.claude/`、日志、`.i64`、ignored binary？
18. 哪些不确定性仍然存在？为什么没有猜测？

只有这些问题都得到可核验答案，且本节对应完成判据满足，才能关闭任务。
