# Canopus 专用通信通道（L2CAP）可行性

结论先说：**手环侧理论上可做，手机侧经典蓝牙 L2CAP 在 Android/iOS 上不可行；更现实的是在 RFCOMM/SPP 上注册一个 Canopus 专用 channel + SDP 记录。**
下面是依据（043 固件 IDB 的静态核对 + 仓库里已有的恢复成果），未在设备/模拟器上实测。

## 手环侧

| 能力 | 现状 | 依据 |
|---|---|---|
| 手环主动发起 L2CAP 连接（band → 对端 PSM） | 已恢复 | `bt_l2cap_connect`（68 字节请求）、`bt_l2cap_submit_cid`、`bt_l2cap_disconnect`、`bt_l2cap_owner`，见 `targets/*/symbols/*private_abi.bt_l2cap_*` 与 `sdk/rust/canopus-target-private`（STATIC_RECOVERED，设备未探测） |
| 在 SDP 里发布自定义服务记录 | 已恢复 | `sdp_builder_create` / `sdp_set_raw_attribute` / `sdp_commit` / `sdp_unregister` |
| 拦截 raw HCI/ACL 接收 | 已恢复 | `bt_gap_install_receive_hook`、`gap_host_stock_receive` / `gap_host_receive_slot` |
| **接受对端发起的 L2CAP 连接（自定义 PSM 监听）** | **没有现成 API** | 固件 BT 服务（`frameworks/connectivity/bluetooth/service`，Vela）里只找到 `rfcomm_server_init`、`spp_server_start/stop`、`bt_socket_server/client`；没有 `l2cap_listen`/`l2cap_server` 之类的名字。`btsnoop_filter.c` 里出现的 `unrecognized psm` 只是抓包过滤器，不是协议栈的 PSM 表 |

手环侧要接受入站连接，有两条路，难度都不小：
1. 恢复 L2CAP FSM 的 PSM 派发表与"连接请求"处理函数，往里登记一个 PSM（需要新的逆向，且改的是协议栈内部状态）。
2. 用 raw ACL 接收钩子自己做最小 L2CAP 信令：收到 Connection Request（PSM = Canopus）就由我们回 Connection Response / Configuration，之后该 CID 上的数据由我们处理；同时必须让钩子吞掉这些包，否则原协议栈会回"PSM 不支持"。需要一条能发 ACL 的路径（`bt_l2cap_submit_cid` + 缓冲区 API 已恢复，但仅为 STATIC_RECOVERED）。

## 手机侧（决定性的限制）

- **iOS**：没有经典蓝牙 L2CAP/RFCOMM 的公开 API（`CBL2CAPChannel` 只支持 BLE CoC）。
- **Android**：公开 API 里 L2CAP 只有 LE CoC（API 29+，`createL2capChannel`/`listenUsingL2capChannel`）；经典 L2CAP 需要隐藏 API 或 root。RFCOMM 自定义 UUID 可以（`createInsecureRfcommSocketToServiceRecord`）。
- **桌面**：macOS（IOBluetooth `openL2CAPChannelSync`）、Linux（BlueZ 原始 L2CAP 套接字）可以；Windows 基本不行。
- 手环的栈里没有看到 LE L2CAP CoC 的迹象，所以"换成 LE CoC"也没有依据。

因此**面向手机的 Canopus 专用通道不应该做在经典 L2CAP 上**。AstroBox 现在已经有 `btclassic-spp` 插件（RFCOMM/SPP），也就是手机侧唯一现成能走的经典蓝牙传输。

## 建议

1. **首选：RFCOMM 专用 channel**。手环侧：用已恢复的 SDP builder 发布一个带 Canopus 自己 UUID 的服务记录，并在 RFCOMM 上起一个 server（固件里有 `rfcomm_server_init`、`spp_server_start`，需要恢复其参数/调用约定）。手机侧：AstroBox 用自定义 UUID 连这个 channel，Android 公开 API 即可。成帧可以直接复用 CPC2（36 字节头），安全上依赖配对加密。
2. **桌面/模拟器场景**：如果只要 macOS/Linux 桌面端，自定义 L2CAP PSM 是可行的（手环侧走上面的方案 2）。Mirage 的 `emubt`（Rust 主机栈）可以对模拟器里的手环开任意 L2CAP/RFCOMM，用来验证手环侧实现。
3. 在做之前先做一次低成本的验证：在 043 模拟器上用 `emubt` 对手环发一个未知 PSM 的 Connection Request，确认原栈确实拒绝（印证"没有监听"），再看 raw ACL 钩子能否在拒绝之前拿到这个包。

## 没有验证的部分
以上都是静态分析；没有在模拟器或真机上发过任何 L2CAP 包。`bt_l2cap_*` 与钩子函数的状态都是 STATIC_RECOVERED，不得据此提升能力状态。
