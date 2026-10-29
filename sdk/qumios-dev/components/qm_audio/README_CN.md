# qm_audio

本文定义 qm_audio 的分层架构、模块边界、媒体合同和验证范围。公共头文件是接口基线；实现和测试用于确认行为。文档与实现不一致时，以接口合同和实现修订结果为准。

英文版本：[README.md](README.md)。

架构图册：[ARCHITECTURE_CN.md](ARCHITECTURE_CN.md)；[English](ARCHITECTURE.md)。

| 项目 | 说明 |
| --- | --- |
| 范围 | components/qm_audio 及其通用 codec/device adapter 边界 |
| 框架模型 | 单一默认音频实例、单一物理输出路由、PCM 或原生编码媒体 |
| 详细依据 | include/、include/adapter/、src/、tests/ |
| 不作为证明 | 宿主 mock 测试不证明目标硬件声学、板级连线、网络或云端协议已联调 |

## 文档与实现对应关系

表 1 列出主要设计项对应的源码位置。

| 当前事实 | 源码锚点 |
| --- | --- |
| 默认实例由 qm_audio_init() 创建与销毁 | src/audio/qm_audio.c |
| 句柄生命周期和订阅静止 | include/qm_audio_control.h、include/qm_audio_capture.h 和 src/control/qm_audio_control.c |
| 固定 profile、codec/framing、packet 类型 | include/qm_audio_types.h |
| Control 创建、播放和 PUSH 输入 | include/qm_audio_control.h、src/control/qm_audio_control.c |
| Service 只管理播放会话 | include/qm_audio_service.h |
| 固定设备 capability 的校验 | src/engine/qm_audio_engine.c |
| 独立原生 Capture | src/capture/qm_audio_capture.c、include/qm_audio_capture.h |
| 操作状态、事件与 release | include/qm_audio_control.h、src/control/qm_audio_control.c |
| 容量与有界背压配置 | qm_kconfig、src/engine/qm_audio_engine.c |
| device/codec adapter 合同 | include/adapter/ 及对应的平台实现 |
| 可选前端唤醒和 capture-feed 边界 | include/adapter/qm_audio_frontend.h、include/adapter/qm_audio_device.h 和 qm_audio.h |
| 可选 cloud adapter 生命周期 | include/cloud/qm_audio_cloud.h 和 src/cloud/qm_audio_cloud.c |
| 可自动验证的核心契约 | components/qm_audio/tests 与集成测试集 |

## 总体分层架构

~~~mermaid
flowchart TB
    App["产品应用 / 协议适配"]
    Source["媒体输入<br/>FILE / URI / MEMORY / CUSTOM / PUSH"]
    Consumer["采集消费者<br/>QUEUE（拉取）/ CALLBACK"]

    subgraph Core["qm_audio"]
        direction TB
        subgraph L1["1. 业务控制层"]
            Control["Control<br/>operation、event、snapshot、PUSH"]
        end
        subgraph L2["2. 会话调度层"]
            Service["Service<br/>准入、排队、优先级、抢占、drain"]
        end
        subgraph L3["3. 媒体处理层"]
            Pipeline["Pipeline<br/>source、路由、透传、decode/encode"]
        end
        subgraph L4["4. 设备路由层"]
            Engine["Engine<br/>固定 profile、render、设备生命周期"]
            Capture["独立 Capture<br/>reader、订阅队列、callback"]
        end
    end

    subgraph Adapter["平台 adapter"]
        Device["device adapter<br/>设备 I/O"]
        Codec["decoder / encoder provider<br/>格式处理"]
    end
    Hardware["设备 / 板级实现"]

    App --> Control
    Control -->|创建 / 校验| Pipeline
    Control -->|提交 session| Service
    Service -->|授权输出| Pipeline
    Pipeline --> Engine
    Source --> Pipeline
    Pipeline -. codec provider .-> Codec
    Engine -->|playback| Device --> Hardware
    Device -->|原生采集| Capture
    Device -. get_caps .-> Engine
    Engine -. 管理生命周期 .-> Capture
    Capture --> Consumer --> App
    Control -->|event / snapshot| App
~~~

表 2 定义框架内层次和平台边界。默认 qm_audio 实例在初始化阶段创建并共享 Service、Engine 对象、实例事件执行器和 Source worker；Control 按业务创建。

| 层次 | 核心对象 | 职责 | 不承担 |
| --- | --- | --- | --- |
| 业务控制层 | Control | 接收业务命令，管理 operation、事件、快照和 PUSH 输入 | 跨会话调度、编解码、设备 I/O |
| 会话调度层 | Service | 会话准入、排队、优先级、抢占、取消和 drain 完成 | Source 读取、媒体处理、设备访问 |
| 媒体处理层 | Pipeline | Source 管理、路由规划、PCM/字节流/packet 交付、decode/encode | 设备所有权、会话调度、产品策略 |
| 设备路由层 | Engine | 固定 profile 校验、设备所有权、render I/O、Capture 对象生命周期 | Capture 内部实现、URI 读取、编解码、业务协议 |
| 原生采集 | Capture | 设备采集 I/O、独立读取/分发任务、有界订阅 | Engine/Service/Pipeline 状态、格式转换、网络传输 |
| 平台边界 | device adapter、codec provider | 发布实际能力，实现设备 I/O 和指定格式处理 | Control/Service 状态机、产品业务流程 |

实线表示调用或媒体数据流；虚线表示能力查询或生命周期管理。Control 创建 Pipeline 并提交 session；Service 准入后向 Pipeline 授予输出权。播放媒体链路为 `Pipeline -> Engine -> device adapter`。Capture 独立读取设备并向订阅者交付，不依赖 Engine，也不经过 Service 播放会话。路由组合见第 4 章，Capture 生命周期见第 6 章。

### 执行模型与并发约束

框架采用单输出、单 Source worker。没有多 worker 配置和独立 Pipeline task。以下约束定义执行、同步和资源回收边界。

| 状态或资源 | 所有者 | 跨任务访问规则 |
| --- | --- | --- |
| operation 状态 | `audio_event` | 命令串行处理；外部查询读取受保护的快照，终态结果只发布一次 |
| Service 准入、抢占和动作状态 | Service 统一动作核心 | 运行期由 `audio_event` 推进；外部同步调用驱动同一核心，登记与状态提交由 Service 锁保护 |
| Source 游标、编解码过程、运行 generation | `audio_source` | pause 保留上下文并让出 worker；cancel 解除阻塞，旧运行退出后才重建上下文 |
| 当前输出授权、队列记账、媒体计数和音量/静音 | Engine / `audio_render` | Pipeline 绑定稳定输出句柄后直接提交；撤销授权与释放句柄分离；音量/静音由 `audio_event` 排序并只在 render worker 调用设备 |
| Capture 订阅表和每个消费者队列 | 独立 Capture | PULL reader 只采集和入队；私有 `audio_cap_cb` 执行回调，QUEUE 由消费者任务读取 |
| 借用 source/context 和 operation 存储 | 应用与 Control | 只有 release 成功才结束借用；超时保留对象并允许重试 |

每块媒体走 `Pipeline → Engine render queue → device adapter`，不经过 Control/Service 的状态锁。状态转换和句柄借用仍需短临界区；PUSH ring、Capture ring、异步 codec 输出与取消之间仍需同步。不得持有上述锁调用应用 callback、执行 Source I/O 或等待 worker 退出。

外部 `qm_audio_set_volume()` 和 `qm_audio_set_mute()` 调用方会一直持有实例设置槽，直到取走 worker 结果。音频 callback 中的调用只做非阻塞获取；其他调用方仍持有请求或未读结果时，立即返回 `-QM_EBUSY`。

取消分为四个边界，不能用一个 terminal 标志替代：

1. 接受取消：锁存取消状态，撤销本轮写入权限，唤醒数据和空间等待。
2. 执行静止：Source I/O 返回，codec 不再输出，Source worker 离开本轮执行。
3. 路由可复用：旧设备写入和 drain 已结束，输出可授予下一个 session。
4. 对象可释放：事件投递、API 借用和 callback 全部静止，才释放 source、codec 和 operation。

CUSTOM 的 cancel 必须线程安全、幂等、不阻塞，不得释放 context；open/read 必须在取消后返回。取消不得依赖已经阻塞的 worker 消费另一条命令。HTTP 使用中断检查，不能从控制任务并发 cleanup 正在使用的 HTTP/TLS 对象；DNS、连接和 TLS 握手的最长阻塞时间仍由传输适配层合同决定。需要确定抢占时限的产品必须验证这一底层边界，或由网络任务通过 PUSH 提供音频。

自然 EOF 才 flush codec 并 drain 输出；取消和错误丢弃尾帧。正常 drain 完成后释放设备授权，终态 operation 继续保存结果，直到业务 release。执行静止等待只报告是否退出，媒体失败码另存于结果字段，避免媒体超时被误判为清理仍未完成。

Pipeline wait 只确认 Source 执行已退出，不代替 provider 关闭。provider close 失败时，destroy/rewind 在执行退出后重试；只有 destroy 或业务 release 成功，才能释放其借用上下文。

