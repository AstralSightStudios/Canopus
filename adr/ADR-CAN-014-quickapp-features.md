# ADR-CAN-014：快应用原生 Feature 的统一注册

状态：host 实现完成；`.043` Mirage 通过（`EVID-QUICKAPP-1043-001`，仅 emulator 证据）：常驻模块登记两个 Feature，快应用
`$app_require$` 均得到对象、未登记的名字得到 `undefined`；冷启动后随 Supervisor 激活再次登记成功，两次独立启动各自 create/require/destroy 一次。
**没有真机验证，没有提升任何能力状态或符号记录。**

## 背景

快应用通过 `$app_require$("@app-module/<name>")` 使用原生 Feature。固件只在应用自己的 JS 任务上、应用初始化时把 Feature 描述登记进该应用的
registry，没有全局登记入口。astrolink 的做法是自己接管 ferry Framework 对象的主 vptr：复制 Framework vtable，改工厂槽，
工厂再把每个新 Application 的主 vptr 换到一份改了 init 槽的 Application vtable 副本上，init 之后调用 `feature_register_feature`。

这个接管点只有一个。第二个模块（例如需要输入法 Feature 的拼音键盘）照做时会看到 vptr 已不是原厂值；astrolink 在这种情况下返回 BUSY。
多个模块各自 hook 同一个对象，彼此必然冲突。

## 决定

1. **服务**：`CANOPUS_SVC_QUICKAPP`（15）版本 1，`ctx->quickapp` 只有
   `register_feature(ctx, const void *description)`。`description` 是精确目标的固件 Feature 描述，布局、生命周期与成员回调由模块负责，
   Supervisor 不解析它。context 末尾追加 `quickapp` 指针，与 ADR-CAN-011 追加 `launcher` 的方式相同。
2. **Supervisor 独占分发点**：第一次登记时才接管。接管方式与 astrolink 相同，只是由 Supervisor 完成：复制两张原厂主 vtable（各含两个 Itanium
   前缀字），工厂槽把新 Application 移到 Application 副本上，init 槽先调原厂 init，再对每个已登记描述调用一次 `feature_register_feature`。
   除两个对象的主 vptr 外，不写任何固件代码或表，次级 vptr 保持原厂。Framework 的切换是一次单字写入，之前有屏障，
   所以同时在构建的快应用要么走原厂工厂，要么走完整的副本。
3. **惰性接管与 BUSY**：在第一次登记之前，固件状态完全不变，仍自行 hook 的旧模块照常工作。
   Framework 尚未构造、或 vptr 已被别的组件换掉时，登记返回 BUSY，不覆盖别人的分发表。
4. **生命周期**：运行中的快应用会一直持有描述指针，所以只接受常驻模块（`lifecycle_class` 不是 removable），removable 返回 ACCESS。
   同一描述重复登记返回 OK；全系统最多 `CANOPUS_QUICKAPP_FEATURES`（8）个，满了返回 NOMEM；没有注销接口。
   模块被释放时，Supervisor 清掉它的条目，之后启动的快应用不再拿到这些描述；已在运行的快应用不受影响。
5. **线程**：登记在串行 module/UI owner 上进行；分发在每个快应用自己的 JS 任务上读取条目，条目是单字指针，读到的要么是完整描述，要么是 NULL。
   init 槽在原厂 init 之后、应用第一行源码之前运行，所以 require 一定发生在登记之后。
6. **目标**：只有 `.043`。`FW_QUICKAPP_*` 宏只为这个精确目标生成，取自 `ferry_framework_instance`、`ferry_framework_vtable`、
   `ferry_application_vtable`、`feature_register_feature` 四条记录。这四条仍是 `restricted` / `STATIC_RECOVERED` / `PENDING`，
   与 `.043` 字体生命周期的绑定方式相同（`generated_stability.rs` 钉死地址与状态）。其他目标的组版本为 0，`ctx->quickapp` 为 NULL。

## 限制

- 只有 Mirage 运行，没有真机；`feature_register_feature` 的返回值没有回报给模块。
- astrolink 的 `.043` 已改用该服务（`src/qjs/supervisor.rs`），Mirage 上与 feature_probe 同时激活，同一个快应用里两者的 Feature 都能 require，
  astrolink 的原生方法也能调用。仍自己 hook 的模块与该服务互斥：谁先接管谁获胜，后来者得到 BUSY。
- 登记之前已启动的快应用拿不到新 Feature。
- Supervisor 不检查 Feature 名字是否重复，重名时由固件 registry 处理。
- Band 11 与 `.036` 没有实现。
