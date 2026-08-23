# 固件符号适配 2.0：BinDiff 风格多算法证据融合方案

> 状态：043 BluetoothAudio 的三次 Bond 失败均已进入 failure-feedback；`-1113` 的 registration-node 修复和 `Bond 3/2 0F -1105` 的通用 mHDT/CID 调度修复均已完成 host gate，device retest pending。036/043 支持矩阵、BinDiff-inspired ensemble matcher、failure-feedback hard-negative、043 正式 symbol pack、以及 9/9 Pro/11 三个新 IDB 的 static candidate pack 已落地；`fw-match` 已开始落地 bounded Corpus v2（函数 data refs + 独立 DataObjectRecord）和 global candidate-only 数据流检索，但真实 036/043 corpus 回归及独立 oracle 晋级仍 pending；设备 gates、三新 target 的 exact ABI/codegen gate 仍保持 pending。
>
> 当前实验结论：043 的 `Bond 1/0 00 -1107` 和 `-1113` 都不是 `bt_create_bond` 固件返回值，而是在调用它之前由 pair-request filter fail closed。第一轮正确撤销了 036 的 16-word/global ABI，并恢复 `core_bt_adapter_instance=0x20126738`、`core_bt_registration_handle=0x20126734` 与 17-word descriptor `0x2CD4A744`，但把 adapter+120 的对象误读成带 +72 descriptor 字段的 client。第二轮 exact IDB 证明该对象是仅 `0x24` 字节的 `callbacks_list` manager；registration handle 才是 `sub_C3A96EC` 返回的 8-byte `{cookie, descriptor}` node。代码现通过固件 register/unregister API 替换节点，不再越界访问 manager+72，并将后续失败细分为 `-1114..-1117`；该修复仍无真机通过结论。
>
> `Bond 3/2 0F -1105` 已证明 Classic 配对完成，但当次运行没有记录 mHDT rewrite。静态地址和 compare/write 成功不能证明 callback data path 的调度顺序：wire Connection Response 与 Configuration Request 可以早于 firmware connection-confirm callback。现由 portable core 从成功的 Connection Response 预取 local CID 并处理 exact `7F 01 01`，所有 runtime-capable targets 均要求 raw-H4 hook；target-private 仅保留 exact writable seam 与 stock dispatcher。新诊断以 `0x80` 区分 hook installed、以 `0x40` 区分 rewrite hit，仍待 exact 043 真机复测。
>
> 036→043 caller-neighborhood graph matcher 将 5/5 可比较人工 UI oracle 找回，`lvx_content_pad_bottom` 正确标记为 source-missing；043 production records 保留 forbidden/withdrawn policy，未把 static 命中声明为 device-proven。新 9-Pro、11、9-3.1.32 corpus 分别重新提取 28065、34775、26325 个函数，ensemble 各产生 123/126 个 static candidates。
>
> 036→043 的 bounded v2 实测 corpus 分别包含 41,763/42,251 个函数和 19,533/19,875 个 referenced data objects。global pass 对 10 个 source globals 完整报告：7 个产生 candidate、3 个因 source/data-flow 不足 `BLOCKED`；新增独立 global oracle 中 `style_misans_regular_24 → 0x2010CE44`、`style_misans_demibold_32 → 0x2010D054` 均命中，连同 5 个 function oracle 为 7/7。输出仍只有 `CANDIDATE/PENDING`，不生成 callable address，不覆盖已晋级、`WITHDRAWN` 或 `FORBIDDEN` production record。
>
> 目标：把“把一个固件 target 的地址迁移到另一个固件”从人工修地址，升级为可审计、可拒绝错误结果、可由一条命令驱动的自动化适配流水线。
>
> 术语：本文中的“匹配”指产生候选；“确认”指经过独立证据和 ABI 验证后允许进入 target pack；“设备证明”仍然必须通过真实设备 gate，不能由静态匹配替代。

---

## 1. 结论先行

当前 `fw-match` 的定位应从“地址生成器”降级为 **候选检索器**。它可以帮助缩小人工搜索范围，但不能凭 composite score、margin 或 GA assignment 直接生成可调用地址。

新的核心架构应为：

```text
精确固件身份
  → 可重建分析输入
  → 多算法独立候选生成
  → BinDiff 风格函数/基本块图匹配
  → 跨函数调用图一致性优化
  → 全局变量/回调表/字符串数据流验证
  → 调用点 ABI 与类型验证
  → 模块依赖闭包验证
  → 证据融合与冲突检测
  → 仅晋级高置信度结果
  → 生成 target pack / Rust / C bindings
  → build + verifier
  → 分阶段设备 gate
```

最终希望达到的用户体验是：

```sh
canopus fw adapt \
  --firmware /path/to/vela_ap.bin \
  --analysis-db /path/to/vela_ap.bin.i64 \
  --target-id xiaomi-band-10-pro-3.101.043 \
  --reference xiaomi-band-10-pro-3.101.036 \
  --module org.canopus.bluetooth-audio \
  --strict
```

在 ABI 没有实质变化、输入分析质量正常的情况下，这条命令应完成绝大多数候选定位、证据报告和代码生成。对于函数删除、拆分、合并、原型变化或 callback/global 语义变化，命令必须明确失败或输出 `REVIEW_REQUIRED`，不能“猜一个最像的地址继续编译”。

---

## 2. 本次 036/043 调查得到的事实

### 2.1 这是地址大幅移动，不是简单的固定偏移

当前固件身份记录：

| 固件 | SHA-256 | IDA corpus 函数数 | 原始镜像大小 |
|---|---|---:|---:|
| Band 10 Pro 3.101.036 | `662d67f5e247e31e194d3161024890ba93b9d29d70b290fadb9aac8ce8ec3c81` | 41,763 | 13,616,208 bytes |
| Band 10 Pro 3.101.043 | `519307675665e4866d722a8119a98589c397b614ac3294cb87bfc86de45756ec` | 42,246 | 13,795,728 bytes |

两份原始镜像：

- 043 比 036 增加 179,520 bytes；
- 相同偏移位置的字节只有约 2.83% 相同；
- 16-byte 对齐块在另一份镜像中找到的比例约为 23.0%；
- 128-byte 对齐块比例约为 4.9%；
- 256-byte 对齐块比例约为 2.7%。

