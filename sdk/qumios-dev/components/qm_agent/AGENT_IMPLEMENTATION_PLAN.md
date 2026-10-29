# qm_agent 实施方案

状态：实施计划。本文以 [AGENT_FRAMEWORK_DESIGN.md](AGENT_FRAMEWORK_DESIGN.md) 为唯一目标架构，不以当前过渡实现替代目标。

日期：2026-09-14。

本轮只更新方案文档，不修改实现。后续涉及 `qm_audio`、云端协议组件、网络传输组件、视频组件、应用或板级代码的改动，必须在执行前单独确认具体范围。

## 1. 实施原则

1. 先确定跨组件契约，再删除旧任务或旧缓存。不能仅删除任务后把阻塞、缓存或生命周期问题转移到回调上下文。
2. `qm_agent` 只保留 `qm_agent_ctl`。它管理状态和事务，不搬运媒体数据。
3. 媒体数据由拥有底层资源的组件保存和调度：Capture/TTS 归 `qm_audio`，上行发送归协议组件，视频帧归视频组件。
4. 公开控制事件统一通过默认 `qm_event` loop；媒体帧不走事件总线。
5. 每一阶段都必须有独立的关闭顺序、满载策略和回归测试，不能依赖延时等待来规避竞态。

## 2. 变更范围和确认门槛

| 阶段 | 目标组件 | 目的 | 是否需要跨组件确认 |
| --- | --- | --- | --- |
| A | `qm_agent` | 收敛控制状态机、事件和内部桥接边界 | 否，限 `qm_agent` 内部 |
| B | `qm_audio` | 提供通用流输入、音频终态和背压语义 | 是 |
| C | 每个启用的云端协议组件 | 实现上行非阻塞提交、下行 TTS 适配和协议流控 | 是，按组件逐项确认 |
| D | 网络传输组件 | 仅在当前队列无法满足协议组件契约时修正通用传输行为 | 是 |
| E | 视频驱动或视频服务 | 改为推送式帧接入和安全退订 | 是 |
| F | 应用、示例、板级集成 | 装配新接口、注册事件、配置资源并进行目标板验证 | 是 |

明确不在本方案中引入或修改 `qm_work`。认证、连接和会话超时都由 `qm_agent_ctl` 的消息和定时机制推进。

## 3. 目标接口契约

接口名称以下以语义描述为准，最终符号需与 `qm_audio` 和现有头文件命名统一后确定。

### 3.1 qm_audio 的 PUSH 输入

`qm_audio_control` 已经是 Service 和 Pipeline 的媒体控制门面。对协议来源、本地推送来源和未来实时输入，最小且一致的扩展是在现有 `qm_audio_control_play()` 上增加 `QM_AUDIO_SOURCE_PUSH`，而不是再建立一套 `qm_audio_stream_*` Service。

```c
/* Existing operation creation path, extended with QM_AUDIO_SOURCE_PUSH. */
qm_audio_control_play(control, &push_request, &operation_id);

/* New Control-level input operations for a PUSH source. */
qm_audio_control_stream_write(control, operation_id, data, len);
qm_audio_control_stream_finish(control, operation_id);

/* Existing operation cancellation and reclamation path. */
qm_audio_control_stop(control, operation_id, reason);
qm_audio_control_wait(control, operation_id, timeout_ms);
qm_audio_control_release(control, operation_id, timeout_ms);
```

Pipeline 通过 Control 绑定的输出句柄直接向 Engine 提交媒体；Service 负责准入和输出授权，不转发数据块。Agent 和协议组件只使用 Control 的 PUSH 输入接口，不直接写 Engine。

实现时必须满足：

| 操作 | 必须满足的语义 |
| --- | --- |
| `play(PUSH)` | Control 准入成功后返回唯一非零 `operation_id`，并建立由 `qm_audio` 拥有的输入缓冲；失败时没有残留 operation |
| `stream_write` | 成功返回前复制到 `PUSH` source 的有界存储；不等待网络、解码完成或播放完成 |
| `stream_write` 满载 | 返回 `-QM_EFULL` 或等价可恢复错误；调用方仍拥有输入缓冲 |
| `stream_finish` | 封闭输入；Pipeline 排空已接收数据和 decoder tail 后，由 Control 发布唯一终态 |
| `stop` | 取消输入、唤醒等待者、停止输出；复用既有 Control 取消路径 |
| 状态通知 | 使用既有 Control operation ID 和类型化状态；Agent 不拼接 Service/Pipeline 多路终态 |

