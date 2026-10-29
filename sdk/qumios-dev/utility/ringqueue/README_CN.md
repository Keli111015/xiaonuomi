# QUMI 环形记录队列

语言：[English](README.md) | [中文](README_CN.md)

`utility/ringqueue/` 是一个由调用方持有的、有界、可变长度字节记录 FIFO。它将完整
记录复制进队列存储，再复制给消费者，因此保留记录边界：严格入队要么完整接受一条记录，
要么完全不接受。

```text
生产者缓冲区 -- 复制 [长度 | payload] --> 调用方持有的环形存储
                                              |
                                              +-- 复制 --> 消费者缓冲区
                                              |
                         push_overwrite 可能淘汰一条或多条最旧完整记录
```

## 记录布局与容量

每条记录在后备存储中的私有布局如下：

```text
[ uint32_t payload_size ][ payload bytes ... ]
```

长度头和 payload 均可分别跨越后备存储末尾。一条记录占用
`QM_RING_QUEUE_RECORD_HEADER_BYTES + payload_size` 字节。调用方提供的全部容量均可用，
没有预留哨兵字节。`capacity_bytes` 至少应为
`QM_RING_QUEUE_RECORD_HEADER_BYTES`。

零长度记录是合法记录，仍占一个长度头，可用 `data == NULL` 入队；出队时使用
`data == NULL`、`capacity_bytes == 0`。内部长度头只是实现细节，不是持久化或跨平台
序列化格式。

## API 契约

| API | 行为 |
| --- | --- |
| `qm_ring_queue_init` | 绑定调用方存储并建立空队列。对静止队列重新初始化会丢弃记录，但不清零 storage。 |
| `qm_ring_queue_reset` | 丢弃全部记录但不清零 storage。只要 queue/storage 绑定仍有效，它可恢复损坏的偏移或计数状态。 |
| `qm_ring_queue_push` | 严格 FIFO 入队。整条记录放不下时返回 `-QM_EFULL`，queue 状态和 storage 均不改变。 |
| `qm_ring_queue_push_segments` | 将多个输入区间按顺序复制为一条完整记录；失败时不改变 queue 或 storage。 |
| `qm_ring_queue_push_overwrite` | 入队最新记录，并按需淘汰一条或多条最旧完整记录。淘汰会先预检，因此输入无效或读取到损坏头时，queue 状态和 storage 保持不变。 |
| `qm_ring_queue_push_segments_overwrite` | 分段版本的 overwrite 入队；按需淘汰最旧完整记录。 |
| `qm_ring_queue_peek_size` | 读取最旧记录 payload 长度，不消费记录。 |
| `qm_ring_queue_pop` | 复制并消费最旧记录。有效输出缓冲区太小时返回 `-QM_EINVAL`，写入所需长度，并保留该记录。 |
| `qm_ring_queue_discard` | 不复制 payload，直接消费最旧记录。 |

`-QM_EINVAL` 表示参数无效、禁止的内存重叠，或单条记录在空队列中也无法容纳；
`-QM_EEMPTY` 表示没有队头记录；`-QM_EIO` 表示本次操作读取到了损坏的记录头，并不表示
未读取的后续记录已经被验证。

除成功的 reset/重新初始化，以及容量不足的 `pop` 按约定写入所需长度外，失败路径不会
修改 queue 状态或 storage。实现的完整记录语义不等于并发同步保证，调用方仍须串行化访问。

### 分段入队

当多个输入缓冲区在同一次调用时已经可用，可以使用分段接口，避免先拼接临时连续
buffer：

```c
qm_ring_queue_segment_t segments[] = {
    { metadata, metadata_size },
    { payload, payload_size },
};

ret = qm_ring_queue_push_segments_overwrite(
    &queue, segments, sizeof(segments) / sizeof(segments[0]));
```

