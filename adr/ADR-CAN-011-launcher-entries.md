# ADR-CAN-011：模块的启动器入口

状态：host 实现完成；Band 11 `.139` 固件仿真（Unicorn，真实固件的 `app_install`/`app_lookup`/`launcher_add` 调用点按模型应答）下通过；
Band 10 Pro 只做了构建。**没有真机验证，没有提升任何能力状态。**

## 背景

模块的界面（ADR-CAN-010）可通过 Manager 的模块详情页「打开界面」进入，也需要能独立出现在系统启动器里。
公共托管 UI 入口与 exact-target 原生页面必须使用同一个应用身份分配器；模块各自写死 ID 再调用 `app_install` / `launcher_add`
会与公共池冲突。固件可能保留先注册的同 ID 应用，而后续 `launcher_add` 再添加一份它的图标；仅检查 lookup 非空无法证明应用归属。
`native-app-demo` 已迁移为提交页面给 Supervisor，不再自行注册应用。

## 决定

1. **唯一登记接口**：`CANOPUS_SVC_LAUNCHER`（14）服务版本为 1，`ctx->launcher` 只提供
   `register_app(ctx, const struct canopus_app_entry_v1 *)`，成功返回正数的已分配 8 位 app ID，失败返回负服务错误。
   此次直接替换未发布的 launcher 接口，不保留旧类型、方法、别名或兼容分支；公共 ABI major/minor 仍为 2.0，context 布局不变。
   `pages == NULL && page_count == 0` 打开先前通过 `ui->set_entry` 登记的页面；没有托管 UI 入口返回 STATE。
   非空数组与 1~16 个页面表示 exact-target 原生应用，不依赖托管 UI 入口。
2. **Supervisor 统一拥有应用与启动器注册**：模块提交标签、图标以及可选原生页面，由 Supervisor 生成绑定模块身份的应用描述符与包名。
   原生页面描述符必须可写且布局与当前固件精确匹配。指针数组返回前复制，描述符、引用数据和回调必须常驻到重启；
   Supervisor 在串行 UI 注册步骤给每页写入统一分配的 `app_id`。模块保留固件页面布局与回调的责任，不再调用应用安装或 launcher 添加。
   `app_install`、`app_lookup`、`app_launcher_add` 的固件符号策略未因本改动提升，没有改动 symbol JSON。
3. **稳定应用身份**：Supervisor 首次托管发布时从 `0xCC..0xDB` 的 16 个 ID 中统一分配，映射绑定 `module_id`，
   持久化在 registry intent word 的 bits 16..23，旧记录的零表示未分配。其他模块删除、slot 压缩、发布顺序变化及重启不改变已有映射。
   包名为 `com.canopus.module.<module_id>`；Manager 仍为 `0xCA`。普通与原生入口共享 8 个常驻回调/描述符槽。
   平台 `native_app_id_in_use` 在首次分配时查询固件已有 ID 并将其保留，能避开先注册的旧式原生模块。
   此后绕过分配器并任意硬编码 ID 的调用不受 Supervisor 控制；外部模块应迁移到统一登记接口。
4. **执行分两步、各占一个 UI 事件轮次**：`register_app` 从串行 module/UI owner 调用，只提交登记并返回分配的 ID。
   心跳每轮执行一次 `launcher_step`：先安装应用，再添加 launcher；这两步使用同一个模块身份。
   Supervisor 处理完排队注册后才继续后续 native-app publication stage，因此 demo 的 stage 2 可以使用返回 ID 导航。
   步骤失败记入 `CANOPUS_SUP_ERR_LAUNCHER`，不能通过已经返回的调用同步报告。
5. **一次发布与重启边界**：同一模块的相同定义重复登记返回原 ID，不新建入口；比较标签、图标、页面指针与数量。
   改变定义返回 BUSY。固件入口和原生页面没有已证明的同步注销，保留到重启；
   托管 UI 入口在模块停止后不再打开页面，原生回调要求模块一直驻留。
6. **重启后重新发布描述符**：持久化的是模块到 app ID 的映射，不是固件描述符；模块激活后重新提交登记。
   系统 launcher 对旧入口、排序和图标的缓存/持久化语义仍未验证，不能以新映射机制宣称已有重复图标会被自动清除。

## 限制

- 既有 `.139` 仿真在注册调用点按模型应答；不构成多应用冲突、真实 launcher 去重或新原生登记接口的设备验证。
- 步骤失败不能通过已返回的调用同步告知模块。
- 入口描述符与原生页面指针数组按需分配并保留到重启；同时最多 8 个普通/原生入口，每个原生入口最多 16 页。
- Mirage 端到端（`scripts/emu/ui_pages.py`）没有覆盖：本机没有 Mirage，示例模块 `ui-demo`/`ui-gallery` 也没有发布入口（多一个图标会改变那些脚本依赖的触摸坐标）；
  `all-services-test` 只验证参数拒绝，不发布真实入口。