流格式必须覆盖 PCM 和编码输入。`PUSH` source 把原始输入交给 Pipeline；Pipeline 根据 codec/container 选择 PCM 直通、解析、解码和转换后端。Agent 不增加 codec 分支。

### 3.2 Agent 与协议组件

协议组件对 Agent 暴露两个方向的边界：

| 方向 | 接口责任 | 返回和所有权 |
| --- | --- | --- |
| 上行 Capture/Video | `submit_media()` 或等价接口接收借用帧 | `QM_EOK` 前完成复制或接管；满载快速返回 |
| 下行 TTS | 类型化 `begin/data/end/error` 回调 | payload 仅在回调期间有效，适配层不得保留副本 |

下行回调不直接修改 Agent 状态。`begin/end/error` 进入 `qm_agent_ctl`；已获准入流的 `data` 直接写入 `qm_audio`。

协议组件必须在流创建完成前暂停或保留该流的入站数据，不能要求 Agent 建立临时媒体队列。音频容量不足时由协议组件自身流控、暂停读取或在自身预算内缓存；超过预算则终止协议流并上报控制错误。

### 3.3 Agent 公开事件

目标公开接口以默认事件服务为中心：

```c
qm_event_handler_register(QM_AGENT_EVENT_BASE,
                          QM_EVENT_ANY_ID,
                          app_agent_event_handler,
                          user_ctx);
```

`qm_agent` 使用 `qm_event_post` 发布低频可复制事件。事件使用 `agent_id`、`generation`、`request_id`、状态和结果码识别上下文；不得携带媒体 payload 或内部对象指针。

## 4. 阶段 A：qm_agent 内部收敛

范围仅限 `components/qm_agent/`。这一阶段不要求改动 `qm_audio` 或任一协议组件，因此不能宣称已经删除 TTS 媒体任务。

### A.1 控制任务和单写者状态

1. 在私有运行时中建立有界控制消息队列和 `qm_agent_ctl`。
2. 将 start、stop、interrupt、认证结果、协议连接事件、TTS begin/end、超时和销毁转换为控制消息。
3. 规定所有会话、连接、认证和请求事务状态只在控制任务中写入。
4. 为数据路径发布只读的 capture gate、generation 和已建立 stream 映射，使用锁、原子快照或引用计数保证停止竞态安全。
5. 删除任何以调用方任务直接修改会话状态的路径。

### A.2 默认事件服务

1. 定义 `QM_AGENT_EVENT_BASE`、事件 ID 和无指针的公开事件载荷。
2. 由控制任务或受控事件桥在任务上下文调用 `qm_event_post` 投递生命周期事件，不使用 ISR 投递接口。
3. 停止/销毁期间按 `agent_id + generation` 过滤迟到事件。
4. 不创建 Agent 专用 event loop 或 event task。
5. 保留高频 Capture/TTS/Video 回调为内部数据路径，禁止转为 `qm_event_post`。
6. 公开事件投递失败不得影响会话状态或资源释放；记录统计，并由快照查询提供最终状态。

### A.3 事务模型

1. 每个文本/TTS 请求在控制任务中预留 `uint32_t request_id`。
2. 将请求、协议流、`qm_audio operation_id` 和最终事件关联到同一 generation。
3. 统一处理远端拒绝、格式失败、用户取消、打断、播放失败、停止和销毁。
4. 每个请求只允许一个终态；重复取消或迟到终态只记诊断信息。

### A.4 关闭屏障

1. stop 先关闭 Capture/Video 和协议入站的媒体门。
2. 等待已进入的媒体回调退出，再撤销 stream 映射和协议回调。
3. 等待控制消息处理至停止终态后结束控制任务。
4. 任一步失败时保留对象，禁止释放仍可能被回调访问的内存。

阶段 A 的测试重点：多线程 start/stop、快速 create/destroy、迟到认证结果、重复 TTS end、回调中再次调用异步 API，以及默认事件中不出现内部指针。

## 5. 阶段 B：qm_audio 流能力

该阶段必须先确认修改 `qm_audio`。它是删除 Agent TTS 缓存、Source 和播放任务的前置条件。

### B.1 Control 和 Service

