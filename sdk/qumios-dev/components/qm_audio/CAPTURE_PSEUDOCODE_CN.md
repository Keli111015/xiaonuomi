# Capture 手工实现伪代码参考

本文用于补齐和核对 `qm_audio_capture.c`。当前 `.c` 已包含 QUEUE 接收、`peek_size`、状态控制、PULL/PUSH 源错误传播、订阅静止等待和 CALLBACK dispatcher；目标板适配与完整并发测试仍需单独验证。

接口依据：[qm_audio_capture.h](include/qm_audio_capture.h)。设备约定依据：[qm_audio_device.h](include/adapter/qm_audio_device.h)。

## 1. 实现范围与阅读顺序

Capture 只负责：从已打开的设备取得原生数据，复制到订阅队列，维护订阅状态，安全停止并释放自己持有的资源。

| 责任 | 所属位置 |
| --- | --- |
| 设备 open/close、选择设备原生格式 | 调用方与 device adapter |
| PULL 读取任务、PUSH 数据接收、订阅队列 | Capture |
| PCM 重采样、声道转换、PCM→Opus | 使用方的处理任务 |
| 网络发送、文件写入、业务启停决策 | 使用方 |
| Engine、Service、Pipeline 的业务状态 | 各自模块；Capture 不访问 |

下面的步骤是算法，不要求每一步都拆成 C 函数；校验、加锁、状态更新可以直接写在对应接口里。

### 交付模式与按需任务

QUEUE 使用方调用 `recv`；CALLBACK 及可选 `state_cb` 使用每个 Capture 至多一个
独立 dispatcher，在锁外串行执行。纯 QUEUE 且无 `state_cb` 不创建回调任务，
但 PULL 仍在同步启动设备成功后按需创建唯一 reader。create 不创建任何任务。

PULL reader 在读取结束后执行设备 stop 再退出。PUSH 没有 reader，源故障后的
停止交给 unsubscribe/shutdown 或下一次启动前的清理；dispatcher 只分发回调。

## 2. 执行上下文与 QUMI 依赖

| 上下文 | 做什么 | 不应做什么 |
| --- | --- | --- |
| 生命周期调用方 | create、subscribe、enable、unsubscribe、shutdown、destroy | 在设备生产者或 Capture 自己的任务里等待自身退出 |
| PULL 读取任务 | `device_capture_read`，校验、复制、入队 | 编码、网络发送、用户回调 |
| PUSH 生产者任务 | 调用 `qm_audio_capture_feed` 提交数据/错误 | ISR 中直接调用、持有阻止设备 stop 完成的外部锁 |
| QUEUE 消费任务 | `recv` 后进行重采样、编码、发送等 | 持有 Capture 内部锁处理数据 |

PULL 模式只有一个物理读取者；PUSH 模式由设备提供生产者，Capture 不额外创建读取任务。多个订阅者共享同一输入，不为每个订阅者重复读取设备。

只使用已有 QUMI 接口：

| 用途 | 接口 |
| --- | --- |
| 内存与复制 | `qm_calloc/qm_malloc/qm_free`、`qm_memcpy/qm_memset` |
| 互斥 | `qm_mutex_new/lock/unlock/free` |
| 唤醒与退出通知 | `qm_sem_new/wait/signal/free` |
| 任务 | `qm_task_new/qm_task_is_current/qm_task_exit` |
| 超时预算 | `qm_utils_time_countdown_ms/qm_utils_time_left` |
| 完整记录队列 | `qm_ring_queue_*` |
| 设备采集 | `qm_audio_device_capture_start/read/abort/stop` |

格式校验可复用 [qm_audio_format.h](priv_include/qm_audio_format.h) 的纯函数；不需要包含 `qm_audio_priv.h`。不引入编译器原子操作、FreeRTOS 直接调用或新的通用同步封装。

## 3. 最少需要保存的数据

下面是逻辑字段，不规定最终结构体布局。能够从已有状态推导的字段不要再保存一份，例如订阅是否启用可直接看 `snapshot.state`。

