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
- **第二十六轮的补充打点**：给 `manager_page_on_create` 加了按页号的阶段标记
  （`g/h/z/a/b/d/p/u/y/k/w/x` + 页号，另外 `K` 存入口栈指针、`M`/`J` 存页面描述符
  0x10..0x4F 两段 32 字节）。实测页面 2（详情页）依次拿到
  `g2 h2 z2 a2 b2 d2 p2 u2 y2`，**没有 `k2`**：
  - 根槽 `page+0x2C` 是**有效指针**（`0x3c7bc9a8`），不是 NULL，所以不是「没有 view」；
  - 入口栈指针与模块列表页**完全一样**（`0x3c8470d3`，概览页是 `0x3c8463f5` 这一段），排除栈深差异；
  - 页面 2 的描述符和 0/1 的差别只在 `+0x3C`（`0x2006f5e4`，落在 Kmem 文本区，而 0/1 是 Umem 指针），
    说明 push 时拿到的是**描述符副本**，`+0x3C` 指向的字段与注册页不同。
  - 试图绕开 `target_refresh_model()`（详情页跳过第二次刷新）**没有把失败点往后挪**，仍然停在
    `y2`——所以要么刷新之后的下一句就出事，要么 app 是在我们返回前后被别处带走的。
  下一步直接用 Mirage 的 gdb stub（`-gdb tcp:127.0.0.1:1234`）在该页的 `on_create` 上设断点，
  看是返回到框架后崩、还是根本没走到下一句。
- **两次排除性实验（都失败，但把范围缩小了）**：
  1. **详情页 on_create 什么都不做**（`-DCANOPUS_MGR_DETAIL_PROBE`，进 DETAIL 分支后写 `dm-N2` 就
     `return 0`）：`dm-N2` 确实写出来了，app 照样黑屏。说明不是我们的建页代码把 app 带走的。
  2. **不推第三页**（`-DCANOPUS_MGR_INPLACE_DETAIL`，`ROUTE_MODULE_DETAIL` 直接在模块列表页上
     `canopus_manager_native_render` 渲染详情视图）：同样黑屏。
  两次的共同点只剩「模型进入 MODULE_DETAIL 视图」本身——要么 `render_module_detail` 的某一行，
  要么进入该视图后框架/回调链里的某一环。两处实验代码都已撤回（不留未验证的行为改动），
  只保留按页打点。下一步：在 `render_module_detail` 里做二分（先只渲染「模块状态」段的状态行，
  再加动作行），以及确认 `on_resume` 在详情视图下的行为；如仍无线索就走 Mirage 的 gdb stub 抓故障地址。
- **第三次实验（只渲染状态行）**：在 `render_module_detail` 的「模块状态」段之后直接 `END`，
  不建任何动作行（`-DCANOPUS_MGR_DETAIL_STATUS_ONLY`）。这次**不是黑屏而是整机复位**
  （出现 Mi 开机 logo，重启后回到安装表盘），比前两次更硬，说明故障点可能在状态行/分节这一段，
  也可能只是同一处崩溃的不同表现。实验代码已撤回；下一步把分节和状态行再分开、并同时用
  gdb stub 抓故障地址，避免继续按现象猜。
- **当前最可疑的两条（下一轮按此顺序验证）**：
  1. **栈**：硬故障/复位更像栈溢出，而详情视图是这条链上唯一新增了大栈帧的地方
     （`render_module_detail` 的 `state[64]`、`error[48]`，叠在 `target_refresh_model` 的
     `canopus_client_*_snapshot` 之上）。验证：在 `render_module_detail` 入口 dump 栈指针，
     与列表页的同一位置比较；若明显更低，就把这两块缓冲挪进 `manager_model` 或改成静态。
  2. **状态行本身**：把「分节」与「状态行」再拆一次（只建分节 / 只建一条状态行），
     若只建分节就死，说明问题在分节或 `canopus_ui_tree_commit` 的快照容量上。
  两条都能在现有 diag 打点体系里用一次构建跑完；之后仍无结论再上 gdb stub 抓 CFSR/PC。
- **第四次实验（把详情视图的两块栈缓冲挪成 static）**：`state[64]`、`error[48]` 改成 static 省下
  112 字节栈，装置上仍然黑屏——单靠这点栈不够，要么溢出发生在更深处（`target_refresh_model`
  的客户端快照链），要么根本不是栈。改动已撤回。
- **重要旁证：host 侧详情视图是好的**。`tests/host/test_manager_native.c` 有多处以
  `model.view = CANOPUS_MANAGER_VIEW_MODULE_DETAIL` 直接渲染详情视图（含状态行与动作行），
  全部通过。也就是说 `render_module_detail` 的**模型/快照逻辑本身没有问题**，崩的是
  3.1.32 上把这棵快照树落到 LVGL 的那一层（`canopus_manager_target_lvgl_v8.c` 的 `apply`）
  或它创建的页面/行对象。下一轮该把 target 后端单独接到 host 上（`fake_target.c` 已有桩），
  用同一份详情快照跑 `apply`，把「快照正确」与「落到 LVGL 出错」分开。
- **第五次实验（详情页跳过嵌套刷新，生产构建）**：`CANOPUS_TARGET_PAGE_DETAIL` 页不再调用第二次
  `target_refresh_model()`，装置上**仍然是黑屏**。结合「空 on_create 也黑屏」，可以定论：
  **3.1.32 上推第三页这个动作本身就会把 app 带走**，与我们页面里做什么无关。
  所以唯一可行的方向是不推第三页（详情视图在模块列表页内联），而第一次内联尝试之所以也黑屏，
  更可能是内联后 `page_index`/模型视图与页面不一致导致的后续回调问题，需要在「不推页」的前提下
  把页面上下文与模型视图对齐后再验证。改动已撤回。
- **第六次实验（不推第三页 + 延后渲染）部分成功**：`ROUTE_MODULE_DETAIL` 不再 `page_goto`，改为置
  `manager_pending_inline_detail`，在 `target_dispatch_row` 的 **dispatch 返回之后**再
  `canopus_manager_native_render`；`target_select_page_view(MODULES)` 在模型视图已是 DETAIL 时
  不再覆盖。设备实测：模块列表 → 点 `hello_portable` → **详情视图渲染出来**（app 存活），
  再点「启用」→ **弹出「在下次手环启动时启用该模块？」确认框**——「启用 / 立即激活」第一次
  从 UI 上够到了。但**点「确认」后整机复位**，所以启用操作本身还没跑通，下一步要查
  `CANOPUS_MANAGER_EVENT_CONFIRM` → `execute_event(ENABLE)` → `refresh_model` → 重渲染这条链。
- **第七步（待做）**：确认框点「确认」后整机复位的排查入口已经明确——`manager_event` 的
  `CANOPUS_MANAGER_EVENT_CONFIRM` 分支在**同一次点击派发里**依次做
  `execute_event(ENABLE)`（一条 CPC2 命令）、`refresh_model`（`target_refresh` 里又渲染一次）、
  最后再 `canopus_manager_native_render`。也就是同一轮里对同一页面连续 commit 了两次快照，
  并且中途还发了一条监督器命令。下一步把这两次渲染延后到派发返回之后（复用
  `manager_pending_inline_detail` 的做法），或者在 `target_refresh` 里检测「派发进行中」时只
  刷新模型、把渲染交给派发结束后的收尾。先验证「只发命令、不重渲染」是否还会复位，可以把
  问题分开。
