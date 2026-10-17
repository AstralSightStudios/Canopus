# Canopus 微内核服务对象规范 (Service Objects Specification)

版本：2.1 (2026-10-15)  
C 规范头文件：`sdk/c/canopus_services.h`  
Rust SDK 模块：`canopus_runtime::{fs, kv, mem, time, hook, haptics, display, bus, system}`

---

## 1. 架构总览与微内核能力分层

Canopus 采用**微内核服务对象（Service Objects）**架构。穿戴设备模块不再直接绑定底层硬件或特定固件版本的裸机符号，而是通过入口上下文传入的类型安全、面向对象的一级服务对象（Service Objects）调用系统能力。

在 **Xiaomi Band 10 Pro**（NuttX 内核）与 **Xiaomi Band 11**（Vela 内核）上，模块面向完全一致的 Object 编程；Supervisor 负责将调用自动转接（Dispatch）到底层原生系统调用，并在不支持的特定子系统上执行安全降级（统一返回 `CANOPUS_SVC_ERR_UNSUPPORTED = -38`）：

```text
                               ┌──────────────────────────────┐
                               │     Canopus Module (C/Rust)  │
                               └──────────────┬───────────────┘
                                              │ ctx->object
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                                 Canopus Context v2                                          │
├─────────────┬─────────────┬─────────────┬─────────────┬─────────────┬─────────────┬─────────┤
│   ctx->fs   │   ctx->kv   │  ctx->mem   │  ctx->time  │  ctx->hook  │ ctx->haptics│ctx->disp│
│  (文件系统) │  (键值存储) │  (动态堆)   │ (时钟/定时) │ (通用 Hook) │ (振动马达)  │(屏幕控制│
├─────────────┼─────────────┼─────────────┼─────────────┼─────────────┼─────────────┼─────────┤
│  ctx->bus   │   ctx->bt   │  ctx->log   │ ctx->notify │ ctx->system │             │         │
│  (事件总线) │  (L2 蓝牙)  │ (结构化日志)│ (通知弹窗)  │ (硬件/电量) │             │         │
└──────┬──────┴──────┬──────┴──────┬──────┴──────┬──────┴──────┬──────┴─────────────┴────┬────┘
       │             │             │             │             │                         │
       ▼             ▼             ▼             ▼             ▼                         ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                            Supervisor 平台转接层 (Platform Dispatcher)                       │
├─────────────────────────────────────────────┬───────────────────────────────────────────────┤
│          Xiaomi Band 10 Pro (NuttX)         │             Xiaomi Band 11 (Vela)             │
│   (nuttx.vfs / mm_malloc / FPB remap)       │        (vela.io / B11_UMEM / FPB remap)       │
└─────────────────────────────────────────────┴───────────────────────────────────────────────┘
```

---

## 2. 核心服务对象规范详解

### 2.1 `ctx->fs` — 生产级文件系统对象 (FileSystem)

提供完整、安全的 POSIX 风格文件与目录 IO 能力。

#### 常量与标志位
```c
#define CANOPUS_FS_READ     0x0001u   /* 只读模式 */
#define CANOPUS_FS_WRITE    0x0002u   /* 只写模式 (截断或新建) */
#define CANOPUS_FS_APPEND   0x0004u   /* 追加模式 */
#define CANOPUS_FS_CREATE   0x0008u   /* 文件不存在时自动创建 */
#define CANOPUS_FS_TRUNC    0x0010u   /* 打开时清空截断文件 */
#define CANOPUS_FS_RDWR     (CANOPUS_FS_READ | CANOPUS_FS_WRITE)

#define CANOPUS_SEEK_SET    0         /* 从文件起始定位 */
#define CANOPUS_SEEK_CUR    1         /* 从当前偏移定位 */
#define CANOPUS_SEEK_END    2         /* 从文件末尾定位 */

#define CANOPUS_FS_MAX_OPEN 16u       /* 单模块最大并发打开 16 个句柄 */
#define CANOPUS_FS_PATH_MAX 128u      /* 支持多级安全子路径 */
```