因此以下方案都不够：

- 所有地址加同一个 delta；
- 只按相同偏移读取地址；
- 只使用函数入口前 8/16/32 bytes；
- 只依赖某个一次性 byte pattern；
- 把另一个固件的 IDB 名称复制到新固件。

043 IDA 的 XIP 代码段为 `0x0C0C0010..0x0CDC40E8`，同时存在 XIP、非缓存 Flash、缓存 Flash 和 RAM/data 映射。地址匹配必须先处理 canonical mapping 和 alias 去重，否则同一份代码可能被当成多个独立候选。

### 2.2 当前 matcher 已经证明“候选正确率”和“确认正确率”不是一回事

当前仓库归档了两份 043 匹配报告：

- `targets/xiaomi-band-10-pro-3.101.043/evidence/fw-match/matches-1036-to-1043.json`
  - 124 个 source symbols；121 个产生候选；71 个 confirmed；
- `targets/xiaomi-band-10-pro-3.101.043/evidence/fw-match/matches-1030-to-1043.json`
  - 124 个 source symbols；123 个产生候选；72 个 confirmed。

用当前 043 target pack 的最终 records 重新比较时：

- 036 → 043 报告中，118 个共有名字里有 12 个地址与当前最终记录不一致；
- 030 → 043 报告中，117 个共有名字里有 17 个地址与当前最终记录不一致。

这不是推测，而是当前仓库中已经发生过的事实。`targets/xiaomi-band-10-pro-3.101.043/evidence/EVID-MATCH-001.json` 还直接记录了典型误匹配：

- `lv_event_get_code` 曾经被匹配到 allocator；
- `lvx_label_create`、`lvx_label_set_text`、`lvx_object_align` 曾经分别被匹配到错误的 UI/属性函数；
- 后来依靠精确 IDB callsite 才修正为 `sub_C588210`、`sub_C588F30`、`sub_C589490`、`sub_C589188` 等入口。

这意味着当前的 `confirmed` 事实上表达的是：

> 该候选超过当前评分阈值，并且在当时的 target records/人工流程中被接受。

它并不表达：

> 该候选已经由与 matcher 独立的证据证明 ABI、调用语义和地址都正确。

此外，当前 `verify_match.py` 的 ground truth 是 target pack 自己的 symbol records。如果这些 records 部分来自同一套 matcher 或同一批人工修正，验证会出现循环依赖。两份 source report 共享同一个 043 corpus，也不能当作完全独立的两次验证。

### 2.3 043 的崩溃日志暂时不能证明具体是哪一个地址

`tmp.log` 在启动和点击原生应用后出现了：

- `15:13:29` 左右进入设置/配对页面；
- `15:13:44` 左右出现应用列表更新和 `org.canopus` 相关 native-app 场景；
- `15:13:48` 出现 `EMERG` 级别的系统诊断转储和大量线程 backtrace。

但日志中没有明确的：

- HardFault/BusFault/UsageFault 标志；
- SCB fault status；
- faulting PC/LR；
- 被崩溃线程的寄存器快照；
- 明确的异常类型。

所以当前只能得出：

> 原生应用发布/Launcher/UI 生命周期是高风险路径，且崩溃发生在该场景之后；不能仅凭这份日志断言 `app_install`、`launcher_add`、某个 UI helper 或某个 callback 地址一定错误。

这正是新方案需要把 **原生应用注册链路拆成独立 ABI 闭包并逐步验证** 的原因，而不是让 Bluetooth、Launcher、LVGL 和 NuttX 地址共享同一个低置信度匹配结果。

### 2.4 当前仓库已经有完整的 target/build 分层，但地址恢复和模块依赖之间还没有自动闭环

当前链路大致是：

```text
targets/<target-id>/symbols/
  → canopus-target-generated
  → canopus-target-private target backend
  → BluetoothAudio target/*.rs
  → target env feature
  → Rust/C link
  → ELF verifier
```

关键位置：

- `Canopus-Module-BluetoothAudio/scripts/build-device.sh:5-68`：按 `CANOPUS_TARGET` 选择 target profile，运行 host tests、Rust build、link 和 verifier；
- `Canopus-Module-BluetoothAudio/scripts/build-targets.sh:1-23`：批量构建 target；
- `Canopus-Module-BluetoothAudio/targets/xiaomi-band-10-pro-3.101.043.env:1-5`：选择 043 的 Rust feature、triple 和 CPU；
- `Canopus-Module-BluetoothAudio/crates/bluetooth-audio-device/src/target/native_app.rs:101-183`：原生应用分两阶段执行 `app_lookup`、`app_install` 和 `launcher_add`；
- `Canopus-Module-BluetoothAudio/crates/bluetooth-audio-device/src/target/mod.rs:63-107`：Bluetooth 注册、音频设备注册和 decoder workspace 初始化；
- `Canopus-Module-BluetoothAudio/crates/bluetooth-audio-device/src/target/bluetooth.rs:326-365`：adapter 获取、callback table 注册和 adapter 状态读取。

目前 build/verifier 能证明 ELF、重定位、undefined symbol、地址范围和目标身份的一部分约束，但不能证明：

- 每个 firmware callable 真的是所需语义函数；
- 全局变量的解引用层数正确；
- callback table 的 slot、生命周期和所有权正确；
- descriptor 的字段布局与调用点一致；
- `app_install`/`launcher_add` 在正确的 miwear 线程上下文执行；
- 043 的所有 UI helper 与 036 具有相同 ABI。

### 2.5 `Bond 3/2 0F -1105` 暴露的是 data-path/timing 证据缺口

第三次 043 反馈与前两次不同：`Bond 3/2` 和 flags `0x0F` 证明 Pair Request filter、Pair Request/Display 回调和 Classic BONDED 均已完成，`-1105` 属于之后的 L2CAP/AVDTP `ERR_REMOTE`。当次诊断不含 `0x40`，因此没有观察到 mHDT Configuration Request rewrite，但 compact 状态行仍不能单独区分 remote AVDTP Reject 与 signaling L2CAP 非正常断开。