- **第七次实验（确认路径延后渲染）成功**：`target_dispatch_row` 在派发期间置
  `manager_in_dispatch`，`target_refresh` 在派发中只刷新模型、把渲染交给派发结束后的收尾
  （`manager_pending_inline_detail`）。设备实测（新建的 canopus-b9-probe，AUTORUN 构建）：
  概览 → 模块 → 点 `hello_portable` → 详情视图（状态/生命周期/动作行）→「启用」确认框 →
  **点「确认」后 app 存活并就地重渲染详情页，不再整机复位**。剩余问题：重渲染后「状态」一栏
  显示成 `remove-pen…`（REMOVE_PENDING 被截断），与刚提交的 ENABLE 不符，需要下一步查
  `execute_event` 到模型回读这条链（怀疑是 `target_refresh` 里那次刷新读到的是旧快照，
  或确认事件被映射成了 REMOVE）。
- **确认路径端到端打通（打点/寄存器之外的第一份硬证据）**：确认后关机读
  `/data/canopus/registry.bin`，`hello_portable` 的 **intent 变成 2（REMOVE）**——说明 UI 上
  点的那一下确实变成了一条监督器命令并落盘了（之前详情页根本进不去）。所以「状态」栏看到的
  `remove-pen…` 是真实的 REMOVE_PENDING，不是回读错误；那一轮我点到的动作行是「移除」而不是
  「启用」（两行相邻，坐标差 30 px）。下一步只需在同样路径上点「启用」并确认，再读 registry
  应看到 intent=1。**注意**：intent=REMOVE 落盘后，下次加载 Supervisor 会删除模块产物。
- **新发现（待查）：带 REMOVE 意图的 boot 会反复复位**。上一轮确认「移除」后 registry 记了
  intent=2；下一次 Run（`RESTORE_AFTER_BOOT` 应用删除意图）期间设备在 Mi logo 与黑屏之间反复
  切换，Manager 页始终没出来。「启用」的端到端复验因此没做成。这条和激活体验同等重要：
  移除也必须能用。接续时先 `erase`（本轮结束时已擦机）再重装模块，然后分别验证
  ENABLE（registry 应为 intent=1）与 REMOVE（应删除产物且设备不复位）。
- **确认动作与文案不一致（新 bug，待修）**：在详情页点「启用」→ 确认框文案是「在下次手环启动时
  启用该模块？」→ 点「确认」→ 设备存活（`2703087` 的修复有效）→ 关机读 registry，
  `hello_portable` 的 intent 仍是 **2（REMOVE）**。也就是说对话框说的是 ENABLE，实际落盘的是
  REMOVE。协议层没问题（探针直接发 ENABLE 时 registry 是 intent=1），问题在
  `manager_event` 的确认分支：`native->confirm_event` 与实际渲染的文案对不上，或确认行执行了
  另一个 `event_id`。下一步在 `render_confirmation`/`execute_event` 之间加一个 diag 标记，
  把「文案用的 event_id」和「确认执行的 event_id」都打出来。
- **确认动作与文案不一致的原因（分析，待验证）**：延后渲染是在 **这一次点击的派发里**结束的，
  也就是「按下 → 列表行 CLICKED → 就地渲染详情 → 松开/CLICKED 落到详情页新建的行上」。
  同一个手势因此产生了两次行事件：第二次落在详情页 y≈75 的动作行（启用/移除之一），
  把 `confirm_event` 覆盖成另一条动作，而对话框文案来自第一次。修法二选一：
  (1) 把渲染再往后推（下一次派发/下一帧），(2) 在就地渲染后吞掉同一个触摸序列里的后续行点击。
  验证方式：diag 里把每次 `target_dispatch_row` 的 row_index 与两次 `confirm_event` 都打出来。
- **点击吞掉（`manager_swallow_click`）实测**：详情页**不再被同一次触摸重复点击**——进入详情后
  干净显示「状态/生命周期/启用/移除」四行，没有多余的确认框。但随后点「启用」行时**没有弹确认框**，
  状态直接变成 `remove-pen…`，registry 里也是 intent=2。说明动作行的**行号/绑定**仍不对：
  y≈170 视觉上是「启用」，实际派发的是「移除」。下一个标记要打在 `target_dispatch_row` 里，
  把 `row_index`、`binding->key`、`binding->event_id` 一并 dump（`dm-E*` 已有事件原文，
  再加一个 `dm-K<row>` 存 key/event 各 4 字节），就能确认是行复用把绑定错位，还是点击几何偏移。
- **`dm-K` 打点结果（关键）**：点模块行进入详情后，真正被执行的那次点击解析成
  `row_index=3, key=0x12e(302), event_id=9, page=1`——302 是确认框的「确认」行，9 是 REMOVE。
  也就是说：**进入详情的那一次手势又在我们渲染出来的确认框上点了一次「确认」**。原因是同一个
  手势里 LVGL 会先后送 `SHORT_CLICKED` 与 `CLICKED`，而列表行对象被详情页复用后绑定换成了
  「移除」行，第二次点击就正好落在它上面并打开确认框，第三次点击落在「确认」。
  修法：进入详情后的第一屏**不接受任何确认类动作**——把 `manager_swallow_click` 从「吞一次点击」
  改成「吞到本次手势结束」，或者干脆在渲染后的一小段时间内只允许 `SHOW_*` 类事件。
- **坐标偏移同样作用于详情页（最可能的「点启用却执行移除」解释）**：概览页早就量过触控区和绘制
  位置差一截（画在 y≈432 的「模块」要在 y≈372 点）。详情页上点的 y=170 视觉上是「启用」，
  按同样的偏移它落到的是下面的「移除」行——每次点都稳定变成 REMOVE，正好符合这个模型。
  所以之前的「绑定错位」结论要撤回：绑定是对的，是**触摸几何比绘制低约 35–45 px**。
  下一轮直接按 128（=170-42）点「启用」验证：确认后 registry 应为 intent=1。
  若成立，真正要修的是 target 里行的对齐方式（`align_to` 用的是 OUT_BOTTOM_MID 链，
  和面板的触控坐标不一致），而不是事件绑定。
- **`manager_swallow_click = 3` 的副作用（实测）**：进入详情页后**头两次真实点击会被吞掉**，
  所以在 y=128/140/155 连点「启用」都不会弹确认框（状态一直是「已安装」）。这比幻影移除安全，
  但用户体验不可接受。正确做法应是按「一次手势」而不是按次数吞：例如在渲染后记录一个
  「本次触摸序列序号」，只吞同一序列里后续的 CLICKED，或在 `manager_event` 里对
  就地渲染后紧接着到达的确认类事件直接丢弃。下一轮先把这个改对，再验证 y≈130 点「启用」。
- **AUTORUN 构建在这台设备上会进入重启循环**：手势守卫版本（`44943d9`）装成 AUTORUN 表盘后，
  屏幕在 Mi logo 与黑屏之间反复切换超过两分钟，Run 从未完成（registry 里 intent=0，说明不是
  REMOVE 循环）。上一版（`846…`/`3e50067`）在同一流程下能跑完，所以这条要单独立项查：
  可能是守卫改动影响了 Run 期间的 `INSTALL` 步骤派发。设备已重建（干净、未启动）。
