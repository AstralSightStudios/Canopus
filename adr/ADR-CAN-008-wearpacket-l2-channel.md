# ADR-CAN-008：WearPacket Layer 2 通信通道

状态：设计已采纳（2026-10-14）；2026-10-17 原生 host 实现完成，Band 11 `.155` 实验后端已完成 Mirage 加密通信闭环；真机与生产批准仍未完成。

## 背景

Canopus 原计划在设备端与手机端之间建立一条直接受控的通信链路。早期探索（commit `bec2415`）曾误将通信层级定位在经典蓝牙底层链路协议 L2CAP 上，进而得出手机端操作系统（Android/iOS）受权限与公开 API 限制、无法走该路径的错误结论。

小米手环穿戴协议体系内部拥有完整的自研分层协议栈：
- **Layer 0 / 1**：SAR V2（分片与重组、可靠传输保序、滑动窗口）；
- **Layer 2**：`L2Packet`，承载由 1 字节 Channel 标识的多个业务通道；
- **Layer 3**：通道内部的数据载荷。

在 L2 层面，承载系统核心业务 Protobuf 的 `L2Channel::Pb` 只是 Channel 1。系统预留了通道编号空间，并具备原生的 AES-128-CTR 会话级加解密（`OpCode::WriteEnc`）。

## 决定

1. **采用 MiWear Layer 2 专用 Channel**：
   - 通道号：`CANOPUS_L2_CHANNEL = 0x43`（ASCII `'C'`，对应枚举 `L2Channel::Canopus`）。
   - 报文头：`[Channel: 0x43] [OpCode: 0x02] [Payload: CPC2 Envelope]`。
   - 加密模式：设备只接受 `OpCode = 0x02 (WriteEnc)`，复用配对协商的 AES-128-CTR 密钥。

2. **单一线格式**：
   - 沿用 ADR-CAN-004 决定的 CPC2 信封（`abi/schema/transport-v2.json`），36 字节 Header + 业务载荷。
   - 统一由 `canopus_supervisor_handle_l2_packet`（Supervisor）与 `canopus_transport_v2_decode_request` / `canopus_transport_v2_encode_response` 编解码。

3. **宿主与设备两端职责**：
   - **手机/AstroBox 端**：在 `layer2.rs` 注册 `L2Channel::Canopus`，通过 `XiaomiSystemExt` 监听与收发 L2Packet，向上层暴露异步 CPC2 RPC 接口。
   - **手环端（Supervisor）**：拦截 L2 RX 数据流，遇到 `channel == 0x43` 时校验加密 opcode、解密并复制进有界队列，由独立 UI owner 定时器调用 Supervisor 的 L2 处理器。响应再次入队，由 Bluetooth owner 的独立定时器调用原厂 `miwear_transport_send`（043 为 `sub_C7D719C`）送回；不能从 UI 任务直接写 Bluetooth 任务持有的 fd。

## 影响

- 彻底移除快应用容器、快应用列表与目录依赖，实现 AstroBox 与手环端 Canopus 模块/Supervisor 的实时双向 RPC。
- 零额外物理连接开销，不侵入系统 Protobuf（`WearPacket.proto`）业务逻辑。

## 后续

模块与 AstroBox 插件之间的消息通道（CanopusConnect）复用本通道，见 ADR-CAN-009。

## 原生包投递（2026-10-17）

CPC2 每帧载荷上限保持 512 字节。数字均为 little-endian：

| opcode | 请求载荷 | 成功语义 |
|---|---|---|
| 15 STAGE_BEGIN | 176 字节 CMI2 回执 | 校验 target/固件绑定，开启 `.ko.upload` 临时文件；替换未完成的旧上传 |
| 16 STAGE_WRITE | u32 offset + 1..508 字节 ELF | 必须等于当前写入位置，不得超过回执长度；每块应答后再发送下一块 |
| 17 STAGE_FINISH | 空 | 长度完整，生成 next 暂存对并验证 artifact SHA-256；不安装、不加载 |
| 18 STAGE_ABORT | 空 | 同时关闭并删除未完成的 ELF 与资源 `.upload`，清除资源上传上下文 |
| 26 STAGE_RES_BEGIN | u8 name_len + name + u32 size + 32 字节 SHA-256 | ELF 收尾校验成功后，开启 `/data/canopus/m.<id>/<name>.upload`；name 为最多 63 字节、不以点开头且只含 ASCII 字母、数字、`.`、`_`、`-` 的平铺文件名，拒绝已有目标或临时文件冲突 |
| 27 STAGE_RES_WRITE | u32 offset + 1..508 字节资源 | offset 必须等于当前写入位置且不得超过 size；错误时清理本流临时文件 |
| 28 STAGE_RES_FINISH | 空 | 校验完整长度和暂存文件 SHA-256，改名为 `/data/canopus/m.<id>/<name>`；校验或 IO 失败时删除本流临时文件 |
| 2 INSTALL | module_id + NUL | 按已有 A/B 状态机提交；COMPLETED=5 或 REBOOT_REQUIRED=8 |

