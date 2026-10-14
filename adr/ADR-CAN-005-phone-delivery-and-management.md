# ADR-CAN-005：手机端投递与管理（O1/O5 的结论）

状态：已采纳（2026-10-14，refactor-plan K13/P5）。证据来自 Mirage 上的 043 模拟器与真实的 AstroBox 核心。

## 背景

原计划（O1）要为手机↔设备端另开一条控制通道（interconnect / hook protobuf / L2），并担心一个"路由锚点快应用"（O5）。
核查后发现：

- 命名 interconnect 的服务端要求目标包在设备的 AIOTJS 包管理器里已安装（`quickapp_get_appinfo` 门槛），
  原生模块没有注册入口；`interconnect_connect` 还需要事件循环指针（`interconnect_loop` 已 WITHDRAWN）。
- 但设备**已经**支持手机通过 Mass(ThirdPartyApp) 投递已签名的快应用 `.rpk`，并把它展开到
  `/data/app/<package>/`，随后在快应用列表里返回 `package / versionCode / fingerprint`。
  `.rpk` 是带 APK Signature Scheme v2 签名块的 zip，**任何发布者自己的密钥都被接受**（043 上实测）。

## 决定

Canopus 模块以快应用容器的形式投递，**不新增设备通道**：

- 包名 `com.canopus.pkg.<id>`（id 遵循 ADR-CAN-003），`versionCode` = 模块 `versionCode`。
- `canopus package --rpk` 产出签名的 `.rpk`：载荷放在 `canopus/<target_id>.cmi`（每个支持的固件一份回执）与
  `canopus/<target_id>.ko`（target 模块）或 `canopus/portable.ko`（portable 模块）。同一个 `.rpk` 因此覆盖所有固件。
- 手机端用**现有**快应用安装流程（AstroBox 的 `install` / 商店 quickapp 条目）投递、升级、删除；
  已装模块与版本由快应用列表给出（核心里 `device::canopus::modules_from_apps`，桥接方法 `canopusModules`），
  更新提醒就是"商店 versionCode > 已装 versionCode"，无需改动 AstroBox 的更新比较逻辑。
- 设备端 Supervisor 的 `SCAN`（CPC1 0x4351000B / CPC2 `CANOPUS_CMD_SCAN`）读取 `/data/app/com.canopus.pkg.*`，
  把匹配本固件的载荷复制进暂存区并走与 INSTALL 相同的校验与 A/B 暂存：新模块装入且保持禁用，
  更高版本进入待切换（REBOOT_REQUIRED），同版本/更低版本不是错误。触发点是 Manager 里的"导入手机推送的模块"
  （带确认，兼作 R6 的用户确认）；校验仍是回执 + SHA-256 + target/固件绑定，所以装错固件的包会在设备端被拒绝（R9）。

## 影响与限制

- 快应用安装器要求签名：发布者用一把固定的密钥签名（指纹由证书派生，升级需同一把密钥）。这是平台要求，不是 Canopus 的信任根，
  Canopus 自己仍无签名（K9）。
- 模块会作为快应用出现在设备的包列表里；是否出现在启动器取决于固件，未处理。
- 手机→设备没有即时触发：模块在用户于 Manager 里确认导入（或下次触发 SCAN）时才被设备拾取。若以后需要即时触发，
  再在此基础上补命名 interconnect（需要先解决 loop 与注册门槛）。
- 只有 Band 10 Pro 043 的目录遍历（opendir/readdir/closedir）已恢复并在模拟器验证；其他固件的 SCAN 返回"不支持"，
  需要为各 target 补这三个符号。
- AstroBox-NG 的 `app`（Tauri 命令）与 `web` 前端是私有仓库，本次未改动；公开部分（core 助手、Mirage 桥接）已实现。
