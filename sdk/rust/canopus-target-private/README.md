# canopus-target-private

本 Crate 提供面向各个具体设备固件的高级私有 Full-Trust 门面（Facade）。

为模块开发者封装了经过安全审核与语义验证的私有 API（例如经典蓝牙 A2DP 音频源注册、AVCTP 控制通道握手、底层文件系统与字符设备调用）。

## 架构与对齐保证

1. **统一的函数签名集**：
   所有目标后端必须暴露完全相同的一组公开函数。如果某个设备固件在硬件或软件层面不具备该能力，该平台将显式返回 `ERR_UNSUPPORTED`（`-38`），严禁静默忽略。此规则由 `tests/api_parity.rs` 在 CI 中进行静态强制对齐检查。

2. **支持设备与目标实现映射**：
   - `targets/xiaomi_band_10_pro_3_101_036.rs`: **Xiaomi Band 10 Pro**（固件 `3.101.036`）
   - `targets/xiaomi_band_10_pro_3_101_043.rs`: **Xiaomi Band 10 Pro**（固件 `3.101.043`）
   - `targets/xiaomi_band_11_4_100_139.rs`: **Xiaomi Band 11**（固件 `4.100.139`）
   - `targets/xiaomi_band_11_4_100_155.rs`: **Xiaomi Band 11**（固件 `4.100.155`）
   - `targets/xiaomi_band_11_4_100_179.rs`: **Xiaomi Band 11**（固件 `4.100.179`）

3. **生成的符号元数据**：
   `generated_symbols_xiaomi_band_10_pro_3_101_036.rs` 和 `generated_symbols_xiaomi_band_10_pro_3_101_043.rs` 由代码生成器输出，包含固件 SHA-256 及符号元数据表。