- **基线（`2703087`，无守卫）复测**：AUTORUN Run 正常、管理器页正常；模块列表 → 点模块 → 详情
  与「启用」确认框（文案「在下次手环启动时启用该模块？」）都出现；点「确认」后设备存活，但
  关机读 registry 仍是 **intent=2（REMOVE）**。所以「文案=E​NABLE、执行=REMOVE」在**没有守卫**
  的基线上也能复现，与幻影点击无关——问题出在 `manager_event` 的确认分支本身：
  `native->confirm_event` 与渲染文案用的不是同一个值，或 `execute_event` 的 `event_id`
  在两步之间被改写。下一步直接在这两处各打一个 diag 标记（文案用的事件、执行用的事件）。
- **对账（很可能就是全部真相）**：`confirmation_message` 与 `render_confirmation` 都用
  `native->confirm_event`，`CONFIRM` 分支也用它去 `execute_event`，代码三处一致；而详情页行
  是 42 px 一节、确认框的「取消/确认」也各占一行，配合已量到的「触摸带比绘制高约 40 px」，
  我这两轮点的 y=125 落在**「取消」**上——所以 registry 里的 intent=2 很可能是**幻影点击**
  （无守卫那次）在真正确认之前就执行掉的 REMOVE，之后我看到的「启用」对话框只是又一次点击
  的结果，而我最后的确认点到了取消。这也解释了为什么一直「文案启用、结果移除」。
  验证顺序：先装守卫（防幻影）→ 点模块行 → 等一帧 → 再点「启用」行（按偏移取
  y≈130）→ 点「确认」（y≈80）→ registry 应为 intent=1。
- **验收达成（设备实测）：管理器能把模块启用并落盘**。干净的 `canopus-b9-probe` 上：Run →
  装 `hello_portable` → 重启 → Run → 模块列表 → 点模块 → 详情 → 点动作行 →
  **关机读 `registry.bin`：`intent = 1（ENABLED）`**（`00000030: 0000 0000 0100 0000 0000 0000
  0100 0000`）。所以「UI 动作 → CPC2 命令 → 监督器 → registry」整条链在 n66 上是通的，
  与 Band 10 Pro / Band 11 一致。
  注：这一次的确认是由**幻影点击**代劳的（第一次点模块行的手势把「启用」及其确认一起点了），
  所以交互本身仍然需要修（幻影/偏移），但**功能通路已经验证**。
- **n67 不显示的分析（代码侧）**：`canopus_manager_native_open()` 只做一件事——
  `page_goto((app_id<<16)|0, 0, 0, 0)`，返回值 >= 0 就当作成功。第二十轮的 n67 打点里
  **`dm-Y0` 与 `dm-Y3` 都在**（`FW_PAGE_LOOKUP` 对 (app_id<<16)|0 和 |1 都能查到节点），
  所以页面 key 是对的、页也确实注册了；`page_goto` 返回 0 但屏幕不动，说明 3.1.190 上
  光 push 页面不够——很可能还需要先把 Manager 这个 **app/activity 切到前台**
  （3.1.28/3.1.32 上从表盘 push 就能显示，所以这条是 3.1.190 特有）。
  下一步在 IDB 里找 3.1.190 的 app 切换/`app_switch`/launcher 打开入口（对照 `FW_APP_INSTALL`
  附近调用），或者用 `FW_PAGE_LOOKUP` 拿到页节点后看它的 owner app 与前台判定。
- **`dm-K` 校准结果（决定性）**：在详情页点 y=140，打点记录 `row=0, key=0x12f(303), event=10,
  page=1`——303 是**确认框的行**，不是详情页的动作行（启用=212、移除=214）。也就是说
  「点开模块行」之后，屏幕上一直有一个**幻影确认框**挡着，之后所有点击都落在它上面。
  这解释了此前所有「点启用得到移除 / 点哪都不对」的现象：根本不是几何偏移，而是**对话框把
  详情页盖住了**。所以唯一正确的方向就是**别让幻影出现**：把就地渲染彻底移出这一次派发
  （下一个非点击行事件或下一帧），而不是在派发里渲染后再想办法吞点击。
- **「延后到下一次行点击」不可行（实测）**：点模块行后详情不出现，因为空点不会产生行回调，
  下一次行点击又被吞掉——界面等于卡死。要延后渲染只能用**不依赖行事件**的时机：
  `FW_LV_TIMER_CREATE`（3.1.32 `0x0C534A61`）/`FW_LV_TIMER_DEL`（`0x0C534DA1`）建一个
  一次性 LVGL timer（约 30 ms），在回调里（UI 任务、行派发之外）渲染待处理视图。这条已设计好，
  下一轮直接实现并验证：点模块行 → 详情在下一帧出现 → 点「启用」→ 确认 → registry `intent=1`。
- **timer 渲染尝试失败**：按上一轮设计加了一次性 `FW_LV_TIMER_CREATE(30ms)` 回调渲染待处理视图，
  实测点模块行后页面仍停在列表（未重渲染）。可能是 timer 未创建成功（`FW_LV_TIMER_CREATE`
  的调用约定/参数顺序待 IDB 核对），也可能是回调里找不到 active 页。改动已撤回，避免留下
  一个「详情永远不出现」的状态。下一轮先用一个最简探针验证 `FW_LV_TIMER_CREATE` 能回调
  （例如回调里写一个 `dm-` 标记），再谈用它渲染。
- **`FW_LV_TIMER_CREATE` 在 3.1.32 上返回 NULL（打点证实）**：在 `ROUTE_MODULE_DETAIL` 里调用
  `FW_LV_TIMER_CREATE(manager_inline_detail_timer, 30, 0)`，`dm-TA`（进入）在、`dm-TB`
  （返回 0）在、`dm-TM`（回调执行）不在——即**timer 根本没建出来**，所以回调永远不跑、详情页
  也不渲染。要么 `0x0C534A61` 不是 `lv_timer_create` 的入口/参数顺序不对，要么它要求从 UI 任务
  之外的上下文调用。下一步去 IDB 核对该地址的函数体与调用点（stock 谁在调、传什么），
  确认后再决定延后机制。
- **行事件不仅有点击**：`dm-E*` 原文显示 row 0 最后一次是 `code=7`（CLICKED），而 row 1/2/3
  是 `code=0x23(35)`——LVGL 会持续给行送刷新类事件。所以「把渲染延后到下一个非点击行事件」
  在原理上可行（我上次实现时它没渲染，更可能是渲染循环里没找到 active 页或提前 return 掉了），
  这比依赖 `FW_LV_TIMER_CREATE`（实测返回 NULL）更靠谱。下一轮回到这条路并加打点：
  在延后渲染的分支里先写 `dm-R1`（进入）、再写 `dm-R2`/`dm-R0`（找到/没找到可渲染页）。

### 第二十七轮：一次点击只派发一次；详情/确认框恢复正常

- **幻影的根因是双击派发，不是几何偏移**。LVGL v8 的一次短按会先送
  `SHORT_CLICKED(4)`、再送 `CLICKED(7)`；行回调把两者都当点击派发，于是第一次点击
  （模块行）在派发尾部就地渲染详情、第二次点击（同一手势）落到渲染后重新绑定的行上，
  把 `confirm_event` 覆盖成另一条动作。修法：`CANOPUS_MGR_INPLACE_DETAIL` 构建只认
  `CLICKED`，`SHORT_CLICKED` 只写打点不派发。
- **设备实测（n66，Mirage）**：模块列表 → 点 `hello_portable` → **详情视图就地渲染**
  （状态/生命周期/启用/移除），**没有幻影确认框**；点「启用」→ 确认框文案是
  「在下次手环启动时启用该模块？」，即动作行的绑定第一次和文案一致。
