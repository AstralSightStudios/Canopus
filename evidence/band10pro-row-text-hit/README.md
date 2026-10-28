# Band 10 Pro 行文字区域点按验证（2026-10-25）

问题：Band 11 上行的标题/描述文字是事件死区（命中对象只收自己的事件，行处理器挂容器上收不到），
Band 10 Pro 是否同样？结论：**没有这个问题**，10 Pro 的行对象从行内任意位置（含文字）都收得到事件。

## 模拟器证据（Mirage + 原生固件 3.101.043，设备 rowhit-043）

用原生固件自家的设置列表（其行与 Manager 的 `FW_LVX_LIST_ROW_CREATE` 行同族：
标题 + 描述 + trailing），像素级定位字形笔画后点按（`probe.py`，点前先验证落点亮度/笔画密度）：

| 帧 | 落点 | 结果 |
| --- | --- | --- |
| `00-settings.png` | — | 设置列表（我的设备/消息通知/…） |
| `01-after-text-tap.png` | (28,135) 行卡左下、(204,135) 标题字下缘 | 两处都进「我的设备」子页（两帧 hash 相同） |
| `11-text1.png` | (217,114)「我的设备」标题笔画正中（窗口密度 339） | 进「我的设备」子页 |
| `13-text2.png` | (112,235)「消息通知」标题笔画正中（密度 454） | 进「消息通知」子页 |
| `20-base3.png` → `21-after-sec-text.png` | (58,409)「接收消息时自动亮屏」描述笔画正中（密度 216） | **开关 OFF→ON**（像素证据：白钮左(255,255,255)/暗轨(64,64,64) → 蓝轨(39,122,247)/白钮右） |

边沿右滑返回后列表帧 hash 与进入前完全一致，导航观测可信。

## 真固件代码证据（IDA，mifw/analysis-p67-043/vela_ap.bin.i64）

- `lvx_list_row_create` @ `0x0C54CA3C`：`sub_C4C8514`（建行）→ `sub_C4C8560`（建内容）→
  `sub_C588628`（几何）→ `sub_C4C88F8`（收尾），**没有任何 `lvx_event_add`（0x0C5883F0）调用**。
- `sub_C4C8560` / `sub_C4C8208` / `sub_C4C843C` / `sub_C4C8038` / `sub_C4C8048`（建 icon、
  标题、描述、样式）逐一检查：**全部没有给子对象注册事件处理器**。
- 对照：`lvx_slider_bar_create`（`sub_C4CAC0C`）确实把处理器挂在子按钮对象上
  （`btn_add`/`btn_sub` 各自独立动作），说明该框架的事件注册本来就是按对象挂的。

推论：行的标签自己没有任何处理器，而点文字能触发行动作 → 事件分发必须把标签落点送到
行对象（标签命中透明或事件上溯到行）。因此**任何挂在行对象上的处理器都能收到行内任意
位置的点按**，与 Manager 的 `host_add_event(row, …)` 完全一致。Band 11 的修复（hit_extra
按命中对象补注册）在 10 Pro 属于冗余保险，已按家族 gate，不需要也不应移植到 10 Pro 路径。

## 复现

```sh
miragectl create rowhit-043 --firmware miwear.watch.p67tc_v3.101.043_full_a4ce8564
miragectl boot rowhit-043
python3 probe.py   # 依赖 scripts/emu/ui_pages.png_rows 做像素解析
```