队列会先校验所有区间、总长度和容量，再写入一个长度头和一个完整 payload。segment
边界不会保存；消费者使用普通 `peek_size()`/`pop()` 读取拼接后的单条记录。分段数据跨
多个回调到达时，仍应由上层组包后再入队。

## 所有权与并发

- 调用方持有 `qm_ring_queue_t` 和可写后备 storage；二者在整个队列生命周期中必须有效。
- queue 控制块与 storage 不得重叠。非零输入或输出 payload 区间、全部 `size` 输出均不得
  与二者重叠；非零 `pop` payload 输出也不得与对应 `size` 输出重叠。
- 调用方对 queue/storage 的持有是生命周期所有权，并不表示可以改写活跃队列内容：初始化后，
  只有本 API 可以修改 queue 字段或含有未消费记录的 storage 字节。队列损坏时使用 `reset`
  放弃其中记录。
- `push` 和分段 push 仅在调用期间读取源数据；`pop` 仅在调用期间写入目标数据。函数返回后调用方可以
  复用这些缓冲区。
- 组件不分配内存、不加锁、不等待、不派发回调，也不提供 ISR 同步或内存序。调用方必须
  串行化同一队列的每次操作，并自行安排唤醒机制。

## QUMI 示例

[`examples/utility/ringqueue/qm_ring_queue_example.c`](../../examples/utility/ringqueue/qm_ring_queue_example.c)
是独立的 QUMI 应用源文件。它初始化调用方持有的存储，展示严格 `push` 在记录放不下时
返回 `-QM_EFULL`，随后通过 `push_overwrite` 入队，并用 `peek_size` 和 `pop` 校验保留记录的
FIFO 顺序。

该源文件定义 `qm_application_start()`。每个 target 只能将它作为唯一的示例入口源文件构建，
不可同时链接另一个定义该入口的源文件。target 仍需加入 [构建接入](#构建接入) 列出的
ringqueue 和 QUMI string 源文件。

## 最小用法

```c
#include "qm_ring_queue.h"

static uint8_t queue_storage[512];
static qm_ring_queue_t queue;
static uint8_t receive_buffer[128];

void queue_init(void)
{
    (void)qm_ring_queue_init(&queue, queue_storage, sizeof(queue_storage));
}

int queue_latest(const uint8_t *payload, uint32_t payload_size)
{
    /* 如有需要，调用方在此持有该队列的外部锁。 */
    return qm_ring_queue_push_overwrite(&queue, payload, payload_size);
}

int queue_receive(void)
{
    uint32_t required_size;
    uint32_t actual_size;
    int ret;

    ret = qm_ring_queue_peek_size(&queue, &required_size);
    if (ret != QM_EOK) return ret;
    if (required_size > sizeof(receive_buffer)) return -QM_EINVAL;

    return qm_ring_queue_pop(&queue, receive_buffer, sizeof(receive_buffer),
                             &actual_size);
}
```

## 边界

`qm_ringbuf` 是字节流 FIFO，不保留可变长度记录边界。QMOS `qm_queue_t` 是具有自身传输和
等待语义的 OS 队列。`qm_ring_queue` 有意不同于二者：它是无阻塞、复制式、调用方提供
存储的记录容器。

本组件只保存字节和内部长度头，不携带 packet metadata、借用指针、所有权回调或音频
生命周期策略。需要 `qm_audio_packet_t` 元数据的代码应自行定义完整记录格式，并将音频
专有同步保留在 audio 层。

## 构建接入

QUMI 固件目标会显式列出 utility 的 include 路径和源文件。某个目标采用本组件时，应将
`utility/ringqueue` 加入 include 目录，并将 `utility/ringqueue/qm_ring_queue.c` 加入
source list。实现使用 `utility/string/qm_string.c` 提供的 `qm_memcpy()`；目标必须已经
链接该 QUMI string utility，或者连同 ringqueue 一起加入其 include 路径和源文件。这里有意
不做全局注册，也不增加 Kconfig 开关：在具体消费者选择使用前，这个独立组件不会改动应用或
`qm_audio`。