- **派发内的渲染改到派发之后**：`target_ui_apply` 在 `manager_in_dispatch` 时只置
  `manager_render_pending`，`target_dispatch_row` 在派发返回后统一渲染一次
  （详情、确认框、确认后的刷新状态都走这一处）。这同时删掉了上一轮的两套守卫
  （按次数吞点击、按手势吞点击）——它们都只是在绕开双击，不能同时保证「一次点击一次动作」。
- **确认动作仍未端到端复验**：确认框弹出后按「确认」，registry 的 `intent` 仍是 0。
  注意 registry 布局：header 16 B、slot 48 B；`intent` 在 `slot+0x2C`（文件偏移 `0x3c`），
  `0x30` 是 `lifecycle_class`（1 = 重启生效），不要再把 0x30 误读成 intent。
  下一步：按实测的触摸带偏移（绘制在 y≈134 的「启用」用 y≈75 点中）校准「确认」行
  （绘制 y≈145 → 大约 y≈85），或用打点把 `target_dispatch_row` 的 row/key/event 再 dump 一次。

### 设备稳定性：panic 路径自身在 0x200D0000 再次崩溃

- **任何 panic 都会被日志环形缓冲区的越界写放大**。崩溃转储稳定落在
  `PC=0x0C141AFA`（`sub_C141A8C`，控制台环形缓冲 putc）、`MMFAR=0x200D0000`，
  `R0=0x2006C0F4`、`R5/R6=0x200CB3D4`：缓冲区 `0x200CB3D4 + 0x4C2D` 正好结束在
  stage-0 cave 的基址 `0x200D0000`。cave 在加载期间被 MPU region 7 映射、之后释放；
  日志索引走到最后一个字节时要么撞上被映射的 cave、要么落在 RAM 映射之外，
  于是第一次 panic 的转储本身又触发 MemManage，表现为「Hard Fault escalation」+ 整机断电。
  所以最近的崩溃日志大多是 panic 路径的二次故障，掩盖了真正的首故障。
- **试过的缓解（未生效）**：在安装器 Run 结束时用 `mw 2006c10c=00004c2c` 把环形缓冲
  的 size 字段减一。设备仍在同一路径断电，说明字段/值还不对，或者首故障之后还有别的映射问题。
  要真正解决，需要把 cave 移出日志缓冲区的末尾，或在加载完成后把 cave 所在页恢复为普通 RW。
- **首故障之一（来自 crash07 的寄存器）**：`UsageFault, PC=0x10`，`LR=0x0C2748D1`
  （`sub_C2748B4`，LVGL 事件派发循环里的 `cb(object, event)`），即某个对象的事件描述符
  `cb` 变成了 `0x10`。它出现在页面/视图销毁与息屏路径上，和「page_goto 在点击派发里
  销毁源页」的时序吻合。下一步应先让 panic 转储能落盘（上述二次故障），再看首故障的
  完整调用栈，而不是继续按现象猜。
- **现场操作经验**：
  - 手动 Run 比 AUTORUN 可靠：AUTORUN 在开机早期加载 Supervisor 会撞上 launcher
    正在为已注册 app 创建主视图（`insert_app_to_list` → `on_ui_create {202,0}`），
    随后在视图销毁路径崩溃。开机后等 ~15 s 再点 Run 可避开。
  - 替换表盘资源时 **stage2 与 supervisor 必须同一次构建**：stage2 把
    `CANOPUS_BAND9_SUPERVISOR_SIZE` 编译进常量，只换 supervisor 会让读循环读到 EOF
    并报 `cannot read staged Supervisor ELF`（-5）。
  - Supervisor 体积敏感：114,704 / 114,640 / 114,256 B 可加载，116,484 B（诊断构建）
    在同一台设备上解析失败（-101）。诊断构建目前不能用来做设备实验。
  - 屏幕约 5 s 息屏且息屏本身会走首故障路径；一次 `tap` 会被用来唤醒，所以每个动作
    实际需要「唤醒 tap + 动作 tap」两下。

### 第二十八轮：启用链路端到端通过；息屏后 Manager 可重新打开

- **「启用 → 确认 → registry intent=1」在 n66 上端到端验证通过**。点中确认行后，
  详情页立刻变成 `状态：已启用`、动作变成 `立即激活 / 禁用 / 移除`；关机后读
  `/canopus/registry.bin`，文件偏移 `0x3c`（slot+0x2C）由 0 变为 `0x01`。
  下一次开机 INSTALL/RESTORE_AFTER_BOOT 把它恢复并激活，详情页显示 `状态：运行中`。
  触摸标定：绘制在 y≈160 的「启用」用 tap y=160 点中；确认框里「取消」绘制 y≈110、
  「确认」绘制 y≈145，实际要 tap **y≈140**（150 会落到行外，点不动）。
  之前把 `0x30` 当作 intent、把确认行按「-60 px 偏移」去点是两处误判。
- **息屏时整条页面栈会被弹掉**（`tmp.log` 证据，08:00:37 息屏）：
  `MiWearScreen ... ON-->OFF` 后 `miwear_alarm start_alarm(14, 10000)`；10 s 后
  `exec_pop_lifecycle_without_cachepolicy {20,1}/{20,0}`（表盘）、
  `{202,1}/{202,0}`（Manager）依次 `on_destroy_wrapped`，接着
  `remove_app_from_list: pkg = com.canopus.manager, start_source = 10`，然后
  `{16,0} resume` 回到主屏。即 3.1.32 的「息屏 10 s 回表盘」策略会把所有页面
  finish 掉；`finish_page`（`sub_C45E5C0`）在 app 的最后一个页销毁时调用
  `remove_app_from_list`（`sub_C45BD5C`），所以 **Manager 的 app 注册会随息屏消失**。
- **修复：`canopus_manager_native_open()` 先重新注册再 push**。
  `canopus_manager_native_install()` 本身是幂等的（`app_lookup(app_id)` 命中就直接返回 0），
  所以在 push 之前调用一次即可让 OPEN_MANAGER 在同一次开机里反复可用。
  安装器表盘的 Run 按钮改成：首次点击执行安装；`run_attempted` 之后再点就发
  `CMD_OPEN_MANAGER`（不再 `clear_flag(CLICKABLE)`），因此「Run」兼作「打开管理器」。
  设备实测：Run → Manager → 息屏 24 s（页面被弹回表盘）→ 再点 Run →
  **Manager 概览页重新出现**（截图 22353 B，`系统信息` 全表），随后 模块 → 详情
  照常显示 `已启用 / 运行中`、`禁用 / 移除`。
- **现场操作教训：正在运行的表盘是 `cp9probe0003`，不是 `366140352`**。
  `[lua] miwear_lua_run: run script: /data/app/watchface/market/cp9probe0003//main.lua`
  才是引擎实际加载的脚本；把 `main.lua`/资源放进 `366140352` 完全不生效（标题改动
  可以直接验证这一点）。以后替换表盘脚本/资源一律写 `cp9probe0003/`。
