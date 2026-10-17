# ADR-CAN-005：手机端投递与管理

状态：旧方案已撤销（2026-10-17），由 ADR-CAN-008 的原生 `.canopus` + CPC2 数据面替代。

2026-10-14 曾采用 `.rpk` 快应用容器及快应用列表识别；用户明确否决该设计。
旧模拟器结果仅是历史记录，不构成继续使用该路径的依据。工具链不再生成 RPK，
Supervisor 不再读取任何快应用目录，手机端模块列表、版本与管理均来自 Supervisor。

## 当前决定

- 唯一模块分发格式是 `.canopus`，沿用 tar 内的 manifest、按 target 绑定的 CMI2 回执与 ELF。
- AstroBox 先通过 CPC2 查询精确 target，校验归档及 manifest 的 SHA-256，选择对应回执与 artifact，
  经 WearPacket Layer 2 的 `0x43` 通道分块发送。不是蓝牙 L2CAP，也不借用 Mass ThirdPartyApp。
- 设备校验回执的 target、固件 SHA、artifact 长度与 SHA 后，才接受 INSTALL。
  新模块默认禁用；升级提交到非活动 generation，重启后切换。
- SCAN 只发现 `/data/canopus/inbox/` 中已完成的 `.cmi.next` / `.ko.next` 暂存对。
  常规手机安装直接调用 INSTALL，不依赖 SCAN 或快应用列表。
- 框架安装器表盘仍是 Supervisor 的引导入口，不属于模块分发格式。

## 证据与限制

符号的 restricted / PENDING、not_probed 状态不变。.043 / .155 的普通构建自动包含
各自的 exact-target L2 后端，不要求额外启用参数；未实现后端的其他 target 仍返回
UNSUPPORTED，不套用别版固件的地址。模拟器验证不等于真机验证或生产批准。

原生投递暂不支持包内资源文件，客户端明确拒绝含资源的包，不静默丢弃。
