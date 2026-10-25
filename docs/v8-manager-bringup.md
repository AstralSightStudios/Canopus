# LVGL v8 Manager bring-up (n66 / n67 / o66)

状态：**o66 上 Manager 页面已能打开并正常绘制**（2026-10-25，仅 Mirage/emulator
证据）。页面经由固件自己的 `page_goto` 压栈，中文标题、分节、状态行（标题 + 右对齐
值）都画出来了。n66/n67 走同一条 v8 后端，但尚未在设备上跑到这一步；所有内容仍是
emulator/static 证据，没有任何真机验证，能力状态不变（restricted / PENDING /
not_probed）。

### 让它画出来的三个条件（2026-10-25）

1. **能力位必须声明。** `canopus_ui_navigation_page` 在 backend 没有
   `CANOPUS_UI_CAP_NAVIGATION_HEADER` 时直接返回 `CANOPUS_UI_ERR_UNSUPPORTED`，
   `canopus_ui_tree_commit` 把它原样传回，`canopus_manager_native_init` 于是失败，
   页面的 `on_create` 返回 -1，固件立刻把页面拆掉——现象就是「页面一打开就弹回表盘」。
   v8 后端现在只声明它真的实现了的 `CANOPUS_UI_CAP_NAVIGATION_HEADER`（
   `lvx_page_title_create` 就是它的页头）。
2. **共享渲染器按能力协商。** `canopus_manager_native.c` 里新增 `native_supports()`：
   没有 `CAP_STYLE` 就不设置标题样式、确认框的文字用 BODY 而不是 DESCRIPTION；没有
   `CAP_EXTENDED_COMPONENTS` 就把确认框画成普通分节而不是 DIALOG。v9 后端声明全部
   能力，行为不变（host 测试全绿）。
3. **中文要显式设字体。** 新建 screen 上的 label 不继承主题字体，直接显示 .notdef
   方块。`lvx_font_create("MiSans-Regular", size, 0)`（o66 `0xc355d68`）建一次字体，
   再用 `lv_obj_set_text_font`（`0xc2bce64`，即 local style prop 90）逐 label 应用；
   `EVID-STATIC-1028-V8-002` 记录了两个地址的证据（stock 的 `alarm_after10min_prompt`
   就是这么拿正文标题字体的）。

顺带修掉一个潜伏 bug：`target_set_hidden` 原来用 `0x10`，那是 LVGL v8 的
`SCROLLABLE`，`HIDDEN` 是 `0x01`——之前「隐藏」的行其实只是变成了可滚动。


### launcher 条目与页面点击（2026-10-25 第二轮）

- **条目显示位**：`launcher_load_app_info`（o66 `0xc29a1f8`）把 app 描述符 **+0x3C
  字节的 bit1** 拷进 launcher 条目 `entry+16` 的 bit2（`4 * ((*(app+60) & 2) != 0)`），
  `draw_page_with_list`（`0xc29b070`）遍历条目时只画带这一位的；`app_install` 只清
  bit0（hidden）。描述符因此设 `hidden_flags = 0x02`（仅 `.28` 布局）。
  之前“注册不进 launcher”是这一位为 0，条目在列表里但永远不画。
- **行点击**：v8 的普通 `lv_obj` 默认没有 `LV_OBJ_FLAG_CLICKABLE`（bit1），
  行对象加上它之后事件才会到达 `target_dispatch_row`。
- **换页必须延后一拍**：`page_goto`/`page_finish` 会同步跑页面过渡并销毁源页，
  在输入回调里调用它会在回调返回后访问 `on_destroy` 刚清空的上下文（o66 上表现为
  整机复位）。现在路由只记录目标页，由 `lv_timer_create(cb, 1, NULL)` 在下一拍
  `page_goto`/`page_finish`，回调返回时不再碰页面上下文。


### 2026-10-25 第三轮：n66/n67 的 page_goto、点击与打点

- **n66/n67 的 page_goto 记录都是错的**：3.1.32 指向 `lvx_pagemanager_prelaunch_check`
  （`0xc45b588`），3.1.190 同样的函数（`0xc449bc4`）。真正的
  `goto_page_no_check_stacktop` 是 3.1.32 `0xc4b456c`、3.1.190 `0xc49e274`
  （`EVID-STATIC-9132-002` / `EVID-STATIC-9190-002`）。修正后 n66/n67 的
  OPEN_MANAGER 不再复位设备，但页面仍未出现。
- **Mirage 上三台 v8 的交互能力不同**：n66 的安装器表盘按钮吃触摸（Run 可用），
  o66/n67 的表盘按钮不吃触摸；系统 UI（表盘上滑、卡片）在 o66 上吃触摸。
- **行点击没有到达回调**：o66 的 Manager 页能创建（打点 `I0`）、能显示，但 3 次点击
  「模块/安装软件包」都没有触发 `target_dispatch_row`（打点既没有 `E` 也没有 `C`），
  即使已补 `LV_OBJ_FLAG_CLICKABLE`（该固件自身用 flag 2 让返回键可点，位值一致）。
  怀疑点：这个固件的输入由自带的 input manager 分发，只认它自己的 `lvx_*` 控件（股票
  页面都用 `lvx_list_item`/`lvx_page_content_create`），裸 `lv_obj` 收不到点击。
  下一步要么找到 input manager 的分发规则，要么把行改成 stock 列表控件。
- 打点工具：`-DCANOPUS_TARGET_UI_DIAG` 编译时**每个标记建一个文件**
  `/data/canopus/dm-<两个字符>`（构造器 `ST`、页面创建 `I<页号>`、点击 `C<行>`、
  事件原始字节 `E<行>`、页查找 `F<序号>` + `Y/N<序号>`、推送 `K`/`X`）。
  不能往同一个文件里追加：这个 VFS 的写从 0 偏移开始，第一版每次覆盖，只剩最后一个标记。设备上只有像 n66 那样能跑 Run 的表盘才加载
  Supervisor，所以打点前必须先让 Supervisor 起来。

### 2026-10-25 第四轮：3.1.32/3.1.190 的页面根与标题

- 打点确认 n66 的页 **能创建**：`context→client init→open→refresh→select view→
  native init→apply 完成`（标记 `c1 l1 o1 q1 v1 n1 Z1 I0`）。`I0` = on_create 跑完、
  页面进入 interactive。
- **页要显示，必须在固件给的容器上建 stock 页标题**：把标题换成普通 label（页面照样
  `I0`、照样 interactive）时 o66 的屏幕永远停在表盘。所以现在：有容器 → 用容器 +
  `lvx_page_title_create`；没有容器 → 自建 screen + stock 标题。
- 3.1.32 上 `lvx_page_title_create` 建在这个容器上**不返回**（打点 `T0` 有、`T1` 没有，
  页面被拆、屏幕变黑）。这是 n66/n67 目前唯一的拦路点。
- n66 上页面创建完成后**屏幕仍显示表盘**：`goto_page_no_check_stacktop` 找到了页
  （`page_lookup` 打点 `Y0`/`Y3`），on_create 也跑完（`I0`），但显示没切过去——
  3.1.32 在 quickapp 表盘之上叠加 native 页的行为还没弄清；下一步是看 `0xc4b456c`
  里 `sub_C45EA4C`/`sub_C45BB5C` 的条件。
- 触摸链路另有发现：这颗固件给我们的点击是 **`SHORT_CLICKED`(4)**，不是 `CLICKED`(7)
  （诊断探针收到 `0/3/4/5/9` 全套，没有 7）。行回调已同时接受 4 与 7。清掉根对象的
  bit4（当 `SCROLLABLE` 用）会让页面完全不显示，说明这颗固件的 bit4 不只是滚动位，
  已回滚。
### 版面约束（三块屏尺寸不同）

n66 192×490、n67 336×480、o66 212×520；`scripts/build_canopus_supervisor.sh` 按
target 传 `-DCANOPUS_MGR_SCREEN_WIDTH`。行高 34、行距 2、标题 18 px、值 14 px，
右对齐的值按宽度截断（UTF-8 安全），这样一屏 11 行（系统信息 8 行 + 管理 3 行）能
全部显示。**没有滚动**：行数再多的页面（模块很多时的列表）会超出一屏，需要另找
v8 的滚动入口。

## 已核对的事实（精确 IDB + 固件日志）

### 控件地址（o66 3.1.28，全部逐条核对）