- **panic 路径的根因在固件里，已有反编译证据**：`/dev/kmsg` 的环形缓冲描述符在
  `0x2006C0F4`，`+0x00` head、`+0x04` tail、`+0x18` size、`+0x1C` buf
  （`sub_C141A8C` 的 `*(buf + head) = ch` 就是崩溃指令 `0x0C141AFA`）。size = `0x4C2D`、
  buf = `0x200CB3D4`，所以**缓冲区最后一个字节正好是 `0x200D0000`，也就是 stage-0 cave
  页的首字节**；head 走到 `0x4C2C` 时写这一字节，而 cave 页被 region 映射成 RX，
  于是 MemManage（DACCVIOL，MMFAR=0x200D0000）。正常日志不会崩（连续 14 次唤醒/息屏、
  加 60 s 空闲都活着），所以这是 **panic 转储自身**被这一字节放大的问题：首故障的寄存器
  与栈在 head 不落在末尾时能完整落盘（crash09/10 里的 UsageFault `PC=0x10`、
  `LR=0x0C2748D1`、调用链 `0x0c274909→…→0x0c463595` 就是 LVGL 事件派发里
  `cb == 0x10`），落在末尾时只剩二次故障。后续若要稳定拿到首故障，可在安装器 Run
  之后把 size 改成 `0x4C2C`（`mw 2006c10c=00004c2c`）或把 cave 移出该页。
- 本轮构建：n66 Supervisor 114,276 B（verifier PASS），stage1 417 B、stage2 3,576 B；
  四个资源和 `main.lua` 一起写进 `cp9probe0003/`（stage2 内嵌 supervisor 尺寸，
  只换 supervisor 会报 -5）。

### 第二十八轮附带：n67（3.1.190）的现场状态

- 设备 `canopus-n67`（不是 `-probe`）上装的是完整安装器表盘（`366240021/`，
  `resource.bin` 211,464 B），但当前选中的表盘是探针表盘 `366240028/`。
  切换方法：`/app/watchface/watchface_list.json` 里把目标项的 `in_use` 置 1、
  其余置 0，重启即生效（本轮用它把 n67 切回了安装器表盘）。
- 用本轮构建（Supervisor 114,160 B）在 n67 上点 Run 之后：**页面确实被创建了**
  （日志 `activitymanager_active_by_layer: {202, 0 pause}`，息屏时 pause），
  但屏幕全黑（2274 B 的纯黑截图），且设备不崩、不失联。也就是说 n67 的问题不是
  「页面没建」而是「页面没被显示」——与 3.1.190 的 `CANOPUS_MGR_PAGE_ROOT_SLOT=0x2c`
  是否指向真正上屏的 view 直接相关，下一步应在 3.1.190 的 IDB 上核对
  `on_ui_create` 交给我们的 root 与 `lv_disp` 当前 screen 的关系。
- n67 的蓝牙配对需要手环上点「确认」（AstroBox 弹窗：左侧取消、右侧蓝色确认，
  336×480 屏上约 (260,440)）；配对流程里若不点，30 s 后 bridge 报
  `pairing was declined on the band`。
- **n67 的「页面不上屏」用轻量打点定位到了渲染成功但显示空白**。新增
  `-DCANOPUS_MGR_TRACE`（只开 `diag_mark`、不开 `diag_dump`，114,968 B 仍可加载）：
  清空 `/canopus/dm-*` 后跑一次 Run，串口外的标记文件给出完整顺序
  `ST → i9/iy → g0 c1 h0 a0 b0 d0 p0 u0 l1 o1 q1 y0 k0 v1 w0 n1 x0 I0 →
  T0 T1 → B0 B1 L1 V1 → A0 A1 Z1`。也就是说 3.1.190 上
  **on_create 走完（I0，interactive）、stock 标题创建成功（T1）、行和标签都建好（B/L/V）、
  target_ui_apply 也走完（Z1）**，但面板从 Run 之后起一直是纯黑（336×480，截图 2274 B，
  连打三帧都一样，唤醒 tap 也不改变）。
  → 结论：n67 不是「没建页面」，而是**建好的视图没有被显示**（或标题视图盖住了整页）。
  下一步该核对 3.1.190 的 `lvx_page_title_create`（`0x0C275500`，标题对象自身是
  `off_2C68DAB8` 类实例、`[13]=56` 高、`[9]` 返回键、`[12]` 时间标签）与页面 view
  的层级关系，以及 `on_ui_create` 里 `sub_C2E9430(view)`（`sub_C240ED0(view, n-1)`）
  究竟把 view 放到显示的哪一层。
- **3.1.190 的页面描述符布局和 3.1.32 不同，已按 IDB 修正**：3.1.190 的
  `on_create_wrapped`（`sub_C44D49C`）在 **+0x48** 取 create 回调、`on_resume_wrapped`
  （`sub_C449D0C`）在 **+0x4C** 取 resume 回调、pause 在 **+0x58**、异步销毁
  （`sub_C449CA8`）在 **+0x60** 取 `on_ui_destroy`，而生命周期信号走 **+0x30**。
  我们原来用的 3.1.32 布局（`runtime_parent` 占 +0x48、create +0x4C、resume +0x50、
  signal +0x34）在 3.1.190 上等于：create 槽为 NULL（固件找不到就不调）、resume 槽里
  放的是我们的 on_create、signal 槽也是空的。现在 `CANOPUS_MGR_APPINFO_190` 构建
  用 3.1.190 的布局（struct 内条件字段 + 条件 `_Static_assert`）。
  设备实测（n67）：create 时刻确实进到我们的 on_create（日志里出现我们创建的
  `lvx_font_create` 警告），页面动画正常，**但仍然没有
  `activitymanager_active_by_layer: {202, 0 resume`，面板依旧是纯黑**。所以布局是
  真 bug（必须修），但不是「不上屏」的最后一个原因；下一步要沿 `state 5 → 8`
  的迁移（页面 in 动画结束后由谁置位）继续查。
- **n67「创建了但没 resume」的下一个线索**：`page_in_anim_ready_cb`（`sub_C44A2F8`）
  在进入动画结束后对页面调用 `sub_C44A140`：页面 flags（页描述符 +0x18 的 dword）
  的 **bit 0x40** 若置位，页面会被推进「ui hidden queue」（10 项队列），否则调用
  `sub_C4477A8()` 走真正的激活/恢复路径。n67 的日志有「page in animation ended」
  但始终没有 `activitymanager_active_by_layer: {202, 0 resume}`，所以下一步应确认
  我们的页面在那一刻的 +0x18 flags 是否被固件置成了 0x40（隐藏），以及
  `sub_C4477A8` 到 `sub_C449D0C`（resume）之间的条件。
- **3.1.190 的 launcher app 描述符也要置「显示」位**：`launcher_load_app_info`
  （`0x0C2A308C`）读 +0x0C 图标、+0x10 app_id、+0x12 的 bit2 并把它写进
  launcher 条目 +20 的 bit1（该位同时被日志称作 “can't be uninstalled”）、+0x1C 元数据
  回调、+0x38 bit0 = 需隐藏。原来 3.1.190 构建这两个字节都是 0，所以条目被标成不显示。
  现在 190 构建把 `.flags = 0x04`。设备上补这一位后 **画面仍然全黑**，说明它不是
  「不上屏」的原因（但 launcher 可见性仍需要它，属未单独复验的修正）。
- **按 IDB 改成 3.1.190 布局后 n67 的 UI 任务会僵死，已回退**。用 3.1.190 的
  槽位（create +0x48、resume +0x4C、signal +0x30）编译并上机后：页面创建时确实进了
  我们的 on_create（日志出现我们建的 `lvx_font_create` 警告），但**此后整机不再处理
  触摸**——00:00:24 息屏后再没有一次 `OFF-->ON`、没有 `{202,0} resume`、按 home 键也无
  反应；而回退成 3.1.32 布局后同样的流程在 00:00:26 被双击唤醒、`{202,0} resume`
  正常、`touchpad_read` 正常。所以那套槽位即使和 IDB 对得上，也不能照搬（很可能
  在 create 阶段从页面管理器回调里做设备往返会卡住 UI 线程）。该改动已回退，
  证据留在本节；后续要动 n67 顺序模型前，先解决「create 回调里能不能做 IPC」。
