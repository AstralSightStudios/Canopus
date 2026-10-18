# ADR-CAN-009：CanopusConnect——模块与 AstroBox 插件的双向消息通道

状态：已实现（2026-10-18，设备端 host 测试 + AstroBox 单元测试）；未做 Mirage 或真机验证，符号与能力状态不变。

## 背景

ADR-CAN-008 的 L2 0x43 通道只承载 CPC2 管理 RPC：手机发请求、Supervisor 回响应。模块没有办法主动把数据交给手机，
手机侧的第三方（AstroBox 插件）也没有办法按模块名把数据交给某个模块。`ctx->bt->l2_register_rx` 只覆盖 0x40..0x4F
的模块通道，AstroBox 没有对应的栈，且 `l2_send` 当时还允许模块直接往 0x43 写原始帧，会破坏管理器的流。

## 决定

1. **仍走 L2 0x43，用自己的 CPC2 opcode**，管理器工作不受影响。ABI minor 保持 2：服务组靠 `group_version[CANOPUS_SVC_CONNECT]`
   与 `ctx->struct_size` 探测（`connect` 追加在 `canopus_context_v1` 末尾），线上新 opcode 在旧 Supervisor 上得到
   「未知命令 → REJECTED、无载荷」，AstroBox 据此报「Supervisor 不支持，请更新」。不升 minor 是为了不让所有重新编译的模块
   拒绝加载在旧 Supervisor 上。

   | opcode | 方向 | 载荷 | 说明 |
   |---|---|---|---|
   | 19 `CONNECT_SEND` | 手机 → 设备 | `kind u8 \| id_len u8 \| id \| data` | 交给该 id 模块的监听回调；响应载荷 1 字节状态 |
   | 20 `CONNECT_LISTEN` | 手机 → 设备 | `enable u8 \| id_len u8 \| id` | 开始/停止推送该模块的消息；响应载荷 1 字节状态 |
   | 21 `CONNECT_MESSAGE` | 设备 → 手机（EVENT，kind 3，request id 0） | `kind u8 \| id_len u8 \| id \| data` | 模块发出的消息 |

   `kind` 1 为 UTF-8 文本，2 为二进制；id 为 ADR-CAN-003 的模块 id（1..16 个 `[a-z0-9_]`）；一条消息一帧，data 最多 480 字节。
   响应：COMPLETED 表示已交付/已登记；DISALLOWED 带状态（1 无此模块、2 模块未监听、3 设备监听表已满）；REJECTED 带状态 4 表示格式错误。

2. **不干扰管理器**：
   - CONNECT 请求在 `canopus_supervisor_handle_l2_packet` 里先于管理器分发处理，不占 pending 槽位，不碰状态快照、`error_code`、`last_kind`。
   - 模块 → 手机的事件用独立的 2 槽发送队列，L2 后端的蓝牙定时器只在没有管理器响应排队时才发事件，所以话痨模块拖不慢也饿不死管理器响应。
   - 手机 → 模块在 AstroBox 侧串行（每台设备同时只有一条在途），回调在模块/UI 任务上执行，须尽快返回。
   - `ctx->bt->l2_send` 现在拒绝 0x43（`ERR_ARGUMENT`），模块消息只能走 `ctx->connect`。

3. **监听按手机会话计**：设备只在手机登记了监听（`CONNECT_LISTEN`）时才推送某个模块的消息；登记绑定登记时的手机会话
   （L2 后端的 `l2_peer`，会话对象变化时自增），重连后失效，AstroBox 在认证完成后补发。没有监听时模块的 `send` 返回
   `CANOPUS_CONNECT_NOT_LISTENING`（正值，什么也没排队），不是错误。设备端监听表 8 项，旧会话的项可被复用。

4. **只在有 L2 后端的 target 提供**（目前 .043 / .155）。其它 target 的 `ctx->connect` 为空、服务组缺失，模块按
   `canopus_services_has` 探测。

## AstroBox 侧

- core：`device/canopus/connect.rs` 负责线格式、`subscribe/unsubscribe` 的按设备按模块计数（跨设备实体存活，所以是模块级登记表而不是组件）、
  认证后的 `resync`，以及把设备事件发到 `CoreEvent::CanopusConnectMessage`。
- pluginsystem（API Level 4）：宿主接口 `astrobox:psys-host-v4/canopusconnect`（`send` / `listen` / `unlisten`，消息是
  `variant payload { text, binary }`）；插件导出可选接口 `astrobox:psys-plugin-v4/canopusconnect.on-message`（世界
  `psys-world-v4-canopusconnect`，与 HTTP 导出同样的可选查找方式，不实现的插件不受影响，也不改动 `event-type` 枚举，避免打破已编译的插件）。
- 权限：`canopusconnect.send` 与 `canopusconnect.listen` 都要在清单声明，并在首次使用时按「插件 × 模块」向用户确认；`unlisten` 不需要。

## 证据与限制

- 设备端：host 测试（含 ASan/UBSan）覆盖交付、拒绝、与管理器状态隔离、监听按会话失效、监听表上限；5 个 target 的 Supervisor 交叉构建与 ELF verifier 通过。
- L2 后端的事件队列、`l2_peer` 和发送顺序是真实编译的 Cortex-M33 代码，在 Unicorn 模型里执行过（`scripts/tests/test_l2_backend.py` + `tests/l2/harness.c`）：
  管理器响应先于事件离开、事件队列满即拒绝、`l2_peer` 随会话变化而不随同会话请求变化、会话失效的事件被丢弃而不发送。
  这是固件边界被建模的 host/emulator 级证据，不覆盖真实蓝牙 owner 的持续调度；Mirage 与真机运行见 `docs/progress.md`。
- AstroBox 侧：线格式、事件解码、状态解释、计数表和类型转换有单元测试；未用真实插件组件做端到端运行。
- .043 的 Supervisor 因此增大约 3 KB（96,372 / 98,304 字节自设预算）。