| 名称 | 地址 | 依据 |
| --- | --- | --- |
| `lv_obj_create` | `0xc2bc5b8` | `lv_obj_class_create_obj(&lv_obj_class 0x2c686a74, parent)` + `init` |
| `lv_label_create` | `0xc347b8c` | `&lv_label_class 0x2c6d643c` + `init` |
| `lv_label_set_text` | `0xc34db6c` | 文本重分配 + invalidate |
| `lv_obj_set_size` | `0xc2bcc22` | 样式属性 1/2 |
| `lv_obj_add_flag` / `clear_flag` | `0xc2bb826` / `0xc2bb8b2` | flags 位操作 |
| `lv_obj_align_to` | `0xc2bd39c` | 对齐枚举 |
| `lv_obj_add_event_cb` | `0xc2b7c40` | 锁对象 + `0xc31ec76` 挂事件（四参数） |
| `lv_img_create` | `0xc22d4f4` | `lv_image_class 0x2c6a69ac`（name = "image"） |
| `lv_image_set_src` | `0xc34ce10` | 函数内断言串就是 `lv_image.c:lv_image_set_src` |
| `lv_bar_create` | `0xc341d04` | `lv_bar_class 0x2c6de644` |
| `lv_bar_set_range` / `set_value` | `0xc341e18` / `0xc341da4` | 用同一个类，min/max 与 clamp 语义 |
| `lvx_page_title_create` | `0xc4f9364` | 自身日志串 `lvx_page_title_create` |

原始记录里 `lv_obj_create`、`lv_obj_add_event_cb`、`lv_img_create`、`lv_bar_create`
四条是错的（分别指向字符串拷贝、绘制例程、单例 getter、字段释放），已在 `826ed30`
和 `6f6bd8f` 修正。

### launcher 与导航

- `launcher_load_app_info`（`0xc29a1f8`）读取 app 描述符的 `+8`（包名）、`+0x0c`
  （图标路径）、`+0x10`（u16 app id）、`+0x12`（标志）、`+0x1c`（显示名回调）、
  `+0x3c`（hidden 位）。我们的 `.1028` 布局与此一致。
- `launcher_page_insert_icon`（`0xc29b21c`）开头检查 `dword_200AB2EC`（launcher UI
  handle），为 0 时只打日志 `launcher UI not created` 并返回。安装器从表盘页跑 Run
  时该 handle 一定还是 0（日志可证），所以条目插不进去。
- 该 handle 只在 launcher 页面被创建时写入（写点在 `sub_C29B070`），也就是用户
  第一次打开应用列表之后。
- `FW_ACTIVITY_NAVIGATE` 在 o66 上是 `show_app`（`0xc44d504`，日志 tag
  `pagemanager`/`show_app`），不是 `page_goto`。用 app id（`0xCA`）和
  `(app<<8)|page`（`0xCA00`）两种 key 调用都得到日志 `show_app: page == NULL`；
  从表盘回调或 UI 定时器里调用都会让 UI 直接死掉（屏幕全黑、点击不再唤醒，QEMU
  进程仍在）。
- `launcher_reset_order_info`（`0xc4fc508`）不检查 launcher UI 是否存在，从安装器
  调用后下一次打开 launcher 会复位整机；已从 v8 安装路径移除（`1bc1e8a`）。

## 尚未完成的部分与推荐实现

唯一剩下的功能缺口：**让 launcher 条目在 launcher UI 存在之后发布**，之后由固件
自己的 launcher 打开 Manager 页（不再依赖我们调 `show_app`）。

建议实现（本会话中写过一版，能编译，未验证，已回滚）：

1. `sdk`/protocol 增加 `CANOPUS_CMD_PUBLISH_LAUNCHER = 30`；
2. Supervisor 增加可选平台钩子 `publish_launcher`：0 = 已发布（本次或更早），
   1 = launcher UI 尚未建立，负值 = 失败；
3. v8 后端实现：`app_lookup(app_id)` → 检查 `*(uint32_t *)0x200ab2ec != 0` →
   `launcher_load_app_info` + `launcher_page_insert_icon`，成功后置位 `launcher_published`；
4. 三个 v8 表盘在 Run 成功后启动 `lvgl.Timer`（v8 绑定上可用，已在设备上实测）
   每 5 秒发送一次该命令，收到 0 后停止并更新状态文案。

验证需要的条件（当前环境无法满足）：

- 一台能稳定打开一次应用列表的 v8 设备（真机最直接）；
- 或在 Mirage 上修好 v8 的息屏/触摸时序（滑动手势后约 1 秒息屏，`tap` 只能唤醒
  且被吞掉），或让 o66 设备的 NSH 控制台不被启动日志卡死。

## 已排除的路径（不要再走）

- 从表盘回调或定时器调用 `show_app`/`FW_ACTIVITY_NAVIGATE`：UI 必死。
- 依赖 `launcher_reset_order_info` 刷新 launcher：会导致复位。
- 在安装器 Run 里直接插入条目：launcher UI 未建立，静默空转。
- 改小图标（64×64）以避开崩溃：无效。

### 2026-10-25 第五轮：点击终于到达（事件码是字节）

- **行确实收到了事件**，之前三次「没到达」是读错了事件码的位置：`target_dispatch_row`
  原来把 `event+8` 当 32 位整数读。o66 上每次点击把 32 字节打出来（`dm-E<行>`）后
  可以看到 `+8` 处是 `1b 00 35 3c`、`07 00 35 3c` 这种形状——**只有低字节是事件码**，
  高两个字节属于相邻字段。改成按字节读之后：
  - `dm-U<行>` 全部出现（说明 `active`/`interactive`/`rows[行]` 三个前置条件都成立）；
  - 点「模块」行出现 `dm-C8`，点「安装软件包」出现 `dm-C9`（`07` = `LV_EVENT_CLICKED`）。
- **行号不是屏幕顺序**：`target_find_row` 按 (kind, key) 匹配槽位，o66 概览页上
  「模块」= 槽 8、「安装软件包」= 槽 9、「导入手机推送的模块」= 槽 10。行高 34、
  间隔 2（`lv_obj_set_size` 生效，实测每行 36 px）。
- **两个固件的 `lv_obj_t` 不一样**（对打点很关键）：o66 的 `lv_obj_align_to`
  (`0xc2bd39d`) 用 `obj+0x1c..0x2b` 的四个 **int32** 作 x1/y1/x2/y2；n66 的
  `lv_obj_align_to` (`0xc278bdd`) 用 `obj+0x14..0x1b` 的四个 **int16**。

### 2026-10-25 第六轮：行点击 → 子页面（o66 已跑通）

- **事件码按字节读之后，点击直接连通**：点「模块」（槽 8）→ `dm-j2`
  （`CANOPUS_MANAGER_ROUTE_MODULES`）→ `page_goto` 压入 page 1 → 打点 `dm-I1`，
  屏幕出现「模块 / 已安装模块」子页面。截图证明子页面标题、分节都正常。
- **换页不需要延后一拍**：`page_goto` 在这一代固件上是压栈（源页只是暂停），
  源页对象不会被销毁，所以在点击回调里直接推页是安全的。之前那版
  `lv_timer_create(cb, 1, NULL)` 延后方案已整段删除（打点 `dm-k1` 显示定时器
  建出来了却从不触发，o66 记录里的 `lv_timer_create` 实为跳进未映射区的 thunk）。
- **行内文字的位置要抵消 22 px 的容器内边距**：`lv_obj_align_to` 用 ref 的
  *内容区* 定位，这个固件的 `lv_obj` 水平方向内边距是 22 px，所以左右贴边要
  传 `8-22` 和 `22-8`。实测（`dm-Gt`/`dm-Gv`）：标题 x1=8、宽 102；值 x2=203、宽 85，
  正好落在 212 宽面板的 8 / -8 边界上。

### 2026-10-25 第七轮：o66 全流程可用；n66 的拦路点在「在页面容器上建子对象」

- **o66 全流程（emulator）**：开机 → Manager 概览 → 点「模块」→ 模块子页面 →
  返回手势 → 回到概览，屏幕全程正常，打点 `I0 I1 KP`。
- **返回手势曾让整机复位**：`on_destroy` 里在页面过渡中调 `canopus_client_close`
  做设备 I/O，返回手势触发销毁时整机重启（截图看到 HyperOS 开机 logo）。改成
  会话进程内常驻（不再在 on_destroy 关闭）后，返回手势稳定。**这是 o66 之前
  「来回操作就黑屏/复位」的根因。**
