# 外部模块的普通 IO 安装入口

BluetoothAudio 和 Lyra Player 的 prod 按设备分别构建。设备目录内可以包含多个
固件版本，由该目录唯一的 `main.lua` 根据 `/etc/build.prop` 选择。打包范围为
设备目录根部的 `main.lua` 和所有 `.bin`；`docs/`、`build/` 不参与表盘打包。

```sh
# 在各自模块仓库执行；默认生成两个设备目录
scripts/build-install-watchface-prod.sh
# 一个设备，可含该设备的多个版本
scripts/build-install-watchface-prod.sh xiaomi-band-10-pro
# 精确选择版本；会重新生成该设备目录，其他设备目录保留
scripts/build-install-watchface-prod.sh xiaomi-band-11-4.100.139
```

框架安装器在自己的安装流程中创建 `/data/canopus/inbox`。Supervisor 同时注册
私有管理设备 `/dev/canopus` 和独立的安装入口 `/canopus/install`。后者是单独的
伪文件系统节点，不是指向 `/dev` 的符号链接，也不在 `/data` 挂载点下。

外部安装 Lua 仅执行以下操作：读取固件身份；校验目标资源与凭证配对；普通
`io.open` 写入和回读 inbox、图标；向 `/canopus/install` 提交签名模块安装请求。
每次写入、回读、提交均有 UI 状态。脚本没有 `os.execute`、execute 恢复代码、
`debug` 访问、原始 IO 上值提取或框架引导代码。

安装入口仅接受 CPC2 的 INSTALL 命令（2）、flags=0 和经过 basename 校验的非空
模块 token。它拒绝 CPC1、指针注册、启用、卸载、更新、管理查询及无 payload 的
框架安装。已有平台代码继续验证 CMI1 的 Ed25519 签名、模块哈希、固件和 target。
安装后的模块保持禁用。私有管理协议仍使用 `/dev/canopus`。

请求为现有 36 字节 CPC2 头加 `token + NUL`。成功写入仅表示完整接收请求；
调用者必须读取 40 字节回复并检查 result。回复前 36 字节为 CPC2，payload_size=4，
offset 36 的 little-endian u32 为 Supervisor diagnostic。私有管理请求与外部安装
请求使用不同的回复缓冲区。Lua 的请求写入、关闭、回复读取之间不让出 UI 协程。

固件证据：Band 11 `.139` SHA-256
`31ce82257f7c127950dc5070b86316730cf468a41f0d004559e41e7d923b2c74`，
`io.open` wrapper 位于 `0x0c6cfc60`，路径检查 `0x0c6cfb34` 先经
`0x0c352af0` 规范化，再查表 `0x2ca73cc4`，拒绝 `/dev`、`/proc`、`/sys`
及其子路径。`/canopus/install` 通过这项检查。测试运行原始路径检查 ARM 指令、
字符串函数和表，只模拟规范化函数；引导测试另行检查两组 fops 的注册和分流。
这不是对所有真实文件系统状态的覆盖。

需要先更新框架 Supervisor。旧版本已常驻时，重启后运行新版框架安装表盘，再运行
外部安装表盘。缺少新入口时，外部 Lua 显示更新框架提示，不自行恢复 shell 能力。

验证覆盖：C 协议及拒绝路径测试、真实 Lua 的普通 IO 场景、生成包签名与哈希校验、
Supervisor ARM 引导测试，以及 `.139` 固件路径检查。新入口尚无本轮真机反馈。
Band 11 两个音频模块已接入独立私有后端，可安装启用；无线播放仍需实机验证。
后端证据与测试见 [Band 11 音频适配](band11-bluetooth-139.md)。

相关实现：`manager/service/canopus_supervisor.c`、
`manager/service/canopus_supervisor_platform.c`、
`scripts/build_module_installer_prod.py`、`watchfaces/module-installer-prod/src/main.lua`。
