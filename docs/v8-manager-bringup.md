# LVGL v8 Manager bring-up (n66 / n67 / o66)

状态：**未完成**。o66 上 Supervisor 全链路跑通、Manager 已注册，但 Manager 页面
打不开；n66/n67 走同一条 v8 后端，结论相同。所有内容仅为 emulator/static 证据，
没有任何真机验证，能力状态不变（restricted / PENDING / not_probed）。

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