资源帧在 ELF 的 17 收尾成功后逐个发送，例如 `[15,16,16,16,17,26,27,27,28]`。
发送端完成 common/target 分层选择；设备按 CMI2 回执的 module_id 创建数据目录并平铺释放文件，
不保留包内 `resources/common/` 或 `resources/<target>/` 层级，不追加扩展名。
因此 Corona 的资源路径为 `/data/canopus/m.corona/<name>.bin`（当发送的文件名为 `<name>.bin`）。
新的有效 15 或 18 均取消未完成的两流并清除旧资源上下文；已完成的资源不受取消影响。

QUERY_DEVICE 的 CQD1 载荷由 36 扩展至 84 字节：原字段不变，+36 是 48 字节零填充 target_id。
旧 Supervisor/安装器须重新部署；CPC2 信封仍为 36 字节，ABI 为 1.2。
2026-10-24 再扩展到 88 字节：+84 是 u32 `request_slots`，L2 传输同时能收下的请求数（0 = 未知，按 4 处理）。
读取方接受 84 字节及以上（AstroBox 早已如此），84 字节的旧记录就是没有这个字段；手机最多让 `request_slots − 1`
条在途，留一条给管理器。
响应值以 `sdk/c/canopus_abi.h` 为准：REJECTED=1、ACCEPTED=2、QUEUED=3、RUNNING=4、
COMPLETED=5、FAILED=6、DISALLOWED=7、REBOOT_REQUIRED=8。

## SRAM 接入与批准边界

- 不使用 Cortex-M33 不支持的 Flash patch/remap。只替换 SAR 连接对象 +12 的 RAM RX 指针，
  保留其他通道的原厂回调。连接链表节点 +12 是连接对象指针，不是 RX 回调。
- 禁止“先调用原回调，不管解密是否成功就解析 payload”。Canopus 通道直接调用原厂 crypto，
  只有成功时才入队。明文控制请求不受理。
- 每个请求保留 session 身份，断连后旧请求不投递给新 session；不在 Bluetooth worker 执行模块/UI 回调。
- 原厂 send 会原地加密，因此传入独立可写缓冲；SAR submit 在返回前复制数据，不持有调用方栈地址。
  原生发送返回负数时不能释放响应/事件槽：保留队列中的明文，下个 BT tick 重试；栈上的工作副本承受原地加密，
  避免失败后重试把密文再加密。会话已变化的旧帧仍丢弃，不跨会话重发。ARM 模型回归覆盖失败保留、原文重试和队列内顺序；
  此修复不等于已确认真机“新 Supervisor 下曲库/导入无响应”的完整根因。
- .043 / .155 的 exact-target 静态证据在各自 `evidence/l2-sram-probe.json`。
  普通构建自动包含已实现的后端，不要求启用参数；未实现后端的 target 会在构建时明确提示，
  运行时返回 UNSUPPORTED，不套用其他固件的地址。
  所有相关符号仍是 restricted / PENDING，未真机探测；构建默认值不等于生产批准。

## 发送失败与队列寿命（2026-10-24，host/static 证据）

`l2_drain` 仅在原厂发送返回 0 或记录的会话已经失效时释放队头。会话相同而
发送返回非零时，保留槽、读指针与明文，下一个 Bluetooth tick 再试。一次失败尝试
也用掉当前 tick 的发送轮次，因此响应与事件仍按原规则轮流，既不忙循环，也不在
同一 tick 重试两遍。保留现有 RX 深度、owner batch 和 2/10 ms 周期。

原厂函数的返回值不是 POSIX `write` 的字节数；不能把正数解释成已发字节。
三个精确镜像的加密 SAR 分支均为：

| target | transport send | SAR submit | 成功 / 失败 |
|---|---|---|---|
| `.043` | `0x0c7d719c` | `0x0c7d639c` | 0 / -1 |
| `.155` | `0x0c98f478` | `0x0c98e6b8` | 0 / -1 |
| `.179` | `0x0c98d7f0` | `0x0c98ca30` | 0 / -1 |