#### 方法列表
```c
struct canopus_fs_obj {
    int32_t (*open)(const struct canopus_context_v1 *ctx, const char *path, uint32_t flags);
    int32_t (*read)(const struct canopus_context_v1 *ctx, int32_t fd, void *buffer, uint32_t size);
    int32_t (*write)(const struct canopus_context_v1 *ctx, int32_t fd, const void *buffer, uint32_t size);
    int64_t (*seek)(const struct canopus_context_v1 *ctx, int32_t fd, int64_t offset, int32_t whence);
    int64_t (*tell)(const struct canopus_context_v1 *ctx, int32_t fd);
    int64_t (*size)(const struct canopus_context_v1 *ctx, int32_t fd);
    int32_t (*sync)(const struct canopus_context_v1 *ctx, int32_t fd);
    int32_t (*close)(const struct canopus_context_v1 *ctx, int32_t fd);
    int32_t (*remove)(const struct canopus_context_v1 *ctx, const char *path);
    int32_t (*rename)(const struct canopus_context_v1 *ctx, const char *old_path, const char *new_path);
    int32_t (*stat)(const struct canopus_context_v1 *ctx, const char *path, struct canopus_fs_stat *out_stat);
    int32_t (*exists)(const struct canopus_context_v1 *ctx, const char *path);
    int32_t (*mkdir)(const struct canopus_context_v1 *ctx, const char *path);
    void *  (*opendir)(const struct canopus_context_v1 *ctx, const char *path);
    int32_t (*readdir)(const struct canopus_context_v1 *ctx, void *dir, struct canopus_fs_dirent *out_entry);
    int32_t (*closedir)(const struct canopus_context_v1 *ctx, void *dir);
    /* 便捷整文件读写方法 */
    int32_t (*read_all)(const struct canopus_context_v1 *ctx, const char *path, void *buffer, uint32_t max_size);
    int32_t (*write_all)(const struct canopus_context_v1 *ctx, const char *path, const void *buffer, uint32_t size);
};
```
- **路径形式**（其余一律 `CANOPUS_SVC_ERR_ARGUMENT`）：
  - `name`：模块私有扁平文件，`/data/canopus/m.<id>.<name>`（旧行为）；
  - `dir/sub/name`：模块私有子树，`/data/canopus/m.<id>/dir/sub/name`；私有根在首次写入时创建，更深层目录用 `mkdir` 逐级创建；
  - `/data/...` 绝对路径，只接受下列共享根：`/data/offlinelog/`（读写）、`/data/canopus/shared/`（读写，模块间共享）、
    `/data/app/`（只读，Band 10 Pro 包目录）、`/data/quickapp/`（只读，Band 11 包目录）；根之外为 `ERR_ACCESS`，写只读根也是 `ERR_ACCESS`。
  - 路径段限 `[A-Za-z0-9_.-]`、不以 `.` 开头，所以 `..`、`//`、末尾 `/` 都被拒绝。
  这是接口约定，不是安全边界：本设备没有进程隔离（见 `security-model.md`）。
- **错误码**：固件返回的 errno 如实映射——文件或父目录不存在是 `ERR_NOTFOUND(-2)`（可据此识别"配置文件尚未创建"），
  另有 `ERR_EXIST`、`ERR_ACCESS`、`ERR_NOTDIR`、`ERR_ISDIR`、`ERR_NOSPACE`、`ERR_BUSY`，其余为 `ERR_IO`。
- **打开标志**：`READ` 只读已有文件；`WRITE` 创建或截断；`WRITE|APPEND` 保留内容并在文件尾写入（`APPEND`/`TRUNC`/`CREATE` 必须与 `WRITE` 同用）；
  `READ|WRITE` 保留内容读写。其余位为 `ERR_ARGUMENT`。固件的 `O_TRUNC` 未证实，截断经 `unlink` 后新建实现。
- **目录**：`mkdir` 调用固件 `mkdir`（单层，已存在为 `ERR_EXIST`）；`opendir("")`/`opendir(".")` 是模块私有目录，不再列出 `/data/canopus` 里别的模块的文件；
  `stat` 用 `opendir` 判断目录，`mtime` 恒为 0（固件无已恢复的时间来源）。
- **`sync`**：目前没有已恢复的 `fsync` 入口，返回 `ERR_UNSUPPORTED`；KV 事务的"写入后同步"在平台提供 `file_sync` 时才执行。

---

### 2.2 `ctx->kv` — 模块轻量持久化键值存储 (KeyValue Store)