验证必须覆盖阻塞 open/read 的取消、PUSH 背压、异步输出、暂停让出 worker、旧 generation 的迟到输出、事件 callback 阻塞时仍保留终态、自然结束后不 release 也能接续、清理超时重试。宿主测试验证并发和所有权；适配层的中断延迟、任务栈和实际声音边界需目标环境验证。

## 1. 范围和限制

### 1.1 应用入口头文件

应用默认包含 `qm_audio.h` 作为统一入口。该头文件聚合共享媒体类型、Control
接口和 Capture 接口，但声明仍分别归属于 `qm_audio_control.h` 与
`qm_audio_capture.h`。主头文件自身提供默认实例生命周期、固定流格式查询、通过
`qm_audio_get_caps()` 查询路由能力以及共享音量/静音控制。

`qm_audio_get_caps()` 报告 `qm_audio_init()` 选定并打开的固定路由，标志位描述采集、
播放、共享输出控制、abort/drain/position 行为和停止语义。它不枚举 codec provider，
不表示硬件一定支持全双工，也不支持运行时重配格式。业务播放使用 Control，采集交付
使用 Capture；只有实现底层边界时才按需包含 Engine、Service、Pipeline、adapter、
frontend 或 cloud 头文件。

qm_audio 是嵌入式音频的通用控制、调度、媒体处理和设备路由组件。产品启动层调用 qm_audio_init() 创建进程级默认实例；业务按场景创建独立的 Control，共用同一 Service 和 Engine。

约束如下：

- 一个 Engine 只管理一个物理设备路由；一个 Service 是该路由唯一的会话准入与抢占裁决者。
- Service session 只表示播放；录音与双工会话不是当前 Service API，采集通过显式的独立 Capture 句柄提供。
- Control 负责业务操作，不直接写设备；Pipeline 负责单次媒体处理，不作跨会话调度；Engine 是设备唯一持有者。
- Pipeline 可选择 PCM、编码字节流或完整编码 packet 的路线，但不提供任意媒体图、动态插件注册或通用 codec registry。
- 不做采样率、声道数、采样位宽转换，不做混音、容器复用/解复用，也不从任意字节块猜测 packet 边界。
- 平台 adapter 必须显式拒绝不支持的 profile，不能把编码媒体静默当作 PCM。

## 2. 对象所有权与启动顺序

| 对象 | 已实现职责 | 不负责 |
| --- | --- | --- |
| qm_audio | 默认实例生命周期、共享 Source worker 和实例级资源 | 产品资源规则、云端业务流程 |
| Control | 播放操作、事件、快照、PUSH 写入、暂停/恢复/停止 | 编解码、物理设备写入、跨会话裁决 |
| Service | 会话准入、优先级、排队、抢占、排空和终态 | Source 读取、编解码、设备 SDK |
| Pipeline | Source、路由规划、透传、解码、编码、媒体回调 | 设备所有权、抢占策略 |
| Engine | 固定设备 profile、播放、设备与 Capture 对象生命周期 | Capture 内部实现、URI 读取、格式解析、产品策略 |
| Capture | 原生采集、有界分发、采集与回调任务 | 上层状态、编解码、网络传输 |
| adapter | 一种 MCU/设备的 codec 或 I/O 绑定 | Service/Control/云端状态机 |

初始化与清理必须遵守下列顺序：

1. 产品填写固定 input_format 和 output_format，调用 qm_audio_init()。
2. 每个业务调用 qm_audio_control_create() 创建自己的 Control。
3. 业务提交 qm_audio_control_play()，通过事件、快照或等待接口观察结果。
4. 关闭时先停止并释放业务操作、订阅和 Control，再调用 qm_audio_deinit()。

qm_audio_init() 对相同配置可重复成功；配置不同时返回 -QM_EBUSY，不会重配正在运行的设备。关闭失败或超时不释放仍可能被后端访问的对象，调用方必须保留句柄并重试清理。

Service shutdown 只有在活动与排队会话已静止后才返回成功。并发 session release
仍持有输出清理时返回 `-QM_EBUSY`；准入保持关闭，调用方须等待 release 完成后重试。

直接装配 Engine 和 Service 时，必须确定唯一的输出所有者。公开
`qm_audio_engine_render_open()` 只用于未绑定 Service 的 Engine。Service 创建会
拒绝已有渲染流的 Engine，并通过校验自身身份的私有入口打开流。Service 直到
destroy 才释放路由所有权：先 shutdown、释放全部 session 并 destroy Service，
再关闭 Engine。Engine 查询统一返回完整的原生 stream profile；仅 PCM 的便捷
查询保留在应用门面 `qm_audio`。

Engine 流的媒体完成和资源释放是两个独立屏障：

| Engine 流路径 | 必须调用 |
| --- | --- |
| 正常结束 | `render_drain()` -> `render_stop()` -> `render_close()` |
| 取消 | `render_stop()` -> `render_close()` |
| open 失败或超时，但输出句柄非 NULL | 保留句柄，重试 `render_stop()` 后 `render_close()`；不得写入媒体 |

drain 确认已接受媒体到达物理输出完成点，不结束后端运行。stop 完成这个停止屏障，
成功 drain 后也不能省略。close 只释放已停止且静止的流，不触发取消也不等待取消；
仍需 stop 时返回 `-QM_EBUSY`。stop 超时保留路由和待完成动作，允许重试。

`qm_audio_engine_shutdown(timeout_ms)` 的 Capture、渲染动作和任务退出等待共用
一个剩余预算。无超时参数的同步 adapter 调用另受平台文档规定的有限界限约束，
因此本参数不保证整个调用的严格返回时限。零值跳过核心等待，但仍可能执行上述
同步调用。只有清理成功才能销毁对象或复用物理路由。

### 2.1 运行任务与执行上下文

任务按运行时所有权分配，并不是每个架构层都机械地对应一个任务。下表中的
`audio_*` 仅是当前实现的任务名，不是公共 API。Control 和 Service 不会为每个
Control、operation 或 session 创建任务。

| 层次或边界 | 任务或执行上下文 | 创建时机与数量 | 工作内容 | 退出条件与限制 |
| --- | --- | --- | --- | --- |
| 业务控制层 | `audio_event` | 每个音频实例一个；默认实例在 `qm_audio_init()` 时创建 | 串行执行命令、分发公开 Control 事件 | 实例关闭时投递 EXIT 屏障。Control callback 不能同步调用生命周期 API，也不能阻塞该任务。 |
| 会话调度层 | 无独立任务 | 不为 Service 或 session 创建 worker | 运行期 Service tick 在 `audio_event` 中推进统一动作核心；外部同步调用等待 Engine progress 并驱动同一核心，清理不依赖 event 仍在运行 | Service priority 是调度策略，不是 RTOS 任务优先级；没有独立 Service 动作任务或队列。 |
| 媒体处理层 | `audio_source` | 每个音频实例常驻一个 | Source I/O、provider 调用、直接向 Engine 交付媒体及等待 drain | 暂停确认后保留上下文并让出执行权；旧运行退出后执行下一轮。没有独立 Pipeline task。反初始化等待注册表清空后停止 worker。 |
| 设备路由层，render | `audio_render` | Engine 的 output profile 有效时每个 Engine 一个 | 执行后端 start/write/pause/resume/stop、位置查询和音量/静音调用 | drain 在调用任务中独占设备访问；stop 从调用任务触发 abort。Engine 关闭记录持久退出请求并等待 worker 退出；worker 不执行应用媒体 callback。 |
| Capture PULL 输入 | `audio_capture` | 首次启用时同步启动设备成功后按需创建；每个 Capture 最多一个；PUSH 模式没有此任务 | 读取原生 PCM 或完整编码包，复制到有界订阅队列 | 最后退订、Capture 关闭或源故障时结束读取，执行设备 stop 后退出；不执行应用回调。 |
| Capture 回调交付 | `audio_cap_cb` | 每个 Capture 至多一个，在首次 CALLBACK 或 `state_cb` 订阅时懒创建；纯 QUEUE 且无 `state_cb` 时不创建 | 在 Capture 锁外轮转选择就绪订阅，串行执行状态与媒体回调 | shutdown 等待分发任务退出，unsubscribe 等待借用回调结束；慢回调影响其他回调，不阻塞 reader、QUEUE 交付或 Control 事件循环。 |
| 应用/协议适配层 | `audio_cloud`（可选） | 启用 cloud support 后，每个可选 cloud adapter handle 一个 | 持有协议工作队列，并把外部工作转换为 Control 调用 | 不属于四个核心层，也不在音频 I/O 路径中。网络工作在此上下文执行，不得在音频锁或媒体 callback 中执行。 |
| 平台边界 | adapter/provider 私有任务 | 由启用的 device adapter 或 codec provider 自行定义 | 设备、DMA、SDK callback 或 codec 实现工作 | qm_audio 不创建，也不计入核心预算。adapter 文档必须说明栈、优先级、取消和关闭行为。 |