1. 为既有 `qm_audio_control_play()` 增加 `QM_AUDIO_SOURCE_PUSH`，并用同一个 `operation_id` 关联后续写入、结束、停止和释放。
2. `play(PUSH)`、Control 级 `stream_write` 与 `stream_finish` 均使用有界命令准入；调用不得无限等待 Control 任务、Pipeline、Decoder、Engine 或物理播放排空。队列满或关闭必须快速返回明确错误。
3. 保持 Service 为音频资源准入和 PCM 输出所有者；`session_write` 仍只接收 Pipeline 产出的 PCM。
4. 使用现有播放优先级和抢占机制处理 TTS，与本地资源、提示音等使用同一仲裁路径。
5. 明确 `PUSH` source 写入为同步复制入队，输入容量为配置化的有界预算，并定义 write/finish/stop 的幂等性和错误码。

### B.2 Pipeline

1. 将 `QM_AUDIO_SOURCE_PUSH` 作为 Pipeline 的一种内部拥有缓冲的通用 source，而不是 Agent 自定义 Source。
2. 按格式选择 PCM 直通、解析、解码和转换后端。
3. 在 finish 后排空已接收数据；在 cancel 后立即唤醒阻塞点并停止输出。
4. 向 Control 返回唯一终态，不能由 Engine、Pipeline 和 Service 分别向 Agent 重复报告完成。

### B.3 事件与生命周期

1. Control operation 事件通过默认 `qm_event` loop 以 `operation_id`、generation、结果和必要状态发布；事件负载不携带数据块。
2. 释放 operation 前确认 Pipeline、Decoder 和 Renderer 不再持有其输入缓冲。
3. 写入和取消并发时，`write` 要么完整接收，要么明确失败；不能部分接收而没有结果。
4. Agent 的内部音频 event handler 只投递控制消息；它不在默认事件任务中等待、释放或停止 Audio operation。

阶段 B 的测试重点：PCM 与编码 TTS、队列满、优先级抢占、finish 排空、cancel 与 write 竞态、解码失败、设备关闭和重复释放。

## 6. 阶段 C：云端协议组件适配

每个实际启用的后端单独确认和验收，例如 JoyInside、Baidu、Lgeine、Agora 或后续组件。不得用一个后端的行为假设替代其他 SDK。

### C.1 上行媒体提交

1. 协议组件提供 Capture/Video 可调用的有界提交入口。
2. 成功返回前复制或接管帧；失败时不保存借用指针。
3. 满载返回 `-QM_EFULL` 或明确可恢复错误，不隐式等待网络发送完成。
4. 组包、序号、重试、发送缓存和连接切换留在协议组件及网络传输组件。

### C.2 下行 TTS

1. 将云端原始报文解析为 `begin/data/end/error`，保留协议私有字段在组件内部。
2. `begin` 等待 Agent 流创建确认后再交付数据，或使用协议组件自身的有界接收预算。
3. `data` 按顺序调用 Agent 适配入口，适配入口直写 `qm_audio`。
4. `end/error` 作为控制通知，等待 Agent 给出唯一事务终态。
5. 停止时先禁止新回调，再确认所有协议回调完成，最后释放协议对象。

### C.3 认证

认证仍是 Agent 会话的一部分，由 `qm_agent_ctl` 调度。协议组件只提供启动、取消和结果回调；不另起 Agent Auth 任务，也不引入 `qm_work`。

阶段 C 的测试重点：发送队列满、断线重连、TTS 乱序/重复 end、begin 后立即 stop、协议回调与销毁并发、SDK 是否真正复制媒体缓冲。

## 7. 阶段 D：网络传输组件

仅当阶段 C 证明现有传输队列不满足“有界、连接隔离、快速失败和安全关闭”契约时才修改网络传输组件，并在开始前单独确认。

需要检查：

- 零等待发送是否真的不阻塞。
- 发送队列是否按连接或会话隔离，旧连接数据是否会泄漏到新连接。
- stop/deinit 是否等待 I/O 回调退出后再释放队列和上下文。
- 协议组件可否准确区分队列满、断线、取消和永久错误。

网络层不增加 Agent 请求 ID、TTS 语义或音频格式分支。

## 8. 阶段 E：视频组件

该阶段必须先确认视频驱动或视频服务改动范围。

