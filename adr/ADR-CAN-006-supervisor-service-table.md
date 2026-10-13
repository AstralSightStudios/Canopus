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
6. `ui`：`canopus_ui` 语义树渲染（复用 `app-sdk/ui`）
7. 后续批次：`bt`、`interconnect`

### 能力缺失

表中某组指针为空即为不可用；模块用 `canopus_services_has(ctx, group, min_version)` 查询。调用缺失服务返回
`CANOPUS_ERR_UNSUPPORTED`，不得崩溃；模块在 `prepare` 阶段检查并拒绝加载。

## portable 模块的 verifier 规则

portable 模块不允许出现任何固件地址：verifier 的地址扫描对 portable 目标不设白名单，命中即失败；零 import、零重定位到
固件符号的规则不变。一个 portable ELF 可在所有实现了所需服务组的固件上加载。
