# Target-private 生成边界

## 目标

`canopus-target-private` 不应重复维护由 exact target records 已经表达的机械 ABI 信息。它应拆成两层：

```text
symbols/*.json + types/*.json + target.toml
        │
        ▼
canopus-target-generated
  - 地址
  - Thumb callable
  - prototype
  - 类型/layout
  - global/table 常量
        │
        ▼
canopus-target-private generated metadata/raw bindings
  - exact target provenance
  - record status/policy
  - target-neutral raw call surface
        │
        ▼
手写 semantic policy facade
  - ownership
  - callback mirror/rollback
  - timer/queue lifetime
  - target ABI semantic translation
  - app/Launcher transaction
  - LVGL v8/v9 semantic adaptation
  - fail-closed capability policy
```

原则：

- 地址、原型、global 地址和 layout 不得再手写到 private backend；
- generated 文件不能直接编辑；
- 手写 facade 不得调用未通过 target record promotion 的 symbol；
- `FORBIDDEN`、`WITHDRAWN`、`PENDING` 记录不能生成可调用 raw binding；
- 036 和已晋级的 043 即使 semantic facade 形状相同，也必须使用各自 firmware hash 的 generated metadata；
- 只有不属于 ABI 机械描述的策略差异才允许留在手写 backend。

## 当前已落地

```sh
python3 tools/generate_target_private_symbols.py \
  --target-id xiaomi-band-10-pro-3.101.036 \
  --symbols-dir targets/xiaomi-band-10-pro-3.101.036/symbols \
  --generated-rust sdk/rust/canopus-target-generated/src/generated_1036.rs \
  --output sdk/rust/canopus-target-private/src/generated_symbols_1036.rs
```

该生成器当前输出：

- target ID 和 firmware SHA-256；
- active function/global record 表；
- exact entry/callable constants；
- status、policy 和 prototype metadata；
- 可由 prototype records 解析出的 target-private raw wrappers；

036 backend 已经通过 `generated_symbols::TARGET_ID` 使用生成的 generated identity；043 backend 也使用独立的 `generated_symbols_1043.rs` 和 043 firmware SHA-256。`scripts/ci.sh` 会在 SDK 测试前重新生成 036/043 两份 metadata 并进行 byte-for-byte 比较，防止 generated metadata 被手工修改后漂移。

## 下一阶段

1. 给 symbol records 增加稳定的 `binding_name`、`adapter_kind` 和 `raw_binding` 字段；
2. 从 Rust prototype/type records 直接生成 private raw wrapper，而不是让 facade 重复写 `extern "C" fn`；
3. 把 `bt_*`、`lvx_*`、NuttX 和 app/Launcher 的机械 wrapper 迁移到 generated module；
4. 将 036 手写 backend 中剩余内容按 `policy`、`ownership`、`translation`、`lifecycle` 分类；
5. 为每个手写 policy 函数建立它消费的 generated symbol IDs 列表；
6. 生成依赖闭包，任何 candidate/review/withdrawn symbol 使 `--strict` 构建失败；
7. 对 callback table、global dereference 和 descriptor layout 生成结构化验证代码，而不是只生成常量；
8. 当下一个固件 target 通过新的 ABI/callsite gate 后，再生成新的 private backend；不复制既有 private 文件作为起点。

## 不应生成的内容

以下内容仍然必须经过人工设计或审查：

- callback table mirror、注册和回滚；
- allocator/free ownership pairing；
- Bluetooth-owner thread 调度；
- target ABI ownership、callback table 与固件对象生命周期适配；
- native app 两阶段 install/Launcher transaction；
- UI owner-thread 生命周期；
- 未证明能力的 fail-closed stub。

生成器的职责是消灭重复和地址漂移，不是把未经证明的语义假设自动扩散到每个固件。mHDT/L2CAP/AVDTP 这类仅依赖 wire bytes 的协议变换应位于 portable module core，不得复制进各 target-private backend，也不得由 target config 决定是否启用；target-private 只提供经 exact firmware 证明的 raw-H4 writable seam、stock dispatcher 与相应 ownership 校验。