渲染命令共用 worker 的持久动作状态；同步调用等待的结果与 Service 异步路径查询
的结果相同。位置查询使用独立的持久结果槽，不会覆盖 Service 尚未读取的控制完成
结果。已有查询请求或等待者时，以及 drain 活跃期间，新位置查询返回 `-QM_EBUSY`；
已受理的查询先完成，再进入设备 drain。设备 drain 在排队写入和普通设备动作静止
后由调用任务执行。drain 占有设备期间，pause、resume 返回 `-QM_EBUSY`，已排队的共享设置等待
它释放设备。stop 从调用任务触发 abort，因此能打断阻塞在 write 的 worker 或
阻塞在 drain 的调用者；后端 stop 仍由 `audio_render` 执行。已停止的流返回缓存的
设备位置观测值；已排空的流仍可由 worker 查询设备位置。

对启用输出的音频实例，核心常驻任务数固定为三个：
`audio_event`、`audio_source` 和 `audio_render`。使用 CALLBACK 或 `state_cb` 时按需增加一个
`audio_cap_cb`；PULL 模式运行时再增加一个 `audio_capture`。纯 QUEUE 且无 `state_cb` 的订阅不增加回调任务，PULL 仍需读取任务。可选协议 adapter 和
adapter 私有任务另行计算。

任务栈和 RTOS priority 是构建期资源，不是 Service 调度优先级。相关配置如下：

| 任务 | 栈配置 | RTOS priority 配置 | 数量或队列上限 |
| --- | --- | --- | --- |
| `audio_event` | `CONFIG_QM_AUDIO_CONTROL_EVENT_TASK_STACK_SIZE` | `CONFIG_QM_AUDIO_CONTROL_EVENT_TASK_PRIORITY` | 每个音频实例一个；还受 event queue 和 pending fact 上限约束 |
| `audio_source` | `CONFIG_QM_AUDIO_CONTROL_SOURCE_TASK_STACK_SIZE` | `CONFIG_QM_AUDIO_CONTROL_SOURCE_TASK_PRIORITY` | 每个实例一个；session 容量不改变任务数量 |
| `audio_render` | `CONFIG_QM_AUDIO_ENGINE_RENDER_TASK_STACK_SIZE` | `CONFIG_QM_AUDIO_ENGINE_RENDER_TASK_PRIORITY` | 每个启用输出的 Engine 一个；render 队列有界 |
| `audio_capture` | `CONFIG_QM_AUDIO_CAPTURE_TASK_STACK_SIZE` | `CONFIG_QM_AUDIO_CAPTURE_TASK_PRIORITY` | PULL Capture 运行时零或一个 |
| `audio_cap_cb` | `CONFIG_QM_AUDIO_CAPTURE_CALLBACK_TASK_STACK_SIZE` | `CONFIG_QM_AUDIO_CAPTURE_CALLBACK_TASK_PRIORITY` | 使用 CALLBACK 或 `state_cb` 时每个 Capture 一个 |
| `audio_cloud` | `CONFIG_QM_AUDIO_CLOUD_TASK_STACK_SIZE` | `CONFIG_QM_AUDIO_CLOUD_TASK_PRIORITY` | 每个已初始化 cloud adapter 一个；还受 cloud 队列和 operation 映射上限约束 |

集成记录必须用栈水位、队列时延和 callback 时长测量说明这些数值。改变 RTOS priority 不会改变 Service priority 或 operation 准入顺序。

### 2.2 数据与上下文所有权

| 对象 | 调用方责任 | 组件行为 |
| --- | --- | --- |
| qm_audio_config_t | 仅调用期间有效 | 初始化时复制 |
| FILE/URI source 的路径或 URL | 仅调用期间有效 | Pipeline 复制字符串 |
| MEMORY source 数据 | 到操作 release 前保持有效 | Pipeline 借用，不释放 |
| CUSTOM source context/回调 | 到操作 release 前保持有效 | Pipeline 借用，不释放 |
| `qm_audio_data_t` | 对生产者写入和回调，在接口合同规定的调用期间有效 | 数据和描述符均为借用；接收队列成功前复制，回调返回后不再保留 |
| adapter backend context | Engine 关闭成功前保持有效 | Engine 在 open/close 间独占使用 |

### 2.3 句柄并发与销毁责任

组件内部锁保护组件状态，但不替产品分配句柄所有权。应用必须串行化每个句柄的终止转换；一旦开始销毁，不得再发起新的访问。改变同一 operation 状态的调用也应由业务所有者串行化；框架不规定并发 pause/seek/stop/release 请求谁获胜。

| 句柄 | 应用必须串行化的操作 | 最终所有权屏障 |
| --- | --- | --- |
| Control | 其他业务任务仍可能提交命令时，不得开始 shutdown 或 destroy。先 shutdown Control，release 全部终态 operation，并取消全部事件订阅后再 destroy。 | 只有 shutdown 完成、保留 operation 和事件订阅均为零、Service/event 使用静止后，`qm_audio_control_destroy()` 才成功。 |
| operation ID | 到 `release()` 成功前保持借用 source/context 有效。release 进行中不得再向同一 operation 提交新命令。 | release 成功后 operation ID 失效；超时或失败时 ID 和清理责任仍保留。 |
| Control event subscription | 只能在业务任务中 unsubscribe，不能在共享事件任务中调用。 | `qm_audio_control_unsubscribe()` 等待在途 callback；成功前不得释放 callback context。 |
| Capture subscription | 同一 subscription 的 `enable`、`flush`、`unsubscribe` 必须串行。QUEUE 交付同一时刻只允许一个 `recv()` 消费者；CALLBACK 交付不能调用 `recv()`。 | unsubscribe 开始后，所有者排除新的句柄访问。成功是 callback/I/O 静止屏障；失败保留 closing 句柄以供重试。 |
| 默认实例和可选 adapter | 在关闭 Control 和 `qm_audio_deinit()` 前停止 cloud、frontend 及 adapter 自有工作。 | stop、close、unsubscribe、shutdown 或 release 失败时，句柄和借用 context 必须保留到后续重试成功。 |

## 3. 固定媒体 profile 与三种表示

qm_audio_stream_format_t 是设备输入或输出的固定媒体 profile。对 PCM，pcm 是真实采样几何；对编码媒体，pcm 是解码或编码路径要求保持的 PCM 几何。opaque 编码透传允许 pcm 全零，但双方必须明确使用同一声明。

| 表示 | profile 条件 | Pipeline/Engine 交付方式 |
| --- | --- | --- |
| PCM | codec 为 QM_AUDIO_CODEC_PCM，packet 字段为零，framing 为 STREAM | `qm_audio_data_t`，按完整 sample frame 对齐 |
| 编码字节流 | 非 PCM 且 max_packet_bytes 为零 | write_bytes；块边界不是 codec 帧 |
| 完整编码 packet | 非 PCM 且 max_packet_bytes 非零 | `qm_audio_data_t`；一次调用正好一个完整 packet |

packet_bytes 是固定 packet 长度，零表示可变长度；packet_duration_ms 是固定 packet 时长，零表示未知或可变。二者都不是通配符。原生 packet 透传要求 packet_duration_ms 精确匹配；sink 声明固定 packet_bytes 时 source 也必须相等，sink 声明可变长度时 source 的固定或可变长度只要不超过 max_packet_bytes 即可。

`qm_audio_data_t` 的 data、size、sequence、timestamp_ms、duration_ms 和 flags 是统一媒体描述。`QM_AUDIO_DATA_TIMESTAMP_VALID` 和 `QM_AUDIO_DATA_DURATION_VALID` 决定时间字段是否有效；`QM_AUDIO_DATA_DISCONTINUITY` 表示前序媒体不连续。空 packet 或不符合固定 packet 长度的 packet 是非法媒体，不是 EOS；可变长度 profile 接受任何非零且不超过 max_packet_bytes 的 payload。暂时无网络数据也不能表示 EOS，正常结束由 finish/drain 路径表达。

### 3.1 输入声明

播放请求使用 qm_audio_codec_config_t 声明 source codec、framing、可选 PCM 几何和 packet 信息。PUSH + PACKET 是完整 packet 的专用入口；普通 PUSH 写入可以拆分或合并 PCM/编码字节流，但不能被解释为 packet 分界。

下列 framing 规则由 Pipeline 在创建时检查：

- Ogg、RAW_HEADER 和 CBR 是 Opus 专用 framing。
- 非 Opus 的 MP3、G.711、G.722、AUTO 等处理型输入只能使用 STREAM，packet_duration_ms 必须为零。
- 非零 packet_duration_ms 的处理型输入目前只用于 Opus RAW_HEADER/CBR；完整 native PACKET 可为产品私有 codec 声明固定时长。
- 私有 codec 从 QM_AUDIO_CODEC_PRIVATE_BASE 起。没有 provider 时仍可按兼容 profile 透传，组件不解析其 payload。

### 3.2 Source 能力矩阵

Source 类型独立决定生命周期、输入结束和 seek 行为。下表属于准入合同，不表示每个构建都启用全部 source。

