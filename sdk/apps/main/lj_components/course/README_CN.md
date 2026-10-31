# 课程表（ljyd_course）

2026-10-31 按源码静态更新；本轮未编译或联调。总体入口见[文档导航](../../../../../docs/README.md)。

## 文件

| 文件 | 说明 |
| --- | --- |
| `ljyd_course.c` | 数据模型、Flash 持久化、`course/sync` 下行、`course/syncAck` 应答、晨间播报 |
| `ljyd_course.h` | 对外 API |
| `README_CN.md` | 本文档 |

## 接入点

| 位置 | 调用 |
| --- | --- |
| `apps/main/main.c` | `ljyd_course_init(ljyd_cloud_get_handle())`，位于 `ljyd_cloud_init()` 之后、`agent_app_init()` 之前 |
| `apps/main/cloud/ljyd_provision.c` | clear-and-reboot 流程调用 `ljyd_course_clear_configs()` 后重启 |
| `apps/main/CMakeLists.txt` | 编译 `./lj_components/course/ljyd_course.c`，并加入 `./lj_components/course` 头文件目录 |

## 存储格式

Flash key 固定为 `course_cfg`，二进制布局与旧固件 / `qm_course` 中间件一致，
OTA 升级后可继续读取旧数据：

```c
typedef struct {
    uint8_t             synced;      /* 1 = 已同步 */
    uint16_t            entry_count;
    ljyd_course_entry_t entries[32]; /* weekday, period, subject[32], textbook[64] */
} ljyd_course_store_t;
```

## 运行时行为

1. **开机**：`ljyd_course_init()` 读取 `course_cfg` 恢复上次同步的课表。
2. **连云**：通过 `qm_iot_event_register()` 追加事件监听（`ljyd_cloud` 的单槽
   回调已被 `agent_app` 占用），收到 `QM_IOT_EVENT_CLOUD_CONN` 后注册 `course`
   下行；重连时重新挂订阅。
3. **同步**：收到 `course/sync` 后解析 `params[0].extensions.courses[]`
   （`weekday` / `period` / `subject` / `textbook`），全量覆盖并写 Flash，
   回复 `course/syncAck`，同时置“已更新”标志
   （`ljyd_course_take_update_flag()` 读取并清除）。
4. **清理接口**：`ljyd_course_clear_configs()` 清空内存并尝试删除 `course_cfg`。
   `ljyd_provision_clear_and_reboot()` 在更新绑定标记、删除云环境和 Wi-Fi 配置后调用它，
   然后重启。删除返回值仅写日志，持久化失败和重启后的行为仍需设备端验收。

端侧是**被动接收端**，不会主动向云端请求课表。

## 查询 API

```c
ljyd_course_entry_t today[LJYD_COURSE_MAX_ENTRIES];
int n = ljyd_course_get_day_entries(3, today, LJYD_COURSE_MAX_ENTRIES); /* 周三，按节次升序 */
```

`ljyd_course_is_synced()` 为 0 时 UI 应展示“请同步”占位。

## 晨间播报（需应用自行触发）

`ljyd_course_broadcast_today()` 读取今日周几，然后以 `siid=10 / piid=1`
上报一个数字属性，由云端 TTS 播报当天课程。

- 本仓库没有 `mode_manager` 的 40 s 定时器，不在 `main.c` 里自动排程，
  由业务代码调用（例如语音唤醒流程）。
- 未同步、云端未就绪、本地时间无效时直接跳过。
- **本地时间**：播报前要求本地年份 ≥ `LJYD_COURSE_MIN_VALID_YEAR`（2024）。
  不能只凭 `CONFIG_QM_IOT_NTP_SUPPORT` 推断无对时：当前应用另有 `cloud/time_sync.c`，
  云连接时调用 `time_sync_start()`。实际对时成功、时区及播报日期仍需联调，不能把
  `qm_localtime()` 非空当作时间已正确。

## 与 WSL `eye_jj` 分支的差异

| WSL | 本仓库 |
| --- | --- |
| `qm_course` 中间件 + `course_integration.c` | 单文件 `ljyd_course.c` 独立实现 |
| `course_integration_init(void)` | `ljyd_course_init(void *iot_handle)`，内部注册事件监听 |
| `ui_timetable_invalidate()` | 置 `s_updated` 脏标志，由 `ljyd_course_take_update_flag()` 读取 |
| `iot_func_req_course_tts()`（`xjiang_cloud.c`） | 本模块内用 `qm_spec_property_*` + `qm_iot_property_report()` 上报 |
| `ai_main_is_rtc_ready()` | 本地年份 ≥ 2024 校验 |
| `mode_manager` 40 s 定时器 | 未移植，保留 API 由业务触发 |
