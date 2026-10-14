# ADR-CAN-008：WearPacket Layer 2 通信通道

状态：已采纳（2026-10-14）

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
   - 报文头：`[Channel: 0x43] [OpCode: 0x01/0x02] [Payload: CPC2 Envelope]`。
   - 加密模式：默认采用 `OpCode = 0x02 (WriteEnc)`，复用配对协商的 AES-128-CTR 密钥。

2. **单一线格式**：
   - 沿用 ADR-CAN-004 决定的 CPC2 信封（`abi/schema/transport-v2.json`），36 字节 Header + 业务载荷。
   - 统一由 `canopus_supervisor_handle_l2_packet`（Supervisor）与 `canopus_transport_v2_decode_request` / `canopus_transport_v2_encode_response` 编解码。

3. **宿主与设备两端职责**：
   - **手机/AstroBox 端**：在 `layer2.rs` 注册 `L2Channel::Canopus`，通过 `XiaomiSystemExt` 监听与收发 L2Packet，向上层暴露异步 CPC2 RPC 接口。
   - **手环端（Supervisor）**：拦截 L2 RX 数据流，遇到 `channel == 0x43` 时直接喂给 Supervisor 的 L2 处理器，响应报文通过原厂 `miwear_transport_send`（043 为 `sub_C7D719C`）送回。

## 影响

- 彻底解除对快应用轮询 SCAN 的唯一依赖，实现 AstroBox 与手环端 Canopus 模块/Supervisor 的实时双向 RPC。
- 零额外物理连接开销，不侵入系统 Protobuf（`WearPacket.proto`）业务逻辑。
