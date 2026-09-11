# Band 11 4.100.139 音频模块适配核验

BluetoothAudio 和 Lyra Player 已接入独立的 `.139` Rust 私有后端，prod 包可安装后
在管理器启用。固件指令与编译后的适配函数已通过 ABI 测试；**无线播放、耳机兼容性、
真实线程调度和显示效果仍需实机验证**。这不等同于已经获得蓝牙播放成功的设备证据。

固件 SHA-256：`31ce82257f7c127950dc5070b86316730cf468a41f0d004559e41e7d923b2c74`。
真实 `vela_ap.bin.i64` 使用 IDA 只读核验，关闭时不保存；10 Pro `.043` 只作语义对照。
精确绑定的证据为 `EVID-BLUETOOTH-4139-006`；符号仍为 restricted/PENDING，未生成
公共 callable wrapper。应用必须先通过固件身份检查，随后调用 target-private 后端。

## 蓝牙调用链

1. 从 `0x200c3148` 获取系统 adapter，注册驻留的 17-word 回调表；获取电源和发现状态。
2. 系统 IPC 完整使用 708-byte envelope，command+0、result+12、payload+60。
   command 37/38 设置/读取扫描模式，92 查询配对状态，94/95 创建/删除配对。
   原生 bond query `0x0c46a884` 提供聚合视图；系统配对回调会接受请求，保留该回调。
3. 使用模块自身的 AVDTP/A2DP、SBC、AVRCP 实现，注册 SDP 记录，经 Bluelet L2CAP 发包。
   不使用固件中虽有 client 却没有 server 的 stock A2DP source 命令。
4. 外部工作经 `0x0c863bc4` 加锁入队；取消回调用三参数 thunk `0x0c858998`，从 r2 释放参数。
5. 六参数定时器 `0x0c8c7ff0` 不接收旧版 tag；取消使用 `0x0c863058`，清空 handle 后
   释放 `0x0c863010` 返回的 token。已测试重复取消不会二次释放。
6. H4 收包槽 `0x200bda80` 只接受已知 stock receiver 或当前模块 hook；兼容处理后
   转交 stock `0x0c863c98`，未知指针不覆盖。

Socket 收包函数 `0x0c46a080` 直接复制请求回复并释放 adapter+36 信号量；通知另行
复制到工作队列，经 `0x0c467940`/`0x0c467970` 分发。服务 socket 使用独立 service loop。
这一静态路径支持回调中的同步请求，但测试没有模拟真实多线程调度，不能据此保证无死锁。

## 关键 ABI 修正

| 对象 | `.139` 布局 / 语义 |
| --- | --- |
| L2CAP connect request | 68 字节，callback+12、address+16、local MTU+52、options+54 |
| 内部 channel | CID+116、remote MTU+96，不能拿来解释 completion event |
| completion event 3 | `0x0c86eb0c` 单独分配 120 字节；CID+108、remote MTU+72、address+100；回调拥有并释放事件 |
| confirm event 2 | 回传原始 connect request，CID+0；回调拥有并释放请求 |
| disconnect / flow / data | 固件分配的事件/StockBuffer，回调接管；data CID+4，payload=base+4+offset |
| StockBuffer | total=length+headroom+4，offset=headroom+4，分配 total+6；适配器额外检查溢出/OOM |
| SDP | builder 12 字节、指针+4；commit 转移所有权至注册表，unregister 释放记录及属性 |
| native app / page | launcher 80 字节、package+12；page 120 字节，UI destroy callback+100 |
| LVGL event | u16 event code+8，userdata+12 |
| VFS | ioctl `0x0c341e38`，lseek i64，register_driver 三参数（mode 被 LTO 消除） |
| timespec | i64 seconds+0，i32 nanoseconds+8；monotonic clock id=1 |

## UI 与安装结构

两个模块使用 Band 11 的原生列表类：204 像素宽、系统 MiSans medium 28/24 字体和
系统尾部控件。Lyra 自绘播放器以 212×520 排布，封面 180×180，标题/作者宽188，
三枚按钮中心间距64、触摸区域56；336宽背景居中裁剪。按压动画使用固件证实的
release=8（2是 pressing），避免长按时提前弹回。保留纵向滚动，以访问下方状态、
音量与返回列表。10 Pro 的 336×480 布局保持原有参数。

每个模块 prod 根目录按设备分文件夹，10 Pro 文件夹同时包含 `.036`/`.043`，
Band 11 文件夹包含 `.139`。设备文件夹根部只含一个 `main.lua` 和 `.bin`；ZIP 与
hash manifest 放 `build/`。安装 Lua 仅通过普通 IO `/canopus/install` 提交签名模块，
不包含 os.execute 恢复、debug 或 bootstrap。安装完成保持禁用，由用户在管理器启用。
先使用更新的 Canopus Supervisor，以提供该 IO 接口并准备 `/data/canopus/inbox`。

## 可复现检查与实机顺序

```sh
build/band11-tests/bin/python scripts/tests/band11_private_backend.py
build/band11-tests/bin/python scripts/tests/band11_arm_bootstrap.py
scripts/ci.sh
../Canopus-Module-BluetoothAudio/scripts/build-install-watchface-prod.sh
../Canopus-App-LyraPlayer/scripts/build-install-watchface-prod.sh
```

编译后的 Rust/真实固件测试覆盖 buffer 一致性及拒绝路径、完整 IPC envelope、
定时器/队列取消所有权、completion event 的生成/读取/释放、SDP commit/unregister、
H4 转发、身份拒绝、monotonic clock、原生中文列表；含5项底层固件测试共16项。
Unicorn 模型替换堆、kernel 同步、socket transport、LVGL core；不模拟射频或实际显示。

实机依次：更新 Supervisor → 安装并启用 BluetoothAudio → 搜索耳机、配对、连接、
测试音、断开/重连 → 安装并启用 Lyra → 播放本地音乐、上下曲/暂停、耳机遥控与音量。
管理器的既有成功不能替代这些音频设备验证。Interconnect、通知和 unregister_driver
目前不提供；这两个驻留模块不调用这些接口。
