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

## 当前 registered target packs

| target_id | 固件版本 | 固件 SHA-256 | 状态 |
|---|---|---|---|
| `xiaomi-band-10-pro-3.101.036` | `3.101.036` | `662d67f5e247e31e194d3161024890ba93b9d29d70b290fadb9aac8ce8ec3c81` | trusted build target |
| `xiaomi-band-10-pro-3.101.043` | `3.101.043` | `519307675665e4866d722a8119a98589c397b614ac3294cb87bfc86de45756ec` | static pack/build target; device gate pending |
| `xiaomi-band-11-4.100.139` | `4.100.139` | `31ce82257f7c127950dc5070b86316730cf468a41f0d004559e41e7d923b2c74` | complete native installer test candidate; Lua recovery, owned staged loader and Manager host-tested; general SDK/device gates pending |
| `xiaomi-band-11-4.100.155` | `4.100.155` | `ea0bdf1920cb30223d616432af00565ca67622e6468328f5eab155f8cdc2fb9f` | independent target and Rust bindings; shared 139/155 installer; corrected notification context, ARM/Lua/verifier passed; device gates pending |

2026-09-15：修复 `.139` 注册 Manager 时的通知空上下文崩溃，详见
[crash3 审计](targets/xiaomi-band-11-4.100.139/loader/notification-crash3.md)。
`.155` 独立构建及测试说明见 [新 target](targets/xiaomi-band-11-4.100.155/loader/README.md)。

036/043 are Xiaomi Band 10 Pro Cortex-M33 / Thumb-2 / soft-float targets using the
NuttX `modlib` ELF32 `ET_REL` zero-import loader. The Band 9 / 9 Pro / 11 packs are
static candidate packs; their target-private ABI and LVGL gates remain pending. Band 9/9
Pro use firmware-bound NSH `mw`/`exec` bootstrap profiles whose command/VFS/heap/MPU/SRAM
primitives are static-recovered; no verifier-clean Band 9 Supervisor is currently staged,
and no device loader gate has passed. Band 11 (`4.100.139`, BES best1503 q66) has no approved insmod. Its independent
Lua-owned staged loader and corrected native Manager ABI now build as a complete test
candidate. The pack replaced `4.100.108`; general SDK callables and physical-device
validation remain pending. See the [native audit](targets/xiaomi-band-11-4.100.139/loader/native-audit.md)
and [packing/testing instructions](watchfaces/canopus-installer-prod/xiaomi-band-11/docs/README.md).
The Band 11 resource root contains only `main.lua` and `.bin` files.

## 仓库结构

```
cli/            canopus CLI（Rust）
schemas/        target/symbol/type/evidence/module/package JSON Schema
sdk/            C / Rust / ABI 定义
runtime/        portable C runtime（module/lifecycle/resources/diagnostics/control）
manager/        device-side supervisor / protocol / storage
app-sdk/        native app SDK（C/Rust/UI/launcher/resources）
targets/        target packs (036/043 build support; static candidates)
modules/        示例与参考模块
tools/          RE orchestrator / symbol-generator / elf-verifier / package-builder
tests/          host / integration / fixtures / hardware
adr/            决策记录（ADR-CAN-001 ...）
```

## 能力状态区分

本项目严格区分四种能力状态，任何文档/README 不得混称（`docs/progress.md` 维护
权威状态；P0/P1/P2 整改状态见 `docs/native-manager-ui-plan.md` §15）：

- **host implemented**：host 测试通过；不代表设备可用。
- **static recovered**：静态恢复出符号/类型/ABI；未经过设备验证。
- **device verified**：带 firmware hash 的真机 probe 通过。
- **production approved**：host + artifact + device gate 全部通过，且
  `approval_state == APPROVED` 且有 evidence。

当前没有任何能力标记为 production approved。supervisor/transport/lifecycle 等
处于 host implemented / device pending；launcher/native-app ABI 仍为
static recovered / BLOCKED-EVIDENCE。

## License

[AGPL-3.0](LICENSE)
