# ADR-CAN-007：移除 P65 target

状态：已采纳（2026-10-14，refactor-plan K3）

2026-09 对 Xiaomi P65 3.100.043 的实验性部署在设备上造成 Logo 开机循环（需 REC 清除数据），
触发原因未确定。P65 target、安装器、SDK feature、脚本、文档与测试已全部移除，不再支持。
事故期间产出的通用机制（CMR1 注册帧、registry 错误码细分与诊断、注入式文件 IO 加固）保留。
历史内容可在提交 `4dc1366` 及更早的 git 历史中找回。
