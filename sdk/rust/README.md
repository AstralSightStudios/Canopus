# Canopus Rust SDK

Canopus 穿戴设备模块的官方 Rust SDK 套件。提供面向 Cortex-M33（Thumbv8M-Main）微控制器的 `no_std` 裸机运行时支持、跨平台公共 ABI 契约、声明式 UI 树及针对小米手环具体型号固件的符号绑定。

---

## 1. 架构与 Crate 职责分工

Rust SDK 由六个核心 crate 组成，各层职责分工明确：

```text
┌────────────────────────────────────────────────────────────────────────┐
│                          Canopus Rust Module                           │
├──────────────────────────────────┬─────────────────────────────────────┤
│      Portable 模块 (推荐)         │      Target-Bound 模块 (Full-Trust)  │
│  (依赖 canopus-runtime/ui-core)  │      (依赖 canopus-target-private)   │
└─────────────────┬────────────────┴──────────────────┬──────────────────┘
                  │                                   │
                  ▼                                   ▼
┌──────────────────────────────────┐ ┌───────────────────────────────────┐
│         canopus-ui-core          │ │       canopus-target-private      │
│  (声明式轻量 UI 布局与视图树)       │ │  (设备私有高级门面，统一类型安全 API) │
├──────────────────────────────────┤ ├───────────────────────────────────┤
│         canopus-runtime          │ │      canopus-target-generated     │
│  (no_std 分配器/生命周期/资源跟踪) │ │  (自动化生成的底层固件符号与内存布局) │
├──────────────────────────────────┤ └─────────────────┬─────────────────┘
│           canopus-abi            │                   │
│  (跨平台公共 ABI 规范与控制头)     │                   │
└─────────────────┬────────────────┘                   │
                  │                                    │
                  ▼                                    ▼
       Canopus Supervisor 服务表             固件硬件抽象与原生符号
```

### 核心 Crate 目录与定位

| Crate | 路径 | 定位与核心职责 | 适用场景 |
|---|---|---|---|
| **`canopus-abi`** | `sdk/rust/canopus-abi` | 跨平台稳定 ABI 规范。定义 `ControlHeader`、`ModuleDescriptor`、`Snapshot`、`StatusWriter` 及核心常量，与 C 头文件 `sdk/c/canopus_abi.h` 严格布局对齐。 | 所有模块基础依赖 |
| **`canopus-runtime`** | `sdk/rust/canopus-runtime` | `no_std` 裸机运行时支持。包含轻量固定容量内存池（`FixedArenaAllocator`）、模块生命周期状态机、资源跟踪器（`ResourceTracker`）与诊断事件环形缓冲。 | 生产级 Rust 模块 |
| **`canopus-ui-core`** | `sdk/rust/canopus-ui-core` | 零分配声明式 UI 核心。提供 `View`、`Element`、`Layout`、`Style` 与路由容器，将组件树无损序列化至 Supervisor UI 渲染管线。 | 具备界面的交互模块 |
| **`canopus-target-generated`** | `sdk/rust/canopus-target-generated` | 固件原始符号绑定。由 `canopus target generate` 根据各固件 IDB 逆向提取出的 Thumb 绝对调用地址（`CANOPUS_FW_*_CALLABLE`）及 packed 内存布局。 | 底层驱动与门面内部 |
| **`canopus-target-private`** | `sdk/rust/canopus-target-private` | 目标设备私有门面（Facade）。封装蓝牙 A2DP/AVCTP 握手、底层驱动注册、文件 IO。所有函数名在所有设备间保持对齐，未适配者返回 `ERR_UNSUPPORTED`（-38）。 | Full-Trust 系统扩展模块 |
| **`canopus-host-fake`** | `sdk/rust/canopus-host-fake` | 宿主单元测试桩。在 x86_64 / aarch64 上仿真手环定时器轮、驱动表、内存分配与生命周期事件，无需依赖真实固件即可运行 `cargo test`。 | 模块单元与集成测试 |

