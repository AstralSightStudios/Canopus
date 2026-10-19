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
│  ctx->bus   │   ctx->bt   │  ctx->log   │ ctx->notify │ ctx->system │ctx->connect │ctx->ui  │
│  (事件总线) │  (L2 蓝牙)  │ (结构化日志)│ (通知弹窗)  │ (硬件/电量) │(与插件消息) │(UI/页面)│
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

#define CANOPUS_FS_MAX_OPEN 32u       /* 单模块最大并发打开 32 个句柄 */
#define CANOPUS_FS_PATH_MAX 256u      /* 支持多级安全子路径 */
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
- **路径形式**：
  - 相对路径（`name`、`dir/sub/name`）：模块私有，位于 `/data/canopus/m.<id>/`，首次写入时创建私有目录，更深层目录用 `mkdir` 逐级创建；
    不能用 `..` 爬出私有目录。每个模块是独立目录，所以 id 互为前缀的模块（`mod.a` 与 `mod.a.b`）不会重名。
  - 绝对路径：设备上的任意路径都可读写。路径先做词法规整（`.`、`..`、`//`、末尾 `/`）再判断，所以换写法不能绕过保护。
  - **唯一的例外是 Canopus 自己的状态**：`/data/canopus`（模块注册表、inbox、KV 文件和所有模块的私有目录）、`/dev/canopus`、`/canopus`，
    读写、删除、重命名、`mkdir`、`stat`、`opendir` 一律返回 `ERR_ACCESS`，不区分大小写。模块能访问自己的私有目录（包括用绝对路径），碰不到别的模块的。
  - 除此以外不设防：绝对路径可以破坏固件自己的文件，由调用方负责。控制字符以及越过根或私有目录的 `..` 返回 `ERR_ARGUMENT`。
  - 这不是安全边界：本设备没有进程隔离（见 `security-model.md`），模块本来就能直接调用固件。
- **错误码**：固件返回的 errno 如实映射——文件或父目录不存在是 `ERR_NOTFOUND(-2)`（可据此识别"配置文件尚未创建"），
  另有 `ERR_EXIST`、`ERR_ACCESS`、`ERR_NOTDIR`、`ERR_ISDIR`、`ERR_NOSPACE`、`ERR_BUSY`，其余为 `ERR_IO`。
- **打开标志**：`READ` 只读已有文件；`WRITE` 创建或截断；`WRITE|APPEND` 保留内容并在文件尾写入（`APPEND`/`TRUNC`/`CREATE` 必须与 `WRITE` 同用）；
  `READ|WRITE` 保留内容读写。其余位为 `ERR_ARGUMENT`。固件的 `O_TRUNC` 未证实，截断经 `unlink` 后新建实现。
- **目录**：`mkdir` 调用固件 `mkdir`（单层，已存在为 `ERR_EXIST`）；`opendir("")`/`opendir(".")` 是模块私有目录，不再列出 `/data/canopus` 里别的模块的文件。
- **`stat` 与 `mtime`**：调用固件 `stat`，从 104 字节 `struct stat` 取类型（目录/文件）、大小（+32）和修改时间（`st_mtim.tv_sec`，+72，秒）。
  `mtime` 的偏移来自 NuttX 标准布局与该结构体的已知字段（总长 104、`st_size`@32、三个 16 字节 timespec 从 +56 起），不是某个固件读取 mtime 的调用点；
  文件系统不记录时间时为 0，表示未知。大于 4 GiB 的文件 `size` 饱和为 `0xFFFFFFFF`。
- **`sync`**：调用固件 `fsync`；没有该入口的目标返回 `ERR_UNSUPPORTED`。KV 事务的"写入后同步"同样走它。

---

### 2.2 `ctx->kv` — 模块轻量持久化键值存储 (KeyValue Store)

