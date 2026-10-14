# Canopus 微内核服务对象规范 (Service Objects Specification)

版本：2.0 (2026-10-15)  
头文件：`sdk/c/canopus_services.h` / Rust 模块：`canopus_runtime::{fs, mem, hook, time, system}`

---

## 1. 架构愿景与面向对象分层模型

Canopus 采用**微内核能力对象（Service Objects）**架构。模块不再直接与具体硬件或特定手环固件的内存符号硬绑定，而是通过入口上下文传入的面向对象接口（Object-Oriented Interfaces）使用系统能力。

无论运行在 **Xiaomi Band 10 Pro**（NuttX 内核）还是 **Xiaomi Band 11**（Vela 内核）上，模块均面向相同的 Object 编程；Supervisor 在模块装载时自动将对象方法指针转接（Dispatch）到底层固件的原生实现：

```text
                               ┌──────────────────────────────┐
                               │     Canopus Module (C/Rust)  │
                               └──────────────┬───────────────┘
                                              │ ctx->object
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                                 Canopus Context v2                                          │
├─────────────┬─────────────┬─────────────┬─────────────┬─────────────┬─────────────┬─────────┤
│   ctx->fs   │  ctx->mem   │  ctx->hook  │  ctx->time  │  ctx->log   │ ctx->notify │ ctx->sys│
│  (文件系统) │  (128K 堆)  │ (通用 Hook) │ (时钟/定时) │ (结构化日志)│ (通知弹窗)  │ (系统/电量)│
└──────┬──────┴──────┬──────┴──────┬──────┴──────┬──────┴──────┬──────┴──────┬──────┴────┬────┘
       │             │             │             │             │             │           │
       ▼             ▼             ▼             ▼             ▼             ▼           ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                            Supervisor 平台转接层 (Platform Dispatcher)                       │
├─────────────────────────────────────────────┬───────────────────────────────────────────────┤
│          Xiaomi Band 10 Pro (NuttX)         │             Xiaomi Band 11 (Vela)             │
│   (nuttx.vfs / heap_malloc / FPB remap)     │       (vela.io / B11_UMEM / FPB remap)        │
└─────────────────────────────────────────────┴───────────────────────────────────────────────┘
```

---

## 2. 核心服务对象规范

### 2.1 `ctx->fs` — 生产级完整文件系统 (FileSystem Object)

提供满足日常生产模块所需的完整 POSIX 风格文件与目录 IO 能力。

#### 核心常量与标志位
```c
#define CANOPUS_FS_READ     0x0001u   /* 只读模式 */
#define CANOPUS_FS_WRITE    0x0002u   /* 只写模式 */
#define CANOPUS_FS_APPEND   0x0004u   /* 追加模式 */
#define CANOPUS_FS_CREATE   0x0008u   /* 文件不存在时自动创建 */
#define CANOPUS_FS_TRUNC    0x0010u   /* 打开时清空截断文件 */
#define CANOPUS_FS_RDWR     (CANOPUS_FS_READ | CANOPUS_FS_WRITE)

#define CANOPUS_SEEK_SET    0         /* 从文件起始定位 */
#define CANOPUS_SEEK_CUR    1         /* 从当前偏移定位 */
#define CANOPUS_SEEK_END    2         /* 从文件末尾定位 */

#define CANOPUS_FS_MAX_OPEN 16u       /* 单模块最大同时打开 16 个文件句柄 */
#define CANOPUS_FS_PATH_MAX 128u      /* 支持最长 128 字节的安全相对路径 */
```

#### 数据结构
```c
struct canopus_fs_stat {
    uint32_t size;     /* 文件大小 (字节) */
    uint8_t  is_dir;   /* 1 为目录，0 为普通文件 */
    uint32_t mtime;    /* 最后修改时间戳 (毫秒) */
};

struct canopus_fs_dirent {
    char    name[64];  /* 文件或目录项名称 */
    uint8_t is_dir;    /* 是否为目录 */
};
```