| Source | 构建前提 | 输入结束 | seek/restart | 所有权与取消 |
| --- | --- | --- | --- | --- |
| MEMORY | 无 | 声明的字节范围耗尽 | 处理路线可从头重建并解码；透传路线仍不可 seek | 缓冲区由调用方借用，到 operation release 前必须保持不变；stop 结束本轮但不释放它 |
| FILE | `CONFIG_QM_AUDIO_SOURCE_FILE_SUPPORT` | 文件读取返回零字节 | 本地文件处理路线可重新打开并重放；原生 byte/packet 透传不可 seek | 路径会复制；文件句柄由 Pipeline 持有并在清理时关闭，关闭失败需重试 |
| URI | `CONFIG_QM_AUDIO_SOURCE_URI_SUPPORT` | HTTP 读取返回零 | 处理路线要求有限 Content-Length 和强 ETag，然后重新下载并校验前缀；原生 byte/packet 透传不可 seek | URL 会复制；HTTP context 在清理成功前保持有效；超时不能直接释放它 |
| PUSH | 无；Control 创建有界 ring | 只有 `qm_audio_control_stream_finish()` 表示正常 EOS | 不可 seek；pause 可以保留缓冲状态，但不等于支持 restart/seek | ring 由 operation 持有并在 release 时销毁；producer 的写入缓冲区仅在单次调用期间借用，写入遵守背压/取消 |
| CUSTOM | 无；必须提供 open/read/cancel/close 回调 | 回调成功返回且 `read_size == 0` | 不可 seek，因为合同没有 seek 回调 | open(ctx, timeout_ms)、read、close 串行执行；cancel 可并发调用并锁存取消、唤醒 I/O；context 借用到 release |

stop、取消和读取失败都会关闭当前 source/codec generation，不会 flush codec tail；只有自然 EOF 才进入 decoder flush、encoder finish 和 Engine drain。

## 4. 路由规划与受限转码

Pipeline 的文件内 build_route() 是唯一的播放路由规划入口。Control 在 Service 准入前创建 Pipeline；此时只校验 source 声明、固定 sink profile 和已编译 provider 能力，不打开 source，也不创建 codec 实例。Service 准入成功后，Pipeline prepare 才分配 packet 缓冲和 encoder context。

~~~mermaid
flowchart TB
    Source["Source codec / framing / packet 合同"]
    Output["固定 Engine output profile"]
    Capability["已编译 decoder / encoder capability"]
    Planner["build_route()"]
    DirectPcm["PCM 直接交付"]
    BytePass["编码字节流透传"]
    PacketPass["原生 packet 透传"]
    DecodePcm["解码到 PCM"]
    PcmEncode["PCM 编码"]
    Transcode["解码到 PCM 后编码"]
    Engine["Engine"]

    Source --> Planner
    Output --> Planner
    Capability --> Planner
    Planner --> DirectPcm --> Engine
    Planner --> BytePass --> Engine
    Planner --> PacketPass --> Engine
    Planner --> DecodePcm --> Engine
    Planner --> PcmEncode --> Engine
    Planner --> Transcode --> Engine
~~~

| 路线 | 选择条件 | provider 要求 |
| --- | --- | --- |
| PCM 直接交付 | source 与输出均为匹配 PCM | 无 |
| 编码字节流透传 | codec、framing、PCM 声明一致，且 packet 元数据字段为零 | 无 |
| 原生 packet 透传 | codec、PCM 几何、packet_duration_ms、packet 长度上限和边界合同兼容 | 无 |
| 解码到 PCM | 输出为 PCM，decoder 接受输入 codec/framing 和目标 PCM | decoder |
| PCM 编码 | 输出为 packet，encoder 接受目标 profile | encoder |
| 解码后编码 | decoder 与 encoder 都接受同一 PCM 几何 | decoder + encoder |

转换的中间 PCM 必须逐项保持 sample_rate_hz、channels 和 sample 不变。任一 provider 不支持、媒体几何不一致、容器/framing 变化未经 adapter 明确支持，或 packet 需要重新划分边界时，创建返回能力错误而不是隐式转换。

相同 codec 也不必然能透传。例如 MP3 字节流到 MP3 设备只有在 codec、framing、几何和表示合同相同时才是字节透传；MP3 字节流不会自动分帧为 packet。MP3 到 Opus 只有在 MP3 decoder、目标 PCM 和 Opus encoder 都支持该固定 profile 时才能成立。

受限能力还包括：

- 原生 byte/packet passthrough 不支持 seek/rewind。
- 处理路线的 rewind 会关闭 source/decoder/encoder，上一个 generation 完全退出后才允许新 generation 使用状态。
- Pipeline 取消不 flush codec tail；自然 EOF 才走 decoder flush、encoder finish 和 Service/Engine drain。

## 5. 会话、并发、事件与进度

Service 的成功受理不等于已出声。STARTED 表示媒体运行已开始；FINISHED 只在 Pipeline 输入结束且 Engine drain 确认后上报；CANCELLED、PREEMPTED 和 ERROR 是终态，终态操作仍需显式 release。

### 5.1 操作状态与命令合法性

下图使用公开的 QM_AUDIO_CONTROL_* 状态名。NEW 是内部的分配状态；qm_audio_control_play() 只有在请求进入准入路径后才向调用方暴露 operation。

~~~mermaid
stateDiagram-v2
    [*] --> WAITING: play 已接受
    WAITING --> PLAYING: Service 准入并 STARTED
    WAITING --> CANCELLED: stop 或准入超时
    WAITING --> ERROR: 准入失败
    PLAYING --> PAUSED: pause 已确认
    PAUSED --> PLAYING: resume 后重新准入
    PLAYING --> DRAINING: Pipeline 自然 EOF
    DRAINING --> FINISHED: Engine drain 已确认
    PLAYING --> CANCELLED: stop
    PAUSED --> CANCELLED: stop
    DRAINING --> CANCELLED: cancel
    PLAYING --> PREEMPTED: Service 抢占
    PAUSED --> PREEMPTED: Service 抢占
    PLAYING --> ERROR: source、provider 或 device 失败
    PAUSED --> ERROR: resume 失败
    DRAINING --> ERROR: drain 失败
    FINISHED --> [*]: release
    CANCELLED --> [*]: release
    PREEMPTED --> [*]: release
    ERROR --> [*]: release
~~~

| API | 必要状态或前置条件 | 成功结果 |
| --- | --- | --- |
| qm_audio_control_play() | Control 有效，source/profile 声明合法 | 返回 operation ID；后续准入决定 WAITING 或 PLAYING |
| qm_audio_control_stream_write() | 有效的 PUSH operation，且符合字节流或 PCM 合同 | 在有界背压条件下复制已接收字节 |
| qm_audio_control_stream_write_packet() | 有效的 PUSH + PACKET operation，且原生 profile 匹配 | 原子复制一个完整 packet；错误时不保留部分 packet |
| qm_audio_control_stream_finish() | 最后一个媒体单元后的有效 PUSH operation | 标记输入 EOF；仍需等待 Pipeline 与 device drain 才 FINISHED |
| pause/resume/seek/stop | operation 有效，且 route/source 支持所请求动作 | 后续产生状态/事件结果；不支持的保留暂停或 seek 返回 -QM_ENOSYS |
| wait()/get_snapshot() | 已知 operation ID | 只观察状态，不改变所有权 |
| release() | 仅终态 operation | 静止资源并使 operation ID 失效 |

### 5.2 准入与调度

| 请求策略 | Service 决策 |
| --- | --- |
| QUEUE | 路由空闲时立即启动，否则进入有界队列。准入超时为零时忙碌即拒绝排队，QM_WAIT_FOREVER 永不过期，有限截止时间到期后进入 CANCELLED。 |
| REJECT_IF_BUSY | 其他 operation 占用输出路由时立即拒绝。 |
| PREEMPT_LOWER | 仅当新 operation 的 Service 优先级高于活动 operation 时请求抢占。 |
| PREEMPT_CURRENT | 不比较相对优先级而请求抢占，但仍以安全释放活动路由为前提。 |
| 多个排队 operation | 先选择最高优先级；相同优先级保持先入先出顺序。已过期请求会在后续准入前被处理。 |

抢占时绝不在旧输出持有者确认停止前写入新媒体。Service priority 是会话准入策略，不是 RTOS task priority。安全停止抢占失败时，Service 会向两个受影响 operation 报告 ERROR；各自仍由调用方持有，直到终态清理成功。

抢占依赖绑定到具体的关闭动作；旧动作的失败只影响依赖该动作的请求。错误通知
期间新提交的抢占由新动作处理，不继承上一轮关闭失败。

Service 的同步 API 与运行期异步路径共用一套“动作登记 → 非阻塞推进 → 状态提交”
机制。`audio_event` 在运行期推进它；外部同步调用在调用方上下文等待会话独立的
Engine progress 通知，再重查状态并继续推进，因此 event 退出后仍可完成同步清理。该通知
只表示需要重查，不代替完成结果；该机制不新增任务或动作队列。

动作持有 session 的内部生命周期引用（action pin），直到状态提交及对应 notice
交付结束才释放。`release_pending` 拒绝新的外部动作，但允许私有 STOP 清理继续。
等待超时保留 session、未完成动作及其必要引用，调用方须保留句柄重试；终态不代表
notice 已交付或存储可以释放。

