# canopus-target-generated

本 Crate 包含由 `canopus target generate` 自动从目标固件符号表提取生成的原始 Thumb 调用地址常量（`CANOPUS_FW_*_CALLABLE`）及对应固件的数据结构布局定义。

## 文件与支持设备对照表

每个源文件对应一个具体的设备型号和固件版本，绝不使用含混的简写：

- `generated_xiaomi_band_10_pro_3_101_036.rs`: **Xiaomi Band 10 Pro**（固件 `3.101.036`）
- `generated_xiaomi_band_10_pro_3_101_043.rs`: **Xiaomi Band 10 Pro**（固件 `3.101.043`）
- `generated_xiaomi_band_11_4_100_139.rs`: **Xiaomi Band 11**（固件 `4.100.139`）
- `generated_xiaomi_band_11_4_100_155.rs`: **Xiaomi Band 11**（固件 `4.100.155`）
- `generated_xiaomi_band_11_4_100_177.rs`: **Xiaomi Band 11**（固件 `4.100.177`）

## 使用规则

编译时必须通过 Cargo feature 且恰好选中一个设备目标，例如：
```toml
[dependencies.canopus-target-generated]
path = "path/to/canopus-target-generated"
features = ["target-xiaomi-band-10-pro-3-101-043"]
```
所有生成的固件调用均为 `unsafe`，调用前必须先执行 `canopus_identity_guard` 校验。
