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
- **沙箱隔离**：路径映射到 `/data/canopus/m.<module_id>/`，自动拦截 `..` 目录穿越。

---

### 2.2 `ctx->kv` — 模块轻量持久化键值存储 (KeyValue Store)

专为配置、偏好设置、状态快照设计的高性能键值存储。内存带缓存加速，自动安全同步至磁盘。

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

### 2.4 `ctx->time` — 单调时钟、UTC 与模块定时器 (Time & Timers)

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
#define CANOPUS_HAPTIC_DOUBLE_CLICK 2u  /* 双击震感 (40ms) */
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
- **系统内置广播主题**：
  - `"canopus/screen/state"`：屏幕点亮/熄灭广播
  - `"canopus/battery/level"`：手环电量更新
  - `"canopus/bt/state"`：手机连接断开与恢复

---

### 2.8 `ctx->hook` — 通用动态函数拦截引擎 (Universal Hook)

专为 ARMv8-M Cortex-M33 架构设计：

```c
typedef enum canopus_hook_type {
    CANOPUS_HOOK_AUTO    = 0,  /* 智能检测：Flash 走 FPB，RAM 走 Trampoline，指针走 Slot */
    CANOPUS_HOOK_FPB     = 1,  /* 强制硬件 FPB 指令重映射 (0xE0002000) */
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
};
```
- **Flash 固件拦截**：针对 XIP NOR Flash（`0x0C000000..0x0D000000`）的只读原生函数，通过 **Cortex-M33 FPB 硬件重映射** 实现零 Flash 擦写拦截，零 HardFault 风险。

---

### 2.9 `ctx->system` & `ctx->bt` — 系统硬件、电量与蓝牙 L2 交互

- **`ctx->system`**：
  - `target_id`、`module_id`、`device_model`（"Xiaomi Band 10 Pro" 或 "Xiaomi Band 11"）、`firmware_version`
  - `battery_level`（0..100%）、`is_charging`（0 或 1）
  - `uptime_ms`、`is_safe_mode`、`reboot`、`get_property`
- **`ctx->bt`**：
  - `l2_send(ctx, channel, payload, len)`：经 WearPacket Layer 2（如 0x43 Channel）向手机配套 App 发送数据
  - `l2_register_rx(ctx, channel, cb, user_data)`：注册监听手机发往手环的特定 L2 Channel 报文
  - `is_connected`、`get_rssi`、`adapter_state`

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