- **n66 精确到调用**：`-DCANOPUS_MGR_TITLE_PLAIN` 分支里，打点 `T0` 之后，
  接下来对页面容器做的子对象创建/初始化就再没有返回（T2/T3 都没有），
  现象是整机重启（截图见小米开机 logo）。stock 的 `lvx_page_title_create`
  在同一个容器上也是同样症状。下一步要在两次渲染之间做区分：该页第一次
  apply 曾经成功过（早期一版截图里能看到探针方块），说明容器状态在第二次
  渲染时已经不同。
- **n67 标题 ABI**：3.1.190 的 `lvx_page_title_create` 是五参（第五个是返回键
  event 的 user data，stock 传 0），已按 `-DCANOPUS_MGR_TITLE_USER_DATA` 调用。
- **n66 的根因不在标题，在字体**：用 `-DCANOPUS_MGR_NO_TITLE` 跳过整个标题块
  （打点 `T9`）后，页面仍然立刻黑屏，而且再没有任何行级打点——说明卡住/复位发生在
  第一次 label 创建或它的文本刷新上。n66 里能找到 `dsc->font == NULL`、
  `Invalid font descriptor`、`Create font(%s, size %d, style %d) failed` 这些
  字体管理器的字符串，`lvx_font_create_core` 在 `0xc29b028`（日志 tag
  `ext/lvx_font_manager.c`）。o66 上同样的问题是用 `lvx_font_create` +
  `lv_obj_set_text_font` 显式给每个 label 设字体解决的；n66 缺这两条记录，下一步
  就是补上（地址可从 `0xc29b028` 及其调用者的风格属性设置函数恢复）。
- **n67 装上了但没人打开页面**：在真正的活动安装目录（`/app/watchface/market/366240001`，
  之前几次都部署到了 `366240003`，那个目录并不生效）跑 Run，打点是
  `ST i0 iy`——Supervisor 起来了、Manager 应用已注册、launcher 载入+插入都返回非负，
  但**没有任何 `c1`/`I0`**：页面根本没被创建。3.1.190 的框架不像 o66 那样在安装后
  自动打开应用页，而安装器的 5 个步骤里没有 OPEN_MANAGER。所以 n67 要走 launcher
  入口（或补发送 OPEN_MANAGER）。
- **o66 上离开 Manager 页没有可靠手势**：模块子页面上返回手势可用（已验证），但
  概览页（根页）上返回手势和 home 键都不回到表盘、屏幕直接变黑，所以还没能在
  o66 上用真实手势走到 launcher 去核对「更多」里的条目。

### 第八轮：3.1.32/3.1.190 的字体记录已补，但崩溃不在字体

- 给两代 Band 9 固件补了字体记录（`lvx_font_create` + 属性 90 的本地样式设置器，
  `EVID-STATIC-9132-V8-002` / `EVID-STATIC-9190-V8-002`），生成器新增可选映射
  `FW_LV_OBJECT_SET_LOCAL_STYLE_PROP`，后端在没有 `lv_obj_set_text_font` 时走这条路径。
  三台 verifier 全 PASS，`target generate --check` 与 host 测试全绿。
- **但它不解决 n66 的崩溃**：不带字体时（`-DCANOPUS_MGR_NO_TITLE` 那版）同样在第一次
  label 创建就复位，说明崩点在 `lv_label_create` 本身（或它的 class init），而字体是在
  之后才应用的。`0xc28f5f4` 与 label class（构造器 `0xc295624`）本身反编译都对得上，
  所以问题更可能在「在页面管理器给的容器上初始化 label」这一步。
- o66 上按用户的真实路径（表盘上滑到底 → 更多）已经能走到应用的「更多」页
  （截图：日程/待办/呼吸放松），但还没翻到底确认 Manager 条目；o66 的 Mirage 实例
  在这之后又进入黑屏不响应的状态。

### 第九轮：n66 的 Run 现在一按就黑屏，先要修的是这条链路

- 今天早些时候 n66 上 Run 能跑完（`Run completed` + `ST i0 iy` 打点）；现在同一套
  部署按 Run 后 2 秒内屏幕变黑，且 `/canopus` 里没有新打点（`dm-*` 一个都没有）。
  交叉验证过：同一台设备的**上滑打开应用列表是正常的**（mean 56.4 的应用列表截图），
  所以触摸链路本身没坏，是按下 Run 之后的那条路径出问题。
- 做过两次 A/B：去掉字体（`CANOPUS_MGR_NO_FONT`）、去掉 PLAIN 标题分支，都不改变
  结果——所以不是这两处新增代码引入的。下一步要在 Run 按下后立刻抓一帧（1 秒内）
  分辨是「页面被关掉变成黑屏」还是「整机复位」：前者看 `stdout.log`/uart 有无重启
  记录，后者看是否出现 MI/HyperOS 开机 logo。
- 结论：n66/n67 的页面渲染问题（第一次 `lv_label_create` 就复位）仍然没有进展，
  launcher 条目也还没在设备上看到。o66 的 Manager 本体（概览、模块子页面、返回）
  是唯一确认可用的部分。

### 第十轮：n66 复位是 stage-0 NSH `exec` 的 UsageFault（有崩溃日志）

固件自己在 `/log/minidump/crash*.log` 里留了完整转储。按 Run 后 2 秒内出现：

```
[35] [touch] touchpad_read: released          (x=95 y=367 = Run 按钮)
[86] [ap] arm_usagefault: PANIC!!! Usage Fault:
CFSR: 01000000  (UFSR.UNALIGNED)  task: system  0xc1cad6f
PC: 0c1c956e  LR: 0c1c956d  R0: 5a5a5a5a  R1: 200d0040
backtrace: 0c1684b6 0c1671e0 0c13e4e6 0c16b92a 0c16a6b4 0c1c956c 0c1c956a 0c1c6db8
```

- `PC 0x0c1c956e` 落在 `sub_C1C9528`——NuttX shell 的 builtin/exec 派发
  （上下文字符串 `nsh: %s: argument invalid`、`Calling %p`，`BLX R5` 在 `0xc1c956a`）。
  也就是说：**崩在 NSH `exec` 调用我们写进 SRAM cave 的那段 stage-0 探针之后**。
- `R0 = 0x5a5a5a5a` 正是安装器里的 `STAGE0_ENTRY_PROBE_MARKER`（`main.lua:33`），
  探针确实被执行过；随后 `R1 = 0x200d0040`（探针要写 result 的地址）。
- 结论：问题在 **stage-0 cave trampoline / profile 的 exec 目标**，不在 Manager 的
  渲染代码里；这也解释了为什么去掉字体、去掉 PLAIN 标题都不改变行为，以及为什么
  `dm-*` 打点写不进去（触发点在 Supervisor 被加载之前）。
- 下一步：核对部署到设备上的 `canopus_loader_profile-*.bin` / `stage1` / `stage2`
  与本次构建产物是否一致（构建日志写 `stage1=417 stage2=3560`，设备上是 1452/3576），
  以及 `profile.exec_handler + 0x45` 在这台固件上是否真的是可执行边界。
- **体积就是触发条件**：补掉 `target_route_schedule` 里只在 diag 构建用到的局部变量
  （非 diag 构建原本直接 `-Werror` 失败）后，3.1.32 的 Supervisor 从 115248 B 降到
  113916 B，同一次 Run **不再产生新的 minidump**——stage-0 的 `exec` UsageFault 消失。
  说明这条 cave trampoline 对镜像体积/Umum 分配很敏感，产物必须按发布体积（非 diag）
  来测。下一次要在设备上确认「无 fault 之后」页面是否真的被创建。
- **修掉 stage-0 之后露出真正的第二个崩溃**：体积降到 113916 B 后，同一次 Run 里出现的是
  **MemManage Fault（IRQ 3）**，backtrace 落在我们模块自己的地址段
  （`0x0c3d14b8 0x0c4f455a 0x0c3cd998`），不再是 stage-0 的 NSH exec。也就是说：
  之前那条 `exec` UsageFault 把真正的页创建崩溃挡住了。下一步直接按这条 backtrace
  对模块的链接图定位（模块运行时基址 + 偏移即可映射回 `canopus_manager_target_lvgl_v8.c`
  的哪个函数），重点仍是 `lv_label_create` / 字体属性那条路径。