Service 公共同步 API 保留。同步 `qm_audio_service_session_finish()` 仍先确认物理
drain，再推进 FINISH；Control 路径保持 Source worker drain 后提交 FINISH。
已受理 resume 的执行失败在同步与异步路径中都提交 ERROR 并发布错误通知，同步
调用同时返回执行错误。

### 5.3 正常结束时序

~~~mermaid
sequenceDiagram
    participant App as Application
    participant Control
    participant Pipeline
    participant Service
    participant Engine
    participant Adapter as Device adapter

    App->>Control: play(request)
    Control->>Pipeline: 校验 source 并 build_route()
    Control->>Service: 提交 session
    Service-->>Control: ACCEPTED
    Service->>Pipeline: 授予输出所有权
    Pipeline->>Engine: PCM 或原生媒体
    Engine->>Adapter: render/write
    Pipeline-->>Control: 持久记录自然 EOF
    Pipeline->>Engine: drain（Source worker 等待）
    Engine-->>Pipeline: drain 已确认
    Pipeline-->>Control: 持久记录输出完成
    Control->>Service: 登记并推进 FINISH（drain 已确认）
    Service->>Engine: stop / close，释放输出授权
    Service-->>Control: FINISHED
    Control-->>App: event、snapshot 或 wait 结果
~~~

一个 Control 有独立操作表和事件订阅，多个 Control 共享 Service。Service 只在旧输出已确认停止后才将抢占会话写入路由。Pipeline 和 callback 通过 operation/generation 边界隔离迟到事件，旧运行的终态不得污染新运行。

保留暂停与取消语义不同：

- pause 仅在设备/Source 能保留上下文时成功；不支持时返回 -QM_ENOSYS，不能假装成 stop/replay。
- QM_AUDIO_PAUSE_YIELD_OUTPUT 释放输出授权并丢弃已经提交的媒体；可 seek 的处理型 source 可在恢复时重建，实时不可 seek source 只能继续向前。
- stop/cancel 中断输入和待写媒体；device adapter 可以有有限尾音或 drain 完成路径，除非 adapter 明确保证，否则调用方不能假定会瞬时静音。

进度和时长必须先检查 qm_audio_control_snapshot_t 的 flags。PCM 及已知 packet 时长可累计媒体时间；opaque 字节流或未知 packet 时长不能伪造位置。设备没有位置能力时，位置查询返回 -QM_ENOSYS。

### 5.4 失败、超时与清理规则

| 情形 | 框架事实 | 业务必须处理 |
| --- | --- | --- |
| source、profile、route 或操作不支持 | 调用显式失败；qm_audio 不静默转换或模拟 | 选择已声明的兼容路线，或向上报告能力错误 |
| 准入截止时间到期 | 排队 operation 以超时结果进入 CANCELLED | release 终态 operation；不要与调用方 wait 超时混淆 |
| wait() 到期 | 只结束本次等待，不取消也不 release operation | 查询 snapshot 或再次等待；是否取消由业务决定 |
| I/O、drain、unsubscribe、shutdown 或 release 超时 | 超时不证明 callback、DMA 或后端访问已经停止 | 保留句柄及其借用 context，按文档重试清理操作 |
| 迟到的终态事件 | generation 过滤阻止旧运行改变新运行 | 用 operation ID 关联业务工作；callback 不表示所有权转移 |

对 PUSH 而言，finish 是唯一的正常输入结束信号。空 packet、空写入或暂时的 source 饥饿都不是 EOF。事件 callback 和 Capture callback 的 payload 都是借用数据；转交给其他任务前先复制，且不要在 callback 中阻塞生命周期操作。

### 5.5 callback 与线程限制

| 上下文 | 可以做的短操作 | 不得同步执行 |
| --- | --- | --- |
| Control event callback | 复制事件字段、记录 operation ID、调度业务工作；PUSH 写入使用零超时 | play、pause、resume、seek、stop、wait、release、shutdown、destroy、unsubscribe 或其他阻塞生命周期操作 |
| Capture CALLBACK 交付 | 复制媒体并调度传输工作 | recv/wait、unsubscribe、shutdown、destroy 或阻塞 I/O |
| CUSTOM source callback | 在共享 Source worker 上完成有界 open/read/close | 超过文档所有权期限保留调用方数据，或无限期等待 |
| device adapter I/O | 在 task context 中执行有界 I/O 并支持 abort | 在 ISR 中运行，或获取被 abort 所要唤醒的阻塞 I/O 持有的锁 |

### 5.6 事件交付与背压

命令、生命周期事实和 Control 状态通知通过实例 event 队列进入
`audio_event`。队列容量由 `CONFIG_QM_AUDIO_CONTROL_EVENT_QUEUE_LEN` 配置，默认是 16。生产者只做一次非阻塞投递，不预留空位，也不在队列满时重试。已接受的同步命令由调用方等待完成，命令存储不能提前释放。事件队列只承载控制事件，媒体字节仍保留在 source、render 或 Capture 的有界环形队列中，不复制进事件队列。

统一事件边界只覆盖控制面：

| 使用实例 event 队列 | 保留在所属 worker、环形队列或等待原语中 |
| --- | --- |
| 命令、生命周期事实、Control 状态快照和 EXIT 屏障 | 设备读写、codec 执行、媒体 payload、独立 Capture 交付、必须中断阻塞 I/O 的取消动作，以及 callback/I/O 静止等待 |

Capture 的待处理状态、回调借用计数与信号量唤醒归其私有分发任务及退订屏障管理，
不进入实例 event 队列。

Pipeline 将状态事实保存在 operation 的固定槽位中，每种事件类型一个槽位，按本轮序号消费。重复的非终态事实可以合并，EOF、输出完成和失败分别保留；旧 generation 的事实被拒绝。队列项只负责唤醒，队列满时已有消息保证下一轮扫描，不丢终态，也不阻塞 Source worker。

Service 在事件任务内产生的事实暂存于有界 FIFO，在命令结束前按序投影；外部清理产生的 Service 事实使用同一入口。公开 Control 生命周期 callback 在事件任务上执行。非事件任务提交的公开通知使用实例队列，关闭阶段先停止新投递，再等待已接纳的投递完成，最后消费 EXIT 屏障。

Capture 状态通知使用私有分发任务：中间非终态允许合并，终态 STOPPED 或 ERROR 保留可查。慢 Capture 回调会延迟其他 Capture 回调，Control 事件分发保持独立；所有应用回调都应及时返回。

### 5.7 时间域

| 值 | 来源与有效性 | 规则 |
| --- | --- | --- |
| qm_audio_event_t.timestamp_ms | 音频单调事件时钟 | 用于关联事件顺序，不是媒体 PTS |
| `qm_audio_data_t` timestamp_ms/duration_ms | 只有对应 `QM_AUDIO_DATA_TIMESTAMP_VALID` 或 `QM_AUDIO_DATA_DURATION_VALID` 标志置位时才有效；PCM 和字节流通常置零 | 保留 producer 语义；采集时间戳使用后端 Capture clock，不是 render presentation time |
| qm_audio_control_snapshot_t position/duration | 只有对应 progress flag 置位时有效；position 在 DAC 前观测且可能为 estimated | 不从零值推导 UI 位置，也不假定已经出声 |

不能从数值零推断时间有效性。跨时间域同步、capture/render 时钟对齐和端到端时延需要当前框架之外明确的产品合同。

## 6. Capture 合同

Capture 是独立组件，只使用公开媒体类型、QUMI 工具和 `qm_audio_device_capture_*`。
调用方传入已打开设备、原生输入格式与缓冲上限。Capture 不打开/关闭共享设备，也不
访问 Engine、Service、Pipeline 或 `audio_event`。Engine 只持有不透明 Capture 句柄，
关闭设备前先通过公开接口关闭 Capture。

首个启用订阅在调用方上下文同步启动物理采集，subscribe/enable 返回真实启动结果。
PULL 启动成功后按需创建唯一的 `audio_capture` 读取任务；读取结束后由它执行设备 stop，
在锁内清除 active 并通知等待者，随后不再访问 Capture。PUSH 使用
设备生产者任务和单例 `qm_audio_capture_feed(input, result)` 入口。PUSH 构建中一个
Capture 从 create 到 destroy 保留该入口。两种模式都将原生数据复制到各订阅者独立的
有界队列；`audio_cap_cb` 在锁外轮转选择就绪订阅，串行执行状态与媒体回调，
QUEUE 消费者使用自己的任务。create 不创建读取或回调任务。

subscribe、enable、shutdown、unsubscribe 通过现有数据锁保护的生命周期占用标志
串行化；发生冲突时返回 `-QM_EBUSY`。设备调用和静止等待在数据锁外进行。

PUSH 生产须在 Capture 创建成功后由设备 `capture_start` 启动；启动失败也须执行 stop。
设备 `capture_stop` 成功前须等待全部在途 feed 返回，并保证旧生产者不再投递；
feed 不得与 create/destroy
并发。这样入口指针在投递期间保持稳定，实例数据和任务退出使用 QMOS 互斥锁、信号量
同步，不需要编译器原子操作或常驻全局锁。