配置、偏好设置与状态快照按模块保存到 `/data/canopus/kv_<module_id>.bin`。
值只存在于该文件里，Supervisor 不在内存中缓存：每次读取逐条扫描文件，常驻内存与模块数量无关。
每个模块最多 `CANOPUS_KV_MAX_PAIRS`（64）对；键不超过 31 字节、字符串值不超过 511 字节，超长返回 `ERR_ARGUMENT`（不截断），
第 65 个新键返回 `ERR_NOMEM`。
变更先把完整的新内容写入临时文件、检查 write/sync（平台提供时）/close，再 rename 替换正式文件；
失败返回负值，上一份有效文件保持不变。该事务保证不等于已验证掉电刷盘，底层 VFS 的物理持久性仍需设备证据。

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
- Band 10 Pro `.036/.043` 与 Band 11 `.139/.155` 的普通 UI 构建使用 10 ms 的 boot-resident LVGL 心跳。
  Band 11 从 exact-target 私有符号配置取入口，原有 `restricted / STATIC_RECOVERED / PENDING` 状态不变。
  创建/启动 timer 必须在模块/UI 所属线程执行，回调也在该线程串行运行，不能从中断或任意 worker 调用。
- LVGL 在息屏等暂停期间不保证心跳继续推进；恢复后合并漏掉的周期，不保证后台准时触发。
  心跳不支持热卸载，也不支持 UI 重建后的自动恢复。
- **后台 timer**（time 组版本 2）：`timer_set_background(ctx, id, 1)` 让这个 timer 在息屏期间继续触发。事件源是 L2 后端的 libuv owner timer
  （和 LVGL 心跳在同一个 UI 所属任务上，供应商在息屏时停掉 miwear-refresh/LVGL，它仍然运行），所以只有有 L2 后端的 `.043` / `.155` 提供，
  其余 target 的 time 组仍是版本 1，调用返回 `ERR_UNSUPPORTED`。息屏时只派发后台 timer，回调里**不能碰 UI**（LVGL 已暂停）；屏幕亮着时两个事件源都在跑，
  到期的 timer 只触发一次。息屏后 CPU 仍可能睡眠，需要持续运行就持有显示服务的唤醒锁。该事件源在 owner timer 启动前（L2 引导尚未完成）返回 `ERR_STATE`。
  这条路径只有静态与仿真依据，没有真机验证。线程和工作队列仍是 FORBIDDEN，不提供。
  模块代码已经会在这个 owner timer 的上下文里运行（手机经 L2 发来的命令在那里执行），所以没有引入新的任务。
- NSH headless 构建以及缺少 timer/单调时钟入口的 target 没有 UI 事件源；`timer_create` 明确返回
  `CANOPUS_SVC_ERR_UNSUPPORTED`，不会注册一个永远不触发的 timer。
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
    int32_t  (*timer_set_background)(const struct canopus_context_v1 *ctx, int32_t timer_id, int background); /* time 组版本 2 */
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
- 用户在系统里关闭了振动时返回 `ERR_STATE`；振动服务没有应答（例如没有马达服务的模拟器）返回 `ERR_IO`。
- `vibrate` 取 1..5000 ms；`vibrate_pattern` 从 **ON** 开始交替"振/停"（`pattern_ms[0]` 是第一次振动），最多 16 项，零项跳过；
  第二步起由 Supervisor 的定时器泵按 10 ms 精度推进（没有泵的目标，多步序列返回 `ERR_UNSUPPORTED`，单次振动不需要泵）。
  序列按计划时间推进，泵迟到时缩短当前脉冲而不累积漂移。`DOUBLE_CLICK` 是 20 ms / 60 ms 间隔 / 20 ms。
- 同一时刻只有一次振动：任何模块的新请求替换正在播放的；`cancel` 只停止调用者自己的振动；模块卸载时其振动被停止。
- 固件返回的拒绝如实传回（用户关闭振动为 `ERR_STATE`，独占振动占用为 `ERR_BUSY`），目标没有马达入口时全部为 `ERR_UNSUPPORTED`，不再"假装成功"。

---

### 2.6 `ctx->display` — 屏幕背光、防息屏 WakeLock 与分辨率 (Display)