- **faulting PC 精确对上 profile 里的 `exec_handler`**：`bootstrap.toml` 的
  `exec_handler = 0x0C1C9528`，而崩溃 PC 是 `0x0c1c956e`＝`exec_handler + 0x45 + 1`。
  安装器的 stage-0 探针（`main.lua` 里的 `call_mailbox`）把 `profile.exec_handler + 0x45`
  当作第三个字面量，探针 `bx r2` 直接跳到这里；在 3.1.32 上这个地址落在
  `sub_C1C9528` 的 `BLX R5` 之后的 `B loc_C1C955A`（0xc1c956c），也就是**函数体中间**，
  跳进去会执行到 `MOV SP,R7; POP {R4-R7,PC}`，弹出伪造的返回地址 → 未对齐访问 panic。
  3.1.190 的同一地址此前已被判定「不是指令边界」，3.1.32 这里是同类问题。
  这和「体积降到 113916 B 后 stage-0 fault 消失」并不矛盾：探针结果是否触发 panic
  取决于弹栈内容，体积变化改变了栈布局。
- 因此 n66/n67 首先要修的不是 Manager，而是 stage-0 trampoline 的返回方式：
  探针应 `bx lr` 返回（或用 `exec_handler` 本体），不要跳进 `+0x45` 的尾部。
- **探针修好后，崩点仍在同一地址**：把探针的 `bx r2` 改成 `bx lr`（只验证 cave 能执行）
  后，同一次 Run 变成 **MemManage Fault**，`PC 仍是 0x0c1c956e`、`R2=0x200d0040`
  （cave_result）。这次是 **trampoline**（`call_mailbox` 的第二张表）最后那条
  `blx` 跳进 `profile.exec_handler + 0x45`。也就是说 3.1.32/3.1.190 上这条
  mailbox 尾部不是可用的续体（3.1.190 早已判定不是指令边界），而 o66(3.1.28) 上是可用的。
- **结论（loader 层）**：要让 n66/n67 装上 Supervisor，必须给这一代固件换一条返回路径
  （`bx lr` 直接回 NSH，并补上原尾部做的收尾，例如 MPU/exec 状态恢复）。这属于
  staged-loader 协议改动，必须连同 stage1/stage2 一起审，不能靠猜。
- **两次 loader 试验都失败，已全部回滚**：把探针改成 `bx lr` 后崩点转移到 trampoline
  自己的 `blx`（MemManage，PC `0x0c1c956e`）；再把 trampoline 尾部的 `bx r3` 改成 `bx lr`
  后，崩溃跑到 **RAM 里的 trampoline 本体（PC `0x3c7c8760`，UsageFault/MemManage）**。
  说明这条 stage0 续体不是简单换个返回指令就能修的：`exec_handler+0x45` 的尾部除了
  返回还承担了别的收尾，必须按 stage1/stage2 协议整体改。三个 v8 安装器的 main.lua
  已 `git checkout` 回滚到已验证的状态（o66 安装路径保持可用）。

### 第十一轮：o66「更多」前 3 页里没有 Manager

- 单次点击「更多」（位置 (106,415)，只有在屏幕已唤醒时有效）可以进入更多页；今天实测到
  的更多页内容是：第 1 页 日程/待办/呼吸放松，第 2 页 世界时钟/手机静音/遥控拍照，
  第 3 页 手电筒/番茄钟/指南针。**这三页里都没有 Manager 条目**（应用 id 0xCA）。
- 操作限制（Mirage）：屏幕 5 秒息屏，一条命令内必须「唤醒 → 多次滑动 → 点击」连续做完；
  间隔超过 5 秒的那次手势会被当成唤醒点击，可能直接点进某个应用（今天的罗盘/呼吸放松
  都是这样被误开的）。因此每次只能验证到固定深度的某一页。
- 结合固件日志里 `[launcher] reset_order_info_list: appid 0x.. show, in more` 的列表
  （0x04–0x3d，没有 0xCA），说明 Manager **没有进入 launcher 的顺序表**：
  `launcher_page_insert_icon` 在安装时（launcher UI 尚未创建）静默失败，
  而 `load_app_info` 只把条目放进 icon 列表，顺序表没有它就不会被画出来。
  下一步要么在 launcher UI 已存在时补插一次，要么把顺序表持久化下来。
- 试过在插入条目后调用 `launcher_reset_order_info` 重建顺序表（并持久化），但这一版**没能在
  设备上验证**（o66 实例当时已经完全不响应），代码已回滚。下次设备可用时，这是最直接的
  一条路：`reset_order_info_list` 正是启动日志里给每个 appid 打 "in more / no in more" 的函数。

### 第十二轮：launcher 条目修好，o66 全流程按用户路径走通

- **修复**：`canopus_manager_native_install` 在 `launcher_page_insert_icon` 之后调用
  `FW_LAUNCHER_RESET_ORDER_INFO`（`reset_order_info_list`），把 icon 列表合并进 launcher
  的**顺序表**并持久化。之前条目只进了 icon 列表，而 `draw_page_with_list` 画的是顺序表，
  所以永远看不到；固件启动日志里那份 appid 列表（0x04–0x3d）也不含我们的 0xCA。
- **用户路径实测（o66，非 diag 构建）**：表盘主页上滑到底 → 「更多」→ 翻到最后一页
  （番茄钟 / 指南针 / **Canopus 管理器**）→ 点进去 → Manager 概览（框架 / 管理器 / 固件 /
  构建 / 目标设备 / 注册表 / 文件系统错误号 / 已验证保存次数 + 管理分节）→ 点「模块」→
  模块子页面（标题「模块」、分节「已安装模块」、空态「暂无已安装模块」）。
- 截图序列：`za3`（更多末页的 Manager 条目）、`ze2`（从 launcher 打开的概览）、
  `zj1`（模块子页面）。

### 第十三轮：n66 的 entry_mode 是 mailbox，切到 cave 后崩点前移

- 部署到设备的 profile 里 `entry_mode = 'mailbox'`（bootstrap.toml 没写这一项，生成器默认
  mailbox），而 o66 的 bootstrap.toml 写的是 `entry_mode = "cave"`——这就是为什么 o66 能装、
  n66 一按 Run 就崩：mailbox 路径的探针/trampoline 会跳进 `exec_handler+0x45`（函数体中间）。
- 给 n66 的 bootstrap.toml 补上 `entry_mode = "cave"` 后（profile 已生成 cave），崩溃**变了**：
  `MemManage Fault, CFSR 0x00020082, MMFAR 0x200d0000`，PC `0x3c7c8760`。0x200D0000 正是
  profile 的 `cave`（`cave_result` = 0x200D0040），说明 `stage0_exec_with_mpu` 给 stage-0
  区域编程的 MPU 属性没有覆盖到 trampoline 访问的那个地址。
- 下一步：核对 `reserve_stage0_region`/`stage0_exec_with_mpu` 写的 MPU 区域（base/size/attr）
  与 cave 的实际范围，把 `cave`+`cave_result` 一起覆盖进去。

### 第十四轮：n66 cave 模式的 MPU 故障（下一步的入口）

- 三台的 stage-0 几何完全一致（`exec_size = 0x40`、`result_word = base + 0x40`、`stage0_region = 7`），
  而 o66（base 0x200E0000）可用、n66（base 0x200D0000）在 `exec` 后报
  `MemManage CFSR 0x00020082 MMFAR 0x200d0000`。所以不是区域尺寸算错，而是
  n66 上这套 MPU 编程没有真正覆盖 cave：
  - 待查 1：`mw 0xE000ED98/9C/A0` 在 3.1.32 的 NSH 任务里写入的是哪个安全态的 MPU
    （Secure 与 Non-secure 的 MPU 是两套），以及 `exec mpu_sync` 后是否还需要 ISB 之外的同步。
  - 待查 2：cave 基址 0x200D0000 在 3.1.32 上是否已经被固件自己的区域占用（n67 用 0x20080000、
    o66 用 0x200E0000，只有 n66 落在这个区间）。