```text
Capture:
    config                       # 创建时复制，device_ctx 借用
    closing = false              # shutdown 开始后关闭准入
    stopped = false              # shutdown 成功后允许 destroy
    source_running = false       # 是否允许接收当前运行的数据
    device_needs_stop = false     # 已尝试 start，尚未成功完成 stop
    source_error = QM_EOK         # 当前一轮采集的首次永久错误
    pcm_sequence = 0              # 本 Capture 内 PCM 序号

    lifecycle_busy = false       # 生命周期占用标志，由现有数据锁保护
    state_lock                   # 实际 capture.lock；保护状态、订阅表、队列
    quiesced                     # 任务/接收者结束时给生命周期等待者提示

    reader_task, reader_active   # 仅 PULL 模式使用
    subscriptions[]              # 容量为 MAX_SUBSCRIBERS 的有界指针表

Subscription:
    capture                      # 所属 Capture，不拥有它
    config                       # 复制配置；user_ctx 仅借用
    snapshot                     # state、result、实际 queue_len
    removing = false             # 退订开始后拒绝新的交付
    recv_active = false          # 同一订阅最多一个 recv
    ring, storage                # qm_ring_queue + 自有存储
    queued_records = 0           # 队列条数，保证 queue_len 是“条”而非“字节”
    data_ready                   # 队列空转非空或状态变化提示，不是记录计数
    discontinuity_pending = false

Record:
    timestamp_ms
    duration_ms
    sequence
    flags
    payload[]                    # 复制字节；绝不能保存借用的 data 指针
```

发布使用 `push_segments` 将栈上 metadata 和借用 payload 直接复制为一条记录，无需拼接缓冲。QUEUE recv 使用本次调用的临时记录缓冲，在复制到调用方存储后释放。dispatcher 使用回调返回前不会被复用的独立缓冲，见第 10 节。

队列预算：

```text
effective_queue_len = config.queue_len 非零 ? config.queue_len : Capture 默认值
max_record_bytes = metadata_bytes + frame_bytes
queue_storage_bytes = effective_queue_len ×
                      (QM_RING_QUEUE_RECORD_HEADER_BYTES + max_record_bytes)

先用足够宽的整数检查加法、乘法与 QUMI 分配接口长度上限，再分配。
```

不能只用 `push_overwrite()` 的字节容量限制队列：可变长包可能让队列装下超过 `queue_len` 的记录。需要显式维护条数，满时丢弃一条最旧记录。

## 4. 状态、锁与等待约定

### 4.1 状态变化

| 对象 | 变化 | 触发 |
| --- | --- | --- |
| Capture | OPEN → CLOSING → STOPPED | shutdown 开始 → 所有停止条件满足 |
| Capture | CLOSING → CLOSING | 停止超时/失败，保留句柄等待重试 |
| Subscription | DISABLED ↔ RUNNING | enable(false/true) |
| Subscription | 非终态 → ERROR | 源故障、启用失败或自身队列损坏 |
| Subscription | 非终态 → STOPPED | shutdown |
| Subscription | ERROR/STOPPED → 释放 | unsubscribe 成功 |

ERROR、STOPPED 是订阅终态，不能用 enable 恢复。允许新建启用订阅重启物理源，但旧订阅保留自己的终态和首次错误。

`device_needs_stop` 不等于正在运行：start 返回失败后也可能需要 stop；stop 失败后也必须保留这个标记。只有 stop 成功才能清除。

### 4.2 现有数据锁与生命周期占用

- `state_lock` 短时间保护状态、订阅表、队列及 `lifecycle_busy`；设备 I/O、阻塞等待和用户回调均放在锁外。
- subscribe、enable、unsubscribe、shutdown、destroy 在锁内检查并设置 `lifecycle_busy`；已占用则返回 `-QM_EBUSY`。除 destroy 成功直接释放实例外，各返回路径清除占用标志。
- 生命周期占用跨设备 start/stop 和静止等待保留，保证旧运行清理不会误停新的运行；不增加另一把互斥锁。
- reader、feed、recv 和 dispatcher 只用数据锁；dispatcher 不参与设备启停。

create/destroy 与句柄取得、最终释放仍由外部调用方串行管理。内部锁无法保护一个已经释放后才被其他任务传入的指针。

### 4.3 信号量是提示，状态才是判断依据

```text
状态变化方，持 state_lock:
    修改队列/状态/active 标记
    给对应信号量一次提示

等待方:
    持 state_lock 检查条件
    条件满足 → 继续
    条件不满足 → 解锁 → 等提示 → 加锁重查
```

有积压数据时不需要等待信号量；有信号也不代表一定存在数据。flush、disable、错误和旧提示都可能让唤醒时队列为空。

`data_ready` 只在队列由空转为非空或状态变化时提示，不对每次入队累加计数。需要清除旧提示时，在同一 `state_lock` 内完成；不能在判断队列为空并解锁后再清空，以免吞掉新数据的唤醒。

一次接口调用只建立一个等待期限；各阶段传入剩余时间。`QM_WAIT_FOREVER` 单独处理，零超时只检查当前条件。设备 stop 没有超时参数，其耗时另受 adapter 的有限停止时限约束。

recv/shutdown 使用参数 `timeout_ms`；unsubscribe 及 subscribe/enable 内部需要停止旧运行时，使用现有 `CONFIG_QM_AUDIO_CAPTURE_STOP_TIMEOUT_MS`。不在每个内部步骤重新开始计时。所有公开接口都要求任务上下文；同一订阅的 enable/flush/unsubscribe 由调用方串行发起。