```c
#define CANOPUS_WAKELOCK_SCREEN 1u   /* 保持手环屏幕常亮（唯一的锁类型） */

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
  `screen_turn_on` 要等显示所有者处理，必须在 UI 任务上调用；无头（NSH 驱动）的构建把模块跑在 NSH 任务上，会卡死在里面，所以无头构建不提供它（`ERR_UNSUPPORTED`）。
- 唤醒锁由固件 PowerManager 的 wakelock（level 6，保持亮屏）实现，Supervisor 为每把锁保留一个原生对象并复用：
  `acquire_wake_lock` 返回正的锁 ID（带代数，旧 ID 失效后不会误释放新锁）；只能释放自己的锁（否则 `ERR_ARGUMENT`）；
  每个模块最多 4 把、全局最多 8 把（超出 `ERR_BUSY`）；模块卸载时全部释放。`lock_flags` 只能是 `WAKELOCK_SCREEN`，
  其余取值（含 0）返回 `ERR_ARGUMENT`。该锁不会主动点亮已熄灭的屏幕，需要时先调用 `wake_screen`。

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
- **Band 11 的 Inline（代码视图）**：Umem 里的模块镜像在 PSRAM 指令别名（`0x1C…`）上执行，却要经数据视图（`+0x20000000`，`0x3C…`）写入，
  别名本身不可写。平台注册 `canopus_hook_code_view` 后由它独自决定能补丁什么：只有调用模块自己的可执行区域，
  补丁和 trampoline 都经数据视图写入，trampoline 放在同一块 Umem（首次 Inline 时分配，随 Supervisor 常驻，经别名执行），
  写完后做一次 D-cache clean + I-cache invalidate（`b11_publish_code`）；发布失败则还原并返回 UNSUPPORTED。
  不经 Umem 别名执行的镜像（默认堆的 MPU 回退）不在视图内，Inline 返回 UNSUPPORTED。
  没有代码视图的目标（Band 10 Pro）仍只在 SRAM 原地补丁。引擎不再把 `0x1C…` 当只读 Flash 一概拒绝；固件 Flash 仍然不可补丁。
  Unicorn 测试（`tests/hook/test_thumb.py`）用"代码视图只读、数据视图不可执行"的两段映射验证；Band 11 Mirage 上 `.155` 的 services 测试执行 Inline 用例。
  模拟器没有 cache 行为，D/I-cache 维护序列未经真机验证。
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
  - `l2_send(ctx, channel, payload, len)`：经 WearPacket Layer 2 向手机配套 App 发送数据。`0x43` 是 Supervisor 的管理通道，
    模块写入会破坏管理器的流，所以返回 `ERR_ARGUMENT`；模块与 AstroBox 插件之间的消息请用 `ctx->connect`（见 2.11）
  - `l2_register_rx(ctx, channel, cb, user_data)`：只接受模块通道 `0x40..0x4F`（`0x43` 是 Supervisor 控制通道，其余通道属于固件原有处理器，
    返回 `ERR_ARGUMENT`）。已注册通道的加密帧经 SRAM L2 后端解密后，在模块/UI 任务上交给回调，不再转给固件处理器；
    同一通道只能一个模块持有（他人 `ERR_BUSY`），传 NULL 回调撤销，模块卸载时自动撤销。没有 L2 后端的目标返回 `ERR_UNSUPPORTED`。
    它与 Supervisor 的 CPC2 管理通信是两条独立路径。
  - `adapter_state`：固件蓝牙栈的适配器状态（0 关闭、4 开启，其余为过渡态），读不到为 `CANOPUS_BT_ADAPTER_UNKNOWN`
  - `is_connected`：读固件 `system_all_state` 的 MiWear 链路状态——手机配套链路在线为 1，断开为 0；固件还没上报时为
    `ERR_STATE`，目标没有该入口为 `ERR_UNSUPPORTED`。`bt` 状态事件的 `link` 字节同源

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
| 目录创建/同步/状态 | `mkdir`、`fsync`、`stat` | 同 |
| 蓝牙适配器状态 | `bt_adapter_get_instance` + `bt_adapter_get_state` | `bt_shared_adapter` + `bt_adapter_get_state` |
| MiWear 链路 | `system_get_state(out, 1)` + 全局 `system_state` | 同 |
| 通知图标 | `lvx_notification_insert_message` 的 `small/large_icon_path` | 同一消息结构（`lvx_notification_insert_message`） |

- **MiWear 链路**：`system_all_state` 的指针全局在 `system_init_state` 之前为 NULL，此时固件的 getter 在 Band 10 Pro 上死循环、
  在 Band 11 上 panic，所以 Supervisor 先读指针，为空就返回 `ERR_STATE`，不调用 getter。getter 的状态 id 1 读结构体偏移 +1
  （`update_from_bt_state` 写入：2 在线、1 断开、0 未上报；偏移 +2 是 bt_acl，另一路）。
- **通知图标**：`post_with_icon(ctx, title, body, icon_path, vibrate)` 的 `icon_path` 是图片文件的绝对路径（`NULL`/`""` 为 Canopus 图标）；
  格式必须是固件能解码的 LVGL v9 ARGB8888 `.bin`（与 `/data/canopus/manager_icon.bin` 相同）。读取规则同文件服务：相对路径 `ERR_ARGUMENT`、
  Canopus 自己的状态 `ERR_ACCESS`、不存在 `ERR_NOTFOUND`、目录 `ERR_ISDIR`，任何拒绝都发生在发布通知与振动之前。
  Band 10 Pro .043 的消息复制函数（`0xCA9A254`）经静态审阅会逐字符串复制标题、来源、正文与两个图标路径（超过 512 字节截断），
  所以调用方的缓冲区无需长期有效；Band 11 以 unicorn 加真实固件验证过默认图标路径被写入消息节点，自定义路径没有单独运行验证。图片内容本身不做校验。
- **证据等级**：全部为 `STATIC_RECOVERED` / `restricted` / `approval_state: PENDING`，`device: not_probed`；
  .036 与 .139 的地址由 .043 与 .155 的已审阅函数体做"屏蔽分支与 PC 相对指令后的整体指令匹配"得到（镜像内唯一），
  MiWearScreen/PowerManager 的 `__func__` 字符串交叉引用互相印证。证据见各 target 的 `evidence/EVID-PLATFORM-HW-<ver>-001.json`。
  这些入口随普通构建默认包含（K16），默认包含不等于设备已验证或生产批准。
- **已移除**：`get_rssi`（没有稳定的固件来源）与仅 CPU 的唤醒锁，不再出现在 ABI 里。


### 2.11 `ctx->connect` — CanopusConnect：与 AstroBox 插件互发消息

模块与 AstroBox 插件按**模块 id** 互发文本或二进制消息（ADR-CAN-009）。走 Supervisor 的 L2 0x43 流，但有自己的 CPC2 opcode、
自己的发送队列，不占管理器的 pending 槽位，也不改管理器状态。只在有 L2 后端的 target 提供（目前 .043 / .155），
其它 target 的 `ctx->connect` 为空，用 `canopus_services_has(ctx, CANOPUS_SVC_CONNECT, 1)` 探测。

```c
#define CANOPUS_CONNECT_TEXT          1u   /* UTF-8 */
#define CANOPUS_CONNECT_BINARY        2u
#define CANOPUS_CONNECT_DATA_MAX      480u /* 一条消息一帧 */
#define CANOPUS_CONNECT_NOT_LISTENING 1    /* send 的返回值：没有插件在听，什么也没发 */