- 试过把 stage-0 的 MPU 区域按 `cave + stage1 大小` 扩大（`stage0_mpu_values(span)`），
  n66 上的崩溃**完全没变**（`CFSR 0x00020082 / MMFAR 0x200d0000 / PC 0x3c7c8760`），
  说明故障不是区域太小，而是**已加载的 Supervisor 代码（PC 在 0x3c7c87xx，即它的 .text）
  访问了 0x200D0000**——更像是重定位/段落放置落到了 cave 区间。该安装器改动未验证，
  已回滚以保留 o66 已验证的状态。
- 用带打点的构建重跑 n67（当前代码）：`ST i0 iy c1 l1 o1 q1 v1 T0` —— 安装完成、页面创建、
  走到标题创建 `T0` 后仍不返回（`T1` 缺失），和 n66 的「label/标题创建在页面容器上不返回」
  是同一类问题，两代 Band 9 固件都卡在这里。
- 在 n67 上换用 PLAIN 标题分支（不调 stock 标题，改为「普通容器 + 标签」）后，打点仍停在
  `T0`，连 `T2`（容器创建后的标记）都没有——和 n66 一样：**在这两代固件上，往
  page manager 给的 root 上创建任何子对象都不返回**。o66(3.1.28) 上同样代码正常，
  所以问题在这一代 `lv_obj_create(parent=页面容器)` 的 class init/refresh 路径，
  下一步必须换成固件自带的容器 API（例如 `lvx_page_content_create`）来承载内容。
- 下一步的具体落点：三台目标里都已经有 `lvx_page_content_create` 的符号记录
  （`targets/*/symbols/*.ui.lvx_page_content_create.json`），但生成器还没有把它映射成宏。
  下一轮要做的就是给 `V8_TEXT_FONT` 之外再加一条可选映射 `FW_LVX_PAGE_CONTENT_CREATE`，
  只在 3.1.32/3.1.190 上把内容挂到它返回的容器里（o66 保持现有已验证路径不变）。
- 试了 stock 容器 API：`lvx_page_content_create(root)` 在 3.1.190 上**同样不返回**
  （打点 `r0` 之后没有 `r2`/`r3`）。结合前面的现象——把标题、标签、普通对象挂在 page
  manager 传入的 root 上都挂死——结论是：**这两代固件在 on_create 里给的 root 不能当作
  普通 LVGL 父对象使用**。下一步要弄清 on_create 第二个参数的真实类型（很可能不是
  lv_obj），stock 页面都是从别处拿容器（例如 `page->parent_descriptor` 或页内 signal）再建 UI。

### 第十五轮：n66 的 0x200D0000 归属定案；cave 区域泄漏修复；两代 Band 9 的 root 结论被推翻

**MPU 归属（n66 3.1.32，guest 侧实测）**：安装器用 `mw` 逐字读回 MPU 寄存器，拿到运行期完整视图：

- `MPU_TYPE=0x0800`（8 region）、`MPU_CTRL=0x0005`（ENABLE|PRIVDEFENA）、`MAIR0=0x00447722`。
- 常驻 region：`r0` `0x00000000..0x0001FFFF`、`r1` `0x0025C000..0x003BBFFF`、
  `r2` `0xD0200000..0xD020FFFF`、`r3` `0x200686C0..0x2006A9BF`（SRAM 文本）、
  `r4..r6` 空闲、`r7` 是 stage-0。
- 这套 PMSAv8 的编码与固件 `mpu_configure`（`0xC52272C`）一致：**RBAR[2:1]=AP、RBAR[0]=XN**，
  **RLAR[2:1] 选 MAIR 属性、RLAR[0] 使能**；`PRIVDEFENA=1` 让未被 region 覆盖的地址对
  特权态可读写执行。安装器的 `mpu_rbar_attr`/`mpu_rlar_attr` 与此完全对得上。
- **`0x200D0000` 就是被 stage-0 的 region 7 覆盖的 cave 基址**，安装器在 `exec` 前把该 region
  收窄成 **RO+X**（`RBAR=cave+6`、`RLAR=cave+0x23`，64 字节）。对 cave 基址的写入因此
  MemManage（DACCVIOL，`MMFAR=0x200D0000`，任务名 `system -c mw`）——这就是 n66
  「一按 Run 就崩」的确切形态（崩溃转储 `crash1999123100001x.log`：R5=0x200d0001，
  PC 在 NSH 的 exec 派发里）。
- **region 7 会泄漏**：任何一次 cave `exec` 没有正常返回（stage1/stage2 卡住或崩溃），
  排在 `exec` 之后的释放命令就不会执行，region 7 以 RO+X 留在整个 boot 期间；跑在
  后续 `system()` 任务里的下一次 Run 读回 `r7 rbar=200d0006 rlar=200d0023` 就是证据
  （本固件不按任务重编程区域）。于是下一次 Run 的 `mw 0x200d0000=...` 直接踩 MemManage。
- **修复**：三个 v8 安装器在**每次写 cave 之前先 `stage0_mpu_release()`**（`mw rnr=7;
  mw rlar=0; exec mpu_sync`），写完再按原流程重新 arm。n66 实测：Run 从 stage0 → stage1
  → stage2 → Supervisor → `/dev/canopus` → CPC2 INSTALL 0/2、RESTORE_AFTER_BOOT、
  INSTALL 1/2 全部完成，`/data/canopus/registry.bin` 写回 `CDR1`。

**「页面 root 不能当 LVGL 父对象」的结论不成立**（推翻第十四轮末的判断）：

- stock 代码就是拿页面容器当父对象：番茄钟的 `sub_C2BFFB4(page_container, data)`
  直接 `lvx_page_title_create(a1, text, mode, cb)`，而 n66 的
  `lvx_page_title_create`（`0xC4C5F60`）第一件事就是
  `lv_obj_class_create_obj(&title_class, a1)`——第一个参数就是 LVGL parent。
- 之前「试 stock 容器 API 也不返回」的两次实验用的地址是**猜的**，不是 stock 函数：
  3.1.32 的 `lvx_page_content_create`（`0xC430CDC`）来自 Band 10 Pro 的 ensemble 匹配；
  3.1.190 的 `0xC4177AA` 是 3.1.175 移植猜测，反编译是
  `*(a1+588)=sub_C433DB4(a2); return sub_C417784(a1)`，根本不是创建函数；3.1.190 的 IDB 里
  也没有 `page_content_create` 这个字符串。**这两条记录都不能当 API 用**（`FW_LVX_PAGE_CONTENT_CREATE`
  在 3.1.190 上仍然是错的映射）。
- 因此 n66/n67 页面内容缺失要按「创建子对象时真正 fault」继续查，而不是「root 类型不对」。

**卡住的位置**：n66 装完 Manager 后，框架打开应用页（日志
`[pagemanager] on_ui_create: create view ...:{202, 0}` + `insert_app_to_list: pkg =
com.canopus.manager, start_source = 10`）时 miwear 任务 MemManage：
`CFSR=0x82 MMFAR=0x0d`、`PC=0xC272254`（`lv_obj_class_create_obj` 里把新对象写回节点），
调用链 `0xC26D37D`（`lv_obj_create`）→ `0xC2742ED` → `0xC272255`，**parent 参数是 5**。
同一个应用页还要读 `/resource/app/app_common/lvx_xiaomi_logo.bin`（应用启动动画），
该文件在 Mirage 的完整 n66 固件包里不存在（`app.bin`/`misc.bin`/`system.bin` 都没有
`app_common` 条目），动画解码连续报错后就踩到空节点。stock 应用（运动记录）打开正常，
说明这条动画只在系统 app（app id ≤ 0xFF）路径上跑。下一步要用可调试的 QEMU
（手动带 `-s` 起同一台设备）在 `0xC26D370` 上下断点，确认 parent=5 是框架还是我们传的。

### 第十六轮：n66 管理器页画出来了（root 参数不是容器，容器在描述符里）

- 用 QEMU gdb stub（手动带 `-s` 起同一台设备，`lldb` 连 `connect://127.0.0.1:1234`）在
  `lv_obj_create`（`0xC26D370`）上设条件断点 `$r0 < 0x1000`，抓到调用现场：
  `r0=5`、`lr` 落在 Supervisor 镜像里（`.text+0xC3F8`，`target_ui_apply` 的
  `CANOPUS_MGR_TITLE_PLAIN` 分支 `object_create(backend->root)`）。