1. 以订阅/回调方式接收视频帧，保留帧所有权在视频组件。
2. 在回调中调用协议组件的非阻塞提交入口。
3. stop 时先退订，等待在途回调归零，再停止视频服务或销毁 Agent。
4. 删除 Agent 主动读帧循环和 `qm_agent_vid` 任务。

视频帧的限速、关键帧、编码和缓存策略由视频服务或协议组件定义，不进入 Agent 控制任务。

## 9. 阶段 F：应用与目标板集成

该阶段必须确认应用和板级修改范围。

1. 应用在默认 `qm_event` loop 注册 Agent 事件 handler，不再依赖组件私有 listener。
2. 应用创建并持有 `qm_audio`，将其公开服务句柄借给 Agent；先启动系统默认事件服务，再启动 Agent。
3. 根据真实编码、采样率、声道、协议包长和内存预算配置 `qm_audio`、协议组件和视频服务。
4. 在目标板验证录音、上行、PCM TTS、编码 TTS、打断、提示音抢占、断网、重连和关闭。

## 10. 文件变更准则

实施时按边界放置代码，不建立平行框架：

| 范围 | 允许新增或修改的内容 |
| --- | --- |
| `qm_agent/include/` | 公开事件定义、精简后的会话/传输契约；不暴露协议 SDK 和音频内部缓冲 |
| `qm_agent/priv_include/` | 控制消息、只读快照、流关联和关闭屏障等私有状态 |
| `qm_agent/src/core/` | 单一控制任务、状态机、事务和事件投递 |
| `qm_agent/src/transport/` | 协议事件转换和数据桥接；不实现协议队列或网络发送 |
| `qm_audio/include/` | `PUSH` source、Control 写入/结束接口和类型化状态事件 |
| `qm_audio/src/control` / `pipeline` | `PUSH` 输入缓冲、解码、排空、取消和状态归并；Service 保持 PCM 输出职责 |
| 协议组件 | 上下行媒体的协议处理、缓存、流控和 SDK 回调封装 |
| 视频组件 | 帧推送、订阅和退订 |

不为每个阶段增加新的通用工作队列、专用任务或重复缓冲。已有组件的队列只能由其所有者维护。

## 11. 验证矩阵

| 场景 | 关键断言 |
| --- | --- |
| 快速 create/start/stop/destroy | 无 use-after-free、无卡死、无迟到事件污染新 generation |
| Capture 上行队列满 | Capture worker 不阻塞；丢帧或流控统计正确 |
| PCM TTS | 数据由 `qm_audio` 流输入处理，finish 后恰好一次完成事件 |
| 编码 TTS | Pipeline 选择正确 decoder；格式错误在准入或解码阶段明确失败 |
| TTS 数据过快 | Agent 不缓存数据；协议组件和 `qm_audio` 的背压可观察 |
| interrupt/cancel | 音频停止、输入唤醒、协议取消和 Agent 终态无重复 |
| 播放优先级 | Audio Service 按统一策略抢占或拒绝，Agent 不直接操作硬件 |
| 断网/重连 | 协议和网络组件清理旧连接队列，不混入新会话数据 |
| 视频 stop | 退订完成后才释放 Agent，帧回调不访问旧映射 |
| 默认事件 loop | 应用 handler 接收低频值事件；媒体帧不进入 loop |

每个代码阶段至少执行：接口/头文件审查、静态检查、对应主机测试、启用后端的编译检查，以及可用时的目标板回归。若目标板工具链或 SDK 不可用，报告未执行项和剩余风险，不能用主机测试代替。

## 12. 完成定义

满足以下条件才视为该架构完成：

1. Agent 只创建 `qm_agent_ctl`，且该任务是唯一状态写入者。
2. Agent 不再拥有 TTS 数据缓存、播放 Source、上传队列、视频拉帧任务或认证任务。
3. `qm_audio` 接收 TTS 数据后独立完成缓冲、解码、渲染和终态通知，支持 PCM 与已配置编码格式。
4. 每个启用协议后端实现有界、非阻塞的上行提交与安全的下行流适配。
5. 应用通过默认 `qm_event` loop 接收公开控制事件。
6. stop/destroy 在 Capture、TTS、Video、协议回调和事件投递并发下没有资源泄漏、重复终态或悬空访问。
7. 所有实际启用的目标板配置通过构建和端到端验收。