旧实现只有在 firmware connection-confirm callback 已将 signaling/media CID 发布到 runtime 后，才会对 Configuration Request 执行 exact `7F 01 01` 变换。该假设不由地址 matcher 或 ABI verifier证明：协议线上 successful Connection Response 先于 peer Configuration Request，而 firmware callback dispatch 可能晚于两个 packet。修复后 portable core 直接从 wire Connection Response 学习 local Source CID，并以 transaction-scoped hint 匹配紧随其后的 Configuration Request；target config 不再拥有启用 mHDT 的 policy boolean。

这次反馈形成两条新的 ABI/semantic hard-negative assumptions，而不是错误地址 veto：

- 不得假设 runtime CID 总在 inbound Configuration Request 之前由 firmware callback 发布；
- 不得把 target-independent mHDT packet policy 放进 target backend 或由 target cfg 开关。

exact firmware 仍必须单独恢复 raw-H4 writable callback seam、pointer chain、stock dispatcher 和 power-transition ownership。静态证明这些地址及 compare/write 路径正确，仍不能证明每个 controller packet 都实际经过 replacement callback；因此新 build 分别记录 hook-installed `0x80` 与 rewrite-hit `0x40`，设备 gate 保持 pending。

---

## 3. 当前 `fw-match` 的具体不足

### 3.1 Corpus 特征过于短和扁平

`tools/fw-match/extract_corpus.py` 现已把原有函数摘要扩展为 bounded Corpus v2：

- 函数入口前最多 32 bytes；
- block 数量、block 相对偏移、大小和 successor edges；
- 最多 64 个 callees/callers；
- 最多 32 个 C string 和 128 个小常量；
- 每函数最多 128 个非代码 data refs（函数内 offset、目标地址、read/write/offset）；
- 顶层独立 `DataObjectRecord`（segment class、writable、size、alignment、bounded bytes、readers/writers/xrefs）；
- 原始 IDB 地址。

v1 corpus 仍可由 Rust/Python matcher 读取，缺失的 `globals`/`data_refs` 默认为空。v2 当前实现足以进行 candidate-only global 检索，但仍不足以支持高风险 ABI 自动晋级：

1. 入口 32 bytes 对 tiny veneer、wrapper、通用 prologue 极易产生碰撞；
2. string 只有内容，没有引用位置、访问方式、字符串长度/编码类别和调用上下文；
3. 常量只有数值集合，没有区分协议值、枚举、位掩码、地址片段、数组长度和编译器生成常量；
4. caller/callee 被 64 个上限截断，公共函数的 degree 会饱和；
5. 已有 bounded global access offset/direction，但还没有栈帧、寄存器读写、返回值使用、pointer depth 和 callback 保存/生命周期信息；
6. 没有描述一个函数在模块 ABI 中属于 allocator、dispatcher、constructor、callback、UI factory 还是数据 accessor；
7. bounded `schema=2` 还没有表达完整证据来源、分析器版本、alias 归一化、normalized instructions 和 ABI hypotheses。

### 3.2 CFG 提取了拓扑，但评分没有真正使用拓扑

`extract_corpus.py` 确实保存了 `succ` 边，但 `tools/fw-match/src/score.rs:96-154` 的 `cfg_score` 实际只比较：

- block 数量比例；
- 归一化后的 block size multiset；
- edge count 比例。

它没有比较：

- 哪个 block 是 entry；
- branch 的方向和条件类型；
- successor 拓扑；
- dominator/post-dominator；
- loop、switch、early return、error exit；
- callsite 在哪个 block；
- load/store/global access 所在 block。

因此两个 block 数量、大小和边数相似、但控制流语义不同的函数仍可能得分很高。

### 3.3 xref/anchor 目前只能弱化 tie，不能真正修正结构误匹配

`tools/fw-match/src/ga.rs:176-200` 把 anchor xref 定义为 structural score 的 tie-breaker；
`tools/fw-match/src/ga.rs:245-270` 又把 xref bonus 乘以 `0.001`。

因此即使 callee/caller 关系已经提供了强证据，也几乎不可能推翻一个错误但结构分较高的候选。相反，`tools/fw-match/src/engine.rs:78-140` 会根据固定的 score/margin 把候选冻结，之后不再重新审查。

当前还有几个结构性问题：

- `CONF_SCORE=11.0`、`CONF_MARGIN=0.25`、4 轮搜索是固定常量：`engine.rs:78-81`；
- candidate pool 默认最多 24 个：`EngineConfig` 和 `ga.rs:133-200`；
- top candidate 相对 runner-up 超过 25% 就会被锁定：`ga.rs:282-293`；
- 未确认结果在 `engine.rs:159-177` 仍会通过 fallback 产生一个“最佳地址”；
- `load_source_symbols` 主要按 `kind=function` 和 `entry_address` 读取：`engine.rs:211-246`，没有把证据状态、approval、policy、provenance 作为匹配前置门禁。

### 3.4 当前验证集不能作为独立 oracle

`tools/fw-match/verify_match.py:60-106` 使用 source/target symbol records 的同名函数作为 ground truth。

这适合测量“与已有 target pack 一致性”，不适合证明新迁移正确。新方案必须建立独立 oracle：

- oracle 的地址来自人工审计的 stock callsite、独立 IDA/Ghidra/原始字节分析或设备探针；
- oracle 记录必须带固件 hash、证据来源和 reviewer；
- matcher 不能修改 oracle；
- 只有 holdout oracle 上的结果才能用于校准算法权重和阈值。

---

## 4. BinDiff 风格的核心改造

### 4.1 借鉴 BinDiff 的部分

新系统不应复制 BinDiff 的实现细节，但应借鉴它的核心思想：

> 不让单一匹配算法决定函数对应关系，而是让多个独立算法产生函数/基本块候选，再利用调用图和结构关系提高置信度。

计划引入以下 matcher family：