配置、偏好设置与状态快照按模块保存到 `/data/canopus/kv_<module_id>.bin`。
首次访问以及两种 descriptor 注册入口共用恢复路径；构造函数里写入的值不会被后续注册覆盖。
变更先完整写入临时文件、检查 write/sync（平台提供时）/close，再 rename 替换正式文件；
失败返回负值，并保留原内存值和上一份有效文件。该事务保证不等于已验证掉电刷盘，底层 VFS 的物理持久性仍需设备证据。

```c
struct canopus_kv_obj {
    int32_t (*get_int)(const struct canopus_context_v1 *ctx, const char *key, int32_t default_val);
    int32_t (*set_int)(const struct canopus_context_v1 *ctx, const char *key, int32_t val);
    int32_t (*get_string)(const struct canopus_context_v1 *ctx, const char *key, char *buf, uint32_t buf_size);
    int32_t (*set_string)(const struct canopus_context_v1 *ctx, const char *key, const char *val);
    int     (*get_bool)(const struct canopus_context_v1 *ctx, const char *key, int default_val);
    int32_t (*set_bool)(const struct canopus_context_v1 *ctx, const char *key, int val);
    int32_t (*remove)(const struct canopus_context_v1 *ctx, const char *key);
    int32_t (*clear)(const struct canopus_context_v1 *ctx);
};
```

---

### 2.3 `ctx->mem` — 动态内存分配与追踪对象 (Memory)

为模块提供无人工上限约束的堆分配管理，仅受固件实际系统堆大小限制。

```c
struct canopus_mem_obj {
    void*    (*alloc)(const struct canopus_context_v1 *ctx, uint32_t size);
    void*    (*calloc)(const struct canopus_context_v1 *ctx, uint32_t num, uint32_t size);
    void*    (*realloc)(const struct canopus_context_v1 *ctx, void *ptr, uint32_t new_size);
    void     (*free)(const struct canopus_context_v1 *ctx, void *ptr);
    uint32_t (*available)(const struct canopus_context_v1 *ctx);  /* 查询可用堆内存预算 */
    uint32_t (*used)(const struct canopus_context_v1 *ctx);       /* 查询当前模块已用内存 */
};
```
- **动态链表管理**：内联头部追踪每个块大小与所有权，支持任意数量的分配。
- **自动垃圾回收**：模块卸载时，Supervisor 自动释放遗留内存，绝不泄漏。

---

### 2.4 `ctx->time` — 时钟与模块定时器 (Time & Timers)

- `sleep_ms` 等待完整请求时长。host 使用可重试的 nanosleep；设备在单调时钟上等待，
  无已批准 OS sleep 原语时是忙等，ARM `yield` 提示不等于调度器休眠。
- timer 周期范围为 `1..CANOPUS_TIMER_MAX_INTERVAL_MS`（`0x7fffffff` 毫秒），使用环绕安全的截止时间比较。
- 分发串行执行，回调内 sleep 不会递归进入另一轮 timer 分发。漏掉的周期合并，不追补无限次回调。
- Band 10 Pro 普通 UI 构建使用已批准的 `lv_timer_create`，Supervisor 的心跳与其同为 boot-resident，
  不支持热卸载。启动 timer 必须在模块/UI 所属线程执行，不能从中断或任意 worker 调用。
- Band 11 原生 timer 地址仍为 restricted，NSH headless 构建也没有 UI 事件源；这些配置的
  `timer_create` 明确返回 `CANOPUS_SVC_ERR_UNSUPPORTED`，不会注册一个永远不触发的 timer。
- 当前 `realtime_ms` 仍返回单调时间的扩展值，不应作为已验证的 UTC 时钟使用。

```c
typedef void (*canopus_timer_callback_t)(const struct canopus_context_v1 *ctx, int32_t timer_id, void *user_data);

struct canopus_time_obj {
    uint32_t (*monotonic_ms)(const struct canopus_context_v1 *ctx);
    uint64_t (*realtime_ms)(const struct canopus_context_v1 *ctx);
    void     (*sleep_ms)(const struct canopus_context_v1 *ctx, uint32_t ms);
    int32_t  (*timer_create)(const struct canopus_context_v1 *ctx, uint32_t interval_ms, int periodic, canopus_timer_callback_t cb, void *user_data);
    int32_t  (*timer_start)(const struct canopus_context_v1 *ctx, int32_t timer_id);
    int32_t  (*timer_stop)(const struct canopus_context_v1 *ctx, int32_t timer_id);
    int32_t  (*timer_destroy)(const struct canopus_context_v1 *ctx, int32_t timer_id);
};
```

