# QM_TIMER_MGR 组件设计方案（倒计时 / 秒表标准组件）

> 版本：v0.2（评审稿，组件已定名 `qm_timer_mgr`）
> 状态：方案评审 → 骨架 → 实现 → 多轮复核
> 适用范围：`qumios-dev/components/qm_timer_mgr`

---

## 1. 背景与目标

现有 `alarm_app/timer/timer_ctrl.c` 把计时功能（倒计时、秒表等）与平台交互
（UI 页面、语音提示、电机、云端 MQTT）耦合在同一个模块里。其中纯计时的核心逻辑
（状态机、时间推进、事件通知）与平台无关，具备独立成标准组件的价值。

本方案设计一个新的 **`qm_timer_mgr`** 标准组件，只承载**倒计时**与**秒表**两类纯计时语义，
核心状态机**不包含**：

- 任何平台交互（UI、音频、电机）——由上层通过事件回调消费；
- 云端**产品策略**（timerType 解析、guide 语音、UI 仲裁）——由上层通过回调注入。

组件可选承载**云端协议层**（下行方法路由、回复/上报帧），做法与 `qm_player` 的
cloud 子模块一致（见第 12 章）：`qm_timer_mgr_cloud` 子模块拥有协议帧与传输，
业务字段与命令执行通过 `qm_timer_mgr_cloud_config_t` 回调交给产品。

核心组件只依赖 `qm_kernel`（定时器、互斥锁、内存）、`qm_errno`、`qm_types`；
cloud 子模块额外依赖 `qm_iot` / `cJSON`，由 `QM_TIMER_MGR_CLOUD_SUPPORT` 独立开关，
关闭时编译为无操作 stub。公共 API 不暴露 platform / IDF / FreeRTOS / mbedTLS 类型，
保证可移植、可复用。

### 1.1 命名决策（为什么叫 qm_timer_mgr）

内核 [qm_kernel.h](../../../include/qmos/qm_kernel.h) 已占用 `qm_timer_*`
函数命名空间（`qm_timer_new / start / stop / free` 等）和 `qm_timer_t` 类型；
`include/hal/soc/qm_timer.h` 也已存在（SoC 定时器 HAL）。若组件沿用 `qm_timer`，
`qm_timer_start` / `qm_timer_stop` 会与内核同名不同签名 → 编译冲突。

因此组件命名为 **`qm_timer_mgr`**，统一前缀 `qm_timer_mgr_`（句柄 `qm_timer_mgr_t`、
枚举 `QM_TIMER_MGR_*`），与内核低层机制、与应用层 `timer_ctrl.c` 都明确区分。

---

## 2. 定位与边界

```
┌────────────────────────────────────────────────────────────┐
│  上层（alarm_app/timer）：UI/音频/电机/云  │
│        ┌──────────────────────────────────────────────┐   │
│        │     qm_timer_mgr（本组件）                    │   │
│        │  倒计时 + 秒表 · 状态机 · 墙钟推进 · 事件回调  │   │
│        │  依赖：qm_kernel / qm_errno / qm_types        │   │
│        └──────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────┘
```

### 2.1 组件职责（做）

- 维护单一计时句柄上的状态机：`IDLE / RUNNING / PAUSED / STOPPED / FINISHED`。
- 支持两种模式：**倒计时**（countdown）与**秒表**（stopwatch）。
- 基于墙钟时间推进秒数，避免内核定时器回调延迟导致的累计漂移。
- 通过事件回调向上层通知：启动 / 秒跳 / 暂停 / 恢复 / 重启 / 停止 / 结束。
- 通过状态查询接口提供只读快照（`get_status` 推进、`peek_status` 不推进）。
- 提供模式 / 状态字符串转换，便于日志与上报。

### 2.2 组件边界（不做）

- 不做提示音、电机、UI 跳转——上层订阅事件后自行处理。
- 核心状态机不做云端方法路由 / 产品策略；可选子模块 `qm_timer_mgr_cloud` 只负责
  **通用协议帧与传输**（见第 12 章），timerType 解析、guide 语音、UI 仲裁等产品策略
  通过回调留在上层。