| Matcher | 主要证据 | 适合场景 | 不能单独证明 |
|---|---|---|---|
| Exact bytes / exact hash | 完整函数或基本块字节完全相同 | 未变化代码、编译器未重排区域 | ABI、global 语义 |
| Relocation-normalized bytes | 对 branch、literal、MOVW/MOVT、地址引用归一化后的字节 | 同一实现发生地址移动 | 函数是否语义相同 |
| Instruction mnemonic hash | 指令类别、操作数角色、寄存器关系 | 编译器重定位和轻微重排 | 全局/回调语义 |
| Basic-block hash | block 内 normalized instruction 序列 | 局部代码相同、入口变化 | 完整函数边界 |
| CFG graph match | block 节点、边、条件、循环和 exit 结构 | 函数重排、插入少量分支 | 原型与数据类型 |
| String/data-reference match | 字符串内容、引用 offset、访问方式 | UI、日志、协议、错误路径 | 无字符串函数 |
| Constant/semantic match | 协议值、枚举、位掩码、数组长度类别 | Bluetooth、LVGL、NuttX dispatcher | 通用数学代码 |
| Call graph neighborhood | callers/callees 的候选集合及方向 | wrapper、公共 helper、短函数 | seed 不足时的冷启动 |
| Degree/context match | caller/callee 数量、线程/段/section/owner context | 区分通用 veneer 和实际入口 | 仅靠数量会碰撞 |
| Type/ABI match | 参数寄存器、返回值、内存读写、调用点约束 | callable、global、callback | 需要真实 callsite |
| Data-flow match | global/table 访问、读写方向、解引用层数 | singleton、callback table、owner queue | 纯函数代码 |

其中前 9 类是 BinDiff 风格的函数级/图级匹配器，后 3 类是针对 Canopus 固件 ABI 额外加入的验证器。

### 4.2 每个 matcher 必须返回结构化证据，而不是一个分数

统一输出：

```json
{
  "matcher": "cfg_graph",
  "source": "0x0ca28770",
  "target": "0x0ca40f68",
  "similarity": 0.94,
  "confidence": 0.78,
  "rank": 1,
  "evidence": [
    {
      "kind": "block-topology",
      "source": "entry->error_exit->return",
      "target": "entry->error_exit->return",
      "strength": 0.91
    },
    {
      "kind": "call-shape",
      "source": "calls adapter_register and get_state",
      "target": "calls adapter_register and get_state",
      "strength": 0.87
    }
  ],
  "negative_evidence": [],
  "analysis": {
    "firmware_sha256": "...",
    "corpus_schema": 2,
    "tool_version": "..."
  }
}
```

必须区分：

- `similarity`：两个对象看起来有多像；
- `confidence`：该算法认为这次对应关系有多可信；
- `margin`：第一候选与第二候选的距离；
- `evidence`：为什么像；
- `negative_evidence`：为什么可能不是；
- `independence_group`：证据是否真正独立。

例如，三个都依赖前 32 bytes 的 matcher 不能算三种独立证据；它们最多属于同一个 `bytes` evidence group。

---

## 5. 新的多算法置信度模型

### 5.1 不采用简单平均分

不能直接做：

```text
confidence = (byte_score + cfg_score + string_score + ga_score) / 4
```

因为：

- 各算法的错误相关性不同；
- byte 和 mnemonic 可能同时被同一个 compiler artifact 误导；
- string match 对 UI 函数有价值，对 allocator 几乎没有价值；
- caller/callee 证据只有在邻居已经独立确认时才有意义；
- 一个强烈的 ABI 反证应该能够否决很多弱正证据。

### 5.2 分层置信度

对每个候选维护三层结果：

#### Layer A：独立 matcher evidence

每个 matcher 先输出 calibration 后的概率 `p_i`，同时标记 evidence group：

```text
bytes
instruction
cfg
string-data
constant
callgraph
dataflow
abi
callsite
```

同一 group 内不直接相加，避免重复计数。

#### Layer B：角色条件化融合

不同 symbol role 使用不同权重和必需证据：

| Symbol role | 主要要求 | 最低晋级条件 |
|---|---|---|
| 普通纯函数 | normalized instruction + CFG + size | 两个独立 group，且无反证 |
| UI factory | CFG + stock caller + prototype + return object | 必须有 callsite ABI 证据 |
| allocator/free | CFG + caller/callee role + size/return semantics | allocator/free pairing 一致 |
| global singleton | data-flow + section/segment + 读写方向 | 不能只靠函数 matcher |
| callback table | table layout + slot + stock callback value | 必须验证 table base、slot 和生命周期 |
| callable wrapper | exact stock callers + parameter flow | 原型和返回值用途必须一致 |
| app/Launcher API | descriptor type + exact caller + lifecycle context | 需要独立 callsite 与结构布局证据 |
| HCI/L2CAP hook | global chain + write policy + stock function guard | 需要数据流和设备探针计划 |

#### Layer C：硬否决规则

以下任一项出现，候选不能 `CONFIRMED`：

- target 函数不在 canonical executable mapping；
- Thumb entry/callable 奇偶关系不符合目标架构；
- 目标函数被反编译为不同参数数量或不同返回值使用；
- 调用点传入的寄存器/栈参数与 symbol prototype 不一致；
- global 地址落在错误 section，或读写方向相反；
- callback slot 当前 stock value 与预期 callback 不一致；
- 同一个 target 地址被多个互斥 symbol 占用；
- 只能依赖另一个未确认候选形成循环证明；
- target pack provenance hash 与输入固件不一致；
- 目标函数只由相似设备或相邻固件地址推断，没有新固件独立证据。

### 5.3 推荐的融合输出

融合器可以使用带先验和校准的 log-odds 模型，或使用可解释的规则化 logistic model：

```text
logit(P(match)) = bias(role)
  + w_bytes(role)      * calibrated(bytes)
  + w_cfg(role)        * calibrated(cfg)
  + w_callgraph(role)  * calibrated(callgraph)
  + w_dataflow(role)   * calibrated(dataflow)
  + w_abi(role)        * calibrated(abi)
  + w_callsite(role)   * calibrated(callsite)
  - penalties(negative evidence)
```

但最终输出不能只有一个概率，必须同时输出：

```text
CONFIRMED
  = P >= role_threshold
  ∧ evidence_groups >= required_groups
  ∧ required_role_evidence_present
  ∧ no_hard_negative
  ∧ candidate_margin >= calibrated_margin

CANDIDATE
  = 有候选，但未满足上述条件

REVIEW_REQUIRED
  = 多算法冲突、原型冲突、global/table 语义不一致

WITHDRAWN
  = 旧候选已被新证据否定，保留历史记录但禁止生成绑定

BLOCKED
  = 输入或分析质量不足，不能安全匹配
```

`confidence=0.99` 不能绕过硬否决规则；`confidence=0.65` 也不能因为 build 成功而晋级。

---

## 6. Corpus v2：从函数摘要升级为可验证对象