---

## 2. 目标设备型号与固件对应清单

SDK 严格标明具体的**设备型号**与**固件构建版本**，所有固件绑定文件均以目标设备型号全称命名，坚决杜绝无意义的四位数字缩写：

| 设备型号 (Device Model) | 固件版本 | 内部架构 | 自动生成底层绑定 (`canopus-target-generated`) | 私有门面实现 (`canopus-target-private`) |
|---|---|---|---|---|
| **Xiaomi Band 10 Pro** | `3.101.036` | Cortex-M33 (NuttX) | `generated_xiaomi_band_10_pro_3_101_036.rs` | `targets/xiaomi_band_10_pro_3_101_036.rs` |
| **Xiaomi Band 10 Pro** | `3.101.043` | Cortex-M33 (NuttX) | `generated_xiaomi_band_10_pro_3_101_043.rs` | `targets/xiaomi_band_10_pro_3_101_043.rs` |
| **Xiaomi Band 11** | `4.100.139` | Cortex-M33 (Vela) | `generated_xiaomi_band_11_4_100_139.rs` | `targets/xiaomi_band_11_4_100_139.rs` |
| **Xiaomi Band 11** | `4.100.155` | Cortex-M33 (Vela) | `generated_xiaomi_band_11_4_100_155.rs` | `targets/xiaomi_band_11_4_100_155.rs` |

---

## 3. 开发指引

### 3.1 便携式模块 (Portable Module, 推荐)
便携式模块不绑定任何具体固件地址，构建单一 ELF 即可在所有支持 Canopus 的设备上直接运行：

```toml
[dependencies]
canopus-abi = { path = "path/to/sdk/rust/canopus-abi" }
canopus-runtime = { path = "path/to/sdk/rust/canopus-runtime" }
canopus-ui-core = { path = "path/to/sdk/rust/canopus-ui-core" }

[dev-dependencies]
canopus-host-fake = { path = "path/to/sdk/rust/canopus-host-fake" }
```

### 3.2 目标特定模块 (Target-Bound Module)
当模块必须使用手环底层的私有硬件能力（如修改经典蓝牙 SDP 广播、拦截 A2DP 音频流）时，引入目标私有门面，并开启对应的 target feature：

```toml
[dependencies]
canopus-abi = { path = "path/to/sdk/rust/canopus-abi" }
canopus-runtime = { path = "path/to/sdk/rust/canopus-runtime" }
canopus-target-private = { path = "path/to/sdk/rust/canopus-target-private", optional = true }

[features]
device = ["canopus-target-private/target-xiaomi-band-10-pro-3-101-043"]
```

在代码中必须先通过身份守护守卫，再调用私有门面接口：
```rust
use canopus_target_private as target;

pub fn init() -> Result<(), i32> {
    // 固件完整性与 SHA-256 守护校验
    unsafe { target::canopus_identity_guard() };
    
    // 安全调用门面能力（未支持的设备返回 ERR_UNSUPPORTED）
    let state = unsafe { target::bt_adapter_get_state(target::bt_adapter_get_instance()) };
    Ok(())
}
```

---

## 4. 常用构建与验证命令

- **创建新 Rust 模块工程**：
  ```bash
  canopus new my-rust-mod --lang rust --target xiaomi-band-10-pro-3.101.043
  ```
- **编译模块（自动生成 C ctor 桥接并执行 relocatable link）**：
  ```bash
  canopus build
  ```
- **校验 ELF 合规性（零未解析符号、合法重定位与地址边界校验）**：
  ```bash
  canopus verify build/xiaomi-band-10-pro-3.101.043/my_rust_mod.elf --target xiaomi-band-10-pro-3.101.043
  ```
- **重新生成所有设备固件符号绑定**：
  ```bash
  canopus target generate --all
  ```
- **检查生成物是否过期**：
  ```bash
  canopus target generate --all --check
  ```
