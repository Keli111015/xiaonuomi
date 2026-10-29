# QUMI 显示控制器客户端

`qm_display_client` 是显示控制器通信协议 V1.0 的主控侧客户端，负责大端帧编解码、
CRC-16/CCITT-FALSE、字节流重同步、序号匹配、超时原事务重发、命令解析、两个逻辑
屏幕的本地状态维护和 DISPLAY_COMPLETE 事件处理。

协议正文以 [PROTOCOL.md](PROTOCOL.md) 为准。本客户端按该 V1.0 文档实现；仓库内
服务端是独立的显示/素材测试对端，当前不包含 OTA 实现。

组件名使用 `client`，是因为 QUMI 主控始终主动发起请求，远端显示控制器负责
响应。它不是本地屏幕驱动，也不配置 UART 引脚、USB 端点、屏端图片解码、RAM
或 Flash。

## 架构

```text
业务应用
  |  GET_FW_VERSION / DISPLAY / SCREEN_CONTROL / ASSET_* / OTA_*
  v
qm_display_client
  |  帧 + SEQ + 重试 + CRC + 字节流解析
  v
read/write 字节流回调
  |  应用选择的承载适配
  v
QUMI UART HAL / USB 承载  <---->  远端显示控制器
```

传输回调及其 `user` 指针在客户端生命周期内由组件借用。回调返回实际传输字节
数，读取可以按任意正长度分段返回，协议解析不依赖底层包边界；返回 0 表示超时，
负值表示承载错误。

client 在创建时分配一块最大发送帧缓冲和一块接收帧缓冲，后续请求及素材分片均复用这两块缓冲。

## 接入

启用 `CONFIG_QM_DISPLAY_CLIENT_SUPPORT`，并在实际目标的源码和头文件列表中加入：

- `components/qm_display_client/src/qm_display_client.c`
- `components/qm_display_client/include`
- `utility/crc/qm_crc.c` 和 `utility/crc`
- `utility/timer/qm_utils_timer.c` 和 `utility/timer`

组件 Kconfig 会选择 `QM_CRC` 和 `QM_TIMER`，并通过
`CONFIG_QM_DISPLAY_CLIENT_ASSET_CHUNK_SIZE` 配置 ASSET_DATA 分片大小（默认 512）。
仓库中使用显式源码列表的目标仍需在对应构建文件中加入上述路径。

## 使用方式

```c
static int32_t display_write(void *user, const uint8_t *data,
                             uint32_t length, uint32_t timeout_ms)
{
    return qm_uart_write((qm_uart_dev_t *)user, data, length, timeout_ms);
}

static int32_t display_read(void *user, uint8_t *data,
                            uint32_t length, uint32_t timeout_ms)
{
    return qm_uart_read((qm_uart_dev_t *)user, data, length, timeout_ms);
}

qm_display_client_config_t config = {
    .write = display_write,
    .read = display_read,
    .user = &uart,
};
qm_display_client_handle_t client;
qm_display_fw_info_t fw;
qm_display_result_t result;

client = qm_display_client_create(&config);
if (client != NULL) {
    qm_err_t ret = qm_display_client_get_fw_version(client, &fw, &result);

    if (ret == QM_EOK && result.status == QM_DISPLAY_STATUS_OK) {
        /* 此时 fw.major、fw.minor、fw.patch 有效。fw.state 仅为兼容字段。 */
    }
    qm_display_client_destroy(client);
}
```

`qm_display_client_create()` 在配置非法或内存分配失败时返回 `NULL`。其余请求 API
返回 `QM_EOK` 表示收到了合法且匹配的响应，还必须检查 `result.status` 判断屏端执行
结果。本地承载、超时和畸形响应使用负 QUMI 错误码。收到 `BUSY` 时，组件按配置的
固定间隔等待，并使用新 SEQ 发起新事务；未收到有效响应时复用原 SEQ，并按协议设置
`RETRY` 重发。

`qm_display_client_set_screen_power()` 用于开关显示输出且不丢弃当前画面；
`qm_display_client_set_brightness()` 设置 0～100 的产品映射亮度，亮度 0 不等于关屏。

`qm_display_client_upload_asset()` 内部完整执行 `ASSET_BEGIN`、循环 `ASSET_DATA` 和
`ASSET_END`。调用方只提供素材元数据和按 offset 读取回调，回调可适配文件、Flash
分区、网络缓存或实时下载流；短读会由 client 聚合成配置的协议分片。

调用前仍需从 HTTP Content-Length 或素材清单获得完整 `total_size`。client 在转发数据
时增量计算 CRC-16/CCITT-FALSE，并在 `ASSET_END` 提交最终值，因此不需要缓存完整素材
或读取两遍。返回 OK 表示新素材已校验并完成；`BUSY` 重试和有界的 `OFFSET_ERROR`
从头恢复由组件内部完成。

client 空闲时，应由拥有承载读侧的任务调用 `qm_display_client_poll()`。收到
`DISPLAY_COMPLETE` 后，组件先发送确认，再调用配置的事件回调。同步请求等待响应期间，
事件也会由事务层内部处理，不会打断原请求。`qm_display_client_upload_ota()` 执行
协议 OTA 数据流；如果 `OTA_END` 响应可能因屏端重启丢失，可设置 `verify_version`
和 `expected_version`，组件会用 `GET_FW_VERSION` 确认结果。

## 责任边界

客户端是同步接口，同一时刻只允许一个请求；多个任务共享句柄时由调用者串行化。
组件不保存素材或固件数据、不自动读取文件、不配置物理链路，也不实现屏端控制器逻辑。

对应的屏端实现和客户端/服务端直接互测位于 `components/qm_display_server`。

可运行的 UART 客户端示例位于 `examples/qm_display`。
