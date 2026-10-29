# QUMI 内存工具

语言：[English](README.md) | [中文](README_CN.md)

本文是 `utility/memory/` 显式内存工具的唯一设计和使用入口。公共头文件是精确结构字段和 Doxygen
参数说明的规范来源。本文不改变 QMOS 分配适配层，也不改变 `qm_malloc()`、`qm_calloc()`、
`qm_realloc()` 与 `qm_free()` 的行为。

## 1. 范围

本组件增加由调用方持有、具有明确所有者和后备存储的分配工具。

| 工具 | 模型 | 适用场景 |
| --- | --- | --- |
| `qm_mem_heap` | 基于一个或多个调用方区域的变长分配器 | 独立子系统需要变长对象和显式释放。 |
| `qm_mem_pool` | 具有一个或多个尺寸类别的固定块分配器 | 对象大小有上界，复用比变长容量更重要。 |
| `qm_mem_arena` | 支持 rewind 或批量 reset 的线性分配器 | 临时数据具有单一同步所有者和共同生命周期终点。 |

`region_count == 1` 时，`qm_mem_heap` 具有 heap_4 类行为；大于一时具有 heap_5 类行为。这只是
行为参照，QUMI 不暴露 FreeRTOS 分配器符号，也不依赖 FreeRTOS。

`qm_mem_pool` 通过类别配置支持一种或多种块大小。

本组件不分配后备存储、不回退到其他分配器、不紧凑移动对象、不增加内部锁，也不迁移现有 QMOS
分配适配层调用方。

### 1.1 设计参照

| 工具 | 行业常用模型 | QUMI 边界 |
| --- | --- | --- |
| Heap | FreeRTOS `heap_4` / `heap_5`：单/多区域中的变长块合并 | 实例和区域由调用方持有；不引入内部锁或 RTOS 依赖。 |
| Pool | 固定块 Slab / Memory Slab 分配器 | 一个 API 覆盖单规格和多规格，不回退到 Heap。 |
| Arena | Bump-pointer 分配器和批量 reset | `mark`、`rewind` 与 generation 校验显式约束所有者生命周期。 |

## 2. 通用契约

### 2.1 公开文件

| 工具 | 公共头文件 | 主要公开 API |
| --- | --- | --- |
| Heap | [heap/qm_mem_heap.h](heap/qm_mem_heap.h) | `qm_mem_heap_init`、`alloc`、`calloc`、`realloc`、`release`、`get_stats`、`deinit` |
| Pool | [pool/qm_mem_pool.h](pool/qm_mem_pool.h) | `qm_mem_pool_init`、`alloc`、`release`、`get_stats`、`get_class_stats`、`deinit` |
| Arena | [arena/qm_mem_arena.h](arena/qm_mem_arena.h) | `qm_mem_arena_init`、`alloc`、`mark`、`rewind`、`reset`、`get_stats`、`deinit` |

使用哪个分配器，就包含对应头文件。`qm_malloc()`、`qm_free()` 等通用分配接口声明在
`qmos/qm_kernel.h` 中，由平台适配层提供，不依赖这三套工具。

### 2.2 存储、对齐与所有权

每个实例对象、元数据数组和后备缓冲区均由调用方持有。实例是公开的定长状态结构，可以静态定义或
嵌入其他结构。字段属于实现状态；首次清零后，调用方只能通过本文 API 修改它。

每个头文件定义组件内 payload 的最小对齐要求：`QM_MEM_HEAP_ALIGNMENT`、
`QM_MEM_POOL_ALIGNMENT` 与 `QM_MEM_ARENA_ALIGNMENT`。各值来自对应的编译期配置，默认均为
4 字节。所有 `init()` API 均接收普通的可写原始字节范围。Pool 和 Arena 使用各自配置的对齐值
作为 `A`；Heap 使用其配置值与私有块头原生对齐要求中的较大值。对于该内部对齐值 `A`，实现管理
`[align_up(raw_begin, A), align_down(raw_end, A))`。两端被排除的字节仍由调用方持有，且不计入
分配器容量。若 ABI 对元数据的对齐要求更严格，Heap 返回的指针可能具有更高对齐，
保留的可用字节也可能更少。

