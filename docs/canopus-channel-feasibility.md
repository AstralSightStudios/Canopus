# Canopus 专用通信通道（WearPacket Layer 2 Channel）设计方案

**状态**：已采纳并实施（2026-10-14，参见 ADR-CAN-008）。

## 1. 背景与澄清

早期探索（commit `bec2415` 之前）曾误将“L2 通道”理解为蓝牙底层链路控制协议 **L2CAP**，并基于 Android/iOS 缺少经典蓝牙 L2CAP 公开 API 得出“手机端不可行，须退回到 RFCOMM/SPP 甚至快应用容器”的推论。

**该推论前提完全错误。**
Canopus 真正的通信通道目标是**小米穿戴私有传输层（MiWear Transport）的 Layer 2（`L2Packet`）**——即与承载 Protobuf 的 `L2Channel::Pb` 处于完全同级的协议 Channel。

---

## 2. MiWear 协议分层模型

小米手环与手机（AstroBox / 官方 App）之间的通信架构如下：

```text
+--------------------------------------------------------------------------+
|  Link Layer: BLE GATT / Classic Bluetooth SPP (RFCOMM)                   |
+--------------------------------------------------------------------------+
|  Layer 0 / 1: SAR V2 (miwear_sar)                                        |
|  负责分片(fragmentation)、滑动窗口、ACK/NAK、重传保序、MTU 协商          |
+--------------------------------------------------------------------------+
|  Layer 2: L2Packet (miwear_transport)                                    |
|  [Channel: 1B] [OpCode: 1B] [Payload: NB]                                |
|                                                                          |
|  OpCode:                                                                 |
|    1 = Write     (明文传输)                                              |
|    2 = WriteEnc  (AES-128-CTR 会话加密)                                  |
|    3 = Read                                                              |
|                                                                          |
|  Channel 分配:                                                           |
|    1: Pb          --> 承载 Protobuf WearPacket (原厂系统业务)            |
|    2: Mass        --> 大文件分块传输                                     |
|    3: MassVoice   --> (固件内大多未激活/drop)                            |
|    4: FileSensor  --> (固件内大多未激活/drop)                            |
|    5: FileFitness --> (固件内大多未激活/drop)                            |
|    6: Ota         --> 固件升级传输                                       |
|    7: Network     --> 原生网络代理通道                                   |
|    8: Lyra        --> 语音/音频通道                                      |
|    9: Research    --> 调试研究                                           |
|   10: MultiModal  --> 多模态传输                                         |
|   ------------------------------------------------------------------     |
|   0x43 ('C'): Canopus --> Canopus 专属通道 (ADR-CAN-008)                 |
+--------------------------------------------------------------------------+
|  Layer 3: 业务层载荷 (Canopus CPC2 统一线格式信封)                      |
|  [36 字节 Header] + [Command / Query Payload]                            |
+--------------------------------------------------------------------------+
```

---

## 3. 核心优势

1. **零平台限制与免新连接**：
   - 完全复用已建立的 BLE GATT 或 SPP 物理连接；
   - 宿主操作系统（Android / iOS / macOS / Windows / Linux）无任何感知，无需申请额外蓝牙权限，更不需要使用非公开的系统 API。
2. **免除自行实现加密握手**：
   - 使用 `OpCode = 2 (WriteEnc)` 时，固件与 AstroBox 会直接使用配对时派生的 AES-128-CTR 会话密钥对载荷进行端到端加密与解密。
   - Canopus 无需在应用层重复做复杂的 Diffie-Hellman 握手或密码协商。
3. **免除自行实现分包与重传**：
   - 底层 L1 SAR V2 会自动将大包拆解为适合 BLE MTU 的分片，并在对端重组；
   - 支持丢包检测、NAK 重传和滑动窗口流量控制。
4. **与原厂业务完全解耦**：
   - 不修改 `WearPacket.proto`，不占用原厂 Protobuf 消息 ID；
   - 原厂系统的 Auth、Bind、Weather、Notification 等业务完全不受影响。

---

## 4. 固件端接入与分发机制

通过静态分析与多版本固件交叉核对（Band 10 Pro 036/043、Band 11 139/155）：

### 4.1 L2 接收分发函数
- **函数符号**：`sub_C7D6E6C` (043) / `sub_C7BE110` (036) / `sub_C98F148` (155) / `sub_C98F158` (139)。
- **调用约定**： Thumb-2 `void l2_rx(session_handle, channel, opcode, payload_ptr, len)`。
- **原厂分支**：
  原厂代码在解密后有一段跳转表逻辑：
  ```arm
  subs    r5, #1
  cmp     r5, #7
  bhi     drop_packet
  adr     r3, jump_table
  ldr.w   pc, [r3, r5, lsl #2]
  ```
  如果 `channel > 8`（如 `0x43`），原厂逻辑直接释放并丢弃。
- **Canopus 拦截方案**：
  在 L2 RX 函数入口或其在 SAR 注册的回调表（存储在 RAM 连接结构体中）设置钩子：
  - 若 `channel == 0x43 (CANOPUS_L2_CHANNEL)`：
    - 取出 Payload（已由原厂或钩子解密的 CPC2 字节流）；
    - 递交给 `canopus_supervisor_handle_l2_packet` 执行；
    - 将生成的 CPC2 响应通过回包函数发回，并直接退出（吞掉该包，不向下传递）。
  - 若 `channel != 0x43`：原样转发给原厂 L2 RX 分发函数。

### 4.2 L2 发送函数
- **函数符号**：`miwear_transport_send`（043 中为 `sub_C7D719C`）。
- **参数**：`(conn, channel, opcode, payload, len)`。
- **优先级行为**：原厂实现中，若 `channel > 10`，其优先级查表逻辑自动 fallback 到 `priority = 0`，不会发生越界或报错。固件会自动完成 CTR 加密、L2 报文头封装并塞入 L1 SAR 的发送队列。

---

## 5. 主机端（AstroBox-NG）集成

1. **数据包定义** (`modules/core/src/device/xiaomi/packet/v2/layer2.rs`)：
   - `L2Channel::Canopus = 0x43`
   - 构造器：`L2Packet::canopus_write(payload)` 与 `L2Packet::canopus_write_enc(payload, cipher)`
2. **系统分发** (`modules/core/src/device/canopus.rs`)：
   - 实现 `XiaomiSystemExt` 监听 `L2Channel::Canopus` 数据；
   - 收到后解码 CPC2 Response 信封，根据 `request_id` 匹配对应的请求并完成异步回调。