- 若需接入 `qm_agent` 会话编排，另行在其上封装，不进本组件。

---

## 3. 目录结构

```text
qumios-dev/components/qm_timer_mgr/
├── include/
│   └── qm_timer_mgr.h        # 公共头文件
├── src/
│   └── qm_timer_mgr.c        # 实现
├── README.md                 # 组件说明 + 基本用法
└── qm_kconfig                # Kconfig 选项
```

### 3.1 构建接线（接入点）

1. `qumios-dev/components/qm_kconfig` 中追加：
   `source "${QDF_PATH}/components/qm_timer_mgr/qm_kconfig"`
2. 工程侧 `alarm_app/board/wl82/Makefile`：
   - `INCLUDES` 追加 `-I$(QUMI_PREFIX)/components/qm_timer_mgr/include`
   - `QUMI_SRC_FILES` 追加 `$(QUMI_PREFIX)/components/qm_timer_mgr/src/qm_timer_mgr.c`

---

## 4. 公共头文件设计（qm_timer_mgr.h）

遵循组件通用风格：不透明句柄、枚举 + 状态/事件结构体、统一返回 `QM_E*`。

### 4.1 模式与状态

```c
typedef struct qm_timer_mgr *qm_timer_mgr_t;

typedef enum {
    QM_TIMER_MGR_MODE_COUNTDOWN = 0,   /**< 倒计时 */
    QM_TIMER_MGR_MODE_STOPWATCH,       /**< 秒表 */
} qm_timer_mgr_mode_t;

typedef enum {
    QM_TIMER_MGR_STATE_IDLE = 0,       /**< 初始/无任务 */
    QM_TIMER_MGR_STATE_RUNNING,        /**< 运行中 */
    QM_TIMER_MGR_STATE_PAUSED,         /**< 已暂停 */
    QM_TIMER_MGR_STATE_STOPPED,        /**< 已停止（保留配置便于 restart） */
    QM_TIMER_MGR_STATE_FINISHED,       /**< 倒计时自然结束 */
} qm_timer_mgr_state_t;
```

### 4.2 事件

```c
typedef enum {
    QM_TIMER_MGR_EVENT_STARTED = 0,    /**< start 进入 RUNNING */
    QM_TIMER_MGR_EVENT_TICK,           /**< 每秒推进（整秒才触发） */
    QM_TIMER_MGR_EVENT_PAUSED,         /**< RUNNING -> PAUSED */
    QM_TIMER_MGR_EVENT_RESUMED,        /**< PAUSED -> RUNNING */
    QM_TIMER_MGR_EVENT_RESTARTED,      /**< restart 重新开始 */
    QM_TIMER_MGR_EVENT_STOPPED,        /**< 停止 -> STOPPED */
    QM_TIMER_MGR_EVENT_FINISHED,       /**< 倒计时结束 -> FINISHED */
} qm_timer_mgr_event_t;
```

### 4.3 状态快照与事件负载

```c
typedef struct {
    qm_timer_mgr_mode_t  mode;         /**< 当前模式 */
    qm_timer_mgr_state_t state;        /**< 当前状态 */
    uint32_t duration_sec;             /**< 倒计时：本轮总时长；秒表：0 */
    uint32_t elapsed_sec;              /**< 本轮已走秒数 */
    uint32_t remaining_sec;            /**< 倒计时：剩余秒数；秒表：0 */
} qm_timer_mgr_status_t;

typedef struct {
    qm_timer_mgr_event_t event;        /**< 事件类型 */
    qm_timer_mgr_state_t previous_state;/**< 迁移前状态（STARTED/RESTARTED 等的旧状态） */
    qm_timer_mgr_status_t status;      /**< 事件后的快照 */
    void *user_ctx;                    /**< 来自 config 的用户上下文 */
} qm_timer_mgr_event_data_t;

typedef void (*qm_timer_mgr_event_cb_t)(qm_timer_mgr_t timer,
                                        const qm_timer_mgr_event_data_t *event);
```

