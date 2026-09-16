# Xiaomi Band 11 4.100.155

独立 exact target；不覆盖 `.139`。固件身份：

- SHA-256：`ea0bdf1920cb30223d616432af00565ca67622e6468328f5eab155f8cdc2fb9f`
- build：`user-4.100.155-cn-202609041500`
- version token：`0x0ca0a8a6`；build token：`0x0ca0a91e`，均 LF 终止。
- exact IDB 输入 SHA 与固件一致；35,193 个函数、16,127 个 data objects。

以修复通知上下文后的 `.139` 为基线，使用完整 Thumb 指令指纹、调用与数据引用
对应关系定位候选，并在 `.155` IDB 独立核查 Lua 入口、app/page/launcher、
register_driver、通知插入/复制/消费者、缓存 leaf 和 Bluetooth 关键回调。
没有套用统一地址偏移。102 个符号保留 STATIC_RECOVERED，52 个保留 CANDIDATE，
1 个 FORBIDDEN；原基线的 REJECTED 保持拒绝。全部公共 callable approval 未开放。

`.155` 通知消费者的 `message+92 → user_data+13` 无条件读取与 `.139` 一致，
因此使用同一份修复后的普通通知上下文实现。C native 地址从本 target 的
symbol records 与 `native-bindings.json` 生成，Rust 地址来自 `generated_1155.rs`。
仅共享不含固件地址的 C/Rust 语义实现。Lua 的 native profile 也绑定本版本。

```sh
cargo build -p canopus-cli
target/debug/canopus target generate-veneer xiaomi-band-11-4.100.155 --targets-dir targets
target/debug/canopus target generate-rust-bindings xiaomi-band-11-4.100.155 \
  --targets-dir targets --output sdk/rust/canopus-target-generated/src/generated_1155.rs
CANOPUS_TARGET=xiaomi-band-11-4.100.155 scripts/build_canopus_supervisor.sh
build/band11-tests/bin/python scripts/tests/band11_155_firmware.py
```

打包目录：`watchfaces/canopus-installer-prod/xiaomi-band-11/`。
139 和 155 共用一个 `main.lua` 与 Manager 图标，各自携带 loader profile、stage1、stage2
和 Supervisor，共九个 `.bin`。入口在恢复执行能力前按 version + build 选择准确 profile。
运行 `python3 scripts/build_band11_installer.py` 重建两版并生成统一的
`build/canopus-installer-prod-xiaomi-band-11.zip`。此 ZIP 是资源集合，按现有方式封装表盘。

验证通过：ARM ELF verifier（80 sections、0 undefined、1134 relocs）、两种
重定位基址的 stage1→stage2→Supervisor、错误身份拒绝、Manager 注册及真实
原生 row/theme 指令、通知插入/消费者和旧空指针故障复现。另以 Bluetooth Audio 与 Lyra Player 的真实 `.155` 签名产物验证了收据验签、
提交后通知、篡改与重复拒绝，以及 Supervisor 的重定位、构造函数、描述符注册和
低 Kmem 余量下的 Umem 加载；固件 RF 服务仍由模型代替，不能据此宣称耳机连接或播放通过。

```sh
CANOPUS_TEST_TARGET=xiaomi-band-11-4.100.155 \
CANOPUS_MODULE_DIR=../Canopus-Module-BluetoothAudio/build/bluetooth-audio-prod/xiaomi-band-11-4.100.155 \
  build/band11-tests/bin/python scripts/tests/band11_module_load.py
```

Lyra 同项测试使用 `../Canopus-App-LyraPlayer/build/lyra-player-prod/xiaomi-band-11-4.100.155`。
两个模块的 `scripts/build-install-watchface-prod.sh xiaomi-band-11` 均将 139/155 打入
各自同一个 `watchfaces/*-prod/xiaomi-band-11/`，按固件 version + build 选择 ELF 和收据。

物理设备验证仍 pending；没有把 `.139` 的设备结果移植为 `.155` 的设备证明。
首次测试重启，注册 Manager 后检查通知、关闭通知、通知列表与模块启停恢复。
详细对应关系见 [port audit](../evidence/fw-match/port-139-to-155.json) 和
[证据](../evidence/EVID-PORT-4155-001.json)。
