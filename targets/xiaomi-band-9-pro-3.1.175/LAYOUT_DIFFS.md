# band-9 vs band-10 类型布局差异（已确认）

| 类型 | band-10 | band-9 | 证据 |
|---|---|---|---|
| launcher_app_descriptor | 64B | 60B | band-9 app_install 分配 0x3C(60)，LDM/STM 拷贝 15 word；深拷贝 +8 +0xC +0x14 +0x18 |
| interconnect 消息/连接 | 20B | 20B | sub_C113E7C 拷贝 5 word 确认一致 |

## LVGL 版本差异（用户确认）
- band-9 (3.1.175): **LVGL v8**
- band-10 (3.101.030): **LVGL v9**
- UI API 存在差异（widget 工厂、事件、样式接口等）
- 管理器在两个 target 上运行需要**条件编译**区分 v8/v9
- 其余 ABI 差异（interconnect_connect 5参 vs 4参、register_driver 3参 vs 4参、
  launcher_app_descriptor 60B vs 64B）同样由条件编译处理
