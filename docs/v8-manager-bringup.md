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
- 打点工具：`-DCANOPUS_TARGET_UI_DIAG` 编译时把 2 字节标记追加到
  `/data/canopus/diag.txt`（构造器 `ST`、页面创建 `I<页号>`、点击 `C<行>`、事件 `E<行>`
  + 16 字节原始事件、推送 `K`/`X`）。设备上只有像 n66 那样能跑 Run 的表盘才加载
  Supervisor，所以打点前必须先让 Supervisor 起来。
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