payload 起点和块跨度均满足该对齐要求。配置为 4 字节不能保证要求 8 字节或更高对齐的对象，
应根据存储对象的要求选择配置值。提高对齐值可能增加填充，使同一后备缓冲区的可用容量减少。

不引入共享内存工具头文件或公共对齐类型。

调用方只需普通 `uint8_t[]`，无需对齐 union、编译器属性或预先对齐地址。实例对象、被借用的
元数据数组和原始后备存储范围不得重叠。

Pool 存储使用 `QM_MEM_POOL_STORAGE_SIZE(block_size, block_count)`。该宏为原始存储预留收敛后仍可
保留全部配置槽位所需的余量，是 Pool 唯一公开的容量计算辅助项。

### 2.3 借用与并发

| 工具 | 被借用输入 | 借用结束时机 |
| --- | --- | --- |
| Heap | 区域描述符数组和每个区域的字节范围 | `qm_mem_heap_deinit()` 成功后 |
| Pool | 类别数组、类别状态数组和每个类别的存储范围 | `qm_mem_pool_deinit()` 成功后 |
| Arena | Arena 字节范围 | `qm_mem_arena_deinit()` 成功后 |

外层 `qm_mem_pool_config_t` 仅在 `qm_mem_pool_init()` 中读取，可以是栈变量。其 `classes` 和
`class_states` 成员在 Pool 活跃期间被借用。

所有 API 均非 ISR 安全。调用方负责串行化 Heap 和 Pool 的全部 API，包括 `init`、统计和
`deinit`。Arena 只能有一个任务上下文所有者，负责串行化分配、mark、rewind、reset、统计和
反初始化。

### 2.4 状态与错误

所有实例在首次 `init` 前必须清零。

非活跃实例只会在 `init` 成功后变为活跃。`init` 失败时，非活跃实例保持不变且可复用；对活跃
实例调用 `init` 返回 `-QM_EBUSY`。Heap 和 Pool 仍有活跃分配时，反初始化返回 `-QM_EBUSY`。
非分配 API 对非活跃实例返回 `-QM_EINIT`。

Heap 与 Pool 成功反初始化后可以再次初始化。Arena 在 `deinit` 后直接再次初始化，不应清零其
状态，因为保留的 generation 用于使前一个生命周期的 mark 失效。

| 条件 | 返回值 |
| --- | --- |
| 指针、原始范围、顺序、Pool 策略值、溢出、外来指针、重复释放、无效 mark 或统计输出为空 | `-QM_EINVAL` |
| 要求活跃实例的操作 | `-QM_EINIT` |
| 活跃实例上的 `init`；仍有活跃分配时 Heap 或 Pool 的 `deinit` | `-QM_EBUSY` |
| Arena reset 或重新初始化将导致 generation 回绕 | `-QM_EFULL` |
| 管理 API 成功 | `QM_EOK` |

`alloc`、`calloc`、Pool 分配和 Arena 分配在零大小请求、实例非活跃、溢出或存储耗尽时返回 `NULL`。
`qm_mem_heap_realloc()` 失败时，原分配保持有效。活跃 Heap 与 Pool 的统计会暴露分配失败次数。

## 3. Heap

### 3.1 接口范围

| 头文件 | 公开类型 | 操作 |
| --- | --- | --- |
| [heap/qm_mem_heap.h](heap/qm_mem_heap.h) | `qm_mem_heap_t`、`qm_mem_heap_region_t`、`qm_mem_heap_stats_t` | 初始化有序区域表；分配、清零分配、调整大小、释放、读取统计和反初始化。 |

每个原始区域为 `[start, start + size)`。初始化要求区域表非空、严格升序且原始范围不重叠。Heap
验证原始所有权边界，推导出对齐后的内部区域，并要求保留区域可容纳首个块和哨兵。它不排序、不复制
区域表，并将该描述符数组借用至反初始化。

实现会在每个区域内写入块元数据，使用按地址排序的 first-fit 空闲链表，切分足够大的块，并只在同一
声明区域内合并相邻空闲块。相邻区域保持独立，单次分配不会跨越区域边界。