已按各 target 的 `evidence/l2-sram-probe.json` SHA-256 核对镜像。SAR 在长度、连接状态、
积压或分配失败时返回 -1，不入队；成功时先复制 payload 到自有分配并链接 TX 队列，
再返回 0。transport 将 SAR 的返回值传回；加密临时分配或 crypto 失败也返回 -1。
成功只表示本地接管，不能证明手机收到。此结论限定为当前后端使用的加密 SAR 路径，
不把其他原厂 transport 模式的返回路径解释成同样的交付保证。

加密成功后，transport 会在调用 SAR 前把密文复制回输入 payload，所以即使返回 -1，
输入也可能已经变化。`l2_send` 现在为每次尝试建立最多 548 字节的栈副本，队列一直
保存明文；成功同步复制之后副本可释放，失败重试不会二次加密旧密文。没有新增常驻
队列或每帧堆分配；`clang --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
-Oz -ffreestanding -fno-builtin -fstack-usage` 编译真实 shim（tests/l2 配置）时，
`l2_drain` 栈帧为 608 字节，原版为 56 字节，增量 552 字节。真机 Bluetooth 栈余量仍待测量。

原厂 -1 不区分暂时拒绝与永久拒绝，因而当前会话仍有效时保留并重试所有非零结果，
不猜测 errno、不按次数静默丢弃。如果会话始终不变且始终失败，该队列会一直背压，
直到原厂接受或会话失效。断开（session 为 NULL）或替换会话时丢弃旧帧，避免把旧响应
送给新会话；仍沿用已有的 session 指针身份约定。

回归：`scripts/tests/test_l2_backend.py` 编译真实 Cortex-M33 shim 后在 Unicorn 执行，
覆盖连续失败后恢复、明文保持、事件队列 BUSY、RPC 响应满时不重复执行 callback、
响应/事件轮次，以及失败后断连/会话替换。修复前新用例返回 102，修复后通过。
`scripts/tests/test_l2_send_firmware.py` 另外执行三个 SHA 匹配的真实 transport 与 SAR 函数体，
覆盖 548 字节成功复制、SAR 分配拒绝、连接失活、加密临时分配失败和 crypto 失败；
分配、AES 与下游唤醒为模型，不是无线或物理设备测试。缺固件/Unicorn 时显式跳过。
相关符号、批准和设备状态保持不变。

验证命令（Python 环境需已有 Unicorn，固件通过 `CANOPUS_FW_DIR` 定位）：

```sh
python3 scripts/tests/test_l2_backend.py -v
python3 scripts/tests/test_l2_send_firmware.py -v
for target in xiaomi-band-10-pro-3.101.043 xiaomi-band-11-4.100.155 xiaomi-band-11-4.100.179; do
    CANOPUS_TARGET="$target" sh scripts/build_canopus_supervisor.sh || exit 1
done
```

本次上述两组各 3 项测试全部通过（固件测试包含 15 个成功/故障子场景）；三目标构建与
ELF verifier 均通过，0 undefined。文件 / resident 字节分别为 `.043` 125128 / 163430、
`.155` 131232 / 182470、`.179` 131404 / 182606，未放宽预算。
另执行 `CARGO_NET_OFFLINE=true CANOPUS_CI_KEEP_GOING=1 sh scripts/ci.sh`：6/9 gates 通过；
gate 4/5 分别因既有 native-demo/cmod 的 unused-but-set-global 在当前 LLVM 上触发 Werror，
gate 7 因安装资源目录额外的 `未命名表盘.bin` 及 timer-pump 测试漏链接
`canopus_ui_host_poll` 失败。本补丁未改这些独立路径，也未把全 CI 记为通过。

## 初次联调记录（2026-10-17，后续修正见下文）

043 / 155 的真实 Mirage 蓝牙查询和安装仍超时。043 实时 RAM 检查确认新连接的 RX
仍是原厂回调，LVGL heartbeat 不持续推进；155 bootstrap 和本地 CPC2 正常。
因此该实验后端尚不能满足持续监听要求。不得用 host 或 ARM shim 模型测试冒充蓝牙端到端通过。
持续 owner 事件源仍需独立 ABI/上下文审核和授权，不能启用 FORBIDDEN 的工作队列或线程入口来绕过。

### 事件源根因与修正（同日，仅 host/emulator 证据，不改变批准状态）