#### 方法列表
| 方法 | 签名 | 说明 |
|---|---|---|
| `open` | `int32_t open(ctx, path, flags)` | 打开或创建文件。返回 `fd >= 0` 或负值错误码。 |
| `read` | `int32_t read(ctx, fd, buf, size)` | 读取指定字节数。返回实际读取字节数。 |
| `write` | `int32_t write(ctx, fd, buf, size)` | 写入数据。返回实际写入字节数。 |
| `seek` | `int64_t seek(ctx, fd, offset, whence)` | 随机定位文件指针。返回定位后的绝对偏移。 |
| `tell` | `int64_t tell(ctx, fd)` | 查询当前文件读取/写入指针位置。 |
| `size` | `int64_t size(ctx, fd)` | 获取当前打开文件的总字节大小。 |
| `sync` | `int32_t sync(ctx, fd)` | 刷新文件缓冲区到物理介质。 |
| `close` | `int32_t close(ctx, fd)` | 关闭文件句柄并释放插槽。 |
| `remove` | `int32_t remove(ctx, path)` | 删除指定文件。 |
| `rename` | `int32_t rename(ctx, old, new)` | 文件原子重命名。 |
| `stat` | `int32_t stat(ctx, path, out_stat)` | 查询文件元数据（大小、类型、时间）。 |
| `exists` | `int32_t exists(ctx, path)` | 快速检查文件是否存在（存在返回 1，不存在返回 0）。 |
| `mkdir` | `int32_t mkdir(ctx, path)` | 创建子目录。 |
| `opendir` | `void* opendir(ctx, path)` | 打开目录流句柄。 |
| `readdir` | `int32_t readdir(ctx, dir, out_entry)` | 依次枚举目录项。读完返回 1，成功返回 0。 |
| `closedir` | `int32_t closedir(ctx, dir)` | 关闭目录流句柄。 |

#### 安全隔离与沙箱规则
所有路径均限制在当前模块专用的沙箱根目录 `/data/canopus/m.<module_id>/` 下。严禁以 `/` 开头或包含 `..` 路径回溯，防止模块越权访问其他系统数据。

---

### 2.2 `ctx->mem` — 128 KiB 模块动态内存对象 (Memory Object)

彻底打破过往 16 KiB / 16 块的局限，配额拉升至 128 KiB，并支持 64 个并发块。

#### 配额与常量
```c
#define CANOPUS_MEM_BUDGET     131072u  /* 128 KiB (单模块总动态内存预算) */
#define CANOPUS_MEM_MAX_BLOCKS 64u      /* 最多跟踪 64 个独立活分配块 */
```

#### 方法列表
```c
struct canopus_mem_obj {
    void*    (*alloc)(const struct canopus_context_v1 *ctx, uint32_t size);
    void*    (*realloc)(const struct canopus_context_v1 *ctx, void *ptr, uint32_t new_size);
    void     (*free)(const struct canopus_context_v1 *ctx, void *ptr);
    uint32_t (*available)(const struct canopus_context_v1 *ctx);  /* 查询当前剩余配额 */
    uint32_t (*used)(const struct canopus_context_v1 *ctx);       /* 查询当前已用分配 */
};
```
- **内存防泄漏担保**：模块卸载时，Supervisor 自动释放该模块遗留的所有未释放内存，坚决杜绝长期运行导致手环内存碎裂或泄露。

---

### 2.3 `ctx->hook` — 通用动态函数拦截引擎 (Universal Hook Object)

针对 Cortex-M33 微控制器的物理特性，提供全自动的跨 Flash 与 RAM 的函数拦截能力。

#### Hook 类型
```c
typedef enum canopus_hook_type {
    CANOPUS_HOOK_AUTO    = 0,  /* 智能检测：Flash 走 FPB，RAM 走 Trampoline，指针走 Slot */
    CANOPUS_HOOK_FPB     = 1,  /* 强制硬件 FPB 指令重映射 (0xE0002000) */
    CANOPUS_HOOK_INLINE  = 2,  /* 强制 Thumb-2 8字节代码跳板 */
    CANOPUS_HOOK_POINTER = 3,  /* 强制 RAM 指针变量原子置换 */
} canopus_hook_type_t;
```

