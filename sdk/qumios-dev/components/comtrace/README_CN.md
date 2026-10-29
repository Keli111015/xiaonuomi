# ComTrace 端侧协议组件

组件包含两项相互独立的协议能力：

- `comtrace_log.h`：CELP/1 日志能力，支持 TCP/UDP；TCP 提供有界发送队列。
- `comtrace_ota.h`：CEUP/1 OTA 能力，直接使用 QUMI OTA HAL。

两项能力共享私有的“2 字节大端长度 + CBOR Map”帧实现。
`comtrace_discovery.h` 只负责 ComTrace 服务的 DNS-SD 适配，通用 DNS-SD
解析仍归 `mdns` 组件。

Socket 和任务由应用负责。发送回调中的帧只在本次回调期间有效；接收消息
字段和发现结果也是回调生命周期数据，需要长期保存时必须复制。

```text
应用
 |                         +-- comtrace_log_t -- CELP/1
 +-- 公共帧实现 ------------+
 |                         +-- comtrace_ota_t -- CEUP/1
 +-- comtrace_discover() -> mdns（通用 DNS-SD）
```

`comtrace.h` 仍可作为总入口。产品应通过
`QM_COMTRACE_LOG_SUPPORT`、`QM_COMTRACE_OTA_SUPPORT`、
`QM_COMTRACE_DISCOVERY_SUPPORT` 只启用实际需要的能力。

## 最小接入

组件不创建任务，socket 和网络任务由应用持有。发送回调同步执行；网络
收到字节后调用 `feed_tcp()`，之前阻塞的 TCP 发送可重试时调用 `flush()`。

```c
comtrace_log_t *log;
comtrace_log_config_t config = {
    .sn = "SN-001",
    .sid = 1,
    .transport = COMTRACE_TRANSPORT_TCP,
    .send = send_frame,
};

comtrace_log_init(&log, &config);
comtrace_log_register(log);
comtrace_log_write_text(log, COMTRACE_LOG_DIRECTION_EVENT,
                        COMTRACE_LOG_LEVEL_INFO, "ready", NULL, 0);
/* 网络接收路径：comtrace_log_feed_tcp(log, buffer, length); */
```

OTA 同样由回调驱动。`comtrace_ota_feed()` 可在现有接收任务中调用，并直接
使用 QUMI OTA HAL。默认情况下，收到 `update_end` 后会复用 chunk 缓冲区
回读镜像并校验 SHA-256，取消或断线会调用 `qm_ota_abort()`；初始化不会创建
ComTrace 专用任务。

只有写入能力、无法回读待升级镜像的平台可启用
`QM_COMTRACE_OTA_STREAM_VERIFY`。启用后，每个成功写入的 chunk 会立即参与
SHA-256 计算，`update_end` 直接校验接收流摘要，不再调用 `qm_ota_read()`。
没有 `qm_ota_abort()` 的平台可关闭 `QM_COMTRACE_OTA_ABORT_SUPPORT`；此时一旦
`qm_ota_start()` 之后发生失败、取消或断线，本次启动内 OTA 状态不可恢复，
应用必须重启后才能重新升级。两个兼容选项的默认值均保持原有的“回读校验
+ abort”行为，不影响已有平台。

OTA 初始化必须提供 `authorize` 回调。回调应验证当前连接的可信身份，或将
`sha256` 与可信发布清单比较；只校验对端同时提供的 SHA-256 不能证明固件来源。
`reboot` 回调在 `rebooting` 状态成功发送后执行，适合立即重启或调度重启；
不提供时组件调用 `qm_reboot()`。

```c
static int authorize_update(const comtrace_ota_request_t *request, void *user)
{
    /* 对照已验证的发布清单、签名结果或受信连接上下文。 */
    return release_is_trusted(request, user) ? QM_EOK : -QM_EINVAL;
}

comtrace_ota_config_t ota_config = {
    .sn = "SN-001",
    .sid = 1,
    .current_version = "1.0.0",
    .product_id = "sensor-a1",
    .max_chunk = 768,
    .max_image_size = 8U * 1024U * 1024U,
    .send = send_frame,
    .authorize = authorize_update,
};
```

应用必须在断线或传输超时时调用 `comtrace_ota_disconnect()`。支持 abort 时，
该接口会清空半帧、中止未提交镜像，并要求下一条连接重新注册；不支持 abort
时会进入终态失败，应用必须重启。镜像进入 `rebooting` 后已经提交，
cancel、error 和 disconnect 都不会再把它改写为 cancelled/failed。新固件启动并
重新注册后，可调用 `comtrace_ota_report_boot_result()` 上报 success 或 rollback。

```text
unregistered -> registering -> idle -> prepared -> transferring -> verifying
                                      |                                |
                                      +---- cancel/disconnect ---------+-> failed/cancelled
                                                                       |
                                                                       +-> rebooting -> reboot
```

组件不是线程安全对象。`register/feed/disconnect/cancel/deinit` 以及状态回调必须由
应用串行调用；需要跨任务控制时，应先投递到持有 OTA 连接的任务。
