# 2026-09-15：注册管理器时的通知崩溃

`crash3.txt` 显示 launcher 已完成添加 Canopus，随后插入 UID
`0x43414e4f50555301`，进入提醒事件后 Bus Fault：PC `0x0c55fe06`、
LR `0x0c5595d5`、R5 `0`、BFAR `0x0000000d`。

故障指令为 `ldrb r3, [r5, #13]`。`notifications_message_reminder_start`
先取 list->head，再从 message+92 取 user_data。即使 message+90 的 is_focus
为零，它仍无条件读取 user_data+13 的 focus_version。原先把 +92 当作
可选 callback cookie、在普通通知中留空，导致了这次确定的空指针访问。

stock `notify_set_user_data` 在 `0x0c8ff218` 为普通消息分配并清零 16 字节上下文：
phone_data+0、focus_v1+4、focus_v2+8、phone_active+12、focus_version+13。
Canopus 两条通知各自持有独立的可写静态上下文，保持 phone/focus 数据与标志为零。
消息复制只借用该上下文，Supervisor 在本次启动期间驻留；不使用栈变量，
也不注册会释放上下文的 stock phone/focus 回调。通知仍正常请求提醒。

旧测试只执行插入、复制与定时器代码，把事件分发替换成了 stub。
修订后的测试执行真实 `ntf_event_all_cb → notifications_message_reminder_start`，
只模拟设置提供者与最后的提醒展示入口。恢复旧的空指针可重现同一故障 PC；
修复后的注册通知、模块安装通知、重复安装及错误固件身份路径均通过。
屏幕、振动、真实异步生命周期与设备内存压力仍需真机复测。

```sh
CANOPUS_TARGET=xiaomi-band-11-4.100.139 scripts/build_canopus_supervisor.sh
build/band11-tests/bin/python scripts/tests/band11_notification_firmware.py
```

证据：[EVID-NOTIFICATION-4139-002](../evidence/EVID-NOTIFICATION-4139-002.json)。
替换安装器前重启，使旧驻留 Supervisor 退出；按原方式将资源目录封装为表盘，
注册 Manager 后检查通知显示、关闭通知、再次打开通知列表及模块安装通知。