---

### 2.5 `ctx->haptics` — 振动马达与触感反馈 (Haptics)

```c
#define CANOPUS_HAPTIC_CLICK        1u  /* 短促点击反馈 (20ms) */
#define CANOPUS_HAPTIC_DOUBLE_CLICK 2u  /* 双击震感 (20ms + 60ms 间隔 + 20ms) */
#define CANOPUS_HAPTIC_HEAVY        3u  /* 强震动反馈 (100ms) */
#define CANOPUS_HAPTIC_ALERT        4u  /* 警报长震动 (250ms) */
#define CANOPUS_HAPTIC_TICK         5u  /* 微弱滴答 (10ms) */

struct canopus_haptics_obj {
    int32_t (*vibrate)(const struct canopus_context_v1 *ctx, uint32_t duration_ms);
    int32_t (*vibrate_pattern)(const struct canopus_context_v1 *ctx, const uint16_t *pattern_ms, uint32_t count);
    int32_t (*vibrate_preset)(const struct canopus_context_v1 *ctx, uint32_t preset);
    int32_t (*cancel)(const struct canopus_context_v1 *ctx);
};
```
- `vibrate` 取 1..5000 ms；`vibrate_pattern` 从 **ON** 开始交替"振/停"（`pattern_ms[0]` 是第一次振动），最多 16 项，零项跳过；
  第二步起由 Supervisor 的定时器泵按 10 ms 精度推进（没有泵的目标，多步序列返回 `ERR_UNSUPPORTED`，单次振动不需要泵）。
  序列按计划时间推进，泵迟到时缩短当前脉冲而不累积漂移。`DOUBLE_CLICK` 是 20 ms / 60 ms 间隔 / 20 ms。
- 同一时刻只有一次振动：任何模块的新请求替换正在播放的；`cancel` 只停止调用者自己的振动；模块卸载时其振动被停止。
- 固件返回的拒绝如实传回（用户关闭振动为 `ERR_STATE`，独占振动占用为 `ERR_BUSY`），目标没有马达入口时全部为 `ERR_UNSUPPORTED`，不再"假装成功"。

---

### 2.6 `ctx->display` — 屏幕背光、防息屏 WakeLock 与分辨率 (Display)

```c
#define CANOPUS_WAKELOCK_SCREEN 1u   /* 保持手环屏幕常亮 */
#define CANOPUS_WAKELOCK_CPU    2u   /* 保持 CPU 处于高频工作状态 */

struct canopus_display_obj {
    uint32_t (*get_brightness)(const struct canopus_context_v1 *ctx);
    int32_t  (*set_brightness)(const struct canopus_context_v1 *ctx, uint32_t percent);
    int      (*is_screen_on)(const struct canopus_context_v1 *ctx);
    int32_t  (*wake_screen)(const struct canopus_context_v1 *ctx);
    int32_t  (*acquire_wake_lock)(const struct canopus_context_v1 *ctx, uint32_t lock_flags);
    int32_t  (*release_wake_lock)(const struct canopus_context_v1 *ctx, int32_t lock_id);
    void     (*get_resolution)(const struct canopus_context_v1 *ctx, uint32_t *width, uint32_t *height);
};
```
- `is_screen_on`：1 仅表示亮屏（息屏显示 AOD 与熄屏都是 0）；目标没有状态来源时返回 `ERR_UNSUPPORTED`（负值），不再恒为 1。
- `set_brightness(percent 0..100)` 经 Canopus 自己的屏幕会话覆盖用户亮度（关闭自动亮度并设定手动值）；百分比映射到固件手动范围 31..255，
  0% 对应固件最低值而不是黑屏。覆盖归最后设置的模块所有，该模块卸载时撤销。
  `get_brightness` 返回当前覆盖值，否则返回用户手动设定（`persist.screen.brightness_value`）；自动亮度开启或目标不可读时返回
  `CANOPUS_DISPLAY_BRIGHTNESS_UNKNOWN(0xFFFFFFFF)`。