- 同时读 `canopus_manager_target_record`（`.data`）确认：`create_count=1`、
  `root_object=5`——**固件传给 `on_create` 的第二个参数就是 5，不是容器**；而
  固件自己刚创建的 view 存在页描述符的 **+0x2C**（日志 `create view 0x3c861338:{202, 0}`
  与描述符里的值一致）。
- 修复：3.1.32/3.1.190 的构建加 `-DCANOPUS_MGR_PAGE_ROOT_SLOT=0x2c`，`on_create`
  改用描述符 +0x2C 里的 view 作为内容根（o66 保持原来的参数）。n66 实测：Run 做完
  stage0→Supervisor→CPC2 六步后框架 `on_ui_create` 建出 `{202, 0}` 的 view 并**画出了
  Manager 概览**（标题 Canopus、8 行系统信息、管理分节三行，截图帧在 Run 后 1 秒），
  整机不再复位；CJK 仍是 .notdef 方块（n66 的字体应用待查）。

### 第十七轮：n67 也跑完整条安装链路

- 3.1.190 走同一套修复（`-DCANOPUS_MGR_PAGE_ROOT_SLOT=0x2c` + 写 cave 前释放 stage-0 region）：
  在活动目录 `/app/watchface/market/366240001` 部署新产物后按 Run，105 秒左右
  `Run completed`，`/data/canopus/registry.bin` = `CDR1`，日志里 `on_ui_create` 建出
  `{202, 0}` 的 view、随后 `on_ui_destroy: delete view …:{202, 0}` 正常销毁，**无 panic**。
- 操作注意：n67 的 Run 要把 UI 线程卡住近两分钟（`run()` 逐条 `mw`），期间面板会息屏，
  不能据此判断失败；等 `Run completed` 出现再截图。
- 仍未做：n66/n67 上让 Manager 页停在屏幕上、行点击、CJK 字体（现在仍是 .notdef 方块）、
  模块安装/激活在 Band 9 上的实测。这些是下一轮的内容。

### 第十八轮：字体与体积的两个未决项

- **CJK 仍是方块**：n66 上 `lvx_font_create("MiSans-Regular", 14, 0)`（以及 18）确实被调用并
  成功返回，`lv_obj_set_local_style_prop(obj, 90, font, 0)` 也带着有效的 font 指针执行了
  （gdb 断点实测），但中文仍是 .notdef 方块；换成未知家族名（`lvx_font_create` 回落
  `LV_FONT_DEFAULT`）也一样。说明这台固件的 MiSans-Regular 链和默认字体都只有拉丁字形，
  中文要走主题里另一套字体对象。stock 的中文标签用的是哪个字体对象，还没追出来。
- **回滚了**一个未验证的 `-DCANOPUS_MGR_FONT_INHERIT` 开关（让标签继承页面容器的字体）：
  它的 113,624 B 构建在 stage2 读 ELF 时稳定失败，没拿到证据，代码已撤销。
- **Supervisor 体积敏感**：113,932 B 稳定可加载，113,624 B 会在 stage2 `read` 时失败
  （`cannot read staged Supervisor ELF`，文件本身校验一致）。Kmem 里 ELF 输入缓冲只有
  一百多 KB，分配位置随体积变化，落点接近堆尾时读越界；这条限制也解释了早期“改小几百字节
  就换一种崩溃”的现象。后续要么给 ELF 输入加显式的容量检查，要么让 stage2 用更小的
  分块读取，避免整文件常驻 Kmem。
- **字体家族线索**：n66 的 `miwear_font_create(index, size)`（`0xC4BD728`）用一张固定的
  家族表 `off_2C70E6BC`，内容是 `MiSans-Demibold / MiSans-Medium / MiSans-Regular /
  MiSans-Demibold / MiSansF-Demibold / MiSansF-Medium / MiSansF-Semibold`。`MiSansF-*`
  很可能就是带中文的“全量”字体（stock 中文走的不是 `MiSans-Regular`）。下一轮先用
  `CANOPUS_TARGET_FONT_FAMILY="MiSansF-Medium"`（与旧名同为 14 字节，构建体积不变）
  在页面上验证是否出中文；另外 `MiSans-Regular@14/18` 那条链在 n66 上确实只有拉丁字形。

- **字体记录已入库**：`EVID-STATIC-9132-V8-003` + `ui.miwear_font_create`（3.1.32 `0xC4BD728`）
  记录了这个索引表函数。用 diag 打点实测：`miwear_font_create(4, 14)` 与 `(4, 13)` 返回的是
  两个不同的 Umem 字体对象（`0x3c879c28` / `0x3c87a060`），**不是** `LV_FONT_DEFAULT`
  （`0x2C63276C`），说明 MiSansF-* 家族在这台固件上确实存在且可创建。
- **但中文是否真的画出来还没抓到证据**：安装器 Run 会阻塞 UI 线程近两分钟，面板随即息屏，
  Mirage 的 `screendump` 在息屏时是空帧；Manager 页有时会停留（抓到过多次），有时在
  OPEN_MANAGER 之后被框架 `on_ui_destroy` 掉。抓一次「新字体 + 页面停留」的帧是下一轮第一步；
  若 MiSansF 仍不出中文，再往 index 5/6 或主题字体对象上找。

### 第十九轮：n66 管理器中文全通（text_font = 0x57）

- **属性号错了**：这台固件的样式属性表是 `名字 -> (type<<16 | prop)`，`text_font` = `0x30057`
  （**prop 0x57**，type 3 = 指针）。之前用的 90 (`0x5a`) 是 `text_decor`，0x55 是 `text_color`
  ——用 0x5a 时字体指针被当成颜色写进去，暗屏帧里字发绿就是证据；改成 0x57 后中文立刻正常。
  3.1.190 的表项同样是 `0x30057`，两个 Band 9 目标都用 `-DCANOPUS_MGR_TEXT_FONT_PROP=0x57`
  （证据 `EVID-STATIC-V8-TEXTFONT-9132/9190`）。
- **n66 全流程实测（Mirage）**：概览页完整中文——标题 Canopus、分节「系统信息」「管理」，8 行
  系统信息（框架/版本 1/、管理器/原生 UI AB…、固件/3.1.32、构建/N66NFC_3.1…、目标设备/
  xiaomi-ban…、注册表/就绪、文件系统错误/0、已验证保存次…/7）和三行管理项（模块 ›、
  安装软件包 ›、导入手机推送 ›）；点「模块」进入子页面，标题「模块」、分节「已安装模块」、
  空态「暂无已安装模块」。与 Band 10 Pro / Band 11 的版面一致。
- **顺手恢复了一条环境经验**：Mirage 上的安装器表盘要重新部署时，`package_lua_watchface`
  必须带 `--protocol 0x0800`，且**换一个 package id**（同 id 会被设备判成 `Downgrade` 拒绝）；
  bridge 安装用 `MAB_SMOKE <port> <authKey> <state> 'install={"path":…,"kind":"watchface"}'`，
  期间按配对对话框。
- **n67 状态**：用同一套修复（root 槽 +0x2c、text_font 0x57）重装后，Run 走完并写入
  `supervisor.elf`(114,008)/`stage2.bin`/`registry.bin`(CDR1)，全程无 panic；但 3.1.190
  这次的屏幕在整个 Run 期间保持黑屏、唤醒点击也不亮（面板不亮，不是崩溃），Manager 页的
  目视确认还没拿到。下一轮先解决 n67 的面板唤醒（或改用 QMP 直接抓帧），再确认中文与行点击。

### 第二十轮：n67 页面渲染已证（打点），模块链路的真正缺口是传输

- **n67 的页面确实建出来并渲染完成**：清空打点后单次 Run 得到
  `ST i0 iy c1 l1 o1 q1 v1 n1 l1 T0 T1 S0 S1 B0 B1 R4..R7 Z1 I0 Y0 Y3` + `registry.bin` = `CDR1`。
  其中 `T1` 说明 3.1.190 的五参 `lvx_page_title_create` 在描述符 +0x2C 的 view 上成功返回，
  `Z1` 说明我们的渲染跑完。**没有 `E*/U*/C*`（触摸没有到达）**，因为 3.1.190 的面板在 Run 之后
  一直不亮：日志显示 5 秒息屏、watchface 正常 pause、闹钟照常走（系统活着），注入了点击也
  唤不醒。Mirage 的触摸唤醒补丁（`hw/input/hynitron_ts.c`）在源码里、二进制比源码新，
  所以这条要在下一轮单独查（面板/PM 侧），不是 Manager 的问题。
