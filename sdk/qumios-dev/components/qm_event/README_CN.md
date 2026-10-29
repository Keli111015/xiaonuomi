# qm_event

英文文档：[README.md](README.md)

`qm_event` 是一个轻量级事件循环组件，同时为 `qm_work` 的延迟任务提供默认调度通道。一个事件由 `event_base + event_id` 标识，事件处理函数接收：

```c
void *event_handler_arg,
qm_event_base_t event_base,
int32_t event_id,
void *event_data,
size_t event_data_size
```

## 架构

```text
生产者：qm_event_post / qm_work
                v
             事件队列
                v
           事件循环分发器
                v
             注册的 handler
```

组件负责排队和分发；应用层负责事件 payload 的存储、handler 工作以及生命周期串行化。无后台任务模式下，应用必须在主循环中主动调用运行/处理接口。

组件源码位于 `components/qm_event`，目录结构为：

- `include/qm_event.h`：事件循环、事件注册、事件投递 API
- `include/qm_work.h`：基于默认事件循环的延迟任务 API
- `src/qm_event.c`：事件循环实现
- `src/qm_work.c`：延迟任务实现

## 配置项

`qm_event` 受 `CONFIG_QM_WORK_SUPPORT` 控制，Kconfig 菜单仍位于 `qm work` 下。

常用配置：

- `CONFIG_QM_WORKQUEUE_NUM`：事件队列长度
- `CONFIG_QM_EVENT_NUM`：每个事件循环可注册的 handler 槽位数
- `CONFIG_QM_WORKQUEUE_NO_TASK`：默认事件循环是否不创建后台任务
- `CONFIG_QM_WORKQUEUE_TASK_NAME`：默认后台任务名
- `CONFIG_QM_WORKQUEUE_TASK_PRIO`：默认后台任务优先级
- `CONFIG_QM_WORKQUEUE_TASK_SIZE`：默认后台任务栈大小

当 `CONFIG_QM_WORKQUEUE_NO_TASK` 为 `0` 且支持 OS task 时，默认事件循环会创建后台任务自动分发事件。否则需要用户调用 `qm_event_default_run()` 或 `qm_workqueue_process()` 驱动分发。

## 事件 Base

事件 base 是一个全局符号，不是整数宏。推荐在头文件中声明：

```c
QM_EVENT_DECLARE_BASE(QM_APP_EVENT);
```

在且只在一个 `.c` 文件中定义：

```c
QM_EVENT_DEFINE_BASE(QM_APP_EVENT);
```

通配符：

- `QM_EVENT_ANY_BASE`：匹配所有 event base
- `QM_EVENT_ANY_ID`：匹配所有 event id

注意：`QM_EVENT_ANY_BASE` 必须和 `QM_EVENT_ANY_ID` 一起使用，不能只通配 base 但指定 id。

## 创建事件循环

手动创建事件循环：

```c
qm_event_handle_t loop = NULL;
qm_event_args_t args = {
    .queue_size = CONFIG_QM_WORKQUEUE_NUM,
    .task_name = NULL,
};

int32_t ret = qm_event_create(&args, &loop);
```

当 `task_name == NULL` 时，不会创建后台任务，用户必须调用：

```c
qm_event_run(loop, QM_NO_WAIT);
```

每次 `qm_event_run()` 最多取出并分发一个事件。

如果需要自动后台分发，可以创建带任务的事件循环：

```c
qm_event_args_t args = {
    .queue_size = CONFIG_QM_WORKQUEUE_NUM,
    .task_name = "app_event",
    .task_priority = 20,
    .task_stack_size = 2048,
};

qm_event_create(&args, &loop);
```

带任务的事件循环目前不能通过 `qm_event_delete()` 强制删除，因为任务退出依赖平台实现。

## 默认事件循环

默认事件循环由：

```c
qm_event_default_create();
```

创建。`qm_work_on_init()` 内部也会确保默认事件循环存在，所以多数系统启动路径会在初始化阶段自动准备好它。

使用默认事件循环时，可以直接注册和投递：

```c
qm_event_handler_register(QM_APP_EVENT,
                          QM_APP_EVENT_STARTED,
                          app_event_handler,
                          app_arg);

qm_event_post(QM_APP_EVENT,
              QM_APP_EVENT_STARTED,
              &payload,
              sizeof(payload),
              QM_NO_WAIT);
```

