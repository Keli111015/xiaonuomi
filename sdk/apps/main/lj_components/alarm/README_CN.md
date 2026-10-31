# 应用层闹钟

2026-10-31 文档静态整理；本轮未编译或实机验证。项目入口见[文档导航](../../../../../docs/README.md)。

这个目录位于 `apps/main/lj_components/alarm`，是平台 `qm_alarm` 组件之上的一层应用
包装，不是通用闹钟组件。云负责下发，设备侧只做透传与行为响应。

提供：

- `app_alarm_init()` 创建并启动组件，`app_alarm_deinit()` 停止并销毁；
- 事件搬运：组件的 4 类通知投递到默认事件循环，在那里跑行为回调；
- 条目、响铃、查询接口的薄转发；
- 调试命令行 `alarm ...`（`CONFIG_QM_CLI_SUPPORT` 打开时编译）。

`apps/main/main.c` 在 `agent_app_init()` 之后调用 `app_alarm_init(ljyd_cloud_get_handle())`。
行为回调在模块内部登记，产品化时替换 `app_alarm.c` 的 `app_alarm_behavior()`。

## 开关

`apps/main/qm_config/qm_config.h`：

```c
#define CONFIG_QM_ALARM_SUPPORT 1
#define CONFIG_QM_ALARM_MAX_ALARMS 8
#define CONFIG_QM_ALARM_SNOOZE_INTERVAL_MIN 5
```

`CONFIG_QM_ALARM_SUPPORT` 同时管平台组件和应用层。置 0 时 `CMakeLists.txt` 不加
`app_alarm.c` 和 `qm_alarm.c`，`main.c`、`time_sync.c` 的调用点也被同一个宏包住。

只包调用点是不够的：`qm_alarm.c` 整体在 `#if defined(CONFIG_QM_ALARM_SUPPORT)` 里，
关掉后编成空 TU 没有符号，`app_alarm.c` 若还在编译就会 undefined reference。
`qm_alarm` 默认不编译，源文件是手工列进 `CMakeLists.txt` 的。

## 产品标识

闹钟的云端同步与下发都走 `qm_iot` 会话，产品身份从本地 `apps/main/cloud/ljyd_cloud_local.h` 提供，不在公共文档中复制产品密钥。配置入口见[完整工程说明](../../../../../docs/FULL_REPOSITORY.md)。下行 topic 使用 `/<PID>/<DID>/device/down`。

## 触发链路

```text
CLI / 云端下行 / 本地调用
        |
        v
   app_alarm_*()        薄转发，不做业务判断
        |
        v
   qm_alarm 组件        owner task 内排程、到点触发
        |
        v
   alarm_notify_cb      只拷贝 + 投递，立即返回
        |
        v
   qm_post_delayed_action(ms=0)
        |
        v
   alarm_evt_work       默认事件循环任务
        |
        v
   app_alarm_behavior   示例：播提示音
```

通知回调跑在组件 owner task 上，只做值拷贝和投递。业务动作在 `alarm_evt_work` 里跑，
所以行为回调里可以安全地调本模块接口，但**不能有阻塞调用**：事件循环还承载
`qm_agent` 的 reconcile work 和 `qm_alarm` 的排程 work，冻住就是整机无日志、唤不醒、
CLI 也停。

响铃时让对话让出喇叭由 `boot_sound` 在自己的任务里做，顺序是：

```text
agent_app_set_resource_mode(QM_TRUE)    预留通路：暂停采集、关上行
agent_app_abort_output()                停对端输出，并重开 TTS 接收
agent_app_play_local_audio()            播提示音
agent_app_set_resource_mode(QM_FALSE)   释放通路：恢复采集与上行
```

不要在行为回调里调 `agent_app_leave()`——它会在事件循环上做 agora 拆链，实测"正在
对话时到点响铃"会卡死；也不要用自己的播放会话 `INTERRUPT_ALL` 去抢 agent 的 TRANSPORT
会话，agent 的 `on_preempt` 会关掉 `tts_accepting`，而 agora 回不来能重开它的事件，
之后 TTS 数据全被丢弃、喇叭没声。

## 接口

`app_alarm.h` 里每个接口都有一行说明，这里只列分组和要点：

| 接口 | 说明 |
| --- | --- |
| `app_alarm_init(iot_handle)` | 注册（创建 + 启动）。句柄传 `NULL` 只做本地闹钟；单例，重复调用直接返回 |
| `app_alarm_deinit()` | 停调度、注销云端方法、销毁实例并清空回调。当前启动流程不调用 |
| `app_alarm_item_defaults / add / modify / delete / enable` | 条目操作。改和删按 `(source, alarm_id)` 主键 |
| `app_alarm_close_ringing / snooze / cancel_snooze` | 响铃流程 |
| `app_alarm_get_next / get_list / get_state` | 查询，给界面渲染和兜底排查，不是触发主路径 |
| `app_alarm_time_changed()` | 对时后必须调，否则按旧时钟误触发 |
| `app_alarm_report()` | 主动整表上报，一般不需要 |

初始化和逆初始化都不动已有闹钟数据：不清列表、不删 KV。

`app_alarm_event_t` 是值语义快照，回调返回后仍有效，可以直接保存；`extensions` 是组件
内的借用指针，不转发，需要完整条目时用 `get_list()` / `get_state()` 再查。

一次性闹钟响过之后由组件自动置为非使能，应用层不需要判断闹钟类型。

## 对时与云端同步

组件按内部时钟算下一次触发，调用点是 `apps/main/cloud/time_sync.c`：`ntpdate` 对时
成功后写 RTC，再通知本模块重新排程（`time_sync_start()` 由 `ljyd_cloud.c` 在网络可用
时发起）。

整表上报、断网延迟补报、下行解析（`alarm/update` 的
`create/modify/delete/enable/update/close/snooze`）都由 `qm_alarm` 内建，应用层不用写
云同步代码；语音"设个闹钟"这条链路的意图转换在云端完成。

## 调试命令行

```text
alarm list|next|state|add <+N|HH:MM>|del <id>|on|off <id>|close|snooze|cancel|clear|test
```

组件没有 clear-all 接口，`alarm clear` 只能按 `(source, id)` 主键逐条删。

## 排查

1. `CONFIG_QM_ALARM_SUPPORT` 已打开且 `qm_alarm.c` 在编译列表里；
2. `alarm state` 到点前后能否从 `idle` 变成 `ringing`；
3. `alarm list` 里的时刻不是真实时间 → `app_alarm_time_changed()` 没被调到；
4. 有 `RING` 日志但没声音 → 看 `boot_snd` 日志，提示音走 agent 的本地通路；
5. 云端不下发 `alarm/create` → 云端意图转换问题，不是设备侧协议问题。