- `wake_screen` 以固件 wake-lock 路径使用的来源 26 调用固件的 `screen_turn_on`，是"请求亮屏"，不保证返回时已亮。
- 唤醒锁由固件 PowerManager 的 wakelock（level 6，保持亮屏）实现，Supervisor 为每把锁保留一个原生对象并复用：
  `acquire_wake_lock` 返回正的锁 ID（带代数，旧 ID 失效后不会误释放新锁）；只能释放自己的锁（否则 `ERR_ARGUMENT`）；
  每个模块最多 4 把、全局最多 8 把（超出 `ERR_BUSY`）；模块卸载时全部释放。`WAKELOCK_SCREEN` 才有固件机制，
  单独的 `WAKELOCK_CPU` 返回 `ERR_UNSUPPORTED`，`CPU|SCREEN` 被接受（亮屏时 CPU 本就保持唤醒）。该锁不会主动点亮已熄灭的屏幕，需要时先调用 `wake_screen`。

---

### 2.7 `ctx->bus` — 模块间与系统事件总线 (Event Bus)

基于发布/订阅模型（Pub/Sub），支持模块间跨组件通信以及监听系统广播事件：

```c
typedef void (*canopus_bus_callback_t)(const struct canopus_context_v1 *ctx,
                                       const char *topic,
                                       const void *payload,
                                       uint32_t len,
                                       void *user_data);

struct canopus_bus_obj {
    int32_t (*publish)(const struct canopus_context_v1 *ctx, const char *topic, const void *payload, uint32_t len);
    int32_t (*subscribe)(const struct canopus_context_v1 *ctx, const char *topic, canopus_bus_callback_t cb, void *user_data);
    int32_t (*unsubscribe)(const struct canopus_context_v1 *ctx, int32_t sub_id);
};
```
- **系统内置广播主题**（只由 Supervisor 发布，模块向这三个主题 `publish` 返回 `ERR_ACCESS`，不能伪造）：
  - `"canopus/screen/state"`：负载 1 字节 `CANOPUS_SCREEN_*`（0 熄屏、1 AOD、2 亮屏）
  - `"canopus/battery/level"`：负载 2 字节，电量百分比与充电标志
  - `"canopus/bt/state"`：负载 2 字节，蓝牙适配器状态与手机连接（1 连接、0 断开、0xFF 未知）
  订阅这些主题会启动 Supervisor 的采样：每 500 ms 从定时器泵读一次设备，值变化时发布，新订阅者在下一次采样时收到一次当前值。
  没有定时器泵的目标订阅系统主题返回 `ERR_UNSUPPORTED`，不会得到一个永远不触发的订阅。

---

### 2.8 `ctx->hook` — 通用动态函数拦截引擎 (Universal Hook)

专为 ARMv8-M Cortex-M33 架构设计：

```c
typedef enum canopus_hook_type {
    CANOPUS_HOOK_AUTO    = 0,  /* Flash 用已注册 RAM 槽位，RAM Thumb 函数用跳板 */
    CANOPUS_HOOK_FPB     = 1,  /* 兼容枚举：仅接受已注册 RAM 槽位，不操作 FPB */
    CANOPUS_HOOK_INLINE  = 2,  /* 强制 Thumb-2 8字节代码跳板 */
    CANOPUS_HOOK_POINTER = 3,  /* 强制 RAM 指针变量原子置换 */
} canopus_hook_type_t;

struct canopus_hook_obj {
    int32_t (*install)(const struct canopus_context_v1 *ctx,
                       uintptr_t target_fn,
                       uintptr_t hook_fn,
                       uintptr_t *orig_fn,
                       canopus_hook_type_t hook_type);
    int32_t (*uninstall)(const struct canopus_context_v1 *ctx, int32_t hook_id);
    int     (*is_hooked)(const struct canopus_context_v1 *ctx, uintptr_t target_fn);
    int32_t (*register_slot_map)(const struct canopus_context_v1 *ctx,
                                 uintptr_t flash_fn, uintptr_t ram_slot);
};
```
- **Flash 固件拦截**：Cortex-M33 不提供 FP_REMAP 指令重映射。引擎不写 Flash，也不配置断点硬件。
  开发者自行确认当前设备/固件的函数与 RAM dispatch/vtable 槽位，运行时调用
  `ctx->hook->register_slot_map(ctx, flash_fn, ram_slot)`，再安装 Hook；或直接对 RAM 槽位使用 POINTER。
  这只拦截经过该槽位的调用，不拦截其他直接调用。未提供映射的 Flash 请求返回 UNSUPPORTED；不内置固件地址表。