Capture 将栈上 metadata 和借用的源 payload 作为两个 segment，复制成环形队列中的
一条记录；入队无需临时分配 payload 或准备拼接缓冲，两个输入区间须保持有效到调用
返回。QUEUE 接收在数据锁内使用临时缓冲，分发任务使用独立缓冲，保证回调返回前数据
保持有效。每个订阅仅持有自己的有界媒体队列。

不需要新增 CaptureRoute。PCM→Opus 使用方从 PCM QUEUE 订阅读取数据，并自行管理
`qm_audio_encoder_open/push/finish/close`；仅源格式与编码器要求不一致时才需要重采样。
Capture 始终不执行格式转换。

默认实例的使用方调用
`qm_audio_capture_subscribe(qm_audio_get_capture(), &config, &subscription)`。
返回的 Capture 句柄为借用句柄，默认实例 init/deinit 必须与查询和句柄访问串行。
独立使用方先打开设备，再调用 `qm_audio_capture_create(&open_config, &capture)`。

PCM 和编码 packet 共用底层读取接口，保持各自的媒体边界：

| 采集格式 | adapter 读取接口 | 上层交付 |
| --- | --- | --- |
| PCM | qm_audio_device_capture_read() | 完整 sample frame 对齐的 PCM block |
| 原生编码 | `qm_audio_device_capture_read()`（固定编码 input profile） | 一个携带完整编码包的 `qm_audio_data_t` |

QUEUE 订阅统一使用
`qm_audio_capture_recv(subscription, frame, timeout_ms)` 接收这两类数据。调用前
`frame->data` 指向调用方可写缓冲区，`frame->size` 表示缓冲区容量；成功后
`frame->size` 改为实际负载长度，元数据字段由 Capture 填写。数据按 Capture 的输入
格式解释；容量不足时保留队头记录，编码包不拆分。

在分配调用方缓冲区前，可使用
`qm_audio_capture_peek_size(subscription, &size)` 非阻塞地查看当前队头的实际负载字节数。
队列为空时返回 `-QM_EEMPTY`。该接口不消费也不预留队头；它必须与后续 `recv()` 串行
使用，若队列在两次调用之间覆盖了队头，仍须处理 `recv()` 的容量错误。`size` 不包含
Capture 私有元数据。

入队、丢弃旧记录、peek、recv 和 callback 发现队列损坏时，仅清空并终止受影响的订阅，状态为
`ERROR(-QM_EIO)`。调用方缓冲区容量不足仍返回 `-QM_EINVAL` 并保留队头。
`data_ready` 只提示队列由空转为非空或状态变化，不累计媒体记录数。

Capture 不支持编码 byte-stream 输入。发生 STOPPED 或 ERROR 后，订阅处于终态；业务应 unsubscribe 并重新创建，而不是只调用 enable。回调数据和 packet payload 都是借用数据，异步传输必须在回调返回前复制。

Capture 状态 callback 与 PCM/packet callback 共用私有 `audio_cap_cb` 上下文，不会彼此并发，且可能在 capture_subscribe() 返回前执行。订阅前先初始化并保留 callback context，较长工作转移到应用任务。

采集源故障使 RUNNING 和 DISABLED 订阅都进入 ERROR；新建启用订阅可以重启采集，
已有终态订阅保留首次错误，shutdown 也不覆盖它。Capture 回调
允许查询和 flush，不得订阅、enable、recv 或调用生命周期接口。shutdown 和
unsubscribe 的各阶段等待共享同一截止时间；同步 device stop 另受适配器约定的
有限停止时限约束。停止失败保留句柄、设备和待清理标志供重试。
shutdown 等待在途回调退出，但不保证交付尚未执行的终态回调；终态快照保留到退订。
它会唤醒阻塞接收者，但不等待 `recv()` 返回；unsubscribe 才在释放订阅存储前等待该
订阅的接收者与回调结束。

### 6.1 Capture 订阅控制

| 操作 | 作用范围与效果 | 调用方约束 |
| --- | --- | --- |
| `capture_enable(false)` | 只禁用当前 subscription，清空自己的待交付队列，并用 `-QM_EBUSY` 唤醒阻塞的 `recv()`；不停止物理读取器，也不影响其他订阅者。 | 到 unsubscribe 成功前保持 callback context；禁用不是 callback 静止屏障。 |
| `capture_enable(true)` | 启用交付，重复调用保留排队数据；需要启动源时同步返回启动结果。STOPPED 和 ERROR 在 unsubscribe 前是终态。 | 检查返回错误和快照；终态订阅清理后重建。 |
| `capture_flush()` | 只清空当前 subscription 的排队块；物理采集和正在执行的 callback 不受影响，sequence 不重新编号。 | 用于丢弃积压，不用于重置物理 Capture 时钟或丢失统计。 |
| `capture_peek_size()` | 非阻塞地返回当前订阅队头的实际 payload 字节数，不消费记录；队列为空返回 `-QM_EEMPTY`。 | 与后续 `recv()` 串行调用；该查询不预留队头，仍须处理队列覆盖。 |
| `capture_recv()` | 将一个完整的原生 PCM 块或编码包复制到 `frame->data`；调用前 `frame->size` 是容量，成功后是实际长度。一个 QUEUE subscription 同时只能有一个 `recv()` 消费者；容量不足时不消费队头。CALLBACK 模式不能调用 `recv()`。 | 串行化 receive，并在调用期间保持目标缓冲区有效。 |
| `capture_unsubscribe()` | 停止当前交付、唤醒等待者，并等待 receiver/callback 静止；最后一个订阅者还会停止物理读取器。 | 不得从 Capture 读取或分发任务调用；超时后用同一句柄重试。 |

### 6.2 Capture 生命周期

~~~mermaid
sequenceDiagram
    participant App as Application
    participant Capture
    participant Adapter as Device adapter
    participant Queue as Subscription queue
    participant Consumer as QUEUE/CALLBACK consumer

    App->>Capture: create(已打开设备, 原生格式)
    App->>Capture: subscribe(capture, config)
    Capture->>Adapter: 首个启用订阅同步启动采集
    Adapter-->>Capture: 启动结果
    Capture-->>App: 发布订阅或返回启动错误
    Adapter-->>Capture: PULL read 或 PUSH feed
    Capture->>Queue: 复制原生块或完整编码包
    Queue-->>Consumer: recv() 或 callback
    App->>Capture: unsubscribe(subscription)
    Capture->>Queue: 停止交付，等待接收者与回调
    Capture->>Adapter: 最后退订时 abort；PULL reader stop 后退出，PUSH stop 等待 feed
    Capture-->>App: 订阅已释放
    App->>Capture: shutdown(capture), destroy(capture)
    App->>Adapter: shutdown 成功后关闭设备
~~~

### 6.3 容量与背压预算

qm_audio 使用有界资源；产品必须根据内存、时延和并发预算选择数值，不能依赖默认值。

| 预算对象 | 主要配置 | 产品决策与验收条件 |
| --- | --- | --- |
| 保留的 Service session | CONFIG_QM_AUDIO_SERVICE_MAX_SESSIONS | 排队、活动、暂停及终态未 release operation 的总上限 |
| Source 执行 | 固定一个 worker | Pipeline 注册表容量由 session 上限决定，暂停保留上下文不增加任务 |
| 单 Control operation | CONFIG_QM_AUDIO_CONTROL_MAX_OPERATIONS | 在显式 release 终态 operation 前允许的业务操作上限 |
| PUSH 输入 | CONFIG_QM_AUDIO_CONTROL_PUSH_BUFFER_BYTES 与写入超时 | producer 在有界背压超时或中断时的行为 |
| render 队列 | CONFIG_QM_AUDIO_ENGINE_RENDER_QUEUE_LEN、CONFIG_QM_AUDIO_ENGINE_RENDER_BLOCK_BYTES 与 render 写入超时 | PCM/字节流按上限拆块；编码 packet 保持完整，由 profile 的 max_packet_bytes 限定 |
| Capture 扇出 | CONFIG_QM_AUDIO_CAPTURE_QUEUE_LEN 与 CONFIG_QM_AUDIO_CAPTURE_MAX_SUBSCRIBERS | 每订阅者时延；队列满时丢弃最旧项，sequence 缺口暴露丢失 |
| 事件交付 | CONFIG_QM_AUDIO_CONTROL_EVENT_QUEUE_LEN 与 QM_AUDIO_EVENT_PENDING_MAX | callback 返回时间预算，以及负载测试证明业务 callback 不会延迟命令、准入或事件投影 |
| 可选 cloud adapter | CONFIG_QM_AUDIO_CLOUD_QUEUE_LEN 与 CONFIG_QM_AUDIO_CLOUD_MAX_OPERATIONS | 仅在 adapter 初始化时计入 cloud ingress 队列、`audio_cloud` 栈和 operation 映射容量 |

所有超时、队列长度、任务优先级和栈大小选择都应有集成侧依据和负载测试。慢 Capture 订阅者不能阻塞物理采集；慢 PUSH producer 不能假定存在无限队列。
第 2.1 节的任务数量及每个任务的栈大小必须一并纳入内存预算；保留的 Service
session 本身不会新增 worker 任务。

一个保守的规划估算（不包含 allocator 和 adapter 私有开销）为：