当前 `schema=1` corpus 不删除，新增 `schema=2`。bounded v1/v2 wire format 由 `schemas/fw-corpus.schema.json` 约束；036/043 两份真实 v2 corpus 已通过该 schema 验证。

### 6.1 函数特征

每个函数记录至少加入：

```json
{
  "target_id": "...",
  "firmware_sha256": "...",
  "canonical_entry": "0x...",
  "entry": { "address": "0x...", "callable": "0x..." },
  "size": 123,
  "instructions": [
    {
      "offset": 0,
      "mnemonic": "push",
      "operand_roles": ["register-set", "lr"],
      "normalized": "push {callee_saved, lr}"
    }
  ],
  "blocks": [
    {
      "id": 0,
      "offset": 0,
      "size": 18,
      "kind": "entry",
      "terminator": "conditional-branch",
      "calls": [0x...],
      "data_refs": [0x...]
    }
  ],
  "cfg": {
    "edges": [[0, 1], [0, 3]],
    "dominators": [],
    "post_dominators": [],
    "loops": [],
    "exit_kinds": ["return", "error-return"]
  },
  "calls": [],
  "callers": [],
  "strings": [],
  "constants": [],
  "globals": [],
  "stack": {},
  "abi_hypotheses": [],
  "fingerprints": {}
}
```

重点是保存 **归一化的指令语义和引用角色**，而不是只保存 raw bytes。

### 6.2 全局变量与表格特征

函数匹配不能替代 global matching。新增独立的 `DataObjectRecord`：

- 地址和 canonical segment；
- 只读/可写属性；
- 对齐和大小；
- 被哪些函数读取/写入；
- 解引用层数；
- 被解释为 pointer、pointer-to-pointer、callback table、counter、handle 还是 opaque bytes；
- table 的长度和 slot 内容；
- 生命周期：boot、service startup、adapter ON、page create、resident。

这对以下对象尤其重要：

- `bt_l2cap_owner`；
- adapter instance；
- callback registration handle；
- callback table；
- GAP receive slot；
- allocator owner；
- Launcher/app registry；
- UI style/global objects。

### 6.3 Callsite ABI 特征

对每一个供模块调用的 symbol，至少记录若干个 stock caller 模板：

```text
caller function
call instruction offset
argument source: R0/R1/R2/R3/stack
argument type hypothesis
return value use
post-call memory effects
error branch
thread/context evidence
```

例如 `app_install` 不能只记录“函数大小、CFG、名字相似”，还必须记录：

- stock caller 是否传 descriptor 指针；
- pages 参数是 pointer array 还是 inline array；
- page count 位宽；
- 返回值是否按 zero-success 使用；
- descriptor 是否被 firmware 保存；
- 调用发生在 miwear/page owner context 还是 module constructor context。

### 6.4 Corpus 必须可重建

抽取器应支持两类输入：

```text
raw firmware
  → 新建/重建分析数据库
  → export corpus v2

valid IDB
  → 验证 IDB 与 raw firmware SHA、loader、segments 一致
  → export corpus v2
```

不能把某个不可打开、空数据库或不匹配 binary 的 `.i64` 当作权威输入。当前 036 路径在本轮实际观察到 `.id0/.id1/.nam/.til` 组件，但 `.i64` 不在目标目录，IDA 曾报告 `Database is empty`/初始化错误；这说明未来脚本必须检测并报告“分析输入不可复现”，不能静默使用旧 corpus。

抽取器还应：

- 使用现代 `ida_*` API，不再依赖旧 `idc` 读取路径；
- 记录 IDA/Hex-Rays/processor/module 版本；
- 记录 canonical mapping 和 alias mapping；
- 对每个导出文件写入 raw firmware SHA；
- 使用 lock，避免 MCP worker 和批处理同时打开同一数据库；
- 支持在临时目录重建分析 DB，避免破坏用户原始 IDB。

---

## 7. 匹配流水线设计

### 7.1 Pass 0：固件身份和布局

输入：raw firmware、可选 IDB、target metadata。

检查：

- SHA-256；
- version/build strings；
- architecture、Thumb、endianness、float ABI；
- segments、XIP/data/RAM mapping；
- binary 与 IDB 的 input hash；
- alias mapping；
- firmware family 与 board identity。

失败时：只生成 `BLOCKED` report，不进入 matcher。

### 7.2 Pass 1：高质量 anchor 检索

优先寻找能独立证明语义位置的 anchor：

1. 唯一版本/build 字符串及其 xref；
2. 独特错误日志、协议字符串、服务名；
3. 固定表头、descriptor magic、callback table 结构；
4. 已知函数族的多个邻居；
5. section/segment 和对齐约束；
6. relocation-normalized byte signature；
7. 数据流中的全局访问模式。

anchor 必须记录来源和独立性。普通短 veneer 只能作为“连接点”，不能作为第一类语义 anchor。

### 7.3 Pass 2：BinDiff 风格函数和基本块匹配

对每个未定位函数运行 matcher ensemble：

```text
exact/normalized bytes
instruction sequence
basic-block hash
CFG graph
strings/data refs
constants
caller/callee neighborhood
size/degree/context
```

候选生成使用索引，而不是对 42,000 个函数全部做昂贵两两比较：

- mnemonic n-gram inverted index；
- normalized block hash index；
- string/data reference index；
- size/block/section bucket；
- call graph degree bucket；
- optional locality-sensitive hash。

第一轮只保留 top-K 候选和所有达到 minimum evidence 的候选，不得因为 pool 上限 24 而丢掉一个可能由图证据恢复的候选。

### 7.4 Pass 3：调用图/基本块图全局优化

这里可以借鉴 BinDiff 的 graph matching，但不建议继续让 GA 直接决定最终地址。

推荐：

1. 用独立 anchors 初始化 matched graph；
2. 对 caller/callee 邻域做逐层扩展；
3. 对每个候选计算 graph consistency；
4. 用确定性 beam search、最大权二分匹配或约束优化解决冲突；
5. 保留多个等价解，直到 ABI/dataflow 证据打破平局；
6. 若仍有多个等价解，输出 `REVIEW_REQUIRED`，不输出 confirmed 地址。

如果保留 GA，应限制其职责：