- **回退后 n67 的画面依旧黑，但页面是活的**：`{202,0}` 能 resume/pause、触摸事件正常，
  只是在空白处点按都不会命中我们的行，说明**视图没被放到显示上**（行存在但不可见、
  不可点）。结合 `page_in_anim_ready_cb → sub_C44A140`（页面 +0x18 bit 0x40 →
  「ui hidden queue」，否则 `sub_C4477A8()`），下一步应当确认 n67 的页面是否落进
  hidden queue，以及 `sub_C4477A8` 到 `sub_C449D0C` 的条件。

### 第二十九轮：o66（Band 10 / 3.1.28）Manager 打开与重开设备验证通过

- 设备 `canopus-o66b`（3.1.28，212×520）。正在运行的表盘目录是
  **`366340011/`**（`/app/watchface/watchface_list.json` 里只有它带 `in_use=1`；
  `366340012/` 目录还在但已不在列表里，写进去不生效）。本轮的 stage1/stage2/
  profile/supervisor（114,284 B）和 `main.lua` 都写进 `366340011/`。
- **Manager 打不开的老原因之一**：o66 的安装器表盘 `steps` 只有 5 步，**没有
  `CMD_OPEN_MANAGER`**（n66/n67 有第 6 步）。本轮给 o66 表盘补上了「首次安装后
  Run 兼作打开管理器」的分支，于是 Run 第二次点击就发 `CMD_OPEN_MANAGER`。
- **设备实测**：Run → **Manager 概览页**（`系统信息`：框架/管理器/固件 3.1.28/构建
  9d8f394:nx_b/注册表 就绪/已验证保存次数 7，`管理`：模块、安装软件包、导入手机推送）；
  等 24 s（息屏 10 s 后被弹回表盘）→ 再点 Run → **Manager 重新出现**（截图 26,829 B），
  同一次开机内可反复打开。触摸标定：Run 按钮在 x=106、**y≈375–385**（y=360 会落到
  「Clear Env」上方的空档，表现为状态文字变化但不开页）。
- 这一步把三个 v8 目标里的 o66 从「只有静态地址」推进到「Manager 能打开、能在息屏后重开」
  的设备证据；o66 的模块列表/详情/启用链路与 launcher 图标仍未逐项复验。
- **launcher 图标：把插入动作从 install 里拆出来，OPEN_MANAGER 时重试**。
  `launcher_page_insert_icon` 需要 launcher 的实时页面（用户打开过一次应用列表才存在），
  安装是在表盘里做的，所以第一次插入必然失败（日志 `launcher UI not created`）。
  现在 `manager_launcher_publish()` 单独负责 load+insert，安装时调用一次，之后每次
  `OPEN_MANAGER` 只要上次 `launcher_add_result < 0` 就再试一次；`reset_order_info_list`
  仍只在安装成功插入后调用一次，避免 o66 上重复 reset 的风险。
  n66 回归：新构建（114,336 B）Run → Manager 概览页照常打开（截图 22,353 B）。
  **图标本身尚未在应用列表里目视确认**（还需要「先打开应用列表 → 回表盘 → Run」的
  一次完整时序），这一步留待下一轮。
- **n67 补充证据（本轮最后几次实验）**：
  - 装上 Manager 之后日志里出现 `[launcher] launcher_page_insert_icon: launcher now has 34 apps`
    ——说明 **n67 上的 launcher 图标插入是成功的**（不像 n66 从表盘加载时那样报
    “launcher UI not created”），只是图标文件本身解码失败
    （`invalid file /data/canopus/manager_icon.bin, cf: 25`，3.1.190 的图片格式与
    3.1.32 不同），所以应用列表里可能只是缺图。
  - 用 `-DCANOPUS_MGR_TITLE_PLAIN`（不走 stock 标题，和 n66 一样用普通容器）重编后
    n67 仍然全黑，**排除「stock 标题盖住内容」**这个假设（该实验用环境变量切过一次，
    之后已还原构建脚本）。
  - `goto_page_no_check_stacktop`（`0x0C49E274`）的行为：新页面 push 后，只有当
    **旧栈顶页的 cache policy ≤ 3** 才会 `sub_C44D608`（finish 旧页）；n67 的旧栈顶是
    表盘页（cache policy 5），所以表盘页留在栈里，日志里也没有 `delete view`。
  - n67 的 launcher 界面本身有问题：打开应用列表后滚动到底只看到黑底和
    `change_layout.launcher` 字样，说明这台设备的 launcher 页面没有正常构建，
    这也是「从应用列表打开 Manager」这条路暂时走不通的原因。
- **决定性实验：n67 的页面视图确实没有被放到显示上**。加了一个只在
  `-DCANOPUS_MGR_VIEW_PROBE` 下编译的探针，在拿到页面视图后把视图本身刷成纯红
  （`lv_obj_set_local_style_prop(view, BG_COLOR/BG_OPA)`）——设备上 Run 之后面板
  **仍然是纯黑（2274 B）**。所以问题不在行/字体/标题/样式，而是
  「page manager 建好并 resume 的 view 没有出现在面板上」。结合
  `goto_page_no_check_stacktop` 不 finish 旧栈顶（表盘页 cache policy 5），
  以及这台 n67 设备的 `/system` 为空（日志里 342 次
  `Can't access font file: /system/font/MiSans-Regular.ttf`，n66 是 0 次），
  下一步应先用**完整的 3.1.190 资源包**把这台设备供起来（Mirage 的 archive 里
  只有 `ZZ~` 自定义容器、`firmware import` 不收），再判断是不是资源缺失导致的
  显示层异常；探针保留在源码里（不定义宏时完全不编译）。
- **o66 的模块页也验证通过，触摸偏移在这块 212×520 面板上是 ~+40 px**。
  用轻量打点（`-DCANOPUS_MGR_TRACE`）在 o66 上确认：行点击派发正常
  （`dm-U7`/`dm-W7` 是状态行 event_id=0 的正常返回，`dm-C8` + `dm-j2` 是「模块」行
  真的把 route=2 发了出去）。之前点不中只是因为坐标：这张屏上 tap y ≈ 绘制 y + 40
  （Run 绘制 ~362 → tap 400/405；「模块」绘制 ~407 → tap 442；Clear Env 绘制 ~405
  → tap 440，注意别连点）。设备实测：Manager 概览 → 模块页
  （`模块 / 已安装模块 / 暂无已安装模块`）正常上屏。
  **o66 上没有装任何模块**，所以详情/启用链路在 o66 上还无法复验——需要先给这台
  设备装一个模块包（走 AstroBox 配对或 inbox 流程）。