#### 方法列表
```c
struct canopus_hook_obj {
    /**
     * 安装一个动态 Hook
     * @param target_fn  目标函数绝对地址或全局指针槽位
     * @param hook_fn    拦截实现的函数指针
     * @param orig_fn    输出：可安全调用原函数逻辑的跳板函数指针
     * @param hook_type  Hook 模式 (推荐 CANOPUS_HOOK_AUTO)
     * @return 成功返回 hook_id (>= 0)，失败返回负值错误码
     */
    int32_t (*install)(const struct canopus_context_v1 *ctx,
                       uintptr_t target_fn,
                       uintptr_t hook_fn,
                       uintptr_t *orig_fn,
                       canopus_hook_type_t hook_type);

    /**
     * 卸载指定的 Hook 并还原现场
     */
    int32_t (*uninstall)(const struct canopus_context_v1 *ctx, int32_t hook_id);
};
```
- **Flash 安全性**：针对 `0x0C000000..0x0D000000` XIP Flash 中的原生固件函数，引擎自动启用 Cortex-M33 内置的 Flash Patch and Breakpoint (FPB) 单元硬件重映射，**无需修改只读 Flash**，彻底规避硬件 BusFault / HardFault。
- **生命周期保护**：模块被禁用或卸载时，Supervisor 自动卸载该模块登记的所有 Hook，避免手环在模块退出后发生跳转悬空。

---

### 2.4 `ctx->time`、`ctx->log`、`ctx->notify`、`ctx->system`

- **`ctx->time`**：
  - `monotonic_ms(ctx)`：微控制器开机单调运行毫秒时间戳（永不回退）。
  - `realtime_ms(ctx)`：RTC 真实时间（UTC 毫秒）。
- **`ctx->log`**：
  - `info(ctx, msg)`、`warn(ctx, msg)`、`error(ctx, msg)`、`debug(ctx, msg)`：带有模块上下文前缀的结构化日志输出。
- **`ctx->notify`**：
  - `post(ctx, title, body)`：在手环屏幕上推送弹窗与震动提醒。
- **`ctx->system`**：
  - `target_id(ctx)`：获取当前设备目标型号（如 `"xiaomi-band-10-pro-3.101.043"`）。
  - `framework_revision(ctx)`：获取 Canopus Supervisor 框架版本。
  - `is_safe_mode(ctx)`：查询手环是否处于安全模式。
  - `reboot(ctx)`：安全请求系统软重启以生效 A/B 代模块。

---

## 3. 编程范例

### C 模块使用范例
```c
#include "canopus_abi.h"
#include "canopus_services.h"

int32_t my_module_activate(const struct canopus_context_v1 *ctx)
{
    // 1. 日志记录
    ctx->log->info(ctx, "My Module Activated!");

    // 2. 128 KiB 动态内存分配
    uint8_t *buffer = (uint8_t *)ctx->mem->alloc(ctx, 32768); // 32 KiB
    ctx->log->info(ctx, "Allocated 32 KiB memory successfully");

    // 3. 文件系统多模式与随机寻址
    int32_t fd = ctx->fs->open(ctx, "settings.json", CANOPUS_FS_WRITE | CANOPUS_FS_CREATE);
    if (fd >= 0) {
        ctx->fs->write(ctx, fd, "{\"enabled\":true}", 16);
        ctx->fs->seek(ctx, fd, 0, CANOPUS_SEEK_SET);
        ctx->fs->close(ctx, fd);
    }

    // 4. 通用动态 Hook 挂钩
    uintptr_t orig_fn = 0;
    int32_t hook_id = ctx->hook->install(
        ctx,
        TARGET_FUNCTION_ADDRESS,
        (uintptr_t)&my_custom_hook,
        &orig_fn,
        CANOPUS_HOOK_AUTO
    );

    // 5. 用户通知
    ctx->notify->post(ctx, "Canopus", "Module ready!");

    return 0;
}
```

### Rust 模块使用范例
```rust
use canopus_runtime::{fs, mem, hook, time, system, CanopusContext, HookType, FS_WRITE, FS_CREATE};

pub fn activate(ctx: &CanopusContext) -> Result<(), i32> {
    // 1. 系统信息
    let model = system::target_id(ctx);

    // 2. 128 KiB 内存管理
    let buf = mem::alloc(ctx, 65536); // 64 KiB
    let remaining = mem::available(ctx);

    // 3. 文件读写与状态
    let mut file = fs::File::open(ctx, "cache.dat", FS_WRITE | FS_CREATE)?;
    file.write(b"Canopus OS Cache Data")?;
    let file_size = file.size()?;

    // 4. 通用 Hook
    let mut orig = 0;
    let hook_id = hook::Hook::install(
        ctx,
        0x0c34cc09, // 目标函数
        my_hook_handler as usize,
        &mut orig,
        HookType::Auto,
    )?;

    Ok(())
}
```
