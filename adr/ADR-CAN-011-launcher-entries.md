# ADR-CAN-011：模块的启动器入口

状态：host 实现完成；Band 11 `.139` 固件仿真（Unicorn，真实固件的 `app_install`/`app_lookup`/`launcher_add` 调用点按模型应答）下通过；
Band 10 Pro 只做了构建。**没有真机验证，没有提升任何能力状态。**

## 背景

模块的界面（ADR-CAN-010）只能从 Manager 的模块详情页「打开界面」进入。要让模块像普通应用一样出现在系统启动器里，
需要一个公共服务，而不是让每个模块自己调 `app_install` / `launcher_add`（`native-app-demo` 的做法：target 模块、full-trust、
自带固件地址）。

## 决定

1. **服务组 `CANOPUS_SVC_LAUNCHER`（14）与对象 `ctx->launcher`**（`canopus_services.h`），位于 context 末尾。
   当前公共 ABI 为 2.0（ADR-CAN-013），不保留旧 major 的兼容布局。唯一的调用是 `set_entry(ctx, &canopus_launcher_entry_v1{label, icon})`。
   选中入口就打开模块用 `ctx->ui->set_entry` 登记的页面（与「打开界面」同一条路径、同一个单页限制），所以没有先登记页面入口就返回 `STATE`。
2. **实现只用 Manager 已经在用的三个固件入口**：`app_install`、`app_lookup`、`app_launcher_add`（Band 10 Pro 与 Band 11 各自的记录），
   描述符布局与 Manager 自己的应用相同，每个入口一个应用、一个页面（`page_id` 0）。这三个符号仍是 `restricted`，只由 Supervisor 调用，
   不对模块开放；没有改动任何 symbol JSON。
3. **应用 ID**：`0xCC..0xD3`，最多 `CANOPUS_LAUNCHER_ENTRIES`（8）个，系统启动动画要求 8 位 ID。0xCA 是 Manager，0xCB 是 `native-app-demo`。
   没有固件里全部原厂 ID 的清单，这个区间与 Manager 一样是假设空闲。
4. **执行分两步、各占一个 UI 事件轮次**：`set_entry` 只在 Supervisor 里排队并启动服务心跳；心跳（LVGL 定时器，UI 任务）每次 tick 调一次平台钩子
   `launcher_step`：第一步 `app_install`（并用 `app_lookup` 确认），第二步 `launcher_add`。与 native app 的 stage 1/2 一样不在同一个回调里连续注册。
   步骤失败只写 `error_code = CANOPUS_SUP_ERR_LAUNCHER`，不重试，也不能告知已经返回的模块。
5. **没有注销**：`app_launcher_del` 仍是 `CANDIDATE`，没有同步注销的证据。因此入口一旦发布就留到下次重启，模块之后停止也一样；
   选中已停止模块的入口时页面创建失败，什么也不打开。Supervisor 记住每个槽发布过的标签与图标：同样的再次调用返回 OK，不同的返回 BUSY（直到重启）。
   这与 native-manager-ui-plan §4.5 的结论一致（无法证明同步注销就按「重启边界生效」处理）。
6. **每次开机重新发布**：Supervisor 不开机自启，模块在 Manager 页面首次打开后才被激活，入口在模块激活时由模块再次登记。启动器是否持久化这些入口、
   重启后残留的入口会怎样，没有证据；与 Manager 自己的入口同一个未知。

## 限制

- 没有真机证据；同时存在多个应用、ID 区间空闲、`app_install` 对 `count=1` 的行为都只有静态与仿真依据。
- 步骤失败不能回报给模块。
- 8 个入口占用 Supervisor 约 2.7 KB 常驻内存（应用/页面描述符与字符串）。
- Mirage 端到端（`scripts/emu/ui_pages.py`）没有覆盖：本机没有 Mirage，示例模块 `ui-demo`/`ui-gallery` 也没有发布入口（多一个图标会改变那些脚本依赖的触摸坐标）；
  `all-services-test` 只验证两条拒绝路径。