### 4.4 配置

```c
typedef struct {
    qm_timer_mgr_mode_t mode;          /**< 默认模式；0 用 Kconfig 默认 */
    uint32_t duration_sec;             /**< 默认倒计时时长；0 用 Kconfig 默认 */
    uint32_t tick_ms;                  /**< 内核定时器周期；0 用 Kconfig 默认；无需整除时长 */
    qm_timer_mgr_event_cb_t event_cb;  /**< 可选事件回调 */
    void *user_ctx;                    /**< 传给事件回调的用户上下文 */
} qm_timer_mgr_config_t;
```

### 4.5 API 一览

```c
qm_timer_mgr_t qm_timer_mgr_create(const qm_timer_mgr_config_t *config); /* 失败返回 NULL */
void       qm_timer_mgr_destroy(qm_timer_mgr_t timer);
int        qm_timer_mgr_start(qm_timer_mgr_t timer, qm_timer_mgr_mode_t mode, uint32_t duration_sec);
int        qm_timer_mgr_pause(qm_timer_mgr_t timer);
int        qm_timer_mgr_resume(qm_timer_mgr_t timer);
int        qm_timer_mgr_restart(qm_timer_mgr_t timer);
int        qm_timer_mgr_stop(qm_timer_mgr_t timer);
int        qm_timer_mgr_get_status(qm_timer_mgr_t timer, qm_timer_mgr_status_t *status); /* 推进后快照 */
int        qm_timer_mgr_peek_status(qm_timer_mgr_t timer, qm_timer_mgr_status_t *status);/* 只读快照 */
int        qm_timer_mgr_is_active(qm_timer_mgr_t timer);   /* RUNNING/PAUSED 为真 */
const char *qm_timer_mgr_mode_str(qm_timer_mgr_mode_t mode);
const char *qm_timer_mgr_state_str(qm_timer_mgr_state_t state);
```

**关键语义：**

- `start(mode, duration_sec)`：可从任意状态发起新的一轮。倒计时 `duration_sec==0`
  时回退到上一次使用或 config 默认的时长；秒表忽略 `duration_sec`。
- `restart()`：以"最后使用的时长 / 模式"重新开始。倒计时回到满时长，
  秒表清零重走。`STOPPED / PAUSED / FINISHED` 均可 restart。
- `stop()`：任意运行态 -> `STOPPED`。倒计时保留 `duration_sec`、把
  `remaining_sec` 重置回满时长、`elapsed_sec` 清零（供 restart / 展示）；
  秒表清零。
- 控制接口成功返回 `QM_EOK`，失败返回负值 `QM_E*`；`create` 失败返回 `NULL`。
- `destroy` 标记销毁中、停止后续回调、等待在途 API/timer 回调退出后释放资源；
  **禁止在事件回调里调用 destroy**。

---

## 5. 语义定义

### 5.1 倒计时（countdown）

| 字段 | IDLE | RUNNING | PAUSED | STOPPED | FINISHED |
|---|---|---|---|---|---|
| duration_sec | 0 | N | N | N | N |
| remaining_sec | 0 | N→0 | 暂停点 | N（重置） | 0 |
| elapsed_sec | 0 | 0→N | 暂停点 | 0 | N |

- 每秒 `remaining_sec--`，`elapsed_sec = duration_sec - remaining_sec`。
- 归零即 `FINISHED`，停止内核定时器，发 `FINISHED` 事件。

### 5.2 秒表（stopwatch）

| 字段 | IDLE | RUNNING | PAUSED | STOPPED |
|---|---|---|---|---|
| duration_sec | 0 | 0 | 0 | 0 |
| elapsed_sec | 0 | 递增 | 暂停点 | 0 |
| remaining_sec | 0 | 0 | 0 | 0 |

- 每秒 `elapsed_sec++`。永不进入 `FINISHED`。
- UI 展示用 `elapsed_sec`。

---