- **o66 的模块安装路径已打通到「staging 文件就位」这一步**：模块包
  `hello_portable-1.0.0.canopus` 是 tar，里面已经有
  `receipts/xiaomi-band-10-3.1.28.cmi` 和 `artifacts/abi-2-thumbv8m/module.elf`。
  按 Supervisor 的约定把它们放到 `/canopus/inbox/hello_portable.ko.next` 与
  `.cmi.next` 后，用表盘发 `CMD_INSTALL`（13，payload = `"hello_portable\0"`）会返回
  **result=7（DISALLOWED）**；改用 `CMD_SCAN`（12，扫描 inbox）则能走完并让 Manager 打开。
  （表盘探针只是临时改动，已还原成仓库版本；staging 文件留在 inbox 里。）
  下一步：在 Manager 的模块页确认 hello_portable 是否已注册，再走详情/启用；
  若 SCAN 没登记，需要看 `list_packages` 对 inbox 文件名的要求。
- 另一个操作教训：o66 上 `Ctrl` 级别的坐标不能盲点——这块屏 tap y ≈ 绘制 y + 40，
  也就是说表盘上 y≈440 是 **Clear Env**（点两次会 `rm -rf /data/canopus`）。
  自动化脚本必须先截图判断当前在哪一页再决定坐标。
- **o66 上 hello_portable 真的装进去了**：把
  `hello_portable.ko.next` / `.cmi.next` 放进 `/canopus/inbox/` 后，用
  **`CMD_INSTALL`（协议号 2，payload = `"hello_portable\0"`）** 安装成功，
  `/canopus/registry.bin` 里出现了 slot 0 = `hello_portable` 的记录。
  注意协议号容易搞混：安装器表盘里的 `CMD_INSTALL = 13` 其实是
  **`CANOPUS_CMD_NATIVE_STAGE`**（payload 必须是 4 字节的 stage 0..2）；真正的
  带 token 安装是 **2**。之前发 13 + token 得到 result=7（DISALLOWED）就是这个原因。
  另外 **`CMD_SCAN`（12）在 Band 9 家族上不可用**：`list_packages` 依赖
  opendir/readdir/closedir，这三个符号靶标配置里没有，所以扫描不会登记 inbox 里的包。
- o66 上 Manager 的模块页/详情/启用没有跑完：这块屏的触摸时序很脆（5 s 息屏 +
  10 s 弹回表盘 + 唤醒需要双击），盲点坐标连续几次点空。已确认的事实是
  Manager 概览/模块页能上屏、重开正常、模块已注册。表盘脚本已还原成仓库版本。
- **o66 上「Run」按钮点不动，而「Clear Env」正常**（本轮最后的定位）：
  装上仓库版本的表盘（`Manager opened` 分支在、没有 `clear_flag(CLICKABLE)`）后，
  连续多次在 y=375/400/405（Run 的绘制区间 355–403）点按，日志里**没有任何
  `exec:` 行**，也没有 `insert_app_to_list`；而 y=442（Clear Env 区间 403–451）
  每次都能把状态改成「Click again to clear」。所以不是坐标问题——**这个表盘的 Run
  按钮在 o66 上不响应点击**（同一个 `make_button` 建的两个按钮，只有一个有反应）。
  下一步应查 3.1.28 上 `lv_obj_add_event_cb`（`0x0C2B7C41`）对**后创建**对象的绑定，
  以及 flex 布局里 Run 是否被状态标签/根容器挡住命中测试；n66/n67 同名按钮都正常。
- **o66 Run 按钮不响应的进一步测量**：从 212×520 的截图像素分析得到两个按钮的精确行带
  ——**Run y=362–405（44 px）、Clear Env y=421–464（44 px）**。在 y=383（Run 正中心）
  点按，屏幕亮着的状态下日志里仍然是 **0 条 `exec:`**；再给 `run_button` 显式
  `add_flag(CLICKABLE)` 之后结果相同。所以这不是坐标、也不是 CLICKABLE 标志位的问题，
  而是 3.1.28 上这个（先创建、位于 flex 首个按钮位置的）对象拿不到点击——同样方式创建的
  Clear Env 正常。下一步要查 3.1.28 的 hit-test/z-order（例如状态标签的 176×246 区域是否
  先于按钮参与命中），或者干脆把安装流程改成「不需要点按钮」的入口。
  （探针版表盘已还原成仓库版本。）
- **更正（重要）：「o66 的 Run 按钮点不动」是错的**。在 `run_installer` 入口写
  `/data/app/watchface/market/366340011/run-tapped.txt` 后，y=383 点按确实生成了该文件
  ——**回调是被调用的**。之前的日志里看不到 `exec:` 行，是因为那些点按发生在屏幕熄灭
  期间（表盘重建、状态回到 "Ready"）。
  真正的 o66 症结是**时序**：安装（`mw`/`exec` 载入 Supervisor）阻塞 UI 约 10 s，
  5 s 后息屏，随后页面/管理器在「屏幕关着」的状态下被推送，`on_resume_wrapped` 报
  "Now screen state isn't on"，10 s 后整栈被弹回表盘——所以运行结束后看不到 Manager。
- **给 o66 的安装流程加 `CMD_OPEN_MANAGER` 第 6 步会让设备重启**（实测一次
  HyperOS 开机画面），该改动已回退（`6e89413`）。o66 保持 5 步：先 Run 完成安装，
  再点一次 Run 走「打开管理器」分支（这条路径此前是能出 Manager 的，26945 截图）。
- **o66 上「已装好 Supervisor 后再点 Run 打开 Manager」会让设备重启**（本轮实测两次
  HyperOS 开机画面）。时序是：慢安装（`mw`/`exec` 载入）→ 息屏 → 10 s 弹回表盘（表盘
  Lua 重建、`run_attempted` 归零）→ 唤醒后第一次 Run 走完整 5 步（此时 supervisor 已在，
  很快）→ 第二次 Run 进「打开管理器」分支 → **设备重启**。之前在 o66 上确实成功打开过
  Manager（26,945 截图），差别可能在于当时 app 还注册着；现在是
  `native_install()` 重新注册 + `page_goto` 在同一次 Lua 回调里完成。
  结论：o66 的「重新注册再 push」这条路在当前设备状态下不安全，需要改成
  「注册」和「push」分成两次交互（或先验证 app 仍注册、只在缺失时重注册）。
- **现场操作教训（重要）：在 o66 的表盘上「长按」＝打开表盘选择器**。本会话很长一段
  时间把「长按 1.5 s」当成唤醒手势，结果反复打开表盘选择器、甚至切换了表盘，
  造成「Run 按钮点不动」「设备重启」「Manager 不见」等一堆假象。正确做法是
  **单次短按唤醒**（或双击），绝不在表盘上长按。
- 设备侧 o66 出现 `UsageFault STKOF`（`CFSR 0x00100000`，task `system`，PC=0x1，
  LR=0x0C5561BB）——是**栈溢出**，发生在开机完成后第一次触摸附近，和安装器
  自身的流程相关，需要单独查（`/log/minidump/crash*.txt` 里有完整转储）。
  用会话开始时的构建（114,264 B）与当前构建（114,400 B）都能复现，说明不是这次
  代码增量造成的。
- **o66 的 STKOF 落在固件的 NAND DMA 路径**（反编译定位，3.1.28 IDB）：
  故障 LR `0x0C5561BB` 属于 `sub_C556198`（`nandflash_start_rx_dma`，内部调用
  `sub_C56A84/start_rx_dma`、`nandflash_start_rx_dma` 的错误串也在其中），
  即**闪存读 DMA 期间发生栈溢出**（`CFSR 0x00100000`，task `system`，PC 被踩成 0x1）。
  所以 o66 那些「装一次就重启」的现场，多半是高频/大块闪存 I/O（`mw` 写 cave、
  stage1/stage2/supervisor 资源读写、`/canopus` 落盘）把这条路径压垮，
  而不是我们的 UI 代码。下一步要么减小安装期间的 I/O 突发（分批写、减少重复读），
  要么先确认这条路径在官方表盘下是否会因为相似 I/O 触发。
