# no-heap-counter: Canopus Rust 模块示例

本工程展示了如何使用纯 Rust 编写一个无堆分配（`no_std` / no-heap）、支持热加载与卸载的计数器模块。

## 目录结构与双 Crate 设计

Rust 裸机模块在开发时采用“宿主测试 + 目标构建”双 Crate 结构：

- **`no-heap-counter` (当前目录, `rlib`)**:
  - 宿主单元测试工程。依赖 `canopus-abi`、`canopus-runtime` 与 `canopus-host-fake`。
  - 直接在开发者电脑（macOS / Linux）上执行 `cargo test`，无需连接设备或模拟器即可验证生命周期状态转移、回调保护策略与计数逻辑。
- **`no-heap-counter-device` (设备端 staticlib)**:
  - 目标构建入口。编译为面向 ARM Cortex-M33 的 `staticlib`（`thumbv8m.main-none-eabi`）。
  - 提供 `#[panic_handler]` 裸机崩溃处理，并引入设备端符号绑定。

## 构建流水线 (`build.sh`)

运行 `./build.sh` 执行以下步骤：
1. **Cargo 裸机编译**: `cargo build --profile device --target thumbv8m.main-none-eabi -p no-heap-counter-device --features device`
2. **编译 C 构造函数垫片**: 编译 `c_shim/canopus_ctor.c`，生成 `.init_array` 入口。
3. **可重定位链接**: 使用 `ld.lld -r` 将静态库与 C 垫片合并为单个 ELF32 (`build/no_heap_counter.elf`)。
4. **ELF 验证器检查**: `canopus verify build/no_heap_counter.elf --target xiaomi-band-10-pro-3.101.036`，确保零未解析符号与重定位合法性。