## 5. 创建和查询接口

### 5.1 `qm_audio_capture_create(config, out_capture)`

```text
out_capture 为空 → -QM_EINVAL
*out_capture = NULL
Capture 编译禁用 → -QM_ENOSYS

校验 config:
    device_ctx 非空，调用方已经打开设备
    input_format 有效，且与打开设备的原生格式一致
    仅支持 PCM 或完整编码包；不支持编码字节流
    frame_bytes 非零
    PCM: frame_bytes 必须是单个采样帧字节数的整数倍
    编码包: frame_bytes >= max_packet_bytes
    queue_len 为零时取现有默认值

PUSH 模式且已有实例占用入口 → -QM_EBUSY

分配 Capture
创建 state_lock、quiesced
初始化空订阅表，closing/stopped 均为 false；此时不调用设备 start

任一步失败:
    逆序释放本次已创建资源
    保持 *out_capture = NULL，返回原错误

PUSH: 完成全部初始化后才发布唯一入口指针
*out_capture = Capture
返回 QM_EOK
```

配置格式匹配是调用方与设备的约定；Capture 不通过 Engine 查询，也不重新 open 设备。纯 QUEUE 且无 `state_cb` 的实例不创建回调资源；首次 CALLBACK 或 `state_cb` 订阅时按需创建 dispatcher，失败则回滚本次订阅。

### 5.2 `qm_audio_capture_get_frame_bytes(capture, bytes)`

```text
任一参数为空 → -QM_EINVAL
*bytes = capture.config.frame_bytes
返回 QM_EOK
```

这里读取的是创建后不再变化的配置。调用方仍必须保证 Capture 尚未销毁。

### 5.3 `qm_audio_capture_get_snapshot(subscription, snapshot)`

```text
任一参数为空 → -QM_EINVAL
持 state_lock 复制 subscription.snapshot
解锁，返回 QM_EOK
```

可以查询 ERROR/STOPPED。退订成功后句柄失效，不得再查询。不要直接返回内部 snapshot 的指针。

## 6. 订阅、启用和清空

### 6.1 `qm_audio_capture_subscribe(capture, config, out_subscription)`

```text
out_subscription 为空 → -QM_EINVAL
*out_subscription = NULL
capture 为空 → -QM_ENOSYS
config/枚举/enabled 值无效 → -QM_EINVAL
QUEUE 却配置 frame_cb/packet_cb → -QM_EINVAL
CALLBACK 缺少或提供了不匹配原生格式的媒体回调 → -QM_EINVAL

锁内取得生命周期占用；已占用 → -QM_EBUSY
持 state_lock 检查 Capture 未 closing、订阅数未满
    否则解锁并释放生命周期占用，返回 -QM_EBUSY 或 -QM_EFULL
释放 state_lock

计算本订阅容量，分配 Subscription、队列存储、data_ready
qm_ring_queue_init(...)
若需要 CALLBACK/state_cb，按需准备 dispatcher
失败 → 释放本次资源及生命周期占用，返回错误

复制订阅配置，记录实际 queue_len
snapshot.state = enabled ? RUNNING : DISABLED
snapshot.result = QM_EOK
若 enabled:
    按第 7.1 节同步启动输入
    失败 → 释放本次订阅及生命周期占用，输出仍为 NULL

持 state_lock:
    enabled 且 source_error 非零 → 保存源错误，暂不发布
    否则将订阅放入表中，写入 out_subscription，提交回调状态通知
释放 state_lock
若发布前检查失败:
    锁外尝试 stop 清理本次启动，停止失败仍保留待清理状态
    释放本次订阅，输出仍为 NULL，保留原源错误作为结果
释放生命周期占用
返回结果
```

启用的新订阅仅在启动成功后发布；`capture_start()` 返回前到达的 PUSH 数据不交付给尚未发布的
新订阅。失败回滚不释放整个 Capture；未成功停止的设备仍由 Capture 保留，供后续清理重试。

此处的“启动成功”是瞬时结果；返回后仍可能发生普通运行期故障，消费者通过 recv/snapshot 观察。

### 6.2 `qm_audio_capture_enable(subscription, enabled)`

```text
校验参数；取得生命周期占用
持 state_lock:
    Capture 正在 closing、订阅 removing 或处于终态 → -QM_EBUSY
    enabled == false:
        清空队列、计数与丢帧提示，置 DISABLED 并唤醒 recv
    否则 source_running == true:
        在当前锁内置 RUNNING，不重新启动设备、不清空队列
    否则:
        need_start = true
释放 state_lock

仅 need_start 为 true 时按第 7.1 节同步启动输入
    start_device 失败 → 保留其错误及已传播的订阅状态
    启动返回成功 → 锁内优先采用本订阅 ERROR 的首次结果，否则检查 source_error
    只有最终结果成功才置 RUNNING，不能覆盖并发发生的终态

释放生命周期占用，返回结果
```

