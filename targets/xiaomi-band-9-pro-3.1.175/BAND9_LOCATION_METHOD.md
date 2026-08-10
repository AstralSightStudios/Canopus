# Band-9 固件符号定位方法论

## 目标
把 band-10（xiaomi-band-10-pro-3.101.030）的每个 symbol，在 band-9 固件
（/Volumes/EXT0/old_fw_extract/data/ota/app/vela_ap.bin.i64）里定位对应函数，
得到 band-9 的入口/可调用地址。

## Band-9 固件事实
- IDA MCP 会话 id: `band9_research`（已打开）
- 版本 3.1.175，build CONBINE_LTALM054_T1175_04141021_release_5793
- 固件 sha256: 4f43b325addd6d9e6e7c7e2a4d00ffe3f23d5fb1560d8fe503544002ac1f516b
- 地址映射: 文件地址 0xC... ↔ 虚拟 0x2C... (+0x20000000)。IDA 返回 canonical 0xC... 地址。
  - 查字符串 xref 用虚拟地址: `0x2C<fileaddr的低24位>`
  - 例如字符串文件地址 0xC5E99E0 → 虚拟 0x2C5E99E0
- 无符号名（剥除了），函数叫 sub_CXXXXXX
- 与 band-10 代码同构但地址全不同、重新编译过（字节 pattern 不可用）

## 定位方法（每个 symbol）
1. 从 band-10 symbol 找**独有字符串锚点**（日志格式、文件名、常量字符串）
   - 在 band-9 用 `find_regex` 搜该字符串（文件地址）
   - 转虚拟地址（+0x20000000），用 `xrefs_to` 找引用它的函数
2. 反编译该函数，验证签名与 band-10 一致
   - 关键：band-9 签名可能与 band-10 有差异（如 interconnect_connect 从 4 参变 5 参），必须逐函数验证
3. 记录 band-9 入口地址（canonical，0xC...）
   - callable = entry | 1（Thumb 位）

## 已确认的 band-9 锚点（供交叉引用）
| band-10 函数 | band-10 地址 | band-9 地址 | 验证方法 |
|---|---|---|---|
| interconnect_connect | 0x0C2D2034 | sub_C1134D4 | miwear-server 字符串 xref |
| interconnect_send | 0x0C2D20C4 | sub_C114044 | __miwear_send 字符串 → 内部调用 |
| interconnect_close | 0x0C2D2198 | sub_C4F7170 | __miwear_connect_cb 内部调用 |
| quickapp_register_app | 0x0C527E38 | sub_C2C655C | quickapp_register_app 字符串 |
| uv_miwear_message_recv_cb | 0x0CAA35A0 | sub_C43BA64 | uv_miwear_message_recv_cb 字符串 |
| app_install | 0x0CA519AC | sub_C44B5D0 | app_install 日志字符串 / off_200A28D0 |
| clock_gettime | 0x0C1EC8B4 | sub_C0BBB10 | clock/clock_gettime.c 字符串 |

## 输出
把每个 symbol 的 band-9 地址写入 /Volumes/EXT0/Canopus/targets/xiaomi-band-9-pro-3.1.175/symbols/
下对应 JSON（参考已存在的 band-9 symbol 格式），并在
/Volumes/EXT0/Canopus/targets/xiaomi-band-9-pro-3.1.175/INTERCONNECT_MAPPING.md 追加记录。
若某函数在 band-9 中找不到（可能已删除/改名），明确报告并说明依据。

## Band-9 UI 后端 (LVGL v8 / BES lvx) 定位记录 (2026-08-10)

Band-9 运行 LVGL v8 + BES 自研事件系统，与 band-10 的 LVGL v9 有结构差异。
以下函数均从 IDA `vela_ap.bin.i64`（会话 5f0f9ae8）通过调用链/字符串锚点定位，
写入 symbols/ 并由生成器产出 `generated_b9.rs`（40 callable）。

| band-9 函数 | band-9 地址 | band-10 对应 | 定位方法 |
|---|---|---|---|
| lvx_timer_create | sub_C25CB8C | ui.lv_timer_create | 从池 &dword_200C026C 分配 timer 节点 (cb@8/period@0/data@12/repeat=-1) |
| lvx_timer_delete | sub_C25B4B8 | ui.lv_timer_del | 链表移除 + 释放 |
| lvx_label_create | sub_C261660 | probe.lvx_label_create | row init sub_C2787A8 创建 primary/secondary |
| lvx_label_set_text | sub_C266C28 | probe.lvx_label_set_text | row init/setupwizard/settings 调用 |
| lvx_object_set_size | sub_C23DDE0 | probe.lvx_object_set_size | row factory (328,112) |
| lvx_object_align | sub_C23DE6A | probe.lvx_object_align | 分发 sub_C23DDCA(obj,x,y) |
| lvx_align_to | sub_C240CE8 | ui.lv_obj_align_to | settings 行链 (obj,prev,14,0,gap) |
| lvx_set_hidden | sub_C23E8F8/96E | ui.lv_obj_set_hidden | add/clear flag 0x10 (LV_OBJ_FLAG_HIDDEN) |
| lvx_style_apply | sub_C371CA0 | ui.lvx_style_apply | 4-arg (obj,style,opacity,state) |
| lvx_event_add | sub_C244F3C | ui.lv_obj_add_event_cb | 12 字节事件条目 [cb@0,user_data@4,code@8] |
| lvx_event_get_code | *(event+8) | ui.lv_event_get_code | 无独立函数，偏移读取 |
| lvx_event_get_user_data | *(event+12) | ui.lv_event_get_user_data | 分发 sub_C243F28 写入 |
| lvx_page_title_create | sub_C2781D4 | ui.lvx_page_title_create | 字符串 "lvx_page_title_create" |
| lvx_list_row_set_trailing | sub_C272E1C | (v9 内联) | row+112 kind；1=switch,12=forward |
| app_lookup | sub_C449334 | app_lookup | 遍历 app 链表 off_200A28B0 比对 app+16 |
| mm_alloc | sub_C0F24E0 | (nuttx) | mm_malloc heap dword_200B17F4 |
| mm_free | sub_C0F19DC | (nuttx) | mm_free(heap,mem) |

### band-9 与 band-10 的 UI 差异（模块条件编译依据）
- list_row_create：band-9 为 2 参 (parent, primary)，trailing 通过
  `lvx_list_row_set_trailing(row, kind, state)` 后置设置；band-10 为 4 参带 trailing。
- forward 行 trailing kind：band-9 = 12，band-10 = 3（TRAILING_FORWARD）。
- 页面 content：band-9 由页面外壳提供全局 content（dword_200CE038），无
  `lvx_content_create`；模块直接在固件页面 root 上渲染。
- 事件对象：band-9 回调收到的事件 code@+8、user_data@+12（dispatch sub_C243F28 写入）。

### band-9 BT 栈状态（device-pending）
- 已逆向：btm_gap（bt_adapter_get_instance / btm_create_bond / btm_remove_bond /
  btm_start_discovery / btm_stop_discovery / btm_reply_pair_request /
  btm_reply_pair_display / btm_set_scan_mode 接口分发）、nuttx mm_heap、
  app_lookup。
- Bluelet classic L2CAP / SDP 服务注册 / buffer / BT timer / 事件队列
  在当前逆向轮次中未定位到等价物（classic 函数名被深度剥除）；模块的
  band-9 媒体路径（AVDTP Source）相关绑定返回 ENOSYS 并标注 pending，
  需后续恢复 Bluelet classic L2CAP/SDP 层。