- **激活体验（模块安装/激活）的真正缺口是传输层**：`scripts/build_canopus_supervisor.sh` 对
  `xiaomi-band-9-3.1.32` 打印 `encrypted L2 0x43: no backend`，即这台固件没有 L2 传输后端
  （Band 10 Pro / Band 11 有 `evidence/l2-sram-probe.json`）。Supervisor 自己的命令面是齐的
  （INSTALL/ENABLE/ACTIVATE/SCAN + native ELF 分块上传），缺的是把模块字节送进来的通道。
  两条可行路线：
  1. 补 Band 9 的 L2 后端（需要 `l2-sram-probe` 静态恢复 + 设备探针，K18 那一套）；
  2. 本地安装：安装器把 `/data` 里的 `.canopus` 按 CPC2 上传分块直接写 `/dev/canopus`——
     不依赖 BLE，可在 Mirage 上验证。建议先做 2，再做 1。

### 第二十一轮：o66 回归修复（region 释放只留给 Band 9）

- 上一轮把「写 cave 前释放 stage-0 region」加到了三个安装器，**o66 因此回归**：AUTORUN 一跑就整机
  复位。把这条改动限制在 3.1.32/3.1.190 之后，o66 的 AUTORUN 再次 `Run completed`，
  `supervisor.elf` 114,132 B、`stage2.bin` 3,596 B、`registry.bin` = `CDR1`。
- 记录：o66 的表盘按钮不吃触摸、AUTORUN 又是唯一触发方式，所以 o66 的每次改动都必须用
  AUTORUN 跑一遍才能确认没有回归；另外 o66 安装新表盘**不会自动切换**，要覆盖「当前活动
  目录」（用日志里的 `watchface(<id>)` 确认是哪一个）才会生效。
- **本地模块安装的协议已经查清**（下一轮可直接实现）：`canopus_supervisor_handle_*` 的
  命令分发里，`CANOPUS_CMD_STAGE_BEGIN..STAGE_RES_FINISH` 走的是同一套 CPC2 命令通道
  （`manager/service/canopus_supervisor.c:1188`），也就是**写 `/dev/canopus` 就能喂**，
  不需要 L2。序列（见 `tests/host/test_supervisor_upload.c`）：
  1. `STAGE_BEGIN`，payload = `struct canopus_install_receipt_v1`（magic/version/header_size/
     module_version/artifact_size/module_id/target_id/firmware_sha256）；
  2. 若干 `STAGE_WRITE`，payload = `[u32 offset][分块数据]`，必须按顺序、offset 连续；
  3. `STAGE_FINISH` 无 payload → 校验并发布到 `/data/canopus/inbox/<module_id>.ko.next`；
  4. 再用 `INSTALL`（带 stage token）/`ENABLE`/`ACTIVATE` 完成安装与激活。
  安装器已经有 `exchange()`（CPC2 请求/应答）和读文件能力，缺的只是把这段序列写成 Lua，
  并把一个给 Band 9 编好的模块（`.canopus` 里的 `*.ko`）随表盘资源一起部署。

### 第二十二轮：n66 的模块安装与「已安装模块」列表跑通

- **本地模块安装可用**：给 `watchfaces/module-installer-prod` 补了 Band 9 的设备钩子
  （`device-xiaomi-band-9.lua`：`/dev/canopus` + 36 字节应答 + `mkdir` 准备 inbox），
  `canopus package --installer` 就能为 n66 生成模块安装表盘。实测：安装器把
  `hello_portable` 的 176 字节 CMI2 回执和 7,892 字节 ELF 写进 `/data/canopus/inbox`，
  Supervisor 接受 INSTALL 并写进 `registry.bin`，表盘显示「安装完成，默认禁用」。
- **管理器「模块」页列出 hello_portable**：修了一个真 bug——`on_create` 只切视图、不刷新
  设备模型，而会话是在模块安装之前建立的，所以列表一直是旧的空态。现在每次建页都先
  `target_refresh_model()`，模块页立刻显示 `hello_portable ›`。
- **n67 的模块安装暂缓并留了结论**：3.1.190 的 `build.prop` 只有 screenshape/devicetype/
  screendensity，**没有 ro.build.version**；版本串只在 flash 头部（`0x810` 附近的 "3.1.190"），
  Lua 侧读不到，模块表盘的固件识别无从下手。要做 n67 的模块安装，先得给它一个可读的
  版本来源（例如让 Supervisor 暴露 firmware_version，或安装表盘改从 `/dev/canopus` 查询身份）。

### 第二十三轮：n66 激活链路打通，residency 上的 ACTIVATE 修正

- **槽位不是参数，而是 payload**：v2 命令里 ENABLE/DISABLE/REMOVE/ROLLBACK/ACTIVATE 的槽位由
  `sup_slot_from_module_id(sup, payload, payload_size)` 从 **payload 的模块 id** 解析
  （`canopus_supervisor.c:1232`），不是请求头里的 arg0。空 payload 一律 `DISALLOWED(7)`；
  探针发的 `arg0=0` 全部落在这里，别再按「槽位号」写。
- **n66 全链路实测（Mirage，hello_portable）**：`QUERY_MODULE`（payload = `u32 slot`）→
  CQM1 里 `state=3`（INSTALLED）、`activation_error=0`、id 正确；`ENABLE`（payload = id）→
  `result=8`（REBOOT_REQUIRED），状态变 5（ENABLED）；`ACTIVATE`（payload = id）→ 加载成功，
  状态 9（ACTIVE）。即管理器详情页的「启用 → 立即激活」在协议层与 Band 10 Pro / Band 11 一致。
- **修正：代码已驻留时的 ACTIVATE 不再二次 insmod**。可达路径：模块 ACTIVE → 详情页「禁用」
  （状态 DISABLED_NEXT_BOOT）→「启用」（状态 ENABLED，但代码从未离开内存）→「立即激活」。
  旧行为会在 `sup_load_slot` 里再跑一次 `insmod`，失败返回 `-1` 并把**仍在运行的**模块标成
  FAILED（`activation_error=0xFFFFFFFF`）并释放它的服务；实测 q2 即为 `state=15 act=4294967295`。
  现在 `descriptor != 0 && state == ENABLED && intent == ENABLED` 的 ACTIVATE 直接按
  REMOVABLE→ACTIVE / 否则 BOOT_RESIDENT 报告 COMPLETED，不重复加载。host 测试
  `supervisor_activation_reports_a_resident_module_instead_of_reloading` 覆盖（去掉修正时
  该测试以 `g_loads == 2` 失败，确认用例有效）。
- **Mirage 环境备忘（n66）**：
  - n66 的 `miragectl sh` 不可用（`shell: nsh pid 8` 之后等不到提示符，p67 正常），
    所以 diag 打点要在关机后用 `fs ls <device> nand_data /canopus` / `fs get` 读，
    live 卷与 `dm-*` 清理都得走关机态；`rm` 通过 `os.execute` 在表盘里删不掉 `dm-*`。
  - **安装表盘会换掉当前表盘，但管理器页会留在屏幕上**（新表盘画面被它压住），要让它消失只能
    重启；因此「点详情页」必须在管理器页刚出现的窗口里做。
  - **registry 里有模块时，Run 之后一旦切换表盘，探针就再也拿不到 `/dev/canopus` 的 CPC2 应答**
    （探针只打印 `open_manager -> nil`，还没区分是打不开设备节点还是没有应答）；registry 为空时
    切表盘不受影响（跑完 Run 再装探针表盘，管理器页照常出现）。这一条是激活体验的下一个卡点：
    要么查清模块恢复路径为什么把 Supervisor 带走，要么让管理器页在 Run 里直接完成交互。
  - n66 表盘数量有上限，测试表盘攒多了会 `ExceedQuantityLimit`；同一个 package id 重装是
    `Downgrade`，每次装新表盘换一个 id。bridge 的 `watchfaces` / `removeWatchface` 可以在不
    擦机的情况下清干净（当前表盘不能删）。
  - `miragectl erase` 会重新生成设备身份（serial/did/bt addr），但 `device.json` 里的 authKey
    还是旧的，BT 认证会一直失败；要么删掉 bridge 的 `abstate/` 重新配对，要么 `delete` 后
    `create` 重建设备。