- **所有权与恢复**：映射绑定注册模块，槽位存储必须存活至模块清理；不能注册一个随后自行 free 的槽位。
  多个函数名解析到同一活动槽位时拒绝重复安装；原值包括 NULL 均精确恢复。释放模块先恢复 Hook，再清除映射，最后释放内存。
- **RAM Inline**：仅搬移明确可复制的 Thumb 指令；PC 相关、分支/返回、IT 块和未实现的编码返回 UNSUPPORTED。
  安装和卸载需要目标函数处于静止状态，不得与执行该函数的任务/中断并发修补代码。
- **Host**：原生 host 不模拟 Inline 成功；无映射的 AUTO 没有 Thumb 标签可供推导，也返回 UNSUPPORTED。
  原生 host 测试使用显式 POINTER；实际 Thumb 跳板另由 `tests/hook/test_thumb.py` 验证。

---

### 2.9 `ctx->system` & `ctx->bt` — 系统硬件、电量与蓝牙 L2 交互

- **`ctx->system`**：
  - `target_id`、`module_id`、`device_model`（"Xiaomi Band 10 Pro" 或 "Xiaomi Band 11"）、`firmware_version`
  - `battery_level`：0..100，固件尚未上报或目标不可读时为 `CANOPUS_SYSTEM_BATTERY_UNKNOWN(0xFFFFFFFF)`；
    `is_charging`：0 或 1，负值是错误（`ERR_STATE` 表示尚未上报，`ERR_UNSUPPORTED` 表示目标没有电池来源）
  - `uptime_ms`、`is_safe_mode`、`reboot`
  - `get_property(key, buf, max_len)`：读固件系统属性（`property_get`），key 限 `[A-Za-z0-9._-]` 且不超过 63 字符，
    返回复制的长度（超出 `max_len - 1` 被截断），不存在为 `ERR_NOTFOUND`，目标没有读取入口为 `ERR_UNSUPPORTED`
- **`ctx->bt`**：
  - `l2_send(ctx, channel, payload, len)`：经 WearPacket Layer 2（如 0x43 Channel）向手机配套 App 发送数据
  - `l2_register_rx(ctx, channel, cb, user_data)`：只接受模块通道 `0x40..0x4F`（`0x43` 是 Supervisor 控制通道，其余通道属于固件原有处理器，
    返回 `ERR_ARGUMENT`）。已注册通道的加密帧经 SRAM L2 后端解密后，在模块/UI 任务上交给回调，不再转给固件处理器；
    同一通道只能一个模块持有（他人 `ERR_BUSY`），传 NULL 回调撤销，模块卸载时自动撤销。没有 L2 后端的目标返回 `ERR_UNSUPPORTED`。
    它与 Supervisor 的 CPC2 管理通信是两条独立路径。
  - `adapter_state`：固件蓝牙栈的适配器状态（0 关闭、4 开启，其余为过渡态），读不到为 `CANOPUS_BT_ADAPTER_UNKNOWN`
  - `is_connected`：存在 MiWear 配套链路时为 1，否则 0，目标无法判断时为负值（仅有 L2 后端的目标能判断）
  - `get_rssi`：目前没有固件来源，恒为 `CANOPUS_BT_RSSI_UNKNOWN(INT32_MIN)`，不再返回固定的 −55 dBm

### 2.10 硬件服务的固件来源与验证状态

haptics / display / 唤醒锁 / 电量 / 属性 / 蓝牙状态不再有占位实现。每一项都调用该 target 自己的固件入口，
入口地址只来自 `targets/<id>/symbols/` 的记录，经 `canopus target generate` 生成到 `canopus_target_config.h`
（Band 10 Pro）或 `canopus_band11_addresses.h`（Band 11）。没有记录的 target，对应钩子为空，服务返回
`ERR_UNSUPPORTED` 或文档化的"未知"值。

