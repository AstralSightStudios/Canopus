# Band 11 .139 原生管理器 UI 修复（2026-09-10）

用户已报告此前版本在真机成功加载，管理器基本功能正常，但列表外观破碎、中文无法显示。
这是本次修复的依据；用户没有独立提供设备上资源的哈希，因此不把这一反馈归因于某个已核对的二进制哈希。
**本次新 UI 已完成固件指令测试和构建，实际屏幕效果尚待复测。**

## 根因和修改

原 Band 11 后端使用普通 `lv_obj` 和一个拼接文本的 Label 模拟每行，固定为176×80，
开关以 `[x]` 表示；没有调用系统字体样式接口。Band 10 Pro 则使用 LVX 原生列表。
两者虽然都是 LVGL v9，厂商的 LVX 封装、类布局和函数地址仍有差异。

本次直接采用 .139 系统设置使用的原生列表类：204×96、24像素圆角、#222222背景，
主文字 MiSans Medium 28、次文字 MiSans Medium 24 / #999999，原生箭头和开关。
两行文字分别交给原生控件管理，保留固件的长文字滚动行为，不再拼成一个文本。
独立标题和说明文字也绑定系统 MiSans 样式。
滚动视口宽212、高444、顶部68，行距8，顶部留4、底部留32像素供最后一行滚动。

每个列表对象只创建一次子控件，后续快照使用原生 updater 更新文字和状态；
空副标题传 NULL，固件会隐藏旧副标题，需要时创建或重新显示。
保留按 key/kind 复用行的逻辑。开关在 trailing 对象上监听 VALUE_CHANGED(30)，
普通行监听 CLICKED(7)，派发时额外核对 event.current_target，避免错页派发和重复切换。

## 固件证据

原始文件 `fwbins/xiaomi-band-11-4.100.139/vela_ap.bin`，SHA-256：
`31ce82257f7c127950dc5070b86316730cf468a41f0d004559e41e7d923b2c74`。
地址均为未加 Thumb 位的入口；C 调用时加1。

| 接口 / 数据 | .139 地址 | 核验内容 |
| --- | --- | --- |
| 原生 list item create | 0x0C50F4E4 | 系统设置0x0C5962F4调用；创建类0x2CA3ED20 |
| 原生 add content | 0x0C50FAD0 → 0x0C50F520 | 七参数：row, icon, primary, secondary, number, selected, trailing |
| 原生 update | 0x0C50EC5C | 六参数；更新状态和文字，副标题增删由0x0C50EAAC处理 |
| trailing 字段 | row+0x54 | 与10 Pro的+0x50不同；类型1为开关、3为forward.bin |
| theme apply | 0x0C5048D8 | 列表分支0x0C504F90，设置204×96、inset4及原生样式 |
| 列表样式 | 0x200C1760 / 176C / 1778 | MAIN背景，part0x90000主文字，part0xA0000副文字 |
| 字体样式 | 0x200C1574 / 1568 | miwear_init_steps中0x0C7E92AC..0x0C7E92EE创建Medium28/24 |
| 字体工厂 | 0x0C89A7E4 | 表0x2CA71738的索引1为MiSans-Medium |
| style apply | 0x0C4FBE54 | 系统设置0x0C597FC4使用；白字基础样式+字体样式+透明度 |
| remove all styles | 0x0C38583C | 清理普通内容视口的默认边框/间距，保持透明 |
| 原生行事件 | 0x0C506B24 | event.current_target在+4，code在+8；将指针事件转给+0x54 |

旧 ensemble 候选 `lvx_list_row_create@0x0C78AA68`、
`lvx_list_row_trailing@0x0C3A06C4` 已证伪并标记 REJECTED。
`lvx_page_content_create@0x0C693F78` 实为带生命周期定时器和父节点回调的 activity root，
不能套用10 Pro内容容器语义，也已拒绝该语义绑定。当前仅为内容视口创建普通对象并移除样式。
list updater和style apply候选已用实证地址纠正，新增原生接口保持restricted/PENDING，
不据此批准整个 Rust SDK。结构化证据见 `EVID-NATIVE-UI-4139-004`。

LVGL 的字体属性可以继承，但普通对象不会自动拥有厂商列表类的专用样式；
参考[官方 LVGL v9 样式文档](https://lvgl.io/docs/open/9.2/overview/style)。
本次接口、常量与样式选择以固件指令为准。

## 验证与边界

`scripts/tests/band11_native_ui_firmware.py` 执行真实 .139 Thumb：

- 原生行构造函数、theme apply、七参数populate、六参数update及内部布局。
- 实际启动初始化片段和字体工厂包装，确认请求MiSans-Medium的28/26/24字号。
- 主/副字体传递、灰字颜色、204×96尺寸、圆角及箭头资源；空副标题到非空再隐藏。
- 多次切换状态不重复创建子控件；原生行事件仅从正确current_target转发。
- 重复应用字体样式保持两个style引用，不累积重复样式。

LVGL底层内存分配、样式存储、图像解码、动画和绘制仍使用模型。
系统字体文件的实际字形输出和物理屏幕无法由该测试证明。
完整bootstrap测试现在也通过上述真实LVX函数渲染Manager首页，验证中文字符串和字体，
并检查重复resume不增长控件数量。加载阶段测得模型Kmem峰值101376字节。

本次构建与 `scripts/ci.sh` 全部通过：0 undefined 的 ELF verifier、根工作区、Rust SDK、
C host / 交叉构建、生成文件稳定性、Lua恢复/启动及打包检查；其中ARM测试为
bootstrap 5项、pmain 2项、固件集成5项、新增原生UI 4项。
日志为仓库下 `build/band11-tests/ui-build.log`、`ui-arm.log`、`ui-ci.log`。

Lua恢复方案和加载器源代码没有修改；因Supervisor内容变化，资源CRC和对应stage资源已重新生成。
新Supervisor：77880字节，SHA-256
`acfa88876159fdcc79ee2925729aec78e7416d53d9f8922f0ed2a06c03fc319a`。
main.lua仍为 `a4247eff1d15d74f74dddccfae0f135e3ff3fc4dd7424fcf987071bd6fd766e0`。
全部资源哈希见 `build/xiaomi-band-11-4.100.139/manifest.json`。

## 复测

重新打包根目录的一个main.lua和五个.bin，或使用本次生成的资源ZIP作为打包输入。
先重启，再运行新版安装表盘：旧Supervisor及其Manager仍驻留时，重新运行安装器不会替换其代码。
安装完成后打开管理器，检查中文、原生列表、上下滚动、进入模块列表/详情和返回。
本次未要求清空注册表；已有模块数据可以保留。