- **管理器点击还没定位完**：diag 打点里 `dm-C0`/`dm-C9`（动作行收到 CLICKED）存在，但
  `dm-j*`（`target_route`）与 `dm-KP`（`FW_ACTIVITY_NAVIGATE`）都不存在，说明点击进入了行
  回调却没有产生路由。给 `canopus_ui_dispatch_event` 的返回值补了 diag 标记
  （`D` + `S`/`A`/`U` = STALE/ARGUMENT/UNSUPPORTED），下一轮先清空打点、只点一次「模块」行，
  看是代次过期还是 key 不匹配；点不动也可能是管理器页本身没拿到输入焦点（页由探针的
  OPEN_MANAGER 建立时会这样），要用 Run 自带的那次页面做对照。

### 第二十四轮：n67 的模块安装通了（固件版本从 Supervisor 拿）

- **不再需要改固件**：模块安装表盘原先靠 `build.prop` 的 `ro.build.version` 选 target，
  3.1.190 的 build.prop 只有 screenshape/devicetype/screendensity。现在设备钩子给了一个
  `device_firmware_version(checkpoint, exchange)`，用 CPC2 `QUERY_DEVICE`（命令 9）读
  Supervisor 自己的 **CQD1 target_id**（`xiaomi-band-9-pro-3.1.190`），取末尾的版本串再查
  `CONFIG.targets`。`version` 为空时不再在启动阶段报「不支持的固件」，改为等 Supervisor
  应答后再判定。
- **踩到的坑：读长度必须 ≥ Supervisor 的整条应答**。`canopus_supervisor_installer_read` 在
  `count < installer_response_len` 时直接返回 -1，而 CQD1 已经是 **88 字节**（旧注释里写的是
  84），所以应答是 36+88 = 124 字节；按 120 读会整条失败、Lua 侧只看到 nil。Band 9 钩子里
  `QUERY_DEVICE_SIZE = 36 + 88`，`failure_detail` 也一并改过来。
- **n67 实测（Mirage，canopus-n67）**：装框架安装表盘（AUTORUN）→ Run 完成 →
  装 `dist/installer/xiaomi-band-9-pro` 模块表盘 → 状态绿字（CJK 是方块，见下）→ 关机读
  `/data/canopus/registry.bin`：`CDR1` + `hello_portable`（version 1、intent disabled）。
  这是 n67 第一次在设备上完成模块安装，且**全程不需要触摸**（表盘创建即安装）。
- **n67 面板并不是完全不亮**：Run 之前它正常显示，配对对话框（AstroBox）也能点确认；只有
  Run 之后才叫不醒（第二十轮的结论仍然成立）。所以 n67 的配对要靠「先点确认再装表盘」，
  否则 bridge 3 秒就判定 `pairing was declined`。
- **n67 的 Lua 表盘画不了中文**：同一段「安装完成」在 3.1.32 上是正常中文，在 3.1.190 上
  是方块；实测 `MiSans-Regular/Medium/Demibold`、`MiSansF-Medium/Demibold/Semibold`
  六种家族以及**不设 text_font 的默认字体**全是 `.notdef`。管理器页的中文来自页面样式属性
  `text_font`（prop 0x57）指向的字体对象，Lua 表盘拿不到那个对象。模块安装表盘的状态文案
  因此在 n67 上是方块（n66 正常），先记录为已知差异，不为此改文案。
- **生成物**：`canopus package --installer` 现在会输出 `dist/installer/xiaomi-band-9-pro/`
  （CLI 与 `build_module_installer_prod.py` 共用 `xiaomi-band-9-pro → xiaomi-band-9` 的钩子
  别名）；`test_module_installer_prod.py` 对「build.prop 没有 ro.build.version」的镜像改为
  要求钩子提供 `device_firmware_version`，并对 ROMFS 里不再以 `ro.build` 开头的 build.prop
  做了正确的定位。

### 第二十五轮：n67 激活链路实测通过，管理器页仍不显示

- **n67 的 ENABLE/ACTIVATE 实测（Mirage，hello_portable，探针把结果写进 `/data/canopus/act.txt`）**：
  `q0 state=3`（INSTALLED）→ `ENABLE` = **8**（REBOOT_REQUIRED），`q1 state=5`（ENABLED）→
  `ACTIVATE` = **5**（COMPLETED），`q2 state=9`（ACTIVE）。关机后 `registry.bin` 里
  `hello_portable` 的 intent 为 1（enabled）。协议与 n66、Band 10 Pro / Band 11 完全一致，
  n67 的「安装 → 启用 → 立即激活」在设备上已经跑通。
- **n67 的面板与 Run**：Run 期间帧稳定在 2274 字节（全黑），`button home` 与点击都唤不醒；
  Run 结束后再点一下就能亮。所以「Run 后黑屏」不是永久性的，只是 Run 那个阻塞窗口里不响应。
- **n67 的管理器页仍然不显示**：探针表盘发 `OPEN_MANAGER`（命令 29）返回 **5 (COMPLETED)**，
  但屏幕上始终是探针表盘自己；第二十轮的打点也证明页面已建好并渲染完（`T1`、`Z1`）。
  也就是说 3.1.190 上页面进了框架的 page manager，却没有被显示出来；下一步要对照
  3.1.28（能显示）和 3.1.32（表盘发 OPEN_MANAGER 也能显示）找出 3.1.190 缺的那一步
  （页面 view 的 attach / 显示切换），这是 n67「管理器体验」最后一块。
- **n67 的 Lua 表盘没有中文**（第二十四轮）：n67 的管理器页即使显示了，中文也只能走页面
  样式属性那条路，表盘层仍是方块。

### 第二十六轮：n66 管理器输入其实没问题，详情页把 app 带走了

- **先纠正一条**：管理器页的行**点得动**。之前判断「点击进了行回调却没有路由」是因为坐标算错了——管理器页的行是按 42 px 间距（34 行高 + 8 间隙）从 y≈369 开始排的，不是按截图里文字的位置。
  实测（192×490）：`模块` 行中心 **y≈385**、`安装软件包` **y≈421**、`导入手机推送的模块` **y≈457**；
  点「模块」进去是「已安装模块」子页（空态「暂无已安装模块」）。点错到 421 那一行时页面会重绘但不跳转，
  因为「安装软件包」在没有待安装包时是 disabled——那时 diag 打出的是 `DE`（dispatch 返回 DISABLED），
  这也解释了上一轮那句「有 C 没有 j」。
- **模块列表页与详情页**：列表页正常，列出 `hello_portable ›`（第二十二轮的结论在 UI 上复现）。
  但**点进详情页会把 app 带走**：屏幕黑掉、几秒后安装表盘（当前表盘）又回来，`uart1` 里没有 panic，
  Mirage 的 QEMU 进程也没重启——是应用层崩了后被框架重启。也就是说「启用 / 立即激活」在 UI 上
  还进不去，**这是激活体验最后一块**。
- **下一步（有明确入口）**：用 `-DCANOPUS_TARGET_UI_DIAG` 构建重跑同一路径，点模块行后读 `dm-*`：
  详情页正常的话应看到 `c1 q1 v1 n1 I2`（`I` 后面是页号 2，DETAIL）；缺哪一步就是崩在哪。
  同时看 `target_route` 的 `j2` 与 `page_goto` 的 `KP/XP`（`page_goto` 原型
  `int(uint32_t key, uint32_t arg, void*, void*)`，我们传的是 `(key, detail, 0, 0)`）。
- **打点结果（第二十六轮的后续）**：点模块行后 `dm-j2`（`target_route` 收到 MODULE_DETAIL）、
  `dm-KP`（`page_goto` 返回 >= 0）都在，但**没有 `dm-I2`**——详情页的 `on_create` 一趟没跑完，
  随后 app 就没了（屏幕全黑、几秒后回表盘，QEMU 进程没换、uart1 无 panic）。两种可能：
  一是详情页的渲染（`canopus_manager_native_init` 里那几步 `c1/q1/v1/n1` 之前）崩了，
  二是 3.1.32 的 page manager 对这个 app 只支持两层页面，push 第三层时在框架里崩。
  下一轮先在 diag 构建里给详情页的 `on_create` 每个阶段单独打一个不重名的标记（例如 `c2/q2/v2/n2`），
  就能把「没进 on_create」和「进了但崩在渲染」分开；若确认是第二层上限，详情就不要做成独立页面，
  在模块列表页上原地展开。