| 能力 | 固件入口（Band 10 Pro .036/.043） | 固件入口（Band 11 .139/.155） |
|---|---|---|
| 电量/充电 | PowerManager getter：`power_battery_ready/level/charging` | `power_battery_ready` + `power_battery_snapshot` |
| 唤醒锁 | `wakelock_create/acquire/release` | 同 |
| 屏幕状态 | `screen_get_state()` | 全局 `screen_state`（0 熄屏 / 1 AOD / 2 亮屏） |
| 亮屏 | `screen_turn_on(tag, source)` | 同 |
| 手动亮度 | `screen_session_create/start/stop/set_auto_brightness/set_brightness_value/restore_default`；读取用 `property_get_int32` | 同 |
| 振动 | `vibrator_hal_transact`（vibratord HAL 消息 4 起振、5 停止） | `vibrator_run`（波形参数块）+ `vibrator_cancel(id)` |
| 属性 | `property_get`、`property_get_int32` | 同 |
| 目录创建 | `mkdir` | 同 |
| 蓝牙适配器状态 | `bt_adapter_get_instance` + `bt_adapter_get_state` | `bt_shared_adapter` + `bt_adapter_get_state`（.177 也有） |

- **Band 11 .177**：本仓库没有其固件镜像，无法对新增入口做静态核对，所以除蓝牙适配器状态（该记录原本就是 STATIC_RECOVERED）外
  全部保持空钩子（`ERR_UNSUPPORTED`）。取得 exact 镜像后再按 `EVID-PLATFORM-HW-*` 的方法补充。
- **证据等级**：全部为 `STATIC_RECOVERED` / `restricted` / `approval_state: PENDING`，`device: not_probed`；
  .036 与 .139 的地址由 .043 与 .155 的已审阅函数体做"屏蔽分支与 PC 相对指令后的整体指令匹配"得到（镜像内唯一），
  MiWearScreen/PowerManager 的 `__func__` 字符串交叉引用互相印证。证据见各 target 的 `evidence/EVID-PLATFORM-HW-<ver>-001.json`。
  这些入口随普通构建默认包含（K16），默认包含不等于设备已验证或生产批准。
- **未覆盖**：`get_rssi`（无固件来源）、`fsync`、CPU 唤醒锁、通知图标表、`mtime`。它们明确返回不支持或未知，而不是成功。

---

## 3. 编程范例

### C 模块开发示例
```c
#include "canopus_services.h"

int32_t on_activate(const struct canopus_context_v1 *ctx)
{
    // 1. 系统与电量探测
    const char *model = ctx->system->device_model(ctx);
    uint32_t battery = ctx->system->battery_level(ctx);
    ctx->log->info(ctx, "Activated on target");

    // 2. 键值偏好存取
    int volume = ctx->kv->get_int(ctx, "volume", 50);
    ctx->kv->set_int(ctx, "volume", volume + 10);

    // 3. 完整文件读写
    int32_t fd = ctx->fs->open(ctx, "data.log", CANOPUS_FS_WRITE | CANOPUS_FS_APPEND | CANOPUS_FS_CREATE);
    if (fd >= 0) {
        ctx->fs->write(ctx, fd, "LOG ENTRY\n", 10);
        ctx->fs->close(ctx, fd);
    }

    // 4. 触觉振动反馈
    ctx->haptics->vibrate_preset(ctx, CANOPUS_HAPTIC_CLICK);

    // 5. 屏幕控制与分辨率
    uint32_t w, h;
    ctx->display->get_resolution(ctx, &w, &h);

    return 0;
}
```

### Rust 模块开发示例
```rust
use canopus_runtime::{fs, kv, mem, haptics, display, bus, system, time, CanopusContext};

pub fn activate(ctx: &CanopusContext) -> Result<(), i32> {
    // 1. 系统信息与硬件属性
    let model = system::target_id(ctx);
    let is_safe = system::is_safe_mode(ctx);

    // 2. 持久化 KV 偏好配置
    let vol = kv::get_int(ctx, "volume", 50);
    kv::set_int(ctx, "volume", vol + 10)?;

    // 3. 屏幕分辨率与常亮控制
    let (width, height) = display::get_resolution(ctx);
    let lock_id = display::acquire_wake_lock(ctx, canopus_abi::WAKELOCK_SCREEN)?;

    // 4. 便捷整文件读写
    fs::write_all(ctx, "hello.txt", b"Hello from Canopus Rust!")?;

    // 5. 触感马达振动反馈
    haptics::vibrate_preset(ctx, canopus_abi::HAPTIC_CLICK)?;

    Ok(())
}
```