disable 清空积压且不停止物理源；重复 enable(true) 保留排队数据。全部订阅禁用后物理源仍可运行；最后一个订阅退订或 shutdown 才停止设备。

disable 不是回调静止屏障：已开始的回调可以继续，不能据此释放 `user_ctx`。

### 6.3 `qm_audio_capture_flush(subscription)`

```text
subscription 为空 → -QM_EINVAL
持 state_lock:
    removing、Capture 正在关闭或订阅处于终态 → -QM_EBUSY
    qm_ring_queue_reset(ring)
    queued_records = 0
    清除随被丢弃队列一起消失的 discontinuity_pending
解锁，返回 QM_EOK
```

不停止设备、不重置 PCM 序号、不改变启用状态。已经借给回调的数据不受影响；flush 本身不新增 discontinuity 提示。

## 7. 输入获取和队列分发

### 7.1 启动输入：subscribe/enable 内的一段共同流程

调用方已经取得生命周期占用，这里不再嵌套调用公开 enable/subscribe。

```text
锁内检查 source_running；已经运行 → QM_EOK
先按第 9.1 节完成上一轮尚未完成的停止；失败 → 返回错误

持 state_lock:
    source_error = QM_EOK
    source_running = true
    device_needs_stop = true       # 在 start 之前设置
释放 state_lock

result = qm_audio_device_capture_start(device_ctx)

持 state_lock:
    start 返回正错误码时规范化为 -QM_EIO
    若 start 成功但 source_error 已非零，采用该源错误
    PULL 且仍成功:
        reader_active = true
        qm_task_new(..., 读取任务, capture, ...)
        创建失败 → reader_active = false，记住错误
    若失败 → 按第 7.5 节记录源故障，保留原始启动错误
释放 state_lock

失败:
    按第 9.1 节尝试停止本轮输入
    返回原启动错误；停止失败时保持 device_needs_stop，之后仍须重试
成功:
    返回 QM_EOK
```

设置 `reader_active` 与创建任务时应防止新任务先运行、再被创建方覆盖退出标记；可在持 `state_lock` 时完成设置及任务创建，让新任务先等这把锁。

### 7.2 PULL 读取任务

```text
循环:
    锁内检查 source_running；false → 退出循环
    锁外调用 qm_audio_device_capture_read(device_ctx, input, READ_TIMEOUT_MS)

    持 state_lock:
        如果 source_running 已 false:
            忽略这次结果，不再发布
        否则如果 result == -QM_ETIMEOUT:
            暂时无数据，继续循环
        否则如果 result != QM_EOK:
            按第 7.5 节记录源故障
        否则如果 input 校验失败:
            记录源故障 -QM_EIO
        否则:
            按第 7.4 节复制、分发 input
    释放 state_lock

退出:
    锁外调用 device_capture_stop，停止失败仍保留待清理事实
    持 state_lock:
        stop 成功 → device_needs_stop = false
        stop 失败 → 保留 device_needs_stop 并记录错误
        将自身 task 句柄复制到任务局部变量
        清除 Capture 内的 task 句柄，reader_active = false
        提示 quiesced
    释放 state_lock
    此后不再访问 Capture 或借用的设备数据
    qm_task_exit(局部 task 句柄)
```

PULL 返回的数据只借用到下一次 read 或 stop；必须在这之前复制完。reader 在最后一次读/复制结束后负责 stop；最后在锁内清 active 并提示 quiesced，解锁后不得再访问 Capture。没有常驻 reader、task_wake、task_stopped 或启动确认等待。具体 QMOS 的自退出句柄回收应按平台实现核对。

### 7.3 `qm_audio_capture_feed(input, result)`：仅 PUSH 编译

```text
取得创建时已发布的 PUSH 实例；没有实例 → -QM_EBUSY
媒体提交: result == QM_EOK，input 必须为有效原生数据
错误提交: result < QM_EOK，input 必须为 NULL
参数组合错误 → -QM_EINVAL

持 state_lock:
    Capture 正在关闭，或 source_running == false → -QM_EBUSY
    媒体提交 → 按第 7.4 节复制、分发
    错误提交 → 按第 7.5 节记录首次源故障
释放 state_lock
返回 QM_EOK
```

feed 返回前完成复制；不调用回调、编码器或网络发送，也不等待消费者腾出空间。生产者必须在任务上下文串行调用。

PUSH 全局入口不需要靠编译器原子操作管理，但以下生命周期条件缺一不可：