`qm_mem_heap_release()` 只接受活跃分配的精确 payload 起始地址。在分配器元数据未被越界写破坏的
前提下，它拒绝内部指针、其他 Heap 的指针和重复释放。

Heap 统计是快照。`usable_bytes`、`used_bytes`、`free_bytes` 和 `minimum_free_bytes` 使用包含
内部块头的管理块记账口径，因此 `used_bytes + free_bytes == usable_bytes`。`largest_free_block`
是 payload 容量，不含内部块头；判断连续请求能否成功时应使用它，而非只看 `free_bytes` 总量。

Heap 不保证确定性分配时延。具有已测量时延上界要求的场景应使用 Pool 或经独立评审的分配器。

## 4. Pool

### 4.1 接口范围

| 头文件 | 公开类型 | 操作与容量辅助项 |
| --- | --- | --- |
| [pool/qm_mem_pool.h](pool/qm_mem_pool.h) | `qm_mem_pool_t`、`qm_mem_pool_config_t`、`qm_mem_pool_class_config_t`、`qm_mem_pool_class_state_t`、`qm_mem_pool_stats_t`、`qm_mem_pool_class_stats_t` | 初始化一个或多个有序类别；分配、释放、读取汇总或单类别统计、反初始化。`QM_MEM_POOL_STORAGE_SIZE(block_size, block_count)` 用于计算原始类别存储大小。 |

`classes` 与 `class_states` 都至少包含 `class_count` 个元素。组件无法验证物理数组长度，因此这是调用方
前置条件。初始化先验证全部类别，再修改状态数组；活跃期间由 Pool 管理该状态数组。

每个类别必须有非空原始存储、非零 `block_size` 和 `block_count`，且 `block_size` 必须相对前一类别
严格递增。原始类别范围不得重叠。Pool 会将每个原始范围的两端收敛，并要求保留存储可容纳全部配置
槽位。声明存储时使用 `QM_MEM_POOL_STORAGE_SIZE(block_size, block_count)`，即可在不显式对齐的
情况下保留全部配置槽位。保留区间中的额外字节不使用。

每个槽位在 payload 前存放两个 `uint32_t` 元数据，共 8 字节。payload 偏移和槽位跨度按配置的
对齐值补齐：

```text
A = QM_MEM_POOL_ALIGNMENT
payload_offset = align_up(8, A)
slot_stride = align_up(payload_offset + block_size, A)
raw_storage_size = slot_stride * block_count + A - 1
```

`QM_MEM_POOL_STORAGE_SIZE()` 使用上述公式。其参数不得使 `size_t` 溢出；初始化时也会检查容量
计算是否溢出。`block_size` 始终表示可用 payload 大小，不含元数据和填充。

分配从能容纳请求的最小类别开始。`allow_larger == QM_FALSE` 时，选中类别已满则立即失败；
`allow_larger == QM_TRUE` 时，允许继续查找更大的适配类别。不会回退到 Heap。

类别选定后，分配和释放使用该类别的空闲链表。释放验证精确且仍活跃的 payload 指针；在元数据未损坏
前提下，它拒绝内部指针、外来指针和重复释放。`qm_mem_pool_get_class_stats()` 对越界的 `class_index`
返回 `-QM_EINVAL`，其报告的 `block_size` 是可用 payload 容量。

## 5. Arena

### 5.1 接口范围

| 头文件 | 公开类型 | 操作 |
| --- | --- | --- |
| [arena/qm_mem_arena.h](arena/qm_mem_arena.h) | `qm_mem_arena_t`、`qm_mem_arena_mark_t`、`qm_mem_arena_stats_t` | 初始化、使用 `qm_mem_arena_alloc(arena, size)` 分配、mark、rewind、reset、读取统计和反初始化。 |

Arena 在调用方缓冲区中推进一个游标。每次成功的 `qm_mem_arena_alloc(arena, size)` 都返回满足
`QM_MEM_ARENA_ALIGNMENT` 的指针，调用方不传入单独的对齐值。Arena 不支持单对象释放；
`qm_mem_arena_reset()` 在 Arena 保持活跃时重新提供全部可用缓冲区。

