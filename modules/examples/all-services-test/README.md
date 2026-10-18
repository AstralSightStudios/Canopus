# Service Objects 与 Hook 冒烟测试

`services_test` 是设备/模拟器中的服务 API 冒烟模块，不是所有方法的行为覆盖证明。
当前有 20 个报告组。每组输出 `[PASS]`、`[FAIL]` 或 `[SKIP]`；不支持的能力不得伪装成通过。
任一失败会使模块 activate 返回负值。

## 验证范围

- Hook：固定的可复制 Thumb 算术入口、trampoline 调用和恢复、显式 POINTER、设备 AUTO 指针槽位、短函数/重复请求拒绝，以及模块自有 RAM dispatch 映射。
  映射测试不修改真实固件 vtable，不证明某个固件 Flash 函数已被拦截。
- KV：int/string/bool、fallback、remove/clear，并留存跨重启 probe。
- FS：读写、seek/tell/size/sync、rename、stat、目录接口调用。
- MEM：alloc/realloc/calloc/free、数据保留、溢出拒绝和使用量。
- SYSTEM：身份/版本/电量等 API，get_property 返回契约；reboot **只检查入口存在，不执行重启**。
- TIME：时钟读取与完整 sleep 时长；有事件源时测试 timer callback/stop/destroy，否则单独报告 SKIP。
- Haptics/Display/Bus/Bluetooth/Log/Notify：API 返回值、同步 bus dispatch 等冒烟检查；返回成功不替代硬件效果验证。
  Bluetooth L2 send 尚可返回 UNSUPPORTED；通知在 headless 配置明确 SKIP。
- UI：使用模块自有 snapshot 测试后端检查 builder/tree 生命周期，不验证屏幕渲染。

## Host 回归与调用门禁

```bash
make -C tests/host run
cargo test -p canopus-abi --test services_coverage
python3 tests/hook/test_thumb.py
```

- host 回归覆盖映射生命周期、物理槽位别名、NULL 恢复、失败加载/激活资源清理、KV 注册路径/原子替换/I/O 失败、timer 环绕/事件驱动/回调重入。
- `make run` 还运行 `-ffreestanding` 的 sleep 测试，确保实际设备分支不会因固定循环预算提前返回。
- Rust 门禁检查 **93 个方法的直接调用语法 + 1 个 reboot 入口检查**，精确区分 `read` 和 `read_all`，忽略注释/字面量等。
  它是 source call-site gate，不检查方法是否真的执行或断言是否充分，不应称为“100% 行为覆盖”。
- Thumb 测试构建真实非 `CANOPUS_HOST` 引擎，用 Unicorn 检查跳板、PC/IT 拒绝和只读 Flash。
  缺 clang/ld.lld/Unicorn 时退出 77；结果仅属 host/emulator 级证据。

## 构建

```bash
canopus build --manifest modules/examples/all-services-test/Canopus.toml
canopus package --manifest modules/examples/all-services-test/Canopus.toml
```

生成物包括五个受支持 target。没有修改任何 target 的批准状态。

## Band 10 Pro `.043` Mirage 流程

```bash
python3 scripts/emu/run_services_test.py
```

脚本自行构建 headless Supervisor 和模块，使用独立设备 `canopus-services-043`，
安装/启用后执行两次断电重启和恢复。除核对 report/probe 与模块状态外，第二次恢复必须读到 `KV_REBOOT_PASS`。
缺 Mirage、CLI 或固件时显式退出 77；失败退出非零；默认清理临时目录并关闭/删除测试设备。
`--keep-device` 可保留关闭后的设备用于检查。

此 NSH headless 配置没有 UI timer 事件源，通知也不可用，预期明确跳过这两组，不写“20/20 全绿”。
普通 Band 10 Pro 与 Band 11 UI 构建均使用 10 ms 的 boot-resident LVGL 心跳；Band 11 通过 exact-target
私有适配接入，符号仍为 restricted/PENDING。息屏暂停期间不保证触发，UI 重建后的自动恢复不在支持范围内。

## Band 11 `.155` Mirage 流程

```bash
python3 scripts/emu/band11_smoke.py 155 --services
```

与默认的 hello-portable SCAN 冒烟不同，services 模式必须真正启用并激活本模块，读取报告与 probe。
普通 UI 构建要求 timer 组执行，报告不接受跳过任何组。结果与缺失前提必须如实记录，
不能把 SCAN 成功当作服务测试通过。

## 报告路径与证据边界

服务私有命名空间中的文件为：

- `/data/canopus/m.services_test/test_report.txt`
- `/data/canopus/m.services_test/probe.txt`
- `/data/canopus/m.services_test/kv_reboot.txt`

报告格式：`SUMMARY: <passed>/<total> PASSED, FAILS=<n>, SKIPS=<n>`。
成功 probe 为 `TESTS_COMPLETE:<passed>/<total> SKIPS=<n>`，不再使用暗示全部行为通过的 `ALL_TESTS_PASS`。
日志和固件镜像不提交；模拟器通过不提升为 device verified 或 production approved。