1. create/destroy 全局串行，feed 不与它们并发；即使是“无实例时试投递”也不能例外。
2. 指针在初始化全部成功后发布；之后才通过设备 start 放行生产者。
3. 设备生产者的启停使用 QMOS 同步，建立初始化到生产者读取之间的可见性。
4. 设备 stop 成功等待全部在途 feed 返回，并禁止旧生产者再次投递。
5. destroy 只能在这个停止屏障之后清空指针、释放实例。下一实例不能接到旧生产者的数据。

这些约束若做不到，就需要重新设计入口注册和生命周期；给普通指针加 `volatile` 不能解决问题。

### 7.4 校验原生单元并复制到订阅队列

校验规则与 `qm_audio_format.h` 保持一致：

| 项目 | 条件 |
| --- | --- |
| 数据 | data 非空，`0 < size <= frame_bytes` |
| PCM | 格式有效，size 按 channels × 单采样字节数对齐 |
| 编码包 | 单次恰好一个完整包；符合 max_packet_bytes、可选固定 packet_bytes 和时长约束 |
| flags | 只接受 TIMESTAMP_VALID、DURATION_VALID、DISCONTINUITY；不接受 EOS |
| 时间戳/时长 | 只在对应 VALID 位有效时解释；不把排队或发送时间冒充采集时间 |

PUSH 的无效媒体返回 `-QM_EINVAL`，不应因此终止正常源；PULL 的无效结果表示设备违反约定，记录 `-QM_EIO`。

以下整段持有 `state_lock`：

```text
从 input 构造栈上 metadata
PCM: 给这个输入块分配一次 Capture 序号，所有订阅看到同一个序号
编码包: 保留设备给出的 sequence；不能因订阅不同而重编号
构造 segments = [{ &metadata, metadata_bytes }, { input.data, input.size }]

依次遍历有界 subscriptions 表:
    removing 或 state != RUNNING → 跳过

    queued_records == queue_len:
        qm_ring_queue_discard() 丢弃一条最旧记录
        queued_records -= 1
        discontinuity_pending = true

    qm_ring_queue_push_segments_overwrite(ring, segments, 2)
    成功 → queued_records += 1
    非预期失败 → 本订阅置 ERROR(-QM_EIO)，reset 队列，计数清零
    队列由空转为非空时提示本订阅 data_ready
```

需要检查 discard/push 返回值，队列损坏后不能盲目更新计数。预先按“每条都是最大记录”分配容量，正常情况下丢掉一条后新记录一定能容纳。异常仅终止受影响订阅，其他订阅仍可接收。

发生覆盖时，在下一次实际交付给该订阅的记录上 OR 上 DISCONTINUITY，然后清除待报告标记。既有源 DISCONTINUITY 位必须保留。变长包不拆包、不拼包；PCM 分块也不为了 Opus 时长在 Capture 内拼接。

### 7.5 源故障

```text
持 state_lock:
    source_error 只记录本轮首次原始负 QUMI 错误
    已处于 ERROR/STOPPED 的订阅不覆盖原状态或错误
    source_running = false
    对所有非终态订阅（包括 DISABLED）:
        state = ERROR，result = source_error
        清空队列、计数和丢帧提示
        提示 data_ready
```

故障传播本身不释放资源、不阻塞等待 stop。PULL reader 随后 stop 并退出；PUSH 生产者从 feed 返回，设备停止由生命周期调用方处理。消费者被唤醒并拿到错误；订阅仍须 unsubscribe，stop 失败仍须用保留句柄重试。

## 8. 接收接口

### 8.1 `qm_audio_capture_recv(subscription, frame, timeout_ms)`

PCM 与编码包使用同一个接收入口。格式由创建时的 `input_format` 固定，消费者按该格式解释输出，不需要额外的类型参数。PCM 交付完整采样帧对齐的数据块；编码媒体交付恰好一个完整包，不拆包、不转换。