~~~text
Capture 记录负载          = capture_frame_bytes + 4 * sizeof(uint32_t)
单个 Capture subscription ~= queue_len * (QM_RING_QUEUE_RECORD_HEADER_BYTES + Capture 记录负载)
                             + 订阅/同步对象
每个 Capture             += QUEUE 接收临时记录缓冲 + 按需回调缓冲 + Capture/同步对象
实例总预算               += PUSH ring + render/event queue + source worker
                             + Capture 分发任务与活跃 PULL reader + decoder/encoder 私有状态
                             + 应用复制数据
~~~

render 媒体存储上界为 `(queue_len + 1) × (item_header + payload_limit)`，额外一项是 writer 当前持有的块。PCM/字节流使用 `RENDER_BLOCK_BYTES`，完整 packet 使用 `max_packet_bytes`。还需计入每个保留 operation 的事实槽位、输出句柄及各 task stack。停止后的旧输出句柄保留引用，但不再保留已清空的媒体队列。

目标内存预算应结合平台 allocator、stack watermark 和最大并发订阅数计算。上述公式为规划下界，不定义分配 ABI。

## 7. 平台 adapter 合同

本节定义 qm_audio 与 adapter 的接口合同。芯片、音频 IC、模块固件、传输总线和板级 profile 由平台 adapter 文档及其测试定义。

### 7.1 device adapter

device adapter 必须：

- open() 成功后，通过 qm_audio_device_get_caps() 只发布自己能够服务的固定输入/输出 profile。Engine 将其与初始化配置比较；不匹配时初始化失败并走 close/重试清理，而不是改变路由选择。
- 显式区分 playback 与 capture 方向；两个方向同时启用本身不保证 duplex 行为。
- 统一通过 playback/capture 设备接口接收 `qm_audio_data_t`；固定 profile 决定其为 PCM block 或完整编码 packet。校验声明 profile，并显式拒绝不支持的媒体。
- 按实际能力声明或拒绝 playback、capture、drain、pause/resume、音量、静音和位置操作。
- 保留设备特有的错误与尾音/drain 行为，不得把编码数据重新解释为 PCM。

### 7.2 codec provider

decoder/encoder provider 是可选的平台实现。Pipeline 只在其声明的输入/输出组合能够保持固定 PCM 几何时选用：

- decoder 接受已声明的 codec/framing，并产出协商的 PCM profile。
- encoder 接受完全相同的 PCM profile，并产出配置的编码 packet profile。
- provider 可以拒绝所有不支持的 codec、framing、采样几何、packet 时长或 packet 长度。qm_audio 不提供后备重采样、声道转换、packet 重分包、parser 或容器处理。

核心对每条流同步调用 encoder。decoder 可以在 provider 自有 worker 中产生输出，但该
worker、DMA 或 SDK callback 不计入核心任务数；provider 必须保证 stop/close 前上下文
有效并明确静止屏障。

### 7.3 平台专属文档与验证

每个平台 adapter 自己维护 profile 矩阵、Kconfig 选择、外部固件依赖、总线/引脚要求、目标构建方法和实板证据。框架的 host 测试只验证通用合同，不证明某一个 adapter 的目标媒体链路。

### 7.4 adapter 准入检查表

| 范围 | adapter 必须提供的证据 |
| --- | --- |
| 链接边界 | 每个已启用的命名 adapter 接口只有一个实现被链接；公共 adapter 头文件不穿透厂商 SDK 类型 |
| open 与 capability | open() 复制后续所需配置，get_caps() 报告实际固定 profile 和标志，Engine 拒绝不匹配配置 |
| Playback 与 Capture | 每个启用方向只接收已声明的媒体表示，并保持调用方 buffer 所有权规则 |
| 取消与 drain | abort/stop 能安全唤醒有界 I/O；只有后端访问与 callback 静止后才允许 close/free |
| codec provider | check_format() 在分配前拒绝不支持组合；open/push/finish/close 保持固定 PCM 几何 |
| 负向测试 | 覆盖不支持 profile、错误 packet、队列压力、I/O 超时、取消和清理重试 |

### 7.5 adapter 调用时序

| 路径 | 必要顺序 |
| --- | --- |
| playback 正常结束 | open -> get_caps -> playback_start -> playback_write -> playback_drain -> playback_stop -> close |
| playback 取消 | open -> get_caps -> playback_start -> playback_write -> playback_abort -> playback_stop -> close |
| Capture | open -> get_caps -> capture_start -> capture_read -> capture_abort -> capture_stop -> close |
| codec provider | 正常 EOF：check_format -> open -> push* -> finish -> close；取消：check_format -> open -> push* -> close |

get_caps() 只有在 open() 后有效。render worker 串行执行后端
start/write/pause/resume/stop、位置查询和共享输出设置。drain 在调用任务中独占
playback 访问；abort 从 stop 调用任务触发，唤醒对应的阻塞 write/drain，不得
等待该 I/O 持有的锁。capture 与 playback 只有在 adapter 明确支持该组合时才可
并发。只有所有后端访问和 callback 都静止后，close 才能成功。

## 8. 应用与协议适配边界

应用或协议 adapter 从已初始化的 `qm_audio` 实例读取 native stream profile，再借用业务创建的 Control，将下行音频写入一个 PUSH operation。它不创建 Engine、Service 或物理设备。编码格式不能假定为 PCM：编码字节流、完整 packet 与 PCM 走不同的写入接口。

可选协议 adapter 解析外部命令，维护有界的外部 request ID 到 operation ID 映射，并调用 Control；网络发送在 adapter 自己的执行上下文完成，不能在音频锁、设备回调或 PCM/packet callback 中执行。协议、鉴权、资源目录和业务重试不属于四个核心层。

### 8.1 可选语音前端边界

语音前端是由应用管理的平台边界，不是 qm_audio 自动启动的层。应用显式调用
`qm_audio_frontend_open()`、`qm_audio_frontend_close()`，并通过
`qm_audio_frontend_set_mode()` 只切换唤醒词检测模式。

处理后的麦克风数据不经过前端控制 API。平台实现可以通过
`qm_audio_capture_feed()` 注入 AEC 处理后的 PCM，应用通过独立的
Capture subscription 接收。PUSH feed 不创建 reader 任务；是否上传由
Capture 消费者决定。

前端检测到唤醒词后，生产者复制元数据并调用
`qm_audio_report_wake(&wake)`。qm_audio 会复制元数据，并在 `audio_event` 上投影
`QM_AUDIO_EVENT_WAKE`，应用通过 `qm_audio_wake_subscribe()` 接收；不得从事件任务调用
report 接口。前端自己的 worker、
DMA 和厂商 callback 不计入核心任务数，关闭前必须先达到静止。应先停止前端 feed，
再移除依赖它的 Capture subscription；如果实现有要求，关闭前先切回 wakeword 模式。

### 8.2 可选 Cloud Adapter

`CONFIG_QM_AUDIO_CLOUD_SUPPORT` 只把模块编入固件，不会自动初始化。Engine、Service、
Control 和底层 cloud handle 准备好后，产品调用 `qm_audio_cloud_init()`。该 adapter
借用传入的 Control，拥有一个 `audio_cloud` 任务、有界命令队列和 cloud-created operation
映射。

协议接收 callback 只复制外部 payload 并入队；命令解析、鉴权、资源解析、Control 调用、
状态投影和终态 operation 清理都在 `audio_cloud` 中执行。resolver 字符串只在 callback
期间借用，产品若需保留必须复制。外部 message ID 只用于回复关联，不能当作 Control
operation ID。resolver 提供的 release callback 在相关 operation 静止后由 `audio_cloud`
执行且只执行一次；不能重新进入该 operation 的生命周期 API。

关闭顺序固定为：停止/注销 cloud ingress，调用
`qm_audio_cloud_shutdown(timeout_ms)`；超时后用同一句柄重试；成功后再调用
`qm_audio_cloud_destroy()`。cloud shutdown 只释放 cloud 所有的 operation，不会关闭借用的
Control，因此必须在 Control shutdown 和 `qm_audio_deinit()` 之前完成。

## 9. 配置、构建与验证

主要 Kconfig 开关如下：

| 开关 | 含义 |
| --- | --- |
| CONFIG_QM_AUDIO_SUPPORT | Control、Service、Pipeline、Engine 基础组件 |
| CONFIG_QM_AUDIO_DECODER_SUPPORT | 平台 decoder provider |
| CONFIG_QM_AUDIO_ENCODER_SUPPORT | PCM 到编码 packet 的 encoder provider |
| CONFIG_QM_AUDIO_SOURCE_FILE_SUPPORT | 内置 FILE source |
| CONFIG_QM_AUDIO_SOURCE_URI_SUPPORT | 内置 HTTP/HTTPS URI source |
| CONFIG_QM_AUDIO_CAPTURE_SUPPORT | 独立原生 Capture 与订阅 |
| CONFIG_QM_AUDIO_ENGINE_RENDER_BLOCK_BYTES | PCM/字节流队列项的最大 payload，超出时按采样帧对齐接收前缀 |
| CONFIG_QM_AUDIO_CLOUD_SUPPORT | 可选 cloud adapter 模块；仍需显式调用 `qm_audio_cloud_init()` |