## 6. 计时策略（墙钟推进，防漂移）

- 内部用内核 `qm_timer_new(period=tick_ms, repeat=1)` 作为**唤醒源**，周期性回调。
- 回调里用墙钟 `qm_now_ms()` 计算与上次推进点之间的真实经过毫秒，
  只把**整秒**累加/累减到状态（`elapsed_sec += ms / 1000`）。
- 采用 `last_tick_ms` 递进（`last_tick_ms += seconds * 1000`）而非直接
  `now - start`，避免长睡眠后一次性补跳过多秒的事件风暴。
- 以墙钟为准，内核定时器回调被高优先级任务延迟也不会产生欠计。

> 假设：`qm_now_ms()` 单调递增且休眠恢复后连续（与 `qm_utils_timer` 使用前提一致）。

---

## 7. 内部结构与线程安全

```c
struct qm_timer_mgr {
    qm_timer_mgr_config_t config;
    qm_timer_t timer;              /* 内核定时器（唤醒源） */
    qm_mutex_t lock;
    qm_bool timer_valid;
    qm_bool lock_valid;
    qm_bool destroying;            /* destroy 后拒绝新工作 */
    int active_ops;                /* 在途 API/回调计数 */
    qm_timer_mgr_mode_t mode;
    qm_timer_mgr_state_t state;
    uint32_t duration_sec;
    uint32_t remaining_sec;
    uint32_t elapsed_sec;
    uint32_t last_used_duration_sec; /* restart 复用 */
    uint32_t last_tick_ms;           /* 墙钟推进基准 */
};
```

- **生命周期**：`enter/leave + active_ops + destroying`，
  保证 destroy 与在途回调安全互斥。
- **事件派发**：回调内先填满最大 `QM_TIMER_MGR_MAX_EVENTS_PER_STEP`（3）的事件数组，
  释放锁后再逐一派发；派发前检查 `destroying`，避免销毁后仍回调。
- **回调上下文**：可能运行于内核定时器任务，回调应避免长时间阻塞。

---

## 8. Kconfig 选项

```kconfig
menu "qm timer mgr"

    config QM_TIMER_MGR_SUPPORT
        bool "qm timer mgr support"
        default "n"
        depends on QM_OS_TIMER_SUPPORT
        depends on QM_OS_MUTEX_SUPPORT
        help
            Enable the qumi countdown / stopwatch timer manager component.

    if QM_TIMER_MGR_SUPPORT

        config QM_TIMER_MGR_DEFAULT_MODE
            int "Default mode (0 countdown, 1 stopwatch)"
            range 0 1
            default 0

        config QM_TIMER_MGR_DEFAULT_DURATION_SEC
            int "Default countdown duration (seconds)"
            range 1 4294967
            default 600

        config QM_TIMER_MGR_TICK_MS
            int "Default tick period (ms)"
            range 100 2147483647
            default 1000
            help
                Kernel timer period used to wake the component. It does not need
                to divide durations; wall-clock advancement avoids drift.

    endif

endmenu
```

---

## 9. 与现有 timer_ctrl 的迁移映射

| timer_ctrl（旧） | qm_timer_mgr（新） | 归属 |
|---|---|---|
| `TIMER_CTRL_TYPE_STOPWATCH` | `QM_TIMER_MGR_MODE_STOPWATCH` | 组件 |
| `TIMER_CTRL_TYPE_COUNTDOWN` | `QM_TIMER_MGR_MODE_COUNTDOWN` | 组件 |
| `TIMER_CTRL_STATE_*` | `QM_TIMER_MGR_STATE_*`（同五态） | 组件 |
| `timer_ctrl_start(type,dur,cloud)` | `qm_timer_mgr_start(timer, mode, dur)` | 组件 + 上层 |
| `timer_ctrl_pause/continue` | `qm_timer_mgr_pause / qm_timer_mgr_resume` | 组件 |
| `timer_ctrl_stop / restart` | `qm_timer_mgr_stop / qm_timer_mgr_restart` | 组件 |
| `timer_ctrl_get_status / peek_status` | `qm_timer_mgr_get_status / peek_status` | 组件 |
| `timer_ctrl_is_active / is_type_active` | `qm_timer_mgr_is_active` | 组件 |
| `timer_ctrl_type_str / state_str` | `qm_timer_mgr_mode_str / state_str` | 组件 |
| 提示音 / 电机 / UI / 云端上报 | 上层订阅事件实现 | 上层 |
| `finish_pending` / guide 队列等 | 上层编排 | 上层 |