```text
frame 为空 → -QM_EINVAL
保存 frame->data 为 caller_buffer，保存 frame->size 为 capacity_bytes
校验 subscription、frame、caller_buffer、capacity_bytes 和 QUEUE 模式
    无效 → -QM_EINVAL
失败路径清零 frame->size，保留 frame->data
不得从 Capture 的读取/回调任务调用

建立本次 deadline
取得 state_lock
recv_active 已 true → 解锁，返回 -QM_EBUSY
recv_active = true

循环，始终先在锁内检查:
    removing 或 state == STOPPED → result = -QM_EINTR，结束
    state == ERROR → result = 保留的错误，结束
    Capture closing → result = -QM_EINTR，结束
    state == DISABLED → result = -QM_EBUSY，结束

    queued_records > 0:
        peek_size 获取整条记录大小
        校验记录包含非空 payload，且不超过配置上限
        payload 大于 capacity_bytes → -QM_EINVAL，保持队头不动，结束
        分配本次临时缓冲；分配失败 → -QM_ENOMEM，保持队头不动
        pop 完整记录到临时缓冲
        queued_records -= 1
        metadata 复制到 frame，payload 复制到 caller_buffer，释放临时缓冲
        frame.data 保持 caller_buffer，frame.size = payload 大小
        frame.timestamp_ms = metadata.timestamp_ms
        frame.duration_ms = metadata.duration_ms
        frame.sequence = metadata.sequence
        frame.flags = metadata.flags
        合并本订阅 discontinuity_pending，然后清除该标记
        result = QM_EOK，结束

    已到期限/上一轮等待超时且仍无可读结果 → -QM_ETIMEOUT，结束
    在锁内清除 data_ready 旧提示
    释放 state_lock
    qm_sem_wait(data_ready, 剩余等待时间)
    重新取得 state_lock
    重查数据/状态；其他 OS 等待错误也必须退出，不能无限循环

结束时仍持 state_lock:
    recv_active = false
    提示 quiesced
    解锁，此后不再访问 subscription
    返回 result
```

peek/pop 的损坏或其他非预期错误必须停止这次接收，清理本订阅队列并记录 `ERROR(-QM_EIO)`，不能交付半条记录。对合法的“调用方容量不足”只报错，不消费队头，也不清除 discontinuity 提示。

### 8.2 `qm_audio_capture_peek_size(subscription, size)`

```text
size 为空 → -QM_EINVAL
*size = 0
校验 subscription 和 QUEUE 模式
    无效 → -QM_EINVAL
持 state_lock:
    removing 或 state == STOPPED → -QM_EINTR
    state == ERROR → 返回本订阅保留的 snapshot.result
    state == DISABLED → -QM_EBUSY
    队列为空 → -QM_EEMPTY
    qm_ring_queue_peek_size 获取整条记录大小
    peek 失败、记录不含 payload 或超出 frame_bytes 上限 → 本订阅 ERROR(-QM_EIO)，清队列
    不影响其他订阅和物理源
    *size = record_bytes - sizeof(metadata)
    返回 QM_EOK
```

`peek_size` 是非阻塞查询，只查看当前队头，不消费也不预留记录。调用方必须与同一订阅
的 `recv()` 串行执行；若两次调用之间慢订阅者的队列发生覆盖，队头可能变化，`recv()`
仍须按容量不足处理。返回的大小只包含媒体 payload，不包含 Capture 私有 metadata。

## 9. 停止、退订和销毁

### 9.1 停止输入：unsubscribe/shutdown/重启共用的步骤

调用方持生命周期占用。本段不能由 PULL reader、dispatcher 或 PUSH 生产者同步执行。

```text
持 state_lock:
    source_running = false
    读取 reader_active 和 device_needs_stop
解锁
设备无待清理状态且 reader 已退出 → QM_EOK

锁外调用 qm_audio_device_capture_abort(device_ctx)
    abort 只是请求中断；不释放借用缓冲，不代表已停止

PULL 且 reader 仍 active:
    reader 完成本次 read/复制后自行 stop，再清 active 并通知
    调用方锁外等 quiesced，使用同一 deadline 的剩余时间
    超时 → -QM_ETIMEOUT，保留全部资源
    reader stop 成功 → 完成；失败 → 保留待清理事实，返回错误

PUSH 或没有 reader 的待清理设备（含上次 stop 失败）:
    锁外调用 qm_audio_device_capture_stop(device_ctx)
    PUSH: adapter 必须等所有在途 feed 返回并关闭后续投递
    成功 → 锁内 device_needs_stop = false
    失败 → 保留 device_needs_stop 和设备上下文，返回负错误
```

start 失败也可能已部分放行设备，因此仍须 stop。reader stop 失败后，下一生命周期调用
可以直接重试 stop。PUSH 全局入口从 create 保留到 destroy；shutdown 只关闭数据准入，
不会释放入口槽位。

### 9.2 `qm_audio_capture_unsubscribe(subscription)`

```text
subscription 为空 → -QM_EINVAL
拒绝从 Capture reader/dispatcher 或 PUSH 生产者执行
取得生命周期占用，建立本次停止 deadline

持 state_lock:
    removing = true                 # 重试时保持这个标记
    清除 state_pending；非终态置 STOPPED，已有 ERROR 保留首错
    清空本订阅队列，禁止后续交付
    提示 data_ready，让阻塞 recv 返回
解锁

若这是最后一个订阅:
    按第 9.1 节停止输入
    失败 → 释放生命周期占用，保留句柄，返回错误

依次等待 recv_active == false、callback_active == false
    等待时释放 state_lock，使用同一 deadline
    失败 → 释放生命周期占用，保留句柄，返回错误

持 state_lock 从表移除本订阅
解锁，释放 data_ready、storage、Subscription
释放生命周期占用，返回 QM_EOK
```