- 只能在候选集合中搜索全局 assignment；
- 不能把低置信度候选变成 confirmed；
- 不能覆盖 hard negative；
- 输出必须带多解差异和约束违反报告；
- 与 deterministic solver 的结果不一致时，标记冲突而不是静默选 GA 结果。

### 7.5 Pass 4：全局/表格/数据流匹配

单独处理 global symbol：

- 从 stock caller 反向追踪全局根地址；
- 验证读/写/volatile/解引用层数；
- 验证 table 长度和 slot 语义；
- 验证全局对象是否由 target firmware 自己初始化；
- 检查相邻变量布局和 section。

特别是 callback table，必须证明：

```text
旧 stock callback table
  → target stock callback table
  → 目标 slot 的原始 callback
  → module replacement 的 slot
  → registration handle
  → unregister/cleanup 生命周期
```

不能因为两个 target 的 table 都是 16 words，就复制 table base 或 slot 假设。

### 7.6 Pass 5：ABI/callsite verifier

这是本项目相对于普通 BinDiff 的关键扩展。

对每个模块需要调用的 function：

- 反编译目标函数；
- 反编译至少一个 stock caller；
- 检查入口参数寄存器/栈；
- 检查返回值类型和使用方式；
- 检查 pointer dereference；
- 检查 callback 保存；
- 检查 error return；
- 检查执行线程/owner context 的静态线索；
- 检查 caller 与 target 的 direct xref 是否存在；
- 检查目标不是通过某个 alias/veneer 误解析到邻近函数。

可将验证模板分为：

```text
FUNCTION_CALL_TEMPLATE
GLOBAL_ACCESS_TEMPLATE
CALLBACK_REGISTRATION_TEMPLATE
UI_FACTORY_TEMPLATE
DESCRIPTOR_CONSUMER_TEMPLATE
ALLOCATOR_PAIR_TEMPLATE
```

只有满足模板的候选才可以进入 `STATIC_CONFIRMED`。

### 7.7 Pass 6：多参考固件交叉验证

对于同设备不同版本：

```text
030 → 036
036 → 043
030 → 043
```

对于跨设备：

```text
Band 10 036/043
  ↔ Band 11 4.100.108
  ↔ Band 9 Pro 3.1.175
```

交叉验证不是要求所有固件都存在同一个函数地址，而是比较：

- 是否属于同一 subsystem family；
- 是否有同一个 semantic contract；
- 是否满足相同或兼容的 ABI 模板；
- 哪些层可以共享（例如 public facade）；
- 哪些层必须重新恢复（地址、global、callback、layout、loader）。

多固件一致只能提升先验，不能替代新固件的 callsite 证明。

---

## 8. BluetoothAudio 的模块依赖闭包

新方案不应该默认迁移 target pack 中所有 124 个 symbol。应该从模块实际依赖生成一个有角色的 manifest。

### 8.1 依赖分组

BluetoothAudio 至少分为：

#### Identity/loader

- firmware version/build string；
- identity guard；
- NuttX open/close/read/write/ioctl/errno；
- module registration descriptor。

#### Allocator/lifetime

- `bt_alloc`；
- `bt_free`；
- `bt_buffer_new`；
- queue cancellation/free；
- timer argument ownership。

#### Bluetooth adapter

- adapter get instance/state；
- register/unregister；
- discovery start/stop；
- scan mode；
- pairing state；
- pair request/display reply；
- create/remove bond。

#### L2CAP/transport

- owner global；
- connect/disconnect/submit CID；
- timer add/cancel；
- external queue；
- GAP receive slot and stock receive dispatcher。

#### SDP

- builder create；
- raw attribute；
- commit/unregister。

#### Native app/Launcher/UI

- app lookup/install；
- launcher add；
- page descriptor type；
- page goto/finish；
- LV timer；
- event code/user data；
- label/list/content/title factory；
- align/hidden/event registration；
- style globals and style functions。

### 8.2 依赖闭包输出

每个模块 target build 前生成：

```json
{
  "module": "org.canopus.bluetooth-audio",
  "target_id": "xiaomi-band-10-pro-3.101.043",
  "required_symbols": [
    {
      "name": "app_install",
      "role": "native-app.descriptor-consumer",
      "prototype": "...",
      "required_state": "STATIC_CONFIRMED",
      "evidence_groups": ["cfg", "callsite", "abi", "type-layout"]
    }
  ],
  "required_globals": [],
  "required_types": [],
  "blocked_capabilities": [],
  "unresolved": []
}
```

规则：

- manifest 中的 `required_symbols` 必须全部达到对应角色的最低状态；
- 任一 critical symbol 为 `CANDIDATE`、`REVIEW_REQUIRED`、`WITHDRAWN` 或 `BLOCKED`，`--strict` 直接失败；
- 不因为另一个模块或另一个 target 已经有同名地址而放行；
- 不在运行时扫描“最像的函数”作为 fallback；
- 只生成当前 target、当前 firmware hash 的 bindings。

### 8.3 当前 043 原生应用路径应单独拆分

由于用户观察到的崩溃发生在 Launcher 打开注册的原生应用之后，第一阶段不应同时验证完整 Bluetooth/audio/UI 功能。

建议将 043 device probe 拆为：

```text
A. identity guard
B. module registration
C. app_lookup only
D. app_install stage 1, 不调用 launcher_add
E. page descriptor create/destroy
F. 单个 label/page title
G. 一个 event callback
H. launcher_add stage 2
I. Bluetooth adapter registration
J. L2CAP/SDP/audio
```

每一步都记录：

- target id/version/build/hash；
- artifact SHA；
- status query；
- 返回值和 errno；
- 前后 page/app registry 状态；
- crash log；
- 是否需要 reboot 恢复。

这样可以将“地址错误”“descriptor/layout 错误”“调用上下文错误”“UI 生命周期错误”和“Bluetooth resident callback 错误”分开。

---

## 9. Target pack 和证据模型改造

现有 `targets/<target-id>/symbols/*.json` 继续作为生成输入，但每个 symbol 应新增或规范化以下字段：