- 原先“Mirage LVGL owner 死锁”的判断已被实际任务上下文和唤醒对照否定。043 的 PID49 `miwear` 在正常的带超时 `epoll_pwait` 中等待，LVGL guard=0；旧栈扫描不能证明刷新/DMA 阻塞。
- 实际刷新入口是 `sub_C4B8388` → `sub_C587CA8` → ROM `0x1C05FE20`，不是未执行的 AP 副本 `0x0C16D260`。原厂 `sub_C4B8450(0,...)` 在熄屏时主动停止 `0x2010C0B8` 的 libuv refresh timer（flags=8）。
- 不改固件或 Mirage，双击唤醒得到 Zinitix gesture=1、面板 SLPOUT/DISPON，refresh timer 恢复 active（flags=12）；250 ms timer 的 `last_run` 为 575324→575592 ms，8 ms timer 为 575520→575737 ms。p67 没有实体按键，之前 home/power 的刺激不构成唤醒对照；单击也不等同双击。
- 因此 LVGL timer 不满足持续监听要求。实验后端改为：LVGL 只引导一次，在原 UI owner 上初始化/启动一个独立的 resident libuv timer，随后删除引导 timer；BT 仍只解密/入队，不执行模块/UI 回调。
- .043 的 timer 存储为 136 字节（含 backtrace），.155 为 88 字节；`uv_timer_start` 使用 AAPCS 的两个 `uint64_t` timeout/repeat 参数。exact-target 地址、布局与上下文审核写在各自 `evidence/l2-sram-probe.json` 的 `owner_source`。init/start 只允许在现有 UI loop 上执行，不使用线程/工作队列入口。
- NSH 在熄屏后加载 Supervisor 时，须双击唤醒使引导回调至少运行一次；这不是“熄屏冷加载已解决”。ARM 模型回归和实验 ELF verifier 已通过，但新的真实蓝牙查询仍在排查，不能据此宣称端到端通过。
- 新私有符号保持 `restricted / STATIC_RECOVERED / PENDING`，未批准任何新能力，未作真机验证。当时默认构建仍为 UNSUPPORTED；该构建开关已由下文默认内置策略取代。

### 双 owner 通信闭环与 Band 11 接入（2026-10-17，仅 emulator 证据）

- 独立 UI 定时器只解决持续接收和命令处理。原先 UI 直接回复时，NuttX 的 task-group fd 隔离使原厂 send 对 Bluetooth 任务持有的 SPP fd 返回 `EBADF`。后端新增有界响应队列，由加密 RX 在 Bluetooth owner 上引导第二个独立原生定时器，执行会话复核、加密和原厂发送。UI 与 Bluetooth 不互相执行对方的文件/模块回调；没有新建线程或工作队列。
- Band 11 `.155` 使用 `BT_LOOP=0x200da540`，两个 resident timer 各为该固件的 88 字节布局；不套用 `.043` 的 136 字节布局。
- 用户打包目录里的旧 `.155` ELF 是默认无 L2 的构建。原样打包对照确认 bootstrap `result=0`，但 AstroBox 查询超时；反汇编的 `canopus_sup_l2_start` 直接返回 `UNSUPPORTED`。这不是“加载器没安装成功”。
- 普通构建命令 `CANOPUS_TARGET=xiaomi-band-11-4.100.155 sh scripts/build_canopus_supervisor.sh` 即包含该后端，并一起更新 Supervisor、CRC profile、stage1/stage2 与资源 ZIP。新 ELF 为 95196 字节，SHA-256 `6fe0e3f12744fae2ba384222eb3c380d50c346c47c87a50672d41b16bef98dd2`。
- `scripts/emu/band11_smoke.py 155 --keep-device` 在新建 Mirage 设备上通过：真实 AstroBox core 安装生产表盘 → 显式 Run → bootstrap `result=0` → 加密 `.canopus` 原生安装约 453 ms → 列表返回 `hello_portable`、slot0/state3/version1/activationError0 → 本地 SCAN `result=5/count=1`。安装后的模块处于已安装未启用状态，不能据此宣称模块已运行。
- `.043` 的独立原生安装已通过，但完整 fresh-device smoke 的 phone flow 尚未闭环；本次 Band 11 结果不替代它。`.139` 未获得本次 L2 后端支持。
- 全部相关符号和真机/生产批准状态保持不变，具体证据见 `.155` 的 `evidence/l2-sram-probe.json`。

### 默认内置后端（2026-10-17）

用户要求取消额外的通信启用参数。现由构建时生成的 exact-target 配置决定是否存在实现，
普通构建自动包含 `.043` / `.155` 的 L2 后端；生产构建、安装器和 smoke 使用同一默认路径。
移除环境变量与 C 侧的实验启用宏，不再保留一套默认无通信的同目标构建。
其他 target 未实现 L2 时明确提示，不借用别版地址，也不更改符号批准状态。

实际执行 `sh scripts/build_prod_all.sh`：5 个 target 构建与严格 ELF verifier 均通过。
`.043` 默认含 Manager UI 和通信后端的 ELF 为 86400 字节；`.155` 为 95196 字节，
哈希与上述已验证通信版本完全相同。默认选择回归和双 owner ARM 行为回归均通过。