从退订开始，调用方须排除新的句柄访问。已进入的唯一 recv 可以由本接口打断。失败后句柄仍有效但已在移除中，仅用于完成清理/必要查询，不重新 enable；成功后 `user_ctx` 才可释放。

### 9.3 `qm_audio_capture_shutdown(capture, timeout_ms)`

```text
capture == NULL → QM_EOK
建立 deadline
拒绝不允许的任务上下文；取得生命周期占用

持 state_lock:
    closing = true
    source_running = false
    所有非终态订阅 → STOPPED，result = QM_EOK
    已有 ERROR 保持原错误
    清空各队列并提示 data_ready
解锁

按第 9.1 节停止输入
失败 → 释放生命周期占用，保留 CLOSING 与全部资源，返回错误

等待 dispatcher 退出；closing 已关闭新回调准入，不强制交付待处理终态回调
设备/reader 与 dispatcher 等待使用同一 deadline；这里不等 recv_active
任何失败 → 保留资源，释放生命周期占用，返回错误

锁内 stopped = true，作为 destroy 的完成条件
释放生命周期占用，返回 QM_EOK
```

shutdown 不释放订阅、不关闭借用设备。只有 shutdown 成功后，设备所有者才可以 close 设备；剩余订阅可查询、退订。关闭是终态，不能再 subscribe/enable。已有 ERROR 快照保留首错；shutdown 不承诺终态回调一定送达，使用方可查询快照。重复 shutdown 再执行上述停止检查，已静止时仍成功。

### 9.4 `qm_audio_capture_destroy(capture)`

```text
capture == NULL → QM_EOK
调用方已排除所有新的 Capture/订阅访问
取得生命周期占用；冲突返回 -QM_EBUSY
锁内检查 stopped == true、subscriptions 为空、设备无待停止状态且 reader/dispatcher 已退出
解锁；条件不满足 → 释放生命周期占用，返回 -QM_EBUSY

PUSH: 确认设备停止屏障已完成，清空全局入口指针
释放 Capture 自有的信号量、互斥锁、buffer 和对象
返回 QM_EOK；从此 capture 失效
```

销毁锁本身不能发生在锁仍被当前任务持有时；外部所有者须排除新访问。只有 shutdown 成功、任务/订阅静止且设备停止屏障完成后才释放资源及 PUSH 槽位。

## 10. CALLBACK：按需创建的独立分发任务

首次 CALLBACK 或 `state_cb` 订阅按需创建每 Capture 唯一 dispatcher；纯 QUEUE 且
无 `state_cb` 不需要它。媒体回调缓冲只在 CALLBACK 使用时分配；状态回调不需要媒体缓冲。

CALLBACK 必须恰好提供匹配原生格式的一个媒体回调：PCM 用 frame_cb，编码包用 packet_cb。
QUEUE 禁止这两个媒体回调，但允许 state_cb。state_pending 表示待通知的最新状态，
中间状态可以合并；所有回调在数据锁外串行执行。

```text
Capture 保存 dispatch_task、dispatch_active、dispatch_wake、dispatch_cursor
媒体 CALLBACK 使用 callback_buffer，回调返回前不复用
Subscription 保存 callback_active、state_pending

回调任务等待 dispatch_wake，然后反复选择待交付工作:
    持 state_lock:
        closing → 退出任务
        从 dispatch_cursor 开始轮转扫描有界订阅表
        跳过 removing 的订阅
        有 state_pending → 复制 snapshot 到局部变量并清除 pending
        否则有 CALLBACK 媒体 → 校验并 pop 一条到 callback_buffer
            队列损坏 → 仅本订阅 ERROR(-QM_EIO)，清队列并安排状态通知
        都没有 → 继续找下一订阅；全部没有则重新等待
        本次选择后前移 dispatch_cursor
        callback_active = true，保存订阅、callback 和 user_ctx
    解锁后调用 state_cb/frame_cb/packet_cb
    持 state_lock:
        callback_active = false
        提示 quiesced
    解锁后不再使用本次借用的订阅指针
    重新扫描当前订阅表，不沿用解锁前的数组元素或 next 指针

退出任务:
    最后在锁内清 dispatch_active、提示 quiesced
    解锁后不再访问 Capture，使用局部任务句柄自退出
```

轮转避免持续有数据的前部订阅阻止后部订阅交付；慢回调仍会延迟其他回调。
状态和媒体回调都用 callback_active 保护，unsubscribe 必须等待它们结束。
回调中允许查询和 flush，不允许 subscribe、enable、recv、unsubscribe、shutdown、destroy。