```json
{
  "symbol_id": "target.semantic.name",
  "target_id": "...",
  "kind": "function",
  "name": "app_install",
  "entry_address": "0x...",
  "callable_address": "0x...",
  "prototype": "...",
  "role": "native-app.descriptor-consumer",
  "status": "STATIC_CONFIRMED",
  "approval_state": "APPROVED",
  "provenance": {
    "firmware_sha256": "...",
    "analysis_db_sha256": "...",
    "corpus_schema": 2,
    "evidence_ids": []
  },
  "match": {
    "source_targets": [],
    "candidate_ids": [],
    "matcher_reports": [],
    "fusion_version": "..."
  },
  "proof": {
    "static": "confirmed",
    "callsite": "confirmed",
    "abi": "confirmed",
    "device": "not_probed"
  },
  "negative_evidence": [],
  "supersedes": [],
  "superseded_by": []
}
```

关键原则：

1. `STATIC_CONFIRMED` 不能只由 score/margin 产生；
2. 被否定的地址不能从仓库中消失，要进入 `WITHDRAWN` 历史；
3. generated 文件只能从 records 生成；
4. `FORBIDDEN`、`WITHDRAWN`、`PENDING` symbol 不得生成 public callable wrapper；
5. `native-full-trust` 必须有 callsite/ABI evidence；
6. global 与 function 使用不同的 matcher 和晋级规则；
7. target pack 的 revision 必须随 corpus/fusion/evidence 规则变化而增加。

---

## 10. CLI 和自动化接口

### 10.1 推荐命令

第一阶段可以实现为 `canopus-cli` 子命令：

```sh
cargo run -p canopus-cli -- \
  fw inspect \
  --firmware /path/to/vela_ap.bin \
  --analysis-db /path/to/vela_ap.bin.i64
```

```sh
cargo run -p canopus-cli -- \
  fw adapt \
  --firmware /path/to/vela_ap.bin \
  --analysis-db /path/to/vela_ap.bin.i64 \
  --target-id xiaomi-band-10-pro-3.101.043 \
  --reference xiaomi-band-10-pro-3.101.036 \
  --module org.canopus.bluetooth-audio \
  --targets-dir targets \
  --report build/fw-adapt/043/report.json \
  --strict
```

```sh
cargo run -p canopus-cli -- \
  fw candidates \
  --report build/fw-adapt/043/report.json \
  --status REVIEW_REQUIRED
```

```sh
cargo run -p canopus-cli -- \
  fw promote \
  --report build/fw-adapt/043/report.json \
  --only module-closure \
  --require-callsite-proof
```

### 10.2 `fw adapt` 的阶段

```text
inspect
  → verify identity
  → normalize segments/aliases
  → export corpus v2
  → load module ABI manifest
  → run matcher ensemble
  → graph consistency solve
  → global/table matching
  → callsite/ABI verification
  → confidence fusion
  → emit candidate report
  → strict gate
  → generate records
  → generate Rust/C bindings
  → generated stability check
  → module build/verifier
```

`--strict` 的语义必须是：

- critical closure 未全部确认时返回非零；
- 不写入 confirmed addresses；
- 可以写 candidate/evidence report；
- 不覆盖已有 target pack；
- 需要显式 `promote` 才能改变 records。

### 10.3 输出目录

建议：

```text
build/fw-adapt/<target-id>/
├── identity.json
├── corpus-v2.json
├── candidates.json
├── fusion-report.json
├── graph-solutions.json
├── abi-verification.json
├── module-closure.json
├── promotion-plan.json
└── rejected/
```

所有输出都写入输入 hash、命令参数、分析工具版本和配置版本，保证同一输入可以重现同一报告。

---

## 11. 验证和校准策略

### 11.1 独立 oracle 数据集

必须建立与 target pack promotion 分离的 oracle：

```text
oracle/<firmware-pair>/<role>/<symbol>.json
```

每条 oracle 至少包含：

- source/target firmware SHA；
- source/target address；
- symbol role；
- 独立证据来源；
- prototype/ABI 结论；
- reviewer；
- 是否允许用于训练权重；
- 是否保留为最终 holdout。

oracle 不得由 `matches.json` 自动生成。

### 11.2 校准数据集

至少覆盖：

- 030 → 036：同设备、相邻固件；
- 036 → 043：同设备、已知大量地址移动；
- 030 → 043：同设备、另一条 source path；
- Band 10 → Band 11：同生态、不同设备；
- Band 10 → Band 9：不同 UI/Bluetooth/loader family；
- 正确匹配、相似错误匹配、wrapper/veneer collision、删除/拆分/合并函数。

指标不再只有 recall：

| 指标 | 目标 |
|---|---:|
| critical symbol confirmed precision | 100% 独立 oracle |
| critical symbol wrong promotion | 0 |
| ambiguous case incorrectly auto-promoted | 0 |
| same-model common function recall | ≥ 95%，按 role 分层统计 |
| cross-device common semantic recall | 仅作为效率指标，不牺牲 precision |
| generated rebuild stability | 100% |
| old-target address leakage | 0 |
| module closure unresolved under `--strict` | 0 |

### 11.3 变异测试

为防止算法只记住 036/043 的具体布局，自动生成变异样本：

- 改 branch offset；
- 移动 literal pool；
- 插入无影响 basic block；
- 改寄存器分配；
- 改函数顺序；
- 加 wrapper；
- 删除一个 string；
- 改常量但保留 control flow；
- 替换 callback slot；
- 改 global pointer 层数。

要求：

- 应保持正确匹配的候选排序；
- 对 ABI 改变应降低 confidence 或输出冲突；
- 不能因为 byte similarity 仍很高就错误晋级。

---

## 12. 实施分阶段计划

### Phase 0：冻结和审计当前 043

不改地址，先建立基线：

- 保存当前 043 target pack、generated 文件和两份历史 matcher report 的 hash；
- 从 `EVID-MATCH-001` 和用户已修正的至少 5 个错误建立 `withdrawn/known-false-positive` 清单；
- 标记哪些错误属于 byte/CFG 冲突，哪些属于 callsite/ABI 错误；
- 将 `confirmed` 与 `device-proven` 明确分离。

交付：`promotion-audit-043.json`、错误分类报告。

### Phase 1：输入和 corpus v2

- 实现 firmware/IDB identity checker；
- 支持 raw firmware 重建分析数据库；
- 处理 XIP/FLASH alias；
- 导出 normalized instructions、完整 CFG、data refs、stack、callsite metadata；
- 加入 corpus schema 2 和 provenance。

交付：036/043/030 corpus v2，可重复导出。

### Phase 2：BinDiff 风格 matcher ensemble

