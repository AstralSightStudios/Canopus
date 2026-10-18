# ADR-CAN-006：Supervisor 服务表（canopus_services_v1）

状态：已采纳（2026-10-14，refactor-plan P0.4/K7；实现见 P2.1）

## 动机

模块目前是零 import ELF，直接绑定固件地址，因此每个模块 × 每个固件都要单独构建、发布，固件一升级全部失效。
`canopus_context_v1` 目前只有前向声明，Supervisor 调用 `activate(0)` 时传空指针，模块得不到任何 Supervisor 服务。

## 决定

通用能力由每个固件的 Supervisor 实现一次，经 `canopus_context_v1` 以函数表传给模块；模块只依赖 Canopus ABI 版本。
`target-private` 保留为 full-trust 逃生口（直接用固件地址）。

### `canopus_context_v1`

```c
struct canopus_context_v1 {
    uint32_t struct_size;   /* 向后追加，旧模块读 struct_size 判断 */
    uint32_t abi;           /* CANOPUS_ABI_MAJOR << 16 | MINOR */
    const struct canopus_services_v1 *services;
};
```

### 服务分组（每组独立版本，`services->groups[]`）

1. `log`：日志/状态
2. `time`：时间/定时器
3. `mem`：内存
4. `fs`：文件
5. `notify`：通知
6. `ui`：`canopus_ui` 语义树渲染（复用 `sdk/c/canopus_ui.h` 与 `runtime/ui`）
7. 后续批次：`bt`、`interconnect`

### 能力缺失

表中某组指针为空即为不可用；模块用 `canopus_services_has(ctx, group, min_version)` 查询。调用缺失服务返回
`CANOPUS_ERR_UNSUPPORTED`，不得崩溃；模块在 `prepare` 阶段检查并拒绝加载。

## portable 模块的 verifier 规则

portable 模块不允许出现任何固件地址：verifier 的地址扫描对 portable 目标不设白名单，命中即失败；零 import、零重定位到
固件符号的规则不变。一个 portable ELF 可在所有实现了所需服务组的固件上加载。

## 实现状态（2026-10-14）

已实现：`module`(0)、`log`(1)、`fs`(2)、`time`(3)、`mem`(4)、`notify`(5)。组编号以 `sdk/c/canopus_services.h` 为准（上文列表仅表示分类）。

- `mem`：无固定块数/字节上限，固件堆是唯一约束（每块内联小头部记账，供归属校验与卸载回收），卸载时由 Supervisor 回收；经固件 `heap_malloc/heap_free`，只由 Supervisor 调用。
- `notify`：来源恒为模块 id，标题为调用方提供的标题（空则用模块 id，截断 40 字节），正文截断 120 字节，每次加载最多 8 条；`post_with_icon` 的图标是调用方给出的图片绝对路径（`NULL` 为 Canopus 图标，读取规则同文件服务）；Band 10 Pro 与 Band 11 各自经 Manager 目标的 `canopus_manager_native_notify`（Band 11 每条通知借用独立的零化 16 字节上下文，见 EVID-NOTIFICATION-4139-002）。
- `ui`、`bt`、`interconnect`：**不实现，等到有具体模块需求时单独立项**。理由：它们落在各固件互不相同的对象模型上
  （lvgl_v9 页面/Band 11 页面描述符、蓝牙栈、互联通道），语义树渲染还需要先定出模块可用的最小 UI 词汇；
  在没有消费方时设计这些接口只会固化错误的抽象。需要这些能力的模块继续走 target-private（full-trust）路径。

## 模块注册入口（2026-10-18 统一）

构造函数拿到的东西因 loader 而异：Band 10 Pro 的原厂 modlib 不传 ctx，模块只能向 `/dev/canopus` 写 CMR1；Canopus 自己的 loader（Band 11、portable）把 ctx 放在 r0，模块走 `ctx->services->module_register`。
此前这两条路各处手写，且脚手架、Rust shim、`hello.c` 根本没有注册 descriptor（`canopus_module_descriptor` 符号没有任何 loader 读取）。
现在只有 `sdk/c/canopus_module_entry.h` 一个入口（`CANOPUS_MODULE_ENTRY` / `canopus_module_register`），按目标的生成配置在编译期选路径；示例、`canopus new` 与 `canopus module new` 模板、Rust 构造函数 shim（`sdk/rust/c_shim/canopus_ctor.c`）都用它。
两条路汇合到 `canopus_supervisor_register_descriptor`，该函数要求 descriptor 的 `module_id` 与槽位 id 相同、`target_id` 为当前目标或 `portable-thumbv8m`。