原始缓冲区必须非空、长度非零，但无需预先对齐。Arena 保存内部对齐区间，并以该区间的实际容量返回
统计。`qm_mem_arena_deinit()` 结束缓冲区借用并使实例失效，不检查未释放指针，也不释放调用方存储。
非活跃 Arena 返回全零 mark。

mark 记录 Arena 身份、generation 和偏移。`rewind` 只接受当前活跃 Arena generation 创建且位于当前
游标之前或相等位置的 mark。`reset` 成功时先推进 generation，再复位游标，使全部旧 mark 失效；成功
重新初始化也会推进 generation。generation 不会回绕。

Arena 生命周期是所有者规则，不是单独的管理器：

```text
所有者初始化 Arena
  -> 同步工作分配临时对象
  -> 跨异步边界的数据复制到 Heap、Pool 或调用方存储
  -> 所有者在成功、失败和取消时 reset
  -> 所有者在释放后备缓冲区前 deinit
```

任何队列项、回调、定时器或其他任务均不得在所有者的 `rewind`、`reset` 或 `deinit` 边界之后保留
Arena 指针。

## 6. 配置

[qm_kconfig](qm_kconfig) 配置工具开关和分配对齐。各对齐选项为整数，放在对应的工具开关下，
启用工具后才显示。

| 功能开关 | 对齐配置 | 默认对齐 | 有效对齐值（字节） |
| --- | --- | --- | --- |
| `QM_MEM_HEAP_SUPPORT` | `QM_MEM_HEAP_ALIGNMENT` | 4 | 4、8、16、32、64 |
| `QM_MEM_POOL_SUPPORT` | `QM_MEM_POOL_ALIGNMENT` | 4 | 4、8、16、32、64 |
| `QM_MEM_ARENA_SUPPORT` | `QM_MEM_ARENA_ALIGNMENT` | 4 | 4、8、16、32、64 |

`QM_MEM_HEAP_SUPPORT`、`QM_MEM_POOL_SUPPORT` 与 `QM_MEM_ARENA_SUPPORT` 相互独立，默认关闭，
可同时启用。公开的 `QM_MEM_*_ALIGNMENT` 宏对应 `qm_config.h` 中的
`CONFIG_QM_MEM_*_ALIGNMENT` 配置值。旧配置缺少对齐值时，头文件默认使用 4。不提供 `0=自动`
模式。工具启用时，若对齐值不在 4 至 64 范围内或不是 2 的幂，对应头文件会在编译期报错。

同一工具的全部实例使用相同对齐配置。实现与调用方必须使用同一份 `qm_config.h`，不得在单个
源文件中覆盖对齐值。分配接口和初始化参数均不增加运行时对齐值。

容量、区域数、类别数和存储通过各实例的初始化参数提供，锁与生命周期仍由调用方负责。工具关闭后
不存在桩实现或回退实现；调用方在对应的 `CONFIG_QM_MEM_*_SUPPORT` 条件下编译其使用路径。

## 7. 示例

独立示例按工具拆分：

| 源文件 | 演示内容 |
| --- | --- |
| `examples/utility/memory/qm_mem_heap_example.c` | 单区域 Heap 的初始化、分配、统计、释放和反初始化。 |
| `examples/utility/memory/qm_mem_pool_example.c` | 两个 Pool 类别；耗尽小类别后演示向较大类别回退，以及释放和反初始化。 |
| `examples/utility/memory/qm_mem_arena_example.c` | Arena 的初始化、mark、分配、rewind、统计和反初始化。 |

每个源文件均为独立应用，并定义 `qm_application_start()`。一个示例目标只能构建其中一个源文件，
不可将三个文件链接到同一应用。所有示例后备存储均为普通 `uint8_t[]`，由工具在初始化时内部收敛。

## 8. 非目标

- 替换或包装 QMOS 分配 API。
- Heap 到 Pool 或 Pool 到 Heap 的回退。
- 垃圾回收、引用计数、重定位、移动紧凑，或通用异步 Arena 生命周期管理器。
- 全局后备缓冲区大小、全局最大区域/类别数或全局锁策略。
- 迁移应用、音频、网络、Agent 或驱动的分配路径。
