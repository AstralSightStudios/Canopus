# Xiaomi Band 11 4.100.179

独立 exact target；不覆盖 `.139` / `.155`。固件身份：

- SHA-256：`307adabe0ff61c77be73d045a8260e279cddd131c7a535a2678eb34199278a2b`
- build：`user-4.100.179-cn-202609302100`
- version token：`0x0ca0a7be`；build token：`0x0ca0a836`，均 LF 终止。
- exact IDB 输入 SHA 与固件一致；35,146 个函数、16,123 个 data objects。

## 怎么移植的

以已审核的 `.155` 为基线（不是 `.139`），不套用统一地址偏移：

1. `tools/fw-match/ensemble_match.py`：26,757 个互相唯一的完整函数体做种子，245 个函数符号命中 242 个，19 个全局命中 19 个。
2. `tools/fw-match/port_review.py`：在两个 IDB 里反编译配对函数，剥掉自动命名、地址和局部变量序号后比较伪代码。
3. `tools/fw-match/port_pack.py`：逐条验收。一条函数匹配只有同时满足下列条件才进 `STATIC_RECOVERED`：
   函数体指纹相等或伪代码相同；所有有唯一锚点的 callee 都映射到对应 callee；
   且它本身是互相唯一的锚点、或 ≥5 个有锚点的 caller 中 ≥90% 调用目标、或前后锚点保持相同的函数间距。
   其余的落到人工复核（`evidence/fw-match/manual-overrides.json`）或保持 `CANDIDATE`。
4. `tools/fw-match/map_data.py`：数据对象不看匹配器，改由访问它的 (函数, 指令偏移) 对独立重新推导；
   与匹配器给出的全部全局一致（kmem `0x200b0070`、umem `0x200b2330`、MPU 位图 `0x200f4080` 等）。

人工纠正一处：匹配器把 `font_reload_metrics_destroy` 放到 `0xc326e0e`（众多相同空函数之一），
按它在源镜像里紧挨 `font_reload_metrics_compare` 的位置改为 `0xc39fcfc`。
`lv_obj_area_is_visible` 的函数体与 `.155` 不同（寄存器分配变了，222 → 218 字节），保持 `CANDIDATE`。

## 信任

**没有任何批准、真机证明或能力状态从 `.155` 带过来。** 全部 270 条记录为 `restricted` 且
`approval_state` 为 `PENDING`（源里被拒绝的仍是 `REJECTED`，`insmod` 仍是 `FORBIDDEN`），
`proof.device` 为 `not_probed`。`.155` 上维护者真机验证过的 UI 宿主入口、`errno_location` 的 managed 批准、
calendar 批准清单，在 `.179` 上都要重新取证并由用户授权，`generated_stability.rs` 的
`band11_179_carries_no_approval_from_155` 钉住这一点。

## 启动与加载

`startup copy loops 0xc0c01a0..0xc0c02cc` 与 `.155` 字节相同；初始化 DATA 的 LMA 由 `0x2cc4be54` 变为 `0x2cc4919c`，
DATA 为 `[0x2007dcc8,0x200add84)`，BSS 为 `[0x200add84,0x201019a4)`。19 个加载器指纹位置按函数内偏移平移，
字内容与 `.155` 逐字相同。Lua 恢复用的 `pmain 0xc6d178c`、`pmain_error_handler 0xc6cd0ac`、
`os.execute 0xc716140`、`luaopen_os 0xc71281c`、`system 0xc370d44`、`io.open 0xc715d68` 都是互相唯一的锚点，
大小相同，callee 一致。

```sh
cargo build -p canopus-cli
target/debug/canopus target generate xiaomi-band-11-4.100.179 --targets-dir targets
CANOPUS_TARGET=xiaomi-band-11-4.100.179 scripts/build_canopus_supervisor.sh
build/band11-tests/bin/python scripts/tests/band11_179_firmware.py
python3 scripts/emu/band11_smoke.py 179
```

打包目录：`watchfaces/canopus-installer-prod/xiaomi-band-11/`。139、155、179 共用一个 `main.lua`
与 Manager 图标，各自携带 loader profile、stage1、stage2 和 Supervisor。入口在恢复执行能力前按
version + build 选择准确 profile。

## 验证

Unicorn 里用真实 `.179` 镜像执行：stage1 → stage2 → Supervisor 在两种重定位基址上启动、错误身份被拒、
Manager 注册、原生 row/theme 指令、通知插入和消费者（含旧空指针故障复现）。L2 后端地址见
`evidence/l2-sram-probe.json`。均为 host / emulator 级证据。

物理设备验证仍 pending（`tests/hardware/gates.md` G21）。详细对应关系见
[port audit](../evidence/fw-match/port-155-to-179.json) 和 [证据](../evidence/EVID-PORT-4179-001.json)。