当前 `components/qm_audio/tests/CMakeLists.txt` 注册了 `test_capture_push`、
`test_capture_pull` 和 `test_engine`。Engine 测试也直接链接 Service，覆盖所有权、
动作转换、清理重试、并发 release 时的 shutdown 和抢占依赖的动作身份。
`components/qm_agent/tests` 中的 `test_agent_audio_integration` 链接真实的 Control、
Service、Pipeline、Engine 和 Capture 实现，覆盖默认实例生命周期、PCM PUSH 播放、
pause/yield/resume、多 Control 抢占和 Agent 终态清理。其他 Source/profile/provider
组合和可选 cloud 运行时集成仍需补齐验收覆盖。平台 adapter 自己维护独立的
contract 与实板测试。

在 `qumios-dev` 仓库根目录运行这些宿主测试：

~~~sh
cmake -S components/qm_audio/tests -B /private/tmp/qm-audio-host-tests
cmake --build /private/tmp/qm-audio-host-tests
ctest --test-dir /private/tmp/qm-audio-host-tests --output-on-failure

cmake -S components/qm_agent/tests -B /private/tmp/qm-agent-host-tests
cmake --build /private/tmp/qm-agent-host-tests
ctest --test-dir /private/tmp/qm-agent-host-tests --output-on-failure
~~~

### 9.1 验收矩阵

| 范围 | 必须提供的证据 | 最小用例 |
| --- | --- | --- |
| 框架 | 宿主 unit/contract 测试 | 生命周期、路由选择、packet 校验、抢占、取消、drain 和 Capture 隔离 |
| device/codec adapter | adapter 局部 contract 测试 | profile 不匹配、不支持操作、有界 I/O、abort、drain、close 重试和 provider 格式拒绝 |
| 产品集成 | 目标固件构建加配置后的 route 矩阵 | 每个启用 source/profile/priority 策略，以及所有预期 PUSH 结束路径 |
| 硬件 | 板级测试记录 | 初始化、真实媒体 I/O、采集/播放交互、打断时延、drain 和设备错误后的恢复 |
| 负载与故障 | 可复现的压力/故障报告 | 队列压力、慢订阅者、慢 producer、source 超时、callback 延迟和重复 start/stop |

### 9.2 需求到证据追溯

| 不变量 | 当前宿主证据 | 产品发布前仍需补齐 |
| --- | --- | --- |
| Capture 队列隔离与生命周期 | `test_capture_push`、`test_capture_pull` | 目标 Capture 数据、丢失行为和 callback 负载 |
| Engine 所有权、worker 串行执行、stop/drain 与清理重试 | `test_engine` | 平台中断时限和真实 device drain/stop 行为 |
| PCM 前缀元数据与原子 packet 提交 | `test_engine` | 每个启用的 device profile 与媒体路径 |
| Service 持有的 open、动作执行与 session 清理 | `test_engine`：启动失败、清理重试、并发 release/shutdown 与 notice 回调期间的新抢占 | 负载下的调度与目标中断时限 |
| 默认实例及 Control operation 生命周期、事件、PUSH 与终态 release | `test_agent_audio_integration`：PCM PUSH、pause/yield/resume、多 Control 仲裁与 Agent 清理 | 其他 source/framing 组合、目标时序与 device 行为 |
| Pipeline 路由、provider 拒绝、Source 取消与旧输出拒绝 | `test_agent_audio_integration` 覆盖 PCM/PUSH 集成，其他路线用例待补 | 启用的 profile/provider 组合、open/read 中断、session 容量与内存压力 |
| callback 阻塞时保留完成状态，release 前接续下一请求 | `test_engine` 的部分故障/notice 用例及 `test_agent_audio_integration` 的 observer 阻塞时清理 | 更多超时路径、目标 callback 负载与反复抢占 |
| 可选 cloud 与协议集成 | 宿主验收测试待补 | adapter 顺序及真实协议、网络和鉴权证据 |

建议验证顺序：

1. 构建并运行 components/qm_audio/tests 的 ctest。
2. 补齐并运行上述缺失的宿主验收测试，以及相关 qm_agent 集成测试。
3. 构建并运行已启用平台 adapter 的 contract 测试。
4. 构建目标固件。
5. 在板上验证设备初始化、协商媒体 profile、I/O、drain、打断、时序和云端传输。

第 1 至 4 步只能证明构建、主机契约和固件链接；第 5 步才验证设备、模块固件、连线和真实媒体链路。

## 10. 交付就绪条件

在产品声明一条音频路线可用前，必须在集成代码旁记录下列决策。这是交付检查表，不表示 qm_audio 已经启用了所有可能路线。

| 交付物 | 必须明确的内容 |
| --- | --- |
| 媒体合同 | 输入/输出 profile、source 声明、packet 边界模式，以及预期透传或 provider 路线 |
| 调度策略 | 优先级、忙时策略、准入截止时间、pause/yield 行为和终态事件责任方 |
| 资源预算 | session/operation/worker/queue 上限、超时、任务优先级及最坏内存/时延预算 |
| 所有权方案 | source 数据、callback context、operation ID、订阅和清理重试句柄由谁持有 |
| 恢复方案 | -QM_ENOSYS、超时、队列压力、抢占、source 失败和 device 失败的处理 |
| 验证记录 | 验收矩阵中的框架、adapter、目标构建、硬件及压力/故障证据均通过 |

### 10.1 产品路线交付记录

每条产品实际启用的路线都应在集成代码旁维护一条受版本控制的记录。它不是运行时配置格式，而是发布评审时用于确认“这条路线究竟承诺什么”的最小合同：

| 记录字段 | 必须写明 |
| --- | --- |
| route_id 与版本 | 稳定的路线标识，以及本次发布对应的合同版本 |
| 输入声明 | source 类型、codec、framing、PCM 几何、字节流或 packet 边界和 EOS 方式 |
| 固定输出 profile | Engine input/output profile 与 device adapter 实际发布的能力 |
| 规划结果 | 透传、解码、编码或解码后编码；所需 provider 及不支持时的预期错误 |
| 调度与背压 | priority、busy policy、准入截止时间、PUSH/队列超时和业务重试责任方 |
| 资源与时延 | session、operation、worker、队列、栈的上限，以及可接受的排队/媒体时延预算 |
| 结束、失败与清理 | finish、drain、cancel、抢占、超时和 device 错误后的终态、句柄持有者与重试方式 |
| 可观测性 | 业务监听的事件、快照字段、sequence 缺口或产品自有统计；不得把未实现指标写成可用 |
| 证据与兼容性 | 对应的 host/adapter/目标构建/硬件证据；公共合同改变时的兼容、回退或拒绝升级策略 |

### 10.2 变更实施门禁

新增或改变一条路线时，按下列顺序推进；前一项没有明确证据时，不进入下一项：

1. **合同门禁**：先确定 source、固定 sink、packet 边界、所有权和不支持时的错误；公共合同变化同时更新双语设计、头文件说明和兼容策略。
2. **路线门禁**：实现或链接所需 provider，并覆盖成功选路与每种拒绝原因；不得用隐式采样率转换、packet 重分帧或格式猜测让测试通过。
3. **生命周期门禁**：覆盖准入、抢占、取消、drain、超时、迟到事件和 cleanup retry；资源上限、callback 时限和负载结果必须可核查。
4. **产品发布门禁**：将路线记录与实际 Kconfig/链接配置绑定，完成目标构建、adapter contract、硬件及压力/故障验收后才对业务开放。

## 11. 待决事项与未实现扩展

下列能力不在当前实现范围内。产品使用前需要完成相应的接口和验收设计：

| 项目 | 当前边界 | 产品需要时的决策 |
| --- | --- | --- |
| 实时播放交付 | QM_AUDIO_DELIVERY_REALTIME 已有类型，但 Control 当前固定使用可靠交付 | 暴露面向产品的策略并定义丢失/时延行为，或明确 realtime delivery 不支持 |
| Capture 可观测性 | Capture sequence 缺口可暴露丢失，但没有公开 dropped count、队列深度或高水位指标 | 若现场运维需要诊断，新增统计/snapshot API 与告警阈值 |
| Event reason | event.reason 没有共享的框架命名空间 | 保持产品私有，或在多个集成基于它分支前定义带版本的通用 reason enum |
| 可听见开始证据 | STARTED 表示处理已开始，不表示扬声器已输出第一个可听样本 | 若需要开始播放 SLA，新增可选 adapter event/capability 与板级时延验收测试 |

## 12. 文档维护规则

- 新增或改变公共类型、生命周期、路由条件或通用 adapter 合同时，同步更新 README.md 和 README_CN.md。
- README.md 和 README_CN.md 是双语技术方案，章节结构和公共合同保持一致。
- 芯片、模块、总线、板级 profile 和目标验证细节必须与对应平台 adapter 放在一起，不写入本文框架设计。
- 未实现能力必须标注为提案或不支持；不能因为协议声明或 Kconfig 存在就标记为已验证。
- 历史方案由 Git 历史保存，不在组件根目录保留与当前接口冲突的平行设计基线。
