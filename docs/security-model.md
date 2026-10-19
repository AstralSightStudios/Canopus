# Canopus 安全与信任模型

> 本文档描述 Canopus 设备控制面的安全模型。它回答"谁可以做什么、信任从哪里来、
> 失败后怎么办"。与 `architecture.md` §19 冲突时，以更严格的 default-deny、
> exact-target 和生命周期规则为准。2026-10-14（K9）起已移除全部签名与验签，见 §3。

## 1. 威胁参与者

| 参与者 | 能力 | 关注的攻击 |
|---|---|---|
| 恶意/受损第三方模块 | 携带任意代码、资源、回调；可被 loader 执行 | 伪造身份、越权 capability、驻留绕过 unload、持久化恶意 active slot |
| 本机同地址空间调用者 | 与 supervisor 共享进程/地址空间，可 open/read/write `/dev/canopus` | 注入命令、读取状态、绕过 UI 确认 |
| 物理/串口操作者 | 重启设备、断电、操作 bootloader/恢复界面 | 断电打断事务、回滚、伪造更新 |
| 供应链/分发方 | 构建并分发 `.canopus` 包 | 注入恶意包、回滚到有漏洞版本 |
| 逆向/研究方（LLM/MCP） | 可读取固件、静态恢复符号、写证据 | 猜测 ABI、把未证明符号提升为 callable |

## 2. 事实边界（不承诺不存在的东西）

- **无沙箱**：本设备不存在可信进程隔离。`/dev/canopus` 的任何调用者都与
  supervisor 在同一地址空间内运行。因此：
  - 节点 mode、uid/gid 不是安全边界；调用者身份即使可区分也不构成隔离。
  - 本地同地址空间调用者 = 未隔离控制面，可读取和驱动一切。
- **没有来源认证**：包与回执不再签名（K9），任何能写入 inbox 的调用者都可以提交
  模块。防线是 exact-target、artifact SHA-256、模块 ID、版本单调、ELF verifier、
  capability 和生命周期 policy 检查。
- **host 测试 ≠ device 证明**：host fake、静态恢复、golden vector 均不证明
  真机 ABI。只有带 firmware hash 的 device probe 才算 device gate。

## 3. 完整性与版本（无签名）

- 无信任根、key role 或吊销：2026-10-14 起移除（K9）。
- 回执（CMI2，176 字节）只绑定 module_id、target_id、固件 SHA-256、artifact 大小与
  SHA-256、生命周期类别和 `module_version`，不含签名；supervisor 逐项校验。
- **anti-rollback**：同 module_id 的 `module_version` 必须单调递增，回滚包被拒绝。
- 可选的非加密缓解：首次安装或升级时在手环 Manager 上弹确认（见 refactor-plan R6）。

## 4. 本地攻击者模型（无沙箱含义）

由于调用者与 supervisor 同地址空间：

- 任何 mutating 操作都必须独立通过：回执与 artifact 哈希校验、target identity、safe-mode
  policy、physical-confirm（如果启用）。
- UI 拒绝不够：supervisor 必须自己拒绝 wrong-target / hash-mismatch /
  rollback / verifier-failed 包。
- 第三方 app 绝不获得：safe-mode 退出、保留 app ID 或
  unrestricted debug 操作。

## 5. 包与存储

- 包路径：supervisor 只接受 bounded token（basename），绝不接受任意路径。
- 归档 entry：canonical path 规则（拒绝绝对/`..`/`.`/反斜杠/NUL/重复 entry/
  symlink/hardlink/device/FIFO），manifest 为每个 artifact 记录 SHA-256。
- 存储事务：journal + fsync 目录 + 原子 rename，任何一步失败都不能留下可加载的
  partial active slot。

## 6. 加载与生命周期

- loader 成功 ≠ READY；必须等待 constructor/init handshake。
- removable disable = stop → drain → unload；有 retained/detached 资源或
  open ref 时绝不 unload，fail-stop + reboot-required。
- resident 模块永不调用 platform unload；只有 next-boot 语义。
- 任何 resident barrier 发布后，unload 路径从物理上不可达。

## 7. 恢复与 safe mode

- 每次 boot：identity guard → journal recovery → boot marker（BOOTING/BOOT_OK）。
- 未完成 BOOTING、重复 crash 或 store corruption → 自动 safe mode。
- safe mode 只允许 query/diagnostics/next-boot/rollback/remove-pending；
  install/enable/load/update activation 一律 DISALLOWED。
- 退出 safe mode = 先选 recovery action，写 next-boot trial，成功 READY 后才清除；
  trial 失败自动回到 safe mode。

## 8. 证据与 codegen gate

- 只有 `approval_state == APPROVED` 且 evidence_ids 非空的符号才生成 callable。
- restricted / FORBIDDEN / PENDING 永不生成 callable。
- device probe 必须记录 firmware hash、artifact hash、操作步骤与观察结果。

## 9. 相关文档

- [progress.md](progress.md)：进度状态（本表 ID 的权威状态见
  `native-manager-ui-plan.md` §15）。
- [archive/architecture.md](archive/architecture.md)：生命周期类别与操作语义。
- [target-authoring.md](target-authoring.md)：target pack 与证据等级。