shutdown 关闭新回调准入后可以放弃 pending 通知，终态快照仍保留至 unsubscribe。
dispatcher 不自动停止设备，不增加错误清理任务。

## 11. PCM→Opus 使用方示例

下面是使用方逻辑，不放进 Capture；“重采样”“编码器”是概念操作，具体参数按所选 codec provider 的能力设置。

```text
准备:
    所有者打开设备，取得固定 PCM 输入格式
    qm_audio_capture_create(...)
    创建 disabled 的 QUEUE subscription
    获取 frame_bytes，分配接收缓冲
    使用方打开 Opus 编码器及必要的格式转换器
    启动消费任务，再 enable(subscription, true)

消费任务:
    while 尚未请求结束:
        recv(subscription, frame, 有限等待)
        -QM_EBUSY（尚未启用）→ 等使用方的启动事件，不能忙循环
        -QM_ETIMEOUT → 检查结束请求后继续
        -QM_EINTR 或永久错误 → 通知生命周期所有者并退出
        成功:
            DISCONTINUITY → 按业务约定处理缺口和未完成的 PCM 拼帧
            只有输入与编码器要求不一致时才做格式转换
            按编码器要求累计 PCM 样本数并提交编码
            消费产生的完整 Opus 包：发送、存储或交给后级

结束，由生命周期所有者执行:
    shutdown(capture, timeout) 唤醒并停止采集
    失败 → 保留设备和句柄，安排重试
    成功 → 等消费任务退出，再释放消费缓冲/编码器
    unsubscribe(subscription) 成功后清空该句柄
    destroy(capture) 成功后清空该句柄
    由设备所有者 close 设备
```

shutdown 停止设备并等待 reader/dispatcher 退出，同时唤醒 recv，但不等待 recv 返回；unsubscribe 才等待该订阅的 recv/callback 静止。两者都不等待消费者在 recv 返回后进行的编码或网络发送，因此消费任务需要自己的停止与退出通知，不能把 shutdown 成功当成编码器已经无人使用。

设备给出 10 ms 或不定长 PCM 块、编码器需要另一种帧长时，使用方负责累计/拆分；Capture 保留原始块边界即可。设备本来就输出 Opus 时，同样使用 `recv()` 取得完整编码包，不重复编码。

## 12. 手工实现后的检查项

| 场景 | 应观察到的结果 |
| --- | --- |
| 创建失败、重复占用 PUSH 入口 | 输出句柄为 NULL，已分配资源回收，不发布半初始化对象 |
| start 内立即 feed，然后 start 返回失败 | 数据不访问未初始化订阅；仍执行 stop 屏障 |
| start 失败且 stop 也失败 | 保留 Capture 与停止责任，重试前不启动下一轮 |
| 两个订阅，一个消费很慢 | 慢订阅丢自己的最旧记录，快订阅继续获得数据 |
| 可变长度编码包填满队列 | 记录数不超过 queue_len；丢弃/交付保持整包 |
| 接收缓冲不足 | 返回 -QM_EINVAL，队头仍在，frame.size 清零，frame.data 保持调用方缓冲 |
| recv 阻塞时 disable/unsubscribe/shutdown | 被唤醒并按状态返回；资源不先于 recv 释放 |
| 源错误与并发重启 | 旧订阅保留 ERROR，新订阅可以重启；旧清理不误停新运行 |
| 同步 start/reader 创建失败 | subscribe 输出保持 NULL，enable 返回真实错误；未清理设备可继续 shutdown 重试 |
| DISABLED 订阅遇到源错误 | 进入 ERROR 并保留首次错误，后续 shutdown 不覆盖 |
| 单订阅队列损坏 | peek/recv/callback 一致记录 -QM_EIO，其他订阅继续交付；容量不足不终止订阅 |
| 持续积压的 CALLBACK 订阅 | dispatcher 轮转交付，后部订阅仍可获得执行机会 |
| PULL reader stop 失败 | active 在最终通知前正确清除，待清理事实保留，下一生命周期调用直接重试 stop |
| shutdown 超时后重试 | 句柄、设备、队列仍可安全清理，不发生二次释放 |
| PUSH 销毁后重建 | 旧生产者不再 feed，新实例不接收旧数据 |
| CALLBACK 中的查询/flush、退订并发 | 允许的调用无死锁，退订等待在途回调，借用数据始终有效 |
| Capture 禁用、PULL、PUSH 三种编译 | 条件编译与头文件一致；feed 仅在 PUSH 模式提供 |

这张表是验收条件，不代表全部场景已经验证；具体主机检查结果以本轮验证记录为准，真实设备停止时序和平台并发行为仍需目标板测试。