如果配置为无后台任务模式，需要在主循环中调度：

```c
while(1){
    qm_event_default_run(QM_NO_WAIT);
    qm_msleep(10);
}
```

对于 `qm_work` 延迟任务，也可以调用：

```c
qm_workqueue_process();
```

## 注册和注销 Handler

注册到指定事件循环：

```c
qm_event_handler_register_with(loop,
                               QM_APP_EVENT,
                               QM_APP_EVENT_STARTED,
                               app_event_handler,
                               app_arg);
```

注册到默认事件循环：

```c
qm_event_handler_register(QM_APP_EVENT,
                          QM_APP_EVENT_STARTED,
                          app_event_handler,
                          app_arg);
```

一次注册由以下字段共同确定：

- `event_base`
- `event_id`
- `event_handler`
- `event_handler_arg`

因此，同一个 handler 函数可以用不同 `event_handler_arg` 注册多次，并会分别被调用。

注销时必须传入同一组字段：

```c
qm_event_handler_unregister_with(loop,
                                 QM_APP_EVENT,
                                 QM_APP_EVENT_STARTED,
                                 app_event_handler,
                                 app_arg);
```

## 投递事件

投递到指定事件循环：

```c
qm_event_post_to(loop,
                 QM_APP_EVENT,
                 QM_APP_EVENT_STARTED,
                 &payload,
                 sizeof(payload),
                 QM_NO_WAIT);
```

投递到默认事件循环：

```c
qm_event_post(QM_APP_EVENT,
              QM_APP_EVENT_STARTED,
              &payload,
              sizeof(payload),
              QM_NO_WAIT);
```

payload 生命周期规则：

- `event_data != NULL && event_data_size > 0`：组件会复制 payload，handler 返回后释放内部副本
- `event_data != NULL && event_data_size == 0`：只传递指针，不复制，调用者必须保证指针在事件分发前有效
- `event_data == NULL`：无 payload

`ticks_to_wait` 当前是保留参数，底层 `qm_queue_send()` 暂未使用发送等待时间。

## ISR 投递

中断上下文中使用 ISR API：

```c
qm_event_isr_post(QM_APP_EVENT,
                  QM_APP_EVENT_STARTED,
                  &payload,
                  sizeof(payload));
```

或投递到指定事件循环：

```c
qm_event_isr_post_to(loop,
                     QM_APP_EVENT,
                     QM_APP_EVENT_STARTED,
                     &payload,
                     sizeof(payload));
```

## qm_work 延迟任务

`qm_work` 基于默认事件循环执行延迟回调：

```c
static qm_work_t work = {0};

static void work_action(void *arg)
{
    QM_LOGD("app", "work arg=%s", (char *)arg);
}

qm_post_delayed_action(&work, work_action, "hello", 1000);
```

取消延迟任务：

```c
qm_cancel_delayed_action(&work);
```

同一个 `qm_work_t` 再次调用 `qm_post_delayed_action()` 会替换之前尚未执行的任务。周期任务可以在回调内再次 post，但需要自行控制退出条件。

## 返回值和常见错误

常见返回值：

- `QM_EOK`：成功
- `-QM_EINVAL`：参数非法，例如 handler 为 `NULL`
- `-QM_ENOENT`：默认事件循环尚未创建
- `-QM_EFULL`：handler 槽位已满
- `-QM_ENOMEM`：内存不足
- `-QM_EINIT`：默认事件循环已创建

## 使用建议

- 业务模块优先声明自己的 event base，不要把不同模块事件混用到同一个 base 下。
- handler 中不要执行长时间阻塞操作，避免阻塞同一事件循环内的其他事件。
- 手动创建且 `task_name == NULL` 的事件循环，要确保有地方持续调用 `qm_event_run()`。
- 带任务的事件循环当前不支持强制删除，适合作为长期生命周期对象。
- 默认事件循环由多个组件共享，注册和注销时要传入完全一致的参数。

## 示例

- `examples/qm_event/qm_event_example.c`：手动创建事件循环、注册多个 handler、投递事件、运行和释放
- `examples/qm_event/qm_work_example.c`：一次性延迟任务、替换、取消和有限周期任务
