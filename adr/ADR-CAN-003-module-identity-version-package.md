# ADR-CAN-003：模块身份、版本与包格式

状态：已采纳（2026-10-14，refactor-plan P0.1）

## 模块身份

`module_id` 取各方语法的交集：`^[a-z][a-z0-9_]{0,15}$`。同一个字符串同时用作商店条目 id、回执 `module_id`、
insmod 模块名和 inbox token。上限 16 字节来自 Mass 16 路径：该路径把 id 当作表盘 ID，AstroBox 会随机化超过 16
字节的 ID，更新匹配随之失效。如果以后不再走 Mass 16，上限可放宽到 31（回执字段为 32 字节、含结尾 NUL）。

现状与目标的差异：设备端回执校验目前接受 `[a-z0-9_.-]`（见 `canopus_install_module_id_valid`），P3 打包工具落地时再收紧到本语法；
示例 `org.example.hello` 等需要改名。

## 版本

- `versionCode`（u32，单调递增）= 回执 `module_version` = 商店 `versionCode`，设备端据此判断升级（见
  `module-installer-io.md` 的更新与回滚）。
- `version` 字符串（semver）只用于展示，不参与比较。

## 包格式（`.canopus`）

tar（确定性：路径排序、mtime=0），不含签名（K9）：

- `manifest.json`：schema 校验后的清单，记录每个产物的 SHA-256。
- portable 产物：`artifacts/abi-<major>-thumbv8m/module.elf`（P2.2 落地后）；per-target 产物：`artifacts/<target_id>/module.elf`。
- 每个产物旁附一份无签名安装回执（CMI2，176 字节）。
- 安装器表盘和商店条目都是 `.canopus` 的派生物，不是另一套来源。

完整性靠 manifest 中的 SHA-256 与回执中的 artifact SHA-256；设备端另外校验 target 与固件 SHA-256、模块 ID、版本单调。