- 分块读（4 KB 一段）实验**没有**消除 o66 的 STKOF（装一次仍然重启，开机出现 mi logo），
  该实验已回退、未提交。结合反编译结论（`nandflash_start_rx_dma`），这条栈溢出更像是
  固件在该任务上的栈本身偏小，需要从**设备/固件侧**处理（或改成不经过大块 NAND 读的
  安装路径），而不是表盘 Lua 的读取方式。
- **o66 STKOF 的完整现场**（`/log/minidump/crash24.txt`）：`CFSR 0x00100000`（STKOF），
  **task `system`**（就是执行 `mw`/`exec` 的那条 shell 任务），`LR=0x0C5561BB`
  （`nandflash_start_rx_dma`）、`R3=0x0C555EFD`、`R5/R8=0x14737`（≈83,767 字节）、
  `R6=0x800`、`SP=0x3C48A420`、`EXC_RETURN=0xFFFFFFED`（handler/PSP）。
  紧接着 panic 转储在 **`MMFAR=0x200E0000`**（o66 的 stage-0 cave 基址）二次 MemManage
  ——和 n66 是同一类「转储被 cave 页放大」的问题。
  这说明 o66 的崩溃发生在 **shell 任务做一次较大的闪存读**时，不是 UI 任务；
  下一步应查安装器发出的哪条 `mw`/`exec` 序列会触发 `system` 任务里的大块读，
  以及 o66 的 cave 页（0x200E0000）同样需要移出日志环形缓冲末尾。
- 把 stage2 的 Supervisor 读取改成 4 KB 分块（`CANOPUS_BAND9_READ_CHUNK`）**同样无效**：
  o66 仍然在 `system` 任务里 STKOF 重启。该实验已回退、未提交——说明这条栈溢出不是
  「一次读多大」决定的，而是该任务/该驱动路径本身的栈预算问题。
- **把 o66 的 cave 从 0x200E0000 移到 0x200E1000 后，panic 转储仍然在 `MMFAR=200E0000`
  二次 MemManage**——说明这一页**不是**被我们的 region 7 覆盖的 cave 页，而是日志/转储
  路径本身无法写的一个字节（正是环形缓冲末尾那一格）。所以 o66 的 cave 迁移**不必要**，
  该改动已回退。o66 的 STKOF 首故障转储里确实带了 `backtrace|49..55`
  （`0x0c11f5a4 0x0c0e8a62 0x0c1bf1d6 0x0c4c7b60 0x0c113672 0x0c0e86b8`），
  下次可以直接从这些地址往上查 shell 任务的调用链。
- **o66 STKOF backtrace 的符号化**：`0x0C113660` = `pthread_start`（线程入口，调
  `pthread/pthread_create.c` 里的启动函数）、`0x0C4C7B10` = `thread_schedule_loop`
  （它调用 `sub_C1BEDB0` 任务体并打印 "thread_schedule_loop async/quit"）、
  `0x0C1BEDB0` 是任务主体（0x920 字节）。也就是说这次栈溢出发生在 **`system` 任务的
  工作线程调度循环里**，正处在一次闪存读（`nandflash_start_rx_dma`）之中；
  与「安装期间的闪存 I/O 把该线程的栈压穿」一致。
- **o66 STKOF 的最可能成因（新假设，待验证）**：`exec` 整条链
  （shell 任务 → cave 蹦床 → stage1 → stage2 → ELF 加载 → **Supervisor 构造/初始化**）
  都跑在 `system`（shell）任务的栈上。on66 上把读取改成分块**没有**缓解，说明不是
  「一次读多大」，而是**这条链的累计栈深度**（尤其 Supervisor 初始化）超过了 3.1.28
  给 shell 任务的栈预算；n66/3.1.32 同样跑这条链但没溢出（两版固件该任务栈大小可能不同）。
  可验证/可修的方向：把 Supervisor 初始化里的大局部变量搬到静态/堆上、或让
  `image_invoke` 在独立线程栈上跑；验证方法是比较「首次 Run（含载入）」与
  「supervisor 已在时的 Run（不载入）」是否只有前者崩。
- **对照实验：会话开始的构建（114,264 B）与当前构建（114,400+ B）在 o66 上都会在
  「首次 Run 载入 Supervisor」时重启**（两次都出现 mi/HyperOS 开机画面）。所以这次
  崩溃不是本会话的代码增量造成的，而是**这台 o66 设备的当前状态**——它在本会话里经历了
  多次安装/重启/表盘切换。下一步应先**擦除重装**（`miragectl erase` 或重建设备），
  再用同一构建复测首次 Run 是否稳定。
- **擦除重装 o66 后，载入崩溃消失、Manager 与模块页都能正常打开**（2026-10-26）：
  先 `fs get` 备份整个 `366340011/`（含 214,795 B 的 `resource.bin`）和
  `watchface_list.json`，再 `miragectl erase`，重启一次让 FS 重建、把表盘目录与
  `watchface_list.json`（只保留 366340011 `in_use=1`）写回，然后恢复 stage1/stage2/
  profile/supervisor。**结果：首次 Run 不再重启**（屏会黑 5–10 s，但设备存活），
  之后 Run 能打开 **Manager 概览（26,829/26,863）**，也能进 **模块页（7,197）**。
  ⇒ 之前 o66 的「装一次就重启」确实是**设备状态**（多次安装/重启/换表盘后劣化），
  不是我们的构建；干净设备上这条链是通的。
  遗留：擦除同时清掉了 `/canopus/registry.bin` 里的模块记录，本轮重新投递
  `hello_portable`（inbox + `CMD_INSTALL=2`）没有生效（registry 仍为空、探针也没写结果），
  需要下一轮重新走一遍模块安装。
- **擦除后 o66 安装流程的打点轨迹**（`stage.txt`，AUTORUN 触发）：
  `enter` → `verify=true` → `present=false` → `after-load present=true` → `icon=true`
  → **到此为止，没有进 `run_all_steps`**。也就是说干净设备上 Supervisor 能载入、
  Manager 图标能落盘，但流程在「开始跑 5 步 CPC2 安装」之前就停了（下一步要在
  `run_all_steps` 之前/之内再细打点，确认是异常、文件句柄还是 pcall 吞掉）。
  同一台设备上 Manager 概览与模块页都能正常打开（26,863 / 7,197）。
- **擦除后的 o66：模块重新装回来了**。用「仓库表盘 + 末尾加一步
  `CMD_INSTALL`(2, `"hello_portable\0"`) + AUTORUN」的临时表盘跑一次：
  `install-result.txt` 里是 **`result=5 err=0`（COMPLETED）**，
  `/canopus/registry.bin` slot 0 = `hello_portable`。随后装回仓库表盘，
  Manager 概览与模块页都能打开（26,863 / 7,197）。
- **仍未解决的一点**：在这台 o66 上，只要在「屏幕关着」的时候做页面推入
  （例如连续几次 Run 里的 OPEN_MANAGER），设备会重启（mi logo）。干净设备上
  载入不再崩，但「灭屏期间 push」这条仍然危险——下一步应让 OPEN_MANAGER 只在
  屏幕点亮时执行（表盘侧先确认屏幕状态，或在 Supervisor 侧检查
  `sub_C448910()==2` 再 push）。