迁移后 `timer_ctrl.c` 退化为"上层适配层"：持有 `qm_timer_mgr` 实例，把事件转成
UI / 电机 / 音频动作；通用云端方法路由由 `qm_timer_mgr_cloud` 持有。

---

## 10. 实现步骤

1. 骨架：目录 + `qm_timer_mgr.h` + `qm_kconfig` + `README.md`。
2. 实现 `qm_timer_mgr.c`：状态机、墙钟推进、事件数组派发、生命周期、API。
3. 示例：`qumios-dev/examples/qm_timer_mgr/qm_timer_mgr_example.c` + README + qm_kconfig。
4. 接入构建（`components/qm_kconfig` + 工程 Makefile）并编译验证。
5. 多轮复核：对照本方案逐项验收（见第 11 节）。

---

## 11. 复核清单（逐项验收）

- [x] 公共 API 只暴露 `qm_*` / 基础类型，不暴露平台类型
- [x] 依赖仅 `qm_kernel / qm_errno / qm_types`
- [x] 组件命名 `qm_timer_mgr`，与内核 `qm_timer_*` / SoC HAL `qm_timer.h` 无冲突
- [x] 五态状态机：`IDLE / RUNNING / PAUSED / STOPPED / FINISHED`
- [x] 双模式：倒计时（remaining 递减、归零 FINISHED）与秒表（elapsed 递增、永不 FINISHED）
- [x] 墙钟推进：整秒跳变、回调延迟不欠计/不风暴
- [x] 事件：STARTED / TICK / PAUSED / RESUMED / RESTARTED / STOPPED / FINISHED 全触发
- [x] `get_status` 推进、`peek_status` 只读；`is_active` 正确
- [x] `restart` 复用最后时长；`stop` 后倒计时保留时长可 restart
- [x] 线程安全：destroy 等待在途回调；回调内不持有锁派发
- [x] Kconfig 选项齐全、默认值回退正确
- [x] 目录/接线与其它组件同款结构，组件严格语法编译通过
- [x] cloud 子模块：`QM_TIMER_MGR_CLOUD_SUPPORT` 开关、stub 分支、方法路由、回复/上报
- [x] cloud 与 `qm_player` 结构一致：`*_cloud_config_t` 注入 iot_handle/command_cb/user_data
- [x] 产品策略（timerType 解析、guide 语音、UI 仲裁）留在上层回调，组件只管协议帧

---

## 12. 可选云端子模块（qm_timer_mgr_cloud）

把 IoT 也收编进组件，做法与 `qm_player` 的 cloud 子模块完全对齐：**协议帧与传输
由组件拥有**，**业务字段与命令执行由产品通过回调注入**。这样组件是自包含的——
上层只要填回调即可上云，不需要再写一套 MQTT 解析/上报代码。

### 12.1 边界划分

| 组件（cloud 子模块负责） | 上层（回调/事件负责） |
|---|---|
| 下行 `timer` 方法注册 / 注销 | `command_cb`：实际执行 start/pause/continue/stop/restart |
| 帧解析（`subMethod` / `id` / `params`） | 业务字段回填：timerType、duration、guide 语音等 |
| 回复帧 `qm_iot_down_reply_ex`（method=`timerReply`） | 回复 payload 的业务字段（timerType/state/秒数） |
| 上报帧 `qm_iot_up_report_ex`（subMethod=`update`） | 产品可叠加自定义字段 |

### 12.2 公共 API（qm_timer_mgr.h 追加）

