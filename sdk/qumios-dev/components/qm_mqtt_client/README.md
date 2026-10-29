# QM MQTT Client

`qm_mqtt_client` 是基于 qumi TCP/TLS 和 OS 抽象实现的通用 MQTT Client。
协议层从零实现，不依赖仓库内已有 `components/mqtt`、ESP-MQTT 或 MQTTPacket。

组件对外提供：

- 后台任务自动 `yield`、心跳和接收消息
- `init/start/stop/destroy` 生命周期
- `subscribe/unsubscribe`
- `publish`
- client 级统一事件回调
- TCP 和 TLS 连接
- MQTT 3.1.1 CONNECT、PUBLISH、SUBSCRIBE、UNSUBSCRIBE、PINGREQ/PINGRESP 编解码
- 当前发送和订阅支持 QoS0/QoS1

## 基本用法

```c
#include "qm_mqtt_client.h"

static void mqtt_event_cb(const qm_mqtt_event_t *event)
{
    switch (event->event_id) {
        case QM_MQTT_EVENT_CONNECTED:
            qm_mqtt_client_subscribe(event->client, "device/down", QM_MQTT_QOS0);
            qm_mqtt_client_publish(event->client, "device/up", "online", 6, QM_MQTT_QOS0, QM_FALSE);
            break;
        case QM_MQTT_EVENT_DATA:
            QM_LOGI("mqtt", "message topic_len=%u payload_len=%lu",
                    (unsigned int)event->topic_len,
                    (unsigned long)event->total_data_len);
            break;
        default:
            break;
    }
}

void app_start_mqtt(void)
{
    qm_mqtt_client_config_t cfg = {
        .broker = {
            .host = "broker.emqx.io",
            .port = 1883,
        },
        .credentials = {
            .client_id = "qumi-device-001",
        },
        .session = {
            .clean_session = QM_TRUE,
            .keepalive_interval_ms = 60000,
        },
        .network = {
            .timeout_ms = 3000,
        },
        .buffer = {
            .rx_size = 4096,
            .tx_size = 1024,
        },
        .on_event = mqtt_event_cb,
    };

    qm_mqtt_client_handle_t client = qm_mqtt_client_init(&cfg);
    if (client) {
        qm_mqtt_client_start(client);
    }
}
```

## TLS

设置 `broker.tls` 后会通过 qumi TLS 建立 MQTT over TLS 连接：

```c
static const qm_mqtt_tls_config_t tls = {
    .server_crt = ca_pem,
    .server_crt_len = ca_pem_len,
    .client_crt = NULL,
    .client_crt_len = 0,
    .client_key = NULL,
    .client_key_len = 0,
};

qm_mqtt_client_config_t cfg = {
    .broker = {
        .host = "example.com",
        .port = 8883,
        .tls = &tls,
    },
    .credentials = {
        .client_id = "qumi-device-001",
    },
    .session = {
        .clean_session = QM_TRUE,
    },
    .network = {
        .timeout_ms = 3000,
    },
    .on_event = mqtt_event_cb,
};
```

证书字符串由调用方持有，组件只保存指针；client 存活期间证书内容必须保持有效。

## 说明

- 事件只保留稳定集合：`CONNECTED`、`DISCONNECTED`、`SUBSCRIBED`、`UNSUBSCRIBED`、`PUBLISHED`、`DATA`、`ERROR`。
- 订阅失败通过 `QM_MQTT_EVENT_SUBSCRIBED` 的 `error_code` 和 `error_type` 表达，链路/超时/缓冲区错误统一派发 `QM_MQTT_EVENT_ERROR`。
- `qm_mqtt_client_start()` 建立连接并启动后台任务；连接成功后派发 `QM_MQTT_EVENT_CONNECTED`。
- 收到订阅消息时派发 `QM_MQTT_EVENT_DATA`，`topic` 和 `data` 只在回调期间有效。
- 大 payload 会按固定 RX buffer 分片派发多次 `QM_MQTT_EVENT_DATA`；首个事件带 `topic/topic_len`，后续事件只带 `data`，可通过 `current_data_offset` 和 `total_data_len` 拼接。
- `QM_MQTT_EVENT_CONNECTED` 会带出 `session_present`，可用于判断 broker 是否保留了会话。
- 收到超出读缓冲区的非 PUBLISH 报文，或 PUBLISH topic 头部本身放不下时，派发 `QM_MQTT_EVENT_ERROR`，`error_type` 为 `QM_MQTT_ERROR_BUFFER`。
- `qm_mqtt_client_subscribe()` 会复制 topic 字符串，业务不需要保证 topic 入参长期有效。
- `qm_mqtt_client_publish()` 在 QoS1 下返回 packet id，收到 PUBACK 后派发 `QM_MQTT_EVENT_PUBLISHED`。
- `publish/subscribe/unsubscribe` 会先进入 QUMI `qm_utils_list` 维护的 `send_queue`，由 MQTT task 统一发送。
- QoS1 publish、subscribe、unsubscribe 发送成功后进入 `outbox`，ACK 到达后从 outbox 回收。
- 断线重连后，若 clean session 或 broker 未保留会话，组件会按内部订阅表恢复订阅。
- `qm_mqtt_client_destroy()` 会先停止后台任务，再释放内部资源。

## 日志配置

`qm_mqtt_client` 的日志由 `CONFIG_QM_MQTT_CLIENT_LOG_CONFIG` 总开关控制。总开关关闭时只保留错误和生命周期日志；打开后可分别配置 `ERROR`、`LIFECYCLE`、`CONTROL`、`UPLINK`、`DOWNLINK`、`PAYLOAD` 和 `STATS` 分类。运行时可通过 `qm_mqtt_client_set_log_flags()` 修改全局 mask，通过 `qm_mqtt_client_get_log_flags()` 读取当前 mask。

日志只影响 `qm_mqtt_client` 自身，不会修改 TCP、TLS 或其他组件的日志开关。日志不输出密码、证书、密钥、完整 topic 或完整 payload，只记录长度、状态和 packet id 等摘要。