typedef void (*canopus_connect_callback_t)(const struct canopus_context_v1 *ctx, uint32_t kind,
                                           const void *data, uint32_t len, void *user_data);
struct canopus_connect_obj {
    int32_t (*send)(const struct canopus_context_v1 *ctx, uint32_t kind, const void *data, uint32_t len);
    int32_t (*listen)(const struct canopus_context_v1 *ctx, canopus_connect_callback_t cb, void *user_data);
    int     (*remote_listening)(const struct canopus_context_v1 *ctx);
};
```
- `listen(cb)`：接收插件发给本模块 id 的消息，`NULL` 撤销；回调在模块/UI 任务上执行，必须尽快返回。文本以 NUL 结尾，`len` 不含 NUL。
  模块卸载时自动撤销。
- `send(kind, data, len)`：发给正在监听本模块 id 的插件。返回 0 表示已排队；`CANOPUS_CONNECT_NOT_LISTENING`（正值）表示当前手机会话里没有插件在听，
  什么也没排队，不算错误；`ERR_BUSY` 表示发出队列（2 槽）已满；`ERR_ARGUMENT` 表示 kind 非法或超过 480 字节。
  `send` 的数据在返回前已复制，缓冲区可立即复用。
- `remote_listening()`：当前手机会话里有插件监听本模块时为 1，否则 0。监听随手机会话失效，重连后由 AstroBox 补发。
- 模块 id 须符合 ADR-CAN-003（`[a-z][a-z0-9_]{0,15}`），超过 16 字符的 id 无法被远端寻址。
- Rust：`canopus_runtime::connect::{send_text, send_binary, listen, unlisten, remote_listening}`，`send_*` 返回 `Delivery::{Queued, NotListening}`。

AstroBox 一侧见 `astrobox:psys-host-v4/canopusconnect`（`send` / `listen` / `unlisten`，需要插件权限 `canopusconnect.send` 与 `canopusconnect.listen`），
消息经插件导出的 `astrobox:psys-plugin-v4/canopusconnect.on-message` 送达。


### 2.12 `ctx->ui` / `ctx->ui_ext` — 语义 UI 树与模块页面

`ctx->ui` 是 `sdk/c/canopus_ui.h`（实现在 `runtime/ui`）的语义 UI 树构造器（页面、分区、文本、状态/操作/开关行、按钮、复选/单选、进度、图片、导航头、
列表/滚动/对话框等容器，ABI 1.4，最多 32 个节点）。`ctx->ui_ext` 补充 `node_set_style`、`node_set_layout`，
并让模块在 Manager 里提供自己的页面（ADR-CAN-010）：

```c
struct canopus_ui_ext_obj {
    int32_t (*node_set_style)(struct canopus_ui_tree_v1 *tree, uint32_t key, const struct canopus_ui_style_v1 *style);
    int32_t (*node_set_layout)(struct canopus_ui_tree_v1 *tree, uint32_t key, const struct canopus_ui_layout_v1 *layout);
    int32_t (*set_entry)(const struct canopus_context_v1 *ctx, const struct canopus_ui_entry_v1 *entry); /* NULL 移除 */
    int32_t (*close_page)(const struct canopus_context_v1 *ctx);
};
```
- 旧 Supervisor 没有 `ui_ext`：先查 `ctx->struct_size >= sizeof(struct canopus_context_v1)` 且 `ctx->ui_ext != 0`。
- `set_entry` 登记页面入口（`label` 最长 23 字节，可为 NULL；`on_open` 必填）。模块运行期间，它在 Manager 的详情页多一行「打开界面」。
  没有 Manager UI 的构建（无头）返回 `ERR_UNSUPPORTED`；入口随模块卸载一起清除，并先关闭它已打开的页面。
- 用户打开时在 UI 任务上调用 `on_open(ctx, ui, user)`：用 `ctx->ui->tree_begin(ui, &tree)` 等构树并 `tree_commit`，返回前必须已提交，
  否则页面不显示。`ui` 一直有效到 `on_close` 返回。之后点击某行 → `on_event(ctx, ui, key, event_id, user)`，在里面构建下一棵树。
- `on_back`（可选，两个家族都有）：标题的返回被使用；返回 0 表示已处理（例如弹出自己的路由），否则页面关闭。右滑返回是系统手势，不经过它。
  `on_close`：用户或 `close_page` 关闭页面时调用，卸载时不调用。`close_page` 结束调用者自己已打开的页面，没有则 `ERR_STATE`。
- 同一时刻只开一个模块页面；回调要尽快返回；页面标题只在页面创建时设置。Rust：`canopus_abi::{CanopusUiExtObj, UiEntryV1}`。
- 渲染后端（Band 10 Pro 与 Band 11 共用 `manager/target/canopus_ui_host.c`，Manager 自己的页面也走它）两个家族画同样的内容：导航页/导航头（页面标题）、
  分区/列表/滚动/分隔/间距（展平）、文本/富文本（标签，MiSans-Medium 24 px，TITLE 28 px）、各类行与按钮、图片、进度条（自动尺寸为整行宽的条），
  以及对话框——一条消息加两个按钮时是固件 msgbox，其它形状和没有恢复 msgbox 入口的目标按普通行内联。滑块已移除。
  不支持的节点类型提交失败，并保留上一版内容。
- 页面里不再出现的行、标签、图片、进度条被隐藏，再出现时按 key 复用同一个控件；内容放得下时页面不能拖动。端到端见 `scripts/emu/ui_pages.py`（Mirage）；`.043` 与 `.155` 已在真机验证（ADR-CAN-010）。

### 2.13 `ctx->launcher` — 模块自己的启动器入口

让模块像普通应用一样出现在系统启动器里，选中后打开它用 `ctx->ui_ext->set_entry` 登记的页面（ADR-CAN-011）：

```c
struct canopus_launcher_entry_v1 {
    uint32_t struct_size;
    const char *label;   /* 图标下的名字：UTF-8，1~23 字节 */
    const char *icon;    /* 固件能读的 LVGL 图片文件的绝对路径（至多 95 字节），NULL 为 Canopus 图标 */
};
struct canopus_launcher_obj {
    int32_t (*set_entry)(const struct canopus_context_v1 *ctx, const struct canopus_launcher_entry_v1 *entry);
};
```
- 旧 Supervisor 没有 `launcher`：先查 `ctx->struct_size >= sizeof(struct canopus_context_v1)` 且 `ctx->launcher != 0`；服务组为 `CANOPUS_SVC_LAUNCHER`。
- 还没登记页面入口返回 `ERR_STATE`；参数不合法 `ERR_ARGUMENT`；8 个入口（`CANOPUS_LAUNCHER_ENTRIES`）用完 `ERR_NOMEM`；
  没有 Manager UI 的构建不提供该对象。
- 返回时固件还没有注册：Supervisor 在 UI 心跳里分两个 tick 各调一次固件（先 `app_install`，再 `launcher_add`）。注册失败不会回报给已经返回的模块，
  只记入 Supervisor 的 `error_code`（`CANOPUS_SUP_ERR_LAUNCHER`）。
- **没有注销**：固件的 `app_launcher_del` 没有证据，所以入口留到下次重启；模块停止后选中它什么也不打开。同一模块再次调用，标签与图标相同返回 OK，
  不同返回 `ERR_BUSY`（直到重启）。入口不跨重启保存，模块每次被激活时再登记。
- Rust：`canopus_runtime::launcher::set_entry`、`canopus_abi::{CanopusLauncherObj, LauncherEntryV1}`。

---

## 3. 编程范例

### 模块入口：注册 descriptor

两种 loader 给构造函数的东西不同，模块只用 `sdk/c/canopus_module_entry.h` 这一个入口，不自己写注册代码：

| loader | 构造函数拿到什么 | 注册方式 |
|---|---|---|
| Band 10 Pro 原厂 modlib（`nuttx-modlib-elf32-rel`） | 没有 ctx（r0 无意义） | 经目标自己的 NuttX 入口向 `/dev/canopus` 写一帧 CMR1，先过 `canopus_identity_guard()` |
| Canopus 自己的 loader（Band 11；portable 模块） | r0 = ctx | `ctx->services->module_register(ctx, &descriptor)` |

```c
#include "canopus_module_entry.h"   /* 目标模块还需把该目标的 generated 目录放进 -I */

static const struct canopus_module_descriptor_v1 s_descriptor = { /* ... */ };
CANOPUS_MODULE_ENTRY(s_descriptor)  /* 或在自己的构造函数里调 canopus_module_register(ctx, &s_descriptor) */
```

编译期按目标的生成配置选路径（有 `CANOPUS_TARGET_ID` 且没有 `CANOPUS_SUP_CUSTOM_LOADER` 的是 CMR1，其余走服务表），所以同一份源码在两个系列上都能用。
portable 模块不带目标配置，只有服务表一条路，因此只能在 Canopus loader 的目标上加载。Rust 模块的构造函数是 `sdk/rust/c_shim/canopus_ctor.c`，同样经该入口注册 crate 导出的 `canopus_module_descriptor`。

两条路最后都进 `canopus_supervisor_register_descriptor`，它要求：descriptor 的 `module_id` 等于正在加载的槽位的 module id，`target_id` 是当前目标或 `portable-thumbv8m`。
不满足时注册被拒绝；脚手架和示例的 descriptor 因此必须写清单里的 id。

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