```c
typedef enum {
    QM_TIMER_MGR_CLOUD_COMMAND_INVALID = 0,
    QM_TIMER_MGR_CLOUD_COMMAND_START,     /* "start" */
    QM_TIMER_MGR_CLOUD_COMMAND_PAUSE,     /* "pause" */
    QM_TIMER_MGR_CLOUD_COMMAND_CONTINUE,  /* "continue" */
    QM_TIMER_MGR_CLOUD_COMMAND_STOP,      /* "stop" */
    QM_TIMER_MGR_CLOUD_COMMAND_RESTART,   /* "restart" */
} qm_timer_mgr_cloud_command_type_t;

typedef struct {
    qm_timer_mgr_cloud_command_type_t type;
    const char *params_json;              /* 原始 params 子串，组件不解释业务字段 */
    uint32_t params_json_len;
} qm_timer_mgr_cloud_command_t;

typedef int (*qm_timer_mgr_cloud_command_cb_t)(qm_timer_mgr_t timer,
                                               const qm_timer_mgr_cloud_command_t *command,
                                               char **reply_payload,   /* 产品回填，cJSON 分配 */
                                               void *user_data);

typedef char *(*qm_timer_mgr_cloud_status_payload_cb_t)(qm_timer_mgr_t timer,
                                                        void *user_data);

typedef struct {
    void *iot_handle;                     /* qm_iot 会话 */
    qm_timer_mgr_cloud_command_cb_t command_cb;
    qm_timer_mgr_cloud_status_payload_cb_t status_payload_cb;
    void *user_data;
} qm_timer_mgr_cloud_config_t;

int qm_timer_mgr_cloud_command_pending(qm_timer_mgr_t timer);
int qm_timer_mgr_cloud_attach(qm_timer_mgr_t timer,
                              const qm_timer_mgr_cloud_config_t *config);
```

`qm_timer_mgr_config_t` 追加可选字段 `const qm_timer_mgr_cloud_config_t *cloud;`
（仿 `qm_player_config_t.cloud`）。传 NULL 则组件不带云能力；配置由调用方持有，
必须持续有效到 timer destroy。若 timer 早于 IoT 会话创建，可在会话就绪后调用
一次 `qm_timer_mgr_cloud_attach()`。

### 12.3 Cloud 接口（include/qm_timer_mgr_cloud.h + src/qm_timer_mgr_cloud.c）

- 独立任务 + 队列 + 信号量，结构与 `qm_player_cloud_t` 一致。
- 生命周期钩子：`qm_timer_mgr_cloud_init / quiesce / deinit / event`。
- 下行注册 `QM_TIMER_MGR_CLOUD_METHOD`（默认 `"timer"`），处理
  `subMethod: start/pause/continue/stop/restart`，`subMethod == "get"` 回当前快照。
- 命令统一投递到云任务，调用 `command_cb`，再按结果码回 `timerReply`。
- 状态事件（STARTED/PAUSED/RESUMED/RESTARTED/STOPPED/FINISHED）经
  `qm_timer_mgr_cloud_event` 转发成 `update` 上报。

### 12.4 Kconfig（追加）

```kconfig
config QM_TIMER_MGR_CLOUD_SUPPORT
    bool "Enable qm_iot timer control and reporting"
    default y
    depends on QM_CLOUD_SUPPORT
    select CJSON_LIB

config QM_TIMER_MGR_CLOUD_METHOD
    string "Cloud method name"
    default "timer"

config QM_TIMER_MGR_CLOUD_QUEUE_LEN
    int "Cloud command queue length"
    range 1 64
    default 4

config QM_TIMER_MGR_CLOUD_TASK_STACK_SIZE
    int "Cloud task stack size"
    default 4096

config QM_TIMER_MGR_CLOUD_TASK_PRIORITY
    int "Cloud task priority"
    default 20
```

`#if !CONFIG_QM_TIMER_MGR_CLOUD_SUPPORT` 编译为无操作 stub（同 qm_player_cloud），
保证核心组件在无云环境仍可单独使用。
