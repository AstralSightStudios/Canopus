# Canopus

面向嵌入式设备固件扩展的原生模块框架。

Canopus 允许开发者：

1. 用 C 或 Rust 编写功能模块；
2. 将模块编译为设备原生内核模块（target-specific ELF）；
3. 使用从目标固件中逆向恢复、经过证据审核的函数、全局对象和 ABI；
4. 针对不同设备、不同固件版本生成不同目标产物；
5. 将多个目标产物打包为同一个可安装模块包（`.canopus`）；
6. 注册真正的原生应用，使模块/应用像固件内置应用一样出现在 launcher 中；
7. 在设备端安装、启用、禁用、更新、回滚和删除模块及其原生应用；
8. 通过类似 KernelSU 管理应用的界面查看设备、框架、模块、应用、兼容性和错误状态。

## 设计原则

- **证据优先**：每个 target 能力必须经过「假设 → 静态证据 → ABI 审核 → 主机测试 → 设备探针 → 设备验证 → 能力批准」链条。
- **精确目标优先**：每个产物默认绑定完整固件 SHA-256，不做“相近版本大概可用”。
- **默认拒绝**：未登记地址、未知原型、未验证重定位、未签名包默认不批准。
- **共享 SDK 不泄漏目标私有 ABI**：固件私有结构只存在于 target pack 或生成代码。

完整设计见 [`docs/architecture.md`](docs/architecture.md)。

## 首个目标

| 字段 | 值 |
|---|---|
| target_id | `xiaomi-band-10-pro-3.101.030` |
| 设备 | 小米手环 10 Pro |
| 固件版本 | `3.101.030` |
| 固件 build | `CONBINE_LTALM078_T3.101.030_06011854` |
| 固件 SHA-256 | `f701a84ffcafa67f4d4603ad8cd66a11e5442f27140f5af0982e0975dccd225b` |
| CPU | Cortex-M33 / Thumb-2 / soft-float |
| loader | NuttX `modlib` ELF32 `ET_REL`（zero-import） |

## 仓库结构

```
cli/            canopus CLI（Rust）
schemas/        target/symbol/type/evidence/module/package JSON Schema
sdk/            C / Rust / ABI 定义
runtime/        portable C runtime（module/lifecycle/resources/diagnostics/control）
manager/        device-side supervisor / protocol / storage
app-sdk/        native app SDK（C/Rust/UI/launcher/resources）
targets/        target packs（xiaomi-band-10-pro-3.101.030/...）
modules/        示例与参考模块
tools/          RE orchestrator / symbol-generator / elf-verifier / package-builder
tests/          host / integration / fixtures / hardware
adr/            决策记录（ADR-CAN-001 ...）
```

## 当前阶段

Phase 0-6 实施中。进度见 [`docs/architecture.md`](docs/architecture.md) §23 任务进度追踪区。

## License

[AGPL-3.0](LICENSE)