先实现不改变 records 的只读报告：

- exact/normalized bytes；
- mnemonic/block hash；
- CFG topology；
- string/data reference；
- call graph neighborhood；
- degree/context；
- deterministic candidate index。

交付：每个候选都有 matcher-by-matcher evidence，不再只有一个 composite score。

### Phase 3：图一致性和冲突求解

- anchor graph；
- caller/callee 多层扩展；
- 最大权匹配或 deterministic beam search；
- 多解保留；
- collision、cycle dependency、negative evidence 检测；
- GA 仅作为可选比较器，不作为唯一 authority。

交付：`graph-solutions.json` 和冲突报告。

### Phase 4：ABI/callsite/global 验证

- function call template；
- UI factory/template；
- descriptor layout validator；
- callback table validator；
- allocator pairing validator；
- global data-flow validator；
- target-private API closure validator。

交付：`STATIC_CONFIRMED` 只能通过新 verifier 产生。

### Phase 5：模块驱动适配

- 从 BluetoothAudio 生成 module ABI manifest；
- 只匹配模块 closure；
- 把 native app/Launcher 从 Bluetooth/AVDTP 分离；
- `--strict` 不允许 unresolved critical symbols 进入生成阶段；
- 生成 target-private facade 和 Rust/C bindings。

交付：043 的 dry-run 适配报告，不上设备。

### Phase 6：独立 oracle 和校准

- 建立人工审计 oracle；
- 运行 030/036/043、Band 11、Band 9 的 holdout；
- 估计 role-specific weights/thresholds；
- 运行 mutation tests；
- 验证旧错误不会再次被 promotion。

交付：校准配置、precision/recall/confusion report。

### Phase 7：设备分阶段 gate

建议顺序：

```text
T0 identity rejection
T1 module registration/status
T2 app_lookup
T3 app_install stage 1
T4 page descriptor create/destroy
T5 single UI label/event
T6 launcher_add stage 2
T7 adapter registration
T8 discovery/pairing
T9 L2CAP/SDP
T10 AVDTP/audio
T11 reboot/disable/recovery
```

043 当前 crash 场景应优先完成 T0–T6，不能先用完整 BluetoothAudio 运行结果判断整个 target 地址集正确。

---

## 13. 需要明确的边界

### 13.1 “几乎一行命令”是自动化目标，不是无条件承诺

对于以下情况，自动化必须停止并要求人工 review：

- 固件删除或合并了目标函数；
- function 被 inline 且没有稳定 callsite；
- 参数数量或 callback ABI 改变；
- global 从 pointer 改为 inline object，或反之；
- callback table slot 重新排列；
- UI major version 改变；
- Bluetooth stack 从 Bluelet/BES family 改变；
- loader/constructor/thread context 改变；
- corpus/IDB 与 raw firmware hash 不一致。

正确的自动化结果是：

```text
无法安全确认，阻止生成
```

而不是：

```text
选择排名第一的地址，继续编译并让设备替我们发现错误
```

### 13.2 跨设备迁移必须以 semantic contract 为中心

Band 10 → Band 11 不应理解为“把 Band 10 地址找一个 Band 11 对应地址”。应拆成：

```text
public semantic facade
  → subsystem family contract
  → target-private ABI adapter
  → exact target symbol/global/type records
```

例如：

- `adapter.register` 可以共享语义 contract；
- callback table 的大小、slot、registration handle 必须重新恢复；
- `lvx_list_row_create` 的语义可以相似，但参数和 trailing ABI 不能默认相同；
- `app_install` 的 app/page descriptor 必须按目标固件重新验证；
- 如果目标没有证明某项能力，必须生成 unavailable/fail-closed，而不是复制 Band 10 的实现。

---

## 14. Definition of Done

### 静态适配完成

- [ ] raw firmware、IDB、corpus 三者 hash 一致；
- [ ] target identity、segments、aliases 已验证；
- [ ] corpus v2 可重建；
- [ ] 每个 critical symbol 有至少两个独立 evidence groups；
- [ ] function 有 callsite/ABI 验证；
- [ ] global/table 有 data-flow/layout 验证；
- [ ] unresolved candidate 不会生成 callable binding；
- [ ] withdrawn/forbidden symbol 不会生成 wrapper；
- [ ] module dependency closure 全部通过 `--strict`；
- [ ] generated Rust/C/config 可 byte-for-byte 重建；
- [ ] target-private 没有硬编码 firmware address；
- [ ] ELF verifier PASS；
- [ ] 所有已有 target 重新构建通过；
- [ ] 独立 oracle 上 critical precision 为 100%。

### 设备支持完成

还必须额外拥有：

- [ ] T0 identity rejection 证据；
- [ ] loader/module registration 证据；
- [ ] native app/Launcher/UI 生命周期证据；
- [ ] Bluetooth callback、L2CAP、SDP、AVDTP/audio 证据；
- [ ] 错误路径和超时路径证据；
- [ ] reboot/disable/remove/recovery 证据；
- [ ] crash log 与恢复方法；
- [ ] 精确 firmware version/build/hash 和 artifact SHA。

---

## 15. 推荐的下一步

在真正修 043 地址之前，按以下顺序实施：

1. **冻结当前 043 records，不继续扩大“confirmed”范围。**
2. **把用户已经修正的至少 5 个错误和 `EVID-MATCH-001` 中的误匹配全部纳入独立 audit fixture。**
3. **实现 module ABI manifest，只列出 BluetoothAudio 真正需要的函数、global、type 和 context。**
4. **先实现 corpus v2 的 normalized instruction、完整 CFG、data-flow/callsite 元数据。**
5. **实现 BinDiff 风格 matcher ensemble，但只输出 candidates/evidence，不写 records。**
6. **实现 ABI/callsite verifier，先用已修正的 UI 和 app/Launcher 场景作为回归集。**
7. **建立独立 oracle 后，才校准 confidence 权重和 role-specific threshold。**
8. **最后才让 `fw adapt --strict` 生成 043 target-private/generated bindings。**
9. **043 设备验证先跑 native app T0–T6，再进入 Bluetooth/audio T7–T10。**

这套顺序的关键是：先让系统能够可靠地说“我不知道”，再让它自动生成地址。只有拒绝错误结果的能力稳定之后，跨固件适配才适合变成一条命令。 
