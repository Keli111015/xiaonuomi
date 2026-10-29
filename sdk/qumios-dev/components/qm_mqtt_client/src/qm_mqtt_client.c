#include "qm_mqtt_client.h"
#include "qm_mqtt_client_log.h"

#include "qm_kernel.h"
#include "qm_string.h"
#include "qm_tcp.h"
#include "qm_tls.h"
#include "qm_utils_list.h"
#include "qm_utils_timer.h"

#define LOG_TAG "MQTT_CLIENT"

#ifndef CONFIG_QM_MQTT_CLIENT_TASK_STACK_SIZE
#define CONFIG_QM_MQTT_CLIENT_TASK_STACK_SIZE 4096
#endif

#ifndef CONFIG_QM_MQTT_CLIENT_TASK_PRIORITY
#define CONFIG_QM_MQTT_CLIENT_TASK_PRIORITY 5
#endif

#ifndef CONFIG_QM_MQTT_CLIENT_RX_BUF_SIZE
#define CONFIG_QM_MQTT_CLIENT_RX_BUF_SIZE 4096
#endif

#ifndef CONFIG_QM_MQTT_CLIENT_TX_BUF_SIZE
#define CONFIG_QM_MQTT_CLIENT_TX_BUF_SIZE 1024
#endif

#ifndef CONFIG_QM_MQTT_CLIENT_REQUEST_TIMEOUT_MS
#define CONFIG_QM_MQTT_CLIENT_REQUEST_TIMEOUT_MS 3000
#endif

#ifndef CONFIG_QM_MQTT_CLIENT_KEEPALIVE_INTERVAL_MS
#define CONFIG_QM_MQTT_CLIENT_KEEPALIVE_INTERVAL_MS 60000
#endif

#ifndef CONFIG_QM_MQTT_CLIENT_YIELD_TIMEOUT_MS
#define CONFIG_QM_MQTT_CLIENT_YIELD_TIMEOUT_MS 100
#endif

#ifndef CONFIG_QM_MQTT_CLIENT_STOP_WAIT_MS
#define CONFIG_QM_MQTT_CLIENT_STOP_WAIT_MS 3000
#endif

#ifndef CONFIG_QM_MQTT_CLIENT_SUBSCRIBE_MAX
#define CONFIG_QM_MQTT_CLIENT_SUBSCRIBE_MAX 20
#endif

#ifndef CONFIG_QM_MQTT_CLIENT_OUTBOX_MAX
#define CONFIG_QM_MQTT_CLIENT_OUTBOX_MAX 8
#endif

#ifndef CONFIG_QM_MQTT_CLIENT_SEND_QUEUE_MAX
#define CONFIG_QM_MQTT_CLIENT_SEND_QUEUE_MAX 8
#endif

#define MQTT_PKT_CONNECT     1
#define MQTT_PKT_CONNACK     2
#define MQTT_PKT_PUBLISH     3
#define MQTT_PKT_PUBACK      4
#define MQTT_PKT_SUBSCRIBE   8
#define MQTT_PKT_SUBACK      9
#define MQTT_PKT_UNSUBSCRIBE 10
#define MQTT_PKT_UNSUBACK    11
#define MQTT_PKT_PINGREQ     12
#define MQTT_PKT_PINGRESP    13
#define MQTT_PKT_DISCONNECT  14

#define MQTT_READ_STREAMED 1

typedef struct {
    qm_bool used;
    char *topic;
    qm_mqtt_qos_t qos;
} subscription_t;

typedef enum {
    OUTBOX_PUBLISH = 1,
    OUTBOX_SUBSCRIBE,
    OUTBOX_UNSUBSCRIBE,
} outbox_type_t;

typedef struct {
    uint16_t packet_id;
    outbox_type_t type;
} outbox_entry_t;

typedef struct {
    uint16_t packet_id;
    outbox_type_t ack_type;
    uint32_t packet_len;
    uint8_t packet[1];
} send_item_t;

struct qm_mqtt_client {
    qm_mqtt_client_config_t config;
    qm_mqtt_tls_config_t tls_config;
    char *host;
    char *client_id;
    char *username;
    char *password;
    uint8_t *read_buf;
    uint8_t *write_buf;
    uint8_t *packet_buf;
    void *transport;
    int tcp_socket;
    qm_bool use_tls;
    qm_task_t task;
    qm_mutex_t lock;
    qm_bool lock_ready;
    qm_bool task_running;
    qm_bool stop_requested;
    qm_bool connected;
    qm_bool session_present;
    qm_bool ping_outstanding;
    uint8_t connack_return_code;
    qm_utils_time_t ping_timer;
    uint16_t next_packet_id;
    qm_list_t outbox;
    qm_list_t send_queue;
    subscription_t subscriptions[CONFIG_QM_MQTT_CLIENT_SUBSCRIBE_MAX];
};

static volatile uint32_t g_qm_mqtt_client_log_flags = QM_MQTT_CLIENT_KCONFIG_LOG_FLAGS;

uint32_t qm_mqtt_client_get_log_flags(void)
{
    return g_qm_mqtt_client_log_flags;
}

int qm_mqtt_client_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~(uint32_t)QM_MQTT_CLIENT_LOG_ALL) != 0U) {
        return -QM_EINVAL;
    }
    g_qm_mqtt_client_log_flags = log_flags;
    return QM_EOK;
}

static void dispatch_event(qm_mqtt_client_handle_t client, const qm_mqtt_event_t *event)
{
    if (client && client->config.on_event) {
        client->config.on_event(event);
    }
}

static void dispatch_simple(qm_mqtt_client_handle_t client, qm_mqtt_event_id_t id, int error_code)
{
    qm_mqtt_event_t event;

    qm_memset(&event, 0, sizeof(event));
    event.event_id = id;
    event.client = client;
    event.user_context = client ? client->config.user_context : NULL;
    event.session_present = client ? client->session_present : QM_FALSE;
    event.error_code = error_code;
    dispatch_event(client, &event);
}

static void dispatch_error(qm_mqtt_client_handle_t client, qm_mqtt_error_type_t type, int error_code)
{
    qm_mqtt_event_t event;

    qm_memset(&event, 0, sizeof(event));
    event.event_id = QM_MQTT_EVENT_ERROR;
    event.client = client;
    event.user_context = client ? client->config.user_context : NULL;
    event.error_type = type;
    event.error_code = error_code;
    dispatch_event(client, &event);
}

static uint16_t next_packet_id(qm_mqtt_client_handle_t client)
{
    uint16_t id;

    qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
    id = ++client->next_packet_id;
    if (id == 0) {
        id = ++client->next_packet_id;
    }
    qm_mutex_unlock(&client->lock);

    return id;
}

static int outbox_add_locked(qm_mqtt_client_handle_t client, outbox_type_t type, uint16_t packet_id)
{
    qm_list_iterator_t iterator;
    qm_list_node_t *node;

    if (!client || packet_id == 0) {
        return -QM_EINVAL;
    }

    qm_list_iterator_init(&client->outbox, LIST_HEAD, &iterator);
    while ((node = qm_list_iterator_next(&iterator)) != NULL) {
        outbox_entry_t *entry = (outbox_entry_t *)qm_list_node_val_get(node);

        if (entry->type == type && entry->packet_id == packet_id) {
            return QM_EOK;
        }
    }

    if (qm_list_len_get(&client->outbox) >= CONFIG_QM_MQTT_CLIENT_OUTBOX_MAX) {
        return -QM_EFULL;
    }

    node = qm_list_node_extra_new(sizeof(outbox_entry_t));
    if (!node) {
        return -QM_ENOMEM;
    }

    {
        outbox_entry_t *entry = (outbox_entry_t *)qm_list_node_val_get(node);

        entry->type = type;
        entry->packet_id = packet_id;
    }

    if (!qm_list_rpush(&client->outbox, node)) {
        qm_list_node_destroy(node);
        return -QM_EIO;
    }

    return QM_EOK;
}

static qm_bool outbox_remove_locked(qm_mqtt_client_handle_t client, outbox_type_t type, uint16_t packet_id)
{
    qm_list_iterator_t iterator;
    qm_list_node_t *node;

    if (!client || packet_id == 0) {
        return QM_FALSE;
    }

    qm_list_iterator_init(&client->outbox, LIST_HEAD, &iterator);
    while ((node = qm_list_iterator_next(&iterator)) != NULL) {
        outbox_entry_t *entry = (outbox_entry_t *)qm_list_node_val_get(node);

        if (entry->type == type && entry->packet_id == packet_id) {
            qm_list_remove(&client->outbox, node);
            qm_list_node_destroy(node);
            return QM_TRUE;
        }
    }

    return QM_FALSE;
}

static qm_bool outbox_remove(qm_mqtt_client_handle_t client, outbox_type_t type, uint16_t packet_id)
{
    qm_bool found;

    qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
    found = outbox_remove_locked(client, type, packet_id);
    qm_mutex_unlock(&client->lock);
    return found;
}

static void outbox_clear(qm_mqtt_client_handle_t client)
{
    qm_list_node_t *node;

    if (!client) {
        return;
    }

    while ((node = qm_list_lpop(&client->outbox)) != NULL) {
        qm_list_node_destroy(node);
    }
}

static void send_queue_clear(qm_mqtt_client_handle_t client)
{
    qm_list_node_t *node;

    if (!client) {
        return;
    }

    while ((node = qm_list_lpop(&client->send_queue)) != NULL) {
        qm_list_node_destroy(node);
    }
}

static int encode_remaining_length(uint8_t *buf, uint32_t len)
{
    int out_len = 0;

    do {
        uint8_t encoded = len % 128;
        len /= 128;
        if (len > 0) {
            encoded |= 0x80;
        }
        buf[out_len++] = encoded;
    } while (len > 0 && out_len < 4);

    return len == 0 ? out_len : -QM_EINVAL;
}

static int build_packet(uint8_t *out, uint32_t out_size, uint8_t packet_type_flags,
                        const uint8_t *body, uint32_t body_len, uint32_t *packet_len)
{
    int rem_len_bytes;

    if (!out || !packet_len || (!body && body_len > 0) || body_len + 5 > out_size) {
        return -QM_EINVAL;
    }

    out[0] = packet_type_flags;
    rem_len_bytes = encode_remaining_length(out + 1, body_len);
    if (rem_len_bytes < 0) {
        return rem_len_bytes;
    }

    if (body_len > 0) {
        qm_memmove(out + rem_len_bytes + 1, body, body_len);
    }
    *packet_len = body_len + (uint32_t)rem_len_bytes + 1;
    return QM_EOK;
}

static int enqueue_packet_locked(qm_mqtt_client_handle_t client, outbox_type_t ack_type,
                                 uint16_t packet_id, uint8_t packet_type_flags,
                                 const uint8_t *body, uint32_t body_len)
{
    qm_list_node_t *node;
    send_item_t *item;
    uint32_t packet_len;
    int ret;

    if (!client || (!body && body_len > 0) || body_len + 5 > client->config.buffer.tx_size) {
        return -QM_EINVAL;
    }
    if (qm_list_len_get(&client->send_queue) >= CONFIG_QM_MQTT_CLIENT_SEND_QUEUE_MAX) {
        return -QM_EFULL;
    }

    node = qm_list_node_extra_new((uint32_t)(sizeof(send_item_t) + client->config.buffer.tx_size - 1));
    if (!node) {
        return -QM_ENOMEM;
    }

    item = (send_item_t *)qm_list_node_val_get(node);
    item->packet_id = packet_id;
    item->ack_type = ack_type;
    ret = build_packet(item->packet, client->config.buffer.tx_size, packet_type_flags,
                       body, body_len, &packet_len);
    if (ret != QM_EOK) {
        qm_list_node_destroy(node);
        return ret;
    }
    item->packet_len = packet_len;

    if (!qm_list_rpush(&client->send_queue, node)) {
        qm_list_node_destroy(node);
        return -QM_EIO;
    }

    return QM_EOK;
}

static uint8_t *write_u16(uint8_t *p, uint16_t value)
{
    *p++ = (uint8_t)(value >> 8);
    *p++ = (uint8_t)(value & 0xff);
    return p;
}

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint8_t *write_string(uint8_t *p, const char *value)
{
    uint16_t len = value ? (uint16_t)qm_strlen(value) : 0;

    p = write_u16(p, len);
    if (len > 0) {
        qm_memcpy(p, value, len);
        p += len;
    }
    return p;
}

static int transport_write(qm_mqtt_client_handle_t client, const uint8_t *buf, int len, uint32_t timeout_ms)
{
    int sent = 0;
    qm_utils_time_t timer;

    qm_utils_time_countdown_ms(&timer, timeout_ms);

    while (sent < len) {
        int ret;
        uint32_t left = qm_utils_time_left(&timer);

        if (left == 0) {
            left = 1;
        }

        if (client->use_tls) {
            ret = qm_tls_write(client->transport, (const char *)buf + sent, len - sent, left);
        } else {
            ret = qm_tcp_write(client->tcp_socket, (const char *)buf + sent, len - sent, left);
        }

        if (ret < 0) {
            return -QM_EIO;
        }
        if (ret == 0) {
            if (qm_utils_time_is_expired(&timer)) {
                return -QM_ETIMEOUT;
            }
            qm_msleep(1);
            continue;
        }
        sent += ret;
    }

    return sent;
}

static int transport_read(qm_mqtt_client_handle_t client, uint8_t *buf, int len, uint32_t timeout_ms)
{
    int got = 0;
    qm_utils_time_t timer;

    qm_utils_time_countdown_ms(&timer, timeout_ms);

    while (got < len) {
        int ret;
        uint32_t left = qm_utils_time_left(&timer);

        if (left == 0) {
            left = 1;
        }

        if (client->use_tls) {
            ret = qm_tls_read(client->transport, (char *)buf + got, len - got, left);
        } else {
            ret = qm_tcp_read(client->tcp_socket, (char *)buf + got, len - got, left);
        }

        if (ret < 0) {
            return -QM_EIO;
        }
        if (ret == 0) {
            if (qm_utils_time_is_expired(&timer)) {
                return -QM_ETIMEOUT;
            }
            qm_msleep(1);
            continue;
        }
        got += ret;
    }

    return got;
}

static void transport_close(qm_mqtt_client_handle_t client)
{
    if (!client) {
        return;
    }

    if (client->use_tls && client->transport) {
        qm_tls_destroy(client->transport);
    } else if (!client->use_tls && client->tcp_socket >= 0) {
        qm_tcp_destroy(client->tcp_socket);
    }

    client->transport = NULL;
    client->tcp_socket = -1;
    client->connected = QM_FALSE;
    client->ping_outstanding = QM_FALSE;
    outbox_clear(client);
    send_queue_clear(client);
    QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE, "transport closed");
}

static int transport_open(qm_mqtt_client_handle_t client)
{
    if (!client) {
        return -QM_EINVAL;
    }

    client->use_tls = client->config.broker.tls ? QM_TRUE : QM_FALSE;
    client->tcp_socket = -1;
    client->transport = NULL;

    if (client->use_tls) {
        qm_tls_cfg_t tls_cfg;

        qm_memset(&tls_cfg, 0, sizeof(tls_cfg));
        tls_cfg.ca_crt = client->config.broker.tls->server_crt;
        tls_cfg.ca_crt_len = client->config.broker.tls->server_crt_len;
        tls_cfg.client_crt = client->config.broker.tls->client_crt;
        tls_cfg.client_crt_len = client->config.broker.tls->client_crt_len;
        tls_cfg.client_key = client->config.broker.tls->client_key;
        tls_cfg.client_key_len = client->config.broker.tls->client_key_len;
        if (tls_cfg.ca_crt && tls_cfg.ca_crt_len == 0) {
            tls_cfg.ca_crt_len = (int)qm_strlen(tls_cfg.ca_crt) + 1;
        }
        if (tls_cfg.client_crt && tls_cfg.client_crt_len == 0) {
            tls_cfg.client_crt_len = (int)qm_strlen(tls_cfg.client_crt) + 1;
        }
        if (tls_cfg.client_key && tls_cfg.client_key_len == 0) {
            tls_cfg.client_key_len = (int)qm_strlen(tls_cfg.client_key) + 1;
        }
        client->transport = qm_tls_establish(client->config.broker.host, client->config.broker.port, &tls_cfg);
        if (!client->transport) {
            QM_MQTT_CLIENT_LOGE(LOG_TAG, "TLS transport open failed ret=%d", -QM_EIO);
            return -QM_EIO;
        }
        QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE,
                            "TLS transport open host_len=%lu port=%u",
                            (unsigned long)qm_strlen(client->config.broker.host),
                            (unsigned int)client->config.broker.port);
        return QM_EOK;
    }

    client->tcp_socket = qm_tcp_establish(client->config.broker.host, client->config.broker.port);
    if (client->tcp_socket < 0) {
        QM_MQTT_CLIENT_LOGE(LOG_TAG, "TCP transport open failed ret=%d", -QM_EIO);
        return -QM_EIO;
    }
    QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE,
                        "TCP transport open host_len=%lu port=%u",
                        (unsigned long)qm_strlen(client->config.broker.host),
                        (unsigned int)client->config.broker.port);
    return QM_EOK;
}

static int send_packet_locked(qm_mqtt_client_handle_t client, uint8_t packet_type_flags,
                              const uint8_t *body, uint32_t body_len)
{
    uint8_t header[5];
    int rem_len_bytes;

    if (!client || (!body && body_len > 0) || body_len + 5 > client->config.buffer.tx_size) {
        return -QM_EINVAL;
    }

    header[0] = packet_type_flags;
    rem_len_bytes = encode_remaining_length(&header[1], body_len);
    if (rem_len_bytes < 0) {
        return rem_len_bytes;
    }

    qm_memcpy(client->write_buf, header, (uint32_t)rem_len_bytes + 1);
    if (body_len > 0) {
        qm_memmove(client->write_buf + rem_len_bytes + 1, body, body_len);
    }

    return transport_write(client, client->write_buf,
                           (int)body_len + rem_len_bytes + 1,
                           client->config.network.timeout_ms);
}

static int send_packet(qm_mqtt_client_handle_t client, uint8_t packet_type_flags,
                       const uint8_t *body, uint32_t body_len)
{
    int ret;

    qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
    ret = send_packet_locked(client, packet_type_flags, body, body_len);
    qm_mutex_unlock(&client->lock);
    return ret;
}

static int send_empty_packet(qm_mqtt_client_handle_t client, uint8_t packet_type_flags)
{
    uint8_t packet[2] = {packet_type_flags, 0};
    int ret;

    qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
    ret = transport_write(client, packet, sizeof(packet), client->config.network.timeout_ms);
    qm_mutex_unlock(&client->lock);
    return ret;
}

static int process_send_queue(qm_mqtt_client_handle_t client)
{
    while (client && client->connected) {
        qm_list_node_t *node;
        send_item_t *item;
        int ret = QM_EOK;

        qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
        node = qm_list_lpop(&client->send_queue);
        qm_mutex_unlock(&client->lock);
        if (!node) {
            return QM_EOK;
        }

        item = (send_item_t *)qm_list_node_val_get(node);
        if (item->ack_type != 0) {
            qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
            ret = outbox_add_locked(client, item->ack_type, item->packet_id);
            if (ret != QM_EOK) {
                qm_list_lpush(&client->send_queue, node);
                qm_mutex_unlock(&client->lock);
                return ret == -QM_EFULL ? QM_EOK : ret;
            }
            qm_mutex_unlock(&client->lock);
        }

        ret = transport_write(client, item->packet, (int)item->packet_len, client->config.network.timeout_ms);
        if (ret < 0) {
            if (item->ack_type != 0) {
                outbox_remove(client, item->ack_type, item->packet_id);
            }
            qm_list_node_destroy(node);
            return ret;
        }

        qm_list_node_destroy(node);
    }

    return QM_EOK;
}

static int send_connect(qm_mqtt_client_handle_t client)
{
    uint8_t *p = client->packet_buf;
    uint8_t flags = 0;
    uint32_t remaining_len;
    int ret;
    uint32_t need = 10 + 2 + qm_strlen(client->config.credentials.client_id);

    if (client->config.credentials.username) {
        need += 2 + qm_strlen(client->config.credentials.username);
    }
    if (client->config.credentials.password) {
        need += 2 + qm_strlen(client->config.credentials.password);
    }
    if (need + 5 > client->config.buffer.tx_size) {
        return -QM_EFULL;
    }

    p = write_string(p, "MQTT");
    *p++ = 4;
    if (client->config.credentials.username) {
        flags |= 0x80;
    }
    if (client->config.credentials.password) {
        flags |= 0x40;
    }
    if (client->config.session.clean_session) {
        flags |= 0x02;
    }
    *p++ = flags;
    p = write_u16(p, (uint16_t)(client->config.session.keepalive_interval_ms / 1000));
    p = write_string(p, client->config.credentials.client_id);
    if (client->config.credentials.username) {
        p = write_string(p, client->config.credentials.username);
    }
    if (client->config.credentials.password) {
        p = write_string(p, client->config.credentials.password);
    }

    remaining_len = (uint32_t)(p - client->packet_buf);
    ret = send_packet(client, MQTT_PKT_CONNECT << 4, client->packet_buf, remaining_len);
    if (ret < 0) {
        return ret;
    }

    return QM_EOK;
}

static int drain_bytes(qm_mqtt_client_handle_t client, uint32_t len)
{
    while (len > 0) {
        uint32_t chunk = len > client->config.buffer.rx_size ? client->config.buffer.rx_size : len;
        int ret = transport_read(client, client->read_buf, (int)chunk, client->config.network.timeout_ms);

        if (ret < 0) {
            return ret;
        }
        len -= chunk;
    }

    return QM_EOK;
}

static void dispatch_buffer_error(qm_mqtt_client_handle_t client, uint32_t total_len)
{
    qm_mqtt_event_t event;

    qm_memset(&event, 0, sizeof(event));
    event.event_id = QM_MQTT_EVENT_ERROR;
    event.client = client;
    event.user_context = client->config.user_context;
    event.total_data_len = total_len;
    event.error_type = QM_MQTT_ERROR_BUFFER;
    event.error_code = -QM_EFULL;
    dispatch_event(client, &event);
}

static void dispatch_publish_data(qm_mqtt_client_handle_t client, uint8_t packet_flags,
                                  const char *topic, uint16_t topic_len,
                                  const uint8_t *data, uint32_t data_len,
                                  uint32_t total_data_len, uint32_t data_offset,
                                  uint16_t packet_id)
{
    qm_mqtt_event_t event;

    qm_memset(&event, 0, sizeof(event));
    event.event_id = QM_MQTT_EVENT_DATA;
    event.client = client;
    event.user_context = client->config.user_context;
    event.topic = topic;
    event.topic_len = topic_len;
    event.data = (const char *)data;
    event.data_len = data_len;
    event.total_data_len = total_data_len;
    event.current_data_offset = data_offset;
    event.qos = (packet_flags >> 1) & 0x03;
    event.retain = packet_flags & 0x01;
    event.dup = (packet_flags >> 3) & 0x01;
    event.packet_id = packet_id;
    if (data_offset == 0U) {
        QM_MQTT_CLIENT_LOGD(LOG_TAG, QM_MQTT_CLIENT_LOG_DOWNLINK,
                            "publish received topic_len=%u payload_len=%lu qos=%u packet_id=%u",
                            (unsigned int)topic_len, (unsigned long)total_data_len,
                            (unsigned int)event.qos, (unsigned int)packet_id);
    }
    dispatch_event(client, &event);
}

static int ack_publish(qm_mqtt_client_handle_t client, uint8_t packet_flags, uint16_t packet_id)
{
    if (((packet_flags >> 1) & 0x03) == 1) {
        uint8_t ack[2];

        write_u16(ack, packet_id);
        return send_packet(client, MQTT_PKT_PUBACK << 4, ack, sizeof(ack));
    }

    return QM_EOK;
}

static int read_publish_streamed(qm_mqtt_client_handle_t client, uint8_t packet_flags, uint32_t remaining_len)
{
    uint8_t packet_id_buf[2];
    uint16_t topic_len;
    uint16_t packet_id = 0;
    uint8_t qos = (packet_flags >> 1) & 0x03;
    uint32_t topic_end;
    uint32_t header_len;
    uint32_t payload_len;
    uint32_t payload_offset = 0;
    uint32_t first_chunk;
    int ret;

    if (remaining_len < 2) {
        return -QM_EINVAL;
    }

    ret = transport_read(client, client->read_buf, 2, client->config.network.timeout_ms);
    if (ret < 0) {
        return ret;
    }

    topic_len = read_u16(client->read_buf);
    topic_end = 2 + topic_len;
    header_len = topic_end + (qos > 0 ? 2 : 0);
    if (remaining_len < header_len) {
        return -QM_EINVAL;
    }
    if (topic_end > client->config.buffer.rx_size) {
        ret = drain_bytes(client, remaining_len - 2);
        if (ret < 0) {
            return ret;
        }
        dispatch_buffer_error(client, remaining_len);
        return -QM_EFULL;
    }

    if (topic_len > 0) {
        ret = transport_read(client, client->read_buf + 2, topic_len, client->config.network.timeout_ms);
        if (ret < 0) {
            return ret;
        }
    }

    if (qos > 0) {
        ret = transport_read(client, packet_id_buf, sizeof(packet_id_buf), client->config.network.timeout_ms);
        if (ret < 0) {
            return ret;
        }
        packet_id = read_u16(packet_id_buf);
    }

    payload_len = remaining_len - header_len;
    first_chunk = client->config.buffer.rx_size - topic_end;
    if (first_chunk > payload_len) {
        first_chunk = payload_len;
    }
    if (first_chunk > 0) {
        ret = transport_read(client, client->read_buf + topic_end, (int)first_chunk, client->config.network.timeout_ms);
        if (ret < 0) {
            return ret;
        }
    }

    dispatch_publish_data(client, packet_flags,
                          (const char *)client->read_buf + 2, topic_len,
                          first_chunk > 0 ? client->read_buf + topic_end : NULL,
                          first_chunk, payload_len, 0, packet_id);
    payload_offset = first_chunk;

    while (payload_offset < payload_len) {
        uint32_t chunk = payload_len - payload_offset;

        if (chunk > client->config.buffer.rx_size) {
            chunk = client->config.buffer.rx_size;
        }

        ret = transport_read(client, client->read_buf, (int)chunk, client->config.network.timeout_ms);
        if (ret < 0) {
            return ret;
        }

        dispatch_publish_data(client, packet_flags, NULL, 0,
                              client->read_buf, chunk,
                              payload_len, payload_offset, packet_id);
        payload_offset += chunk;
    }

    ret = ack_publish(client, packet_flags, packet_id);
    return ret < 0 ? ret : MQTT_READ_STREAMED;
}

static int read_packet(qm_mqtt_client_handle_t client, uint8_t *packet_type, uint8_t **body, uint32_t *body_len, uint32_t timeout_ms)
{
    uint8_t first;
    uint8_t encoded;
    uint32_t multiplier = 1;
    uint32_t remaining_len = 0;
    int i;
    int ret;

    ret = transport_read(client, &first, 1, timeout_ms);
    if (ret < 0) {
        return ret;
    }

    for (i = 0; i < 4; i++) {
        ret = transport_read(client, &encoded, 1, client->config.network.timeout_ms);
        if (ret < 0) {
            return ret;
        }
        remaining_len += (uint32_t)(encoded & 127) * multiplier;
        if ((encoded & 128) == 0) {
            break;
        }
        multiplier *= 128;
    }
    if (i == 4) {
        return -QM_EINVAL;
    }

    if (remaining_len > client->config.buffer.rx_size) {
        if ((first >> 4) == MQTT_PKT_PUBLISH) {
            return read_publish_streamed(client, first & 0x0f, remaining_len);
        }

        ret = drain_bytes(client, remaining_len);
        if (ret < 0) {
            return ret;
        }
        dispatch_buffer_error(client, remaining_len);
        return -QM_EFULL;
    }

    if (remaining_len > 0) {
        ret = transport_read(client, client->read_buf, (int)remaining_len, client->config.network.timeout_ms);
        if (ret < 0) {
            return ret;
        }
    }

    *packet_type = first;
    *body = client->read_buf;
    *body_len = remaining_len;
    return QM_EOK;
}

static int wait_connack(qm_mqtt_client_handle_t client)
{
    uint8_t packet_type;
    uint8_t *body;
    uint32_t body_len;
    int ret;

    ret = read_packet(client, &packet_type, &body, &body_len, client->config.network.timeout_ms);
    if (ret != QM_EOK) {
        return ret;
    }
    if ((packet_type >> 4) != MQTT_PKT_CONNACK || body_len < 2) {
        return -QM_EIO;
    }
    if (body[1] != 0) {
        client->connack_return_code = body[1];
        return -QM_EIO;
    }

    client->connack_return_code = 0;
    client->session_present = (body[0] & 0x01) ? QM_TRUE : QM_FALSE;
    return QM_EOK;
}

static int resubscribe_all(qm_mqtt_client_handle_t client)
{
    int i;
    int ret = QM_EOK;

    qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
    for (i = 0; i < CONFIG_QM_MQTT_CLIENT_SUBSCRIBE_MAX; i++) {
        subscription_t *sub = &client->subscriptions[i];
        uint16_t packet_id;
        uint8_t *p;
        uint32_t body_len;

        if (!sub->used || !sub->topic) {
            continue;
        }

        packet_id = ++client->next_packet_id;
        if (packet_id == 0) {
            packet_id = ++client->next_packet_id;
        }

        p = client->packet_buf;
        p = write_u16(p, packet_id);
        p = write_string(p, sub->topic);
        *p++ = (uint8_t)sub->qos;
        body_len = (uint32_t)(p - client->packet_buf);

        ret = enqueue_packet_locked(client, OUTBOX_SUBSCRIBE, packet_id,
                                    (MQTT_PKT_SUBSCRIBE << 4) | 0x02,
                                    client->packet_buf, body_len);
        if (ret < 0) {
            break;
        }
    }
    qm_mutex_unlock(&client->lock);

    return ret < 0 ? ret : QM_EOK;
}

static int connect_broker(qm_mqtt_client_handle_t client)
{
    int ret;

    client->connack_return_code = 0;
    client->session_present = QM_FALSE;
    ret = transport_open(client);
    if (ret != QM_EOK) {
        QM_MQTT_CLIENT_LOGW(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE,
                            "connect transport failed ret=%d", ret);
        return ret;
    }
    ret = send_connect(client);
    if (ret == QM_EOK) {
        ret = wait_connack(client);
    }
    if (ret != QM_EOK) {
        transport_close(client);
        QM_MQTT_CLIENT_LOGW(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE,
                            "broker connection failed ret=%d connack=%u",
                            ret, (unsigned int)client->connack_return_code);
        return ret;
    }

    client->connected = QM_TRUE;
    qm_utils_time_countdown_ms(&client->ping_timer, client->config.session.keepalive_interval_ms);
    client->ping_outstanding = QM_FALSE;

    if (client->config.session.clean_session || !client->session_present) {
        ret = resubscribe_all(client);
        if (ret != QM_EOK) {
            transport_close(client);
            return ret;
        }
    }

    QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE,
                        "broker connected session_present=%u",
                        (unsigned int)client->session_present);
    return QM_EOK;
}

static subscription_t *find_subscription_exact(qm_mqtt_client_handle_t client, const char *topic)
{
    int i;

    if (!client || !topic) {
        return NULL;
    }

    for (i = 0; i < CONFIG_QM_MQTT_CLIENT_SUBSCRIBE_MAX; i++) {
        subscription_t *sub = &client->subscriptions[i];

        if (sub->used && sub->topic && qm_strcmp(sub->topic, topic) == 0) {
            return sub;
        }
    }

    return NULL;
}

static void clear_subscription(subscription_t *sub)
{
    if (!sub) {
        return;
    }
    if (sub->topic) {
        qm_free(sub->topic);
    }
    qm_memset(sub, 0, sizeof(*sub));
}

static subscription_t *alloc_subscription(qm_mqtt_client_handle_t client, const char *topic, qm_mqtt_qos_t qos)
{
    int i;

    for (i = 0; i < CONFIG_QM_MQTT_CLIENT_SUBSCRIBE_MAX; i++) {
        if (!client->subscriptions[i].used) {
            client->subscriptions[i].topic = qm_strdup(topic);
            if (!client->subscriptions[i].topic) {
                return NULL;
            }
            client->subscriptions[i].qos = qos;
            client->subscriptions[i].used = QM_TRUE;
            return &client->subscriptions[i];
        }
    }

    return NULL;
}

static void free_subscriptions(qm_mqtt_client_handle_t client)
{
    int i;

    for (i = 0; client && i < CONFIG_QM_MQTT_CLIENT_SUBSCRIBE_MAX; i++) {
        if (client->subscriptions[i].topic) {
            qm_free(client->subscriptions[i].topic);
        }
        qm_memset(&client->subscriptions[i], 0, sizeof(client->subscriptions[i]));
    }
}

static void handle_puback(qm_mqtt_client_handle_t client, uint16_t packet_id)
{
    qm_mqtt_event_t event;
    qm_bool found = outbox_remove(client, OUTBOX_PUBLISH, packet_id);

    qm_memset(&event, 0, sizeof(event));
    event.event_id = QM_MQTT_EVENT_PUBLISHED;
    event.client = client;
    event.user_context = client->config.user_context;
    event.packet_id = packet_id;
    event.error_type = found ? QM_MQTT_ERROR_NONE : QM_MQTT_ERROR_PROTOCOL;
    event.error_code = found ? QM_EOK : -QM_ENOENT;
    QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_CONTROL,
                        "publish ack packet_id=%u result=%d",
                        (unsigned int)packet_id, event.error_code);
    dispatch_event(client, &event);
}

static void handle_suback(qm_mqtt_client_handle_t client, const uint8_t *body, uint32_t body_len)
{
    qm_mqtt_event_t event;
    uint16_t packet_id;
    qm_bool failed;

    if (body_len < 3) {
        return;
    }

    packet_id = read_u16(body);
    failed = body[2] == 0x80 ? QM_TRUE : QM_FALSE;
    outbox_remove(client, OUTBOX_SUBSCRIBE, packet_id);

    qm_memset(&event, 0, sizeof(event));
    event.event_id = QM_MQTT_EVENT_SUBSCRIBED;
    event.client = client;
    event.user_context = client->config.user_context;
    event.packet_id = packet_id;
    event.qos = failed ? 0 : body[2];
    event.error_type = failed ? QM_MQTT_ERROR_PROTOCOL : QM_MQTT_ERROR_NONE;
    event.error_code = failed ? -QM_EIO : QM_EOK;
    QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_CONTROL,
                        "subscribe ack packet_id=%u result=%d",
                        (unsigned int)packet_id, event.error_code);
    dispatch_event(client, &event);
}

static void handle_unsuback(qm_mqtt_client_handle_t client, const uint8_t *body, uint32_t body_len)
{
    qm_mqtt_event_t event;
    uint16_t packet_id;
    qm_bool found;

    if (body_len < 2) {
        return;
    }

    packet_id = read_u16(body);
    found = outbox_remove(client, OUTBOX_UNSUBSCRIBE, packet_id);

    qm_memset(&event, 0, sizeof(event));
    event.event_id = QM_MQTT_EVENT_UNSUBSCRIBED;
    event.client = client;
    event.user_context = client->config.user_context;
    event.packet_id = packet_id;
    event.error_type = found ? QM_MQTT_ERROR_NONE : QM_MQTT_ERROR_PROTOCOL;
    event.error_code = found ? QM_EOK : -QM_ENOENT;
    QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_CONTROL,
                        "unsubscribe ack packet_id=%u result=%d",
                        (unsigned int)packet_id, event.error_code);
    dispatch_event(client, &event);
}

static void handle_publish(qm_mqtt_client_handle_t client, uint8_t packet_flags, uint8_t *body, uint32_t body_len)
{
    uint16_t topic_len;
    uint16_t packet_id = 0;
    uint8_t qos = (packet_flags >> 1) & 0x03;
    uint32_t offset;

    if (body_len < 2) {
        return;
    }
    topic_len = read_u16(body);
    if (body_len < (uint32_t)topic_len + 2) {
        return;
    }

    offset = 2 + topic_len;
    if (qos > 0) {
        if (body_len < offset + 2) {
            return;
        }
        packet_id = read_u16(body + offset);
        offset += 2;
    }

    dispatch_publish_data(client, packet_flags,
                          (const char *)body + 2, topic_len,
                          body + offset, body_len - offset,
                          body_len - offset, 0, packet_id);
    ack_publish(client, packet_flags, packet_id);
}

static int process_one_packet(qm_mqtt_client_handle_t client, uint32_t timeout_ms)
{
    uint8_t packet_type;
    uint8_t *body;
    uint32_t body_len;
    int ret;

    ret = read_packet(client, &packet_type, &body, &body_len, timeout_ms);
    if (ret == MQTT_READ_STREAMED) {
        return QM_EOK;
    }
    if (ret != QM_EOK) {
        return ret;
    }

    switch (packet_type >> 4) {
        case MQTT_PKT_PUBLISH:
            handle_publish(client, packet_type & 0x0f, body, body_len);
            break;
        case MQTT_PKT_PUBACK:
            if (body_len >= 2) {
                handle_puback(client, read_u16(body));
            }
            break;
        case MQTT_PKT_SUBACK:
            handle_suback(client, body, body_len);
            break;
        case MQTT_PKT_UNSUBACK:
            handle_unsuback(client, body, body_len);
            break;
        case MQTT_PKT_PINGRESP:
            client->ping_outstanding = QM_FALSE;
            break;
        default:
            break;
    }

    return QM_EOK;
}

static int send_ping_if_needed(qm_mqtt_client_handle_t client)
{
    if (!client->connected || client->config.session.keepalive_interval_ms == 0) {
        return QM_EOK;
    }
    if (!qm_utils_time_is_expired(&client->ping_timer)) {
        return QM_EOK;
    }
    if (client->ping_outstanding) {
        return -QM_ETIMEOUT;
    }

    client->ping_outstanding = QM_TRUE;
    qm_utils_time_countdown_ms(&client->ping_timer, client->config.session.keepalive_interval_ms);
    return send_empty_packet(client, MQTT_PKT_PINGREQ << 4);
}

static void client_task(void *arg)
{
    qm_mqtt_client_handle_t client = (qm_mqtt_client_handle_t)arg;

    QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE, "task started");

    while (!client->stop_requested) {
        int ret;

        if (!client->connected) {
            ret = connect_broker(client);
            if (ret == QM_EOK) {
                QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE, "connected event dispatched");
                dispatch_simple(client, QM_MQTT_EVENT_CONNECTED, QM_EOK);
            } else {
                QM_MQTT_CLIENT_LOGW(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE,
                                    "connect retry ret=%d", ret);
                dispatch_error(client,
                               client->connack_return_code ? QM_MQTT_ERROR_CONNECTION_REFUSED : QM_MQTT_ERROR_TRANSPORT,
                               client->connack_return_code ? client->connack_return_code : ret);
                qm_msleep(1000);
                continue;
            }
        }

        ret = process_send_queue(client);
        if (ret != QM_EOK) {
            QM_MQTT_CLIENT_LOGE(LOG_TAG, "send queue failed ret=%d", ret);
            transport_close(client);
            dispatch_simple(client, QM_MQTT_EVENT_DISCONNECTED, ret);
            dispatch_error(client, QM_MQTT_ERROR_TRANSPORT, ret);
            qm_msleep(200);
            continue;
        }

        ret = process_one_packet(client, CONFIG_QM_MQTT_CLIENT_YIELD_TIMEOUT_MS);
        if (ret == -QM_ETIMEOUT || ret == -QM_EFULL) {
            ret = send_ping_if_needed(client);
            if (ret == -QM_ETIMEOUT) {
                QM_MQTT_CLIENT_LOGW(LOG_TAG, QM_MQTT_CLIENT_LOG_CONTROL,
                                    "keepalive timeout");
                transport_close(client);
                dispatch_simple(client, QM_MQTT_EVENT_DISCONNECTED, ret);
                dispatch_error(client, QM_MQTT_ERROR_TIMEOUT, ret);
                qm_msleep(200);
                continue;
            }
        }

        if (ret != QM_EOK && ret != -QM_ETIMEOUT) {
            QM_MQTT_CLIENT_LOGE(LOG_TAG, "receive processing failed ret=%d", ret);
            transport_close(client);
            dispatch_simple(client, QM_MQTT_EVENT_DISCONNECTED, ret);
            dispatch_error(client, QM_MQTT_ERROR_TRANSPORT, ret);
            qm_msleep(200);
        }
    }

    if (client->connected) {
        send_empty_packet(client, MQTT_PKT_DISCONNECT << 4);
    }
    transport_close(client);

    qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
    client->task_running = QM_FALSE;
    client->task.hdl = NULL;
    qm_mutex_unlock(&client->lock);
    QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE, "task stopped");
    qm_task_exit(NULL);
}

static void free_config(qm_mqtt_client_handle_t client)
{
    if (!client) {
        return;
    }
    if (client->host) {
        qm_free(client->host);
    }
    if (client->client_id) {
        qm_free(client->client_id);
    }
    if (client->username) {
        qm_free(client->username);
    }
    if (client->password) {
        qm_free(client->password);
    }
}

static int copy_config(qm_mqtt_client_handle_t client, const qm_mqtt_client_config_t *config)
{
    if (!client || !config || !config->broker.host ||
        !config->credentials.client_id || config->broker.port == 0) {
        return -QM_EINVAL;
    }

    client->config = *config;
    client->host = qm_strdup(config->broker.host);
    client->client_id = qm_strdup(config->credentials.client_id);
    client->username = qm_strdup(config->credentials.username);
    client->password = qm_strdup(config->credentials.password);
    if (!client->host || !client->client_id ||
        (config->credentials.username && !client->username) ||
        (config->credentials.password && !client->password)) {
        free_config(client);
        return -QM_ENOMEM;
    }

    client->config.broker.host = client->host;
    client->config.credentials.client_id = client->client_id;
    client->config.credentials.username = client->username;
    client->config.credentials.password = client->password;
    if (config->broker.tls) {
        client->tls_config = *config->broker.tls;
        client->config.broker.tls = &client->tls_config;
    }

    if (client->config.task.stack_size <= 0) {
        client->config.task.stack_size = CONFIG_QM_MQTT_CLIENT_TASK_STACK_SIZE;
    }
    if (client->config.task.priority <= 0) {
        client->config.task.priority = CONFIG_QM_MQTT_CLIENT_TASK_PRIORITY;
    }
    if (client->config.buffer.rx_size == 0) {
        client->config.buffer.rx_size = CONFIG_QM_MQTT_CLIENT_RX_BUF_SIZE;
    }
    if (client->config.buffer.tx_size == 0) {
        client->config.buffer.tx_size = CONFIG_QM_MQTT_CLIENT_TX_BUF_SIZE;
    }
    if (client->config.network.timeout_ms == 0) {
        client->config.network.timeout_ms = CONFIG_QM_MQTT_CLIENT_REQUEST_TIMEOUT_MS;
    }
    if (client->config.session.keepalive_interval_ms == 0) {
        client->config.session.keepalive_interval_ms = CONFIG_QM_MQTT_CLIENT_KEEPALIVE_INTERVAL_MS;
    }
    if (client->config.buffer.tx_size < 16 || client->config.buffer.rx_size < 16) {
        return -QM_EINVAL;
    }

    return QM_EOK;
}

qm_mqtt_client_handle_t qm_mqtt_client_init(const qm_mqtt_client_config_t *config)
{
    qm_mqtt_client_handle_t client = (qm_mqtt_client_handle_t)qm_calloc(1, sizeof(*client));

    if (!client) {
        QM_MQTT_CLIENT_LOGE(LOG_TAG, "init rejected: null allocation");
        return NULL;
    }
    client->tcp_socket = -1;
    client->next_packet_id = 1;

    if (copy_config(client, config) != QM_EOK) {
        QM_MQTT_CLIENT_LOGE(LOG_TAG, "init rejected: invalid config");
        free_config(client);
        qm_free(client);
        return NULL;
    }

    client->read_buf = (uint8_t *)qm_malloc(client->config.buffer.rx_size);
    client->write_buf = (uint8_t *)qm_malloc(client->config.buffer.tx_size);
    client->packet_buf = (uint8_t *)qm_malloc(client->config.buffer.tx_size);
    if (!client->read_buf || !client->write_buf || !client->packet_buf) {
        QM_MQTT_CLIENT_LOGE(LOG_TAG, "init failed: buffer allocation");
        if (client->read_buf) {
            qm_free(client->read_buf);
        }
        if (client->write_buf) {
            qm_free(client->write_buf);
        }
        if (client->packet_buf) {
            qm_free(client->packet_buf);
        }
        free_config(client);
        qm_free(client);
        return NULL;
    }

    if (qm_mutex_new(&client->lock) != QM_EOK) {
        QM_MQTT_CLIENT_LOGE(LOG_TAG, "init failed: mutex allocation");
        qm_free(client->read_buf);
        qm_free(client->write_buf);
        qm_free(client->packet_buf);
        free_config(client);
        qm_free(client);
        return NULL;
    }
    client->lock_ready = QM_TRUE;
    QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE,
                        "initialized tls=%u rx=%lu tx=%lu",
                        (unsigned int)(client->config.broker.tls ? QM_TRUE : QM_FALSE),
                        (unsigned long)client->config.buffer.rx_size,
                        (unsigned long)client->config.buffer.tx_size);
    return client;
}

int qm_mqtt_client_start(qm_mqtt_client_handle_t client)
{
    if (!client) {
        return -QM_EINVAL;
    }

    QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE, "start requested");

    qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
    if (client->task_running) {
        QM_MQTT_CLIENT_LOGW(LOG_TAG, QM_MQTT_CLIENT_LOG_CONTROL, "start rejected: task already running");
        qm_mutex_unlock(&client->lock);
        return -QM_EBUSY;
    }
    client->stop_requested = QM_FALSE;
    client->task_running = QM_TRUE;
    qm_mutex_unlock(&client->lock);

    if (qm_task_new(&client->task, "qm_mqtt_client", client_task, client,
                    client->config.task.stack_size, client->config.task.priority) != QM_EOK) {
        qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
        client->task_running = QM_FALSE;
        qm_mutex_unlock(&client->lock);
        QM_MQTT_CLIENT_LOGE(LOG_TAG, "task creation failed");
        return -QM_EIO;
    }

    QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE, "start accepted");
    return QM_EOK;
}

int qm_mqtt_client_stop(qm_mqtt_client_handle_t client)
{
    int waited = 0;

    if (!client) {
        return -QM_EINVAL;
    }

    QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE, "stop requested");

    qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
    client->stop_requested = QM_TRUE;
    qm_mutex_unlock(&client->lock);

    while (waited < CONFIG_QM_MQTT_CLIENT_STOP_WAIT_MS) {
        qm_bool running;

        qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
        running = client->task_running;
        qm_mutex_unlock(&client->lock);
        if (!running) {
            QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE, "stopped");
            return QM_EOK;
        }
        qm_msleep(20);
        waited += 20;
    }

    QM_MQTT_CLIENT_LOGW(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE, "stop timeout");
    return -QM_ETIMEOUT;
}

int qm_mqtt_client_destroy(qm_mqtt_client_handle_t client)
{
    int ret;

    if (!client) {
        return -QM_EINVAL;
    }

    QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE, "destroy requested");

    ret = qm_mqtt_client_stop(client);
    if (ret != QM_EOK) {
        return ret;
    }
    transport_close(client);
    free_subscriptions(client);
    free_config(client);
    if (client->read_buf) {
        qm_free(client->read_buf);
    }
    if (client->write_buf) {
        qm_free(client->write_buf);
    }
    if (client->packet_buf) {
        qm_free(client->packet_buf);
    }
    if (client->lock_ready) {
        qm_mutex_free(&client->lock);
    }
    qm_free(client);
    QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_LIFECYCLE, "destroyed");
    return QM_EOK;
}

int qm_mqtt_client_subscribe(qm_mqtt_client_handle_t client, const char *topic, qm_mqtt_qos_t qos)
{
    uint8_t *p;
    uint16_t packet_id;
    uint32_t body_len;
    subscription_t *sub;
    qm_bool created = QM_FALSE;
    int ret;

    if (!client || !topic || qos > QM_MQTT_QOS1 || !client->connected) {
        return -QM_EINVAL;
    }
    if (qm_strlen(topic) + 7 > client->config.buffer.tx_size) {
        QM_MQTT_CLIENT_LOGW(LOG_TAG, QM_MQTT_CLIENT_LOG_CONTROL,
                            "subscribe rejected: topic_len=%lu",
                            (unsigned long)qm_strlen(topic));
        return -QM_EFULL;
    }

    packet_id = next_packet_id(client);

    qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
    sub = find_subscription_exact(client, topic);
    if (!sub) {
        sub = alloc_subscription(client, topic, qos);
        created = sub ? QM_TRUE : QM_FALSE;
    } else {
        sub->qos = qos;
    }
    if (!sub) {
        qm_mutex_unlock(&client->lock);
        return -QM_EFULL;
    }

    p = client->packet_buf;
    p = write_u16(p, packet_id);
    p = write_string(p, topic);
    *p++ = (uint8_t)qos;
    body_len = (uint32_t)(p - client->packet_buf);

    ret = enqueue_packet_locked(client, OUTBOX_SUBSCRIBE, packet_id,
                                (MQTT_PKT_SUBSCRIBE << 4) | 0x02,
                                client->packet_buf, body_len);
    if (ret < 0 && created) {
        clear_subscription(sub);
    }
    qm_mutex_unlock(&client->lock);
    if (ret < 0) {
        QM_MQTT_CLIENT_LOGW(LOG_TAG, QM_MQTT_CLIENT_LOG_UPLINK,
                            "subscribe enqueue failed ret=%d topic_len=%lu",
                            ret, (unsigned long)qm_strlen(topic));
    } else {
        QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_UPLINK,
                            "subscribe queued topic_len=%lu qos=%u packet_id=%u",
                            (unsigned long)qm_strlen(topic), (unsigned int)qos,
                            (unsigned int)packet_id);
    }
    return ret < 0 ? ret : packet_id;
}

int qm_mqtt_client_unsubscribe(qm_mqtt_client_handle_t client, const char *topic)
{
    uint8_t *p;
    uint16_t packet_id;
    uint32_t body_len;
    subscription_t *sub;
    int ret;

    if (!client || !topic || !client->connected) {
        return -QM_EINVAL;
    }

    packet_id = next_packet_id(client);

    qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
    p = client->packet_buf;
    p = write_u16(p, packet_id);
    p = write_string(p, topic);
    body_len = (uint32_t)(p - client->packet_buf);

    ret = enqueue_packet_locked(client, OUTBOX_UNSUBSCRIBE, packet_id,
                                (MQTT_PKT_UNSUBSCRIBE << 4) | 0x02,
                                client->packet_buf, body_len);
    qm_mutex_unlock(&client->lock);
    if (ret >= 0) {
        qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
        sub = find_subscription_exact(client, topic);
        clear_subscription(sub);
        qm_mutex_unlock(&client->lock);
    }
    if (ret < 0) {
        QM_MQTT_CLIENT_LOGW(LOG_TAG, QM_MQTT_CLIENT_LOG_UPLINK,
                            "unsubscribe enqueue failed ret=%d topic_len=%lu",
                            ret, (unsigned long)qm_strlen(topic));
    } else {
        QM_MQTT_CLIENT_LOGI(LOG_TAG, QM_MQTT_CLIENT_LOG_UPLINK,
                            "unsubscribe queued topic_len=%lu packet_id=%u",
                            (unsigned long)qm_strlen(topic), (unsigned int)packet_id);
    }
    return ret < 0 ? ret : packet_id;
}

int qm_mqtt_client_publish(qm_mqtt_client_handle_t client, const char *topic, const void *payload,
                           uint32_t payload_len, qm_mqtt_qos_t qos, qm_bool retain)
{
    uint8_t *p;
    uint16_t packet_id = 0;
    uint32_t body_len;
    uint8_t flags;
    int ret;

    if (!client || !topic || qos > QM_MQTT_QOS1 || (payload_len > 0 && !payload) || !client->connected) {
        return -QM_EINVAL;
    }
    if (qm_strlen(topic) + payload_len + (qos > QM_MQTT_QOS0 ? 9 : 7) > client->config.buffer.tx_size) {
        QM_MQTT_CLIENT_LOGW(LOG_TAG, QM_MQTT_CLIENT_LOG_CONTROL,
                            "publish rejected: topic_len=%lu payload_len=%lu",
                            (unsigned long)qm_strlen(topic), (unsigned long)payload_len);
        return -QM_EFULL;
    }

    if (qos > QM_MQTT_QOS0) {
        packet_id = next_packet_id(client);
    }

    qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
    p = client->packet_buf;
    p = write_string(p, topic);
    if (qos > QM_MQTT_QOS0) {
        p = write_u16(p, packet_id);
    }
    if (payload_len > 0) {
        qm_memcpy(p, payload, payload_len);
        p += payload_len;
    }
    body_len = (uint32_t)(p - client->packet_buf);
    flags = (uint8_t)((MQTT_PKT_PUBLISH << 4) | ((uint8_t)qos << 1) | (retain ? 1 : 0));

    ret = enqueue_packet_locked(client, qos > QM_MQTT_QOS0 ? OUTBOX_PUBLISH : 0,
                                packet_id, flags, client->packet_buf, body_len);
    qm_mutex_unlock(&client->lock);
    if (ret < 0) {
        QM_MQTT_CLIENT_LOGW(LOG_TAG, QM_MQTT_CLIENT_LOG_UPLINK,
                            "publish enqueue failed ret=%d topic_len=%lu payload_len=%lu",
                            ret, (unsigned long)qm_strlen(topic), (unsigned long)payload_len);
        return ret;
    }
    QM_MQTT_CLIENT_LOGD(LOG_TAG, QM_MQTT_CLIENT_LOG_PAYLOAD,
                        "publish queued topic_len=%lu payload_len=%lu qos=%u packet_id=%u",
                        (unsigned long)qm_strlen(topic), (unsigned long)payload_len,
                        (unsigned int)qos, (unsigned int)packet_id);
    return qos > QM_MQTT_QOS0 ? packet_id : QM_EOK;
}

qm_bool qm_mqtt_client_is_connected(qm_mqtt_client_handle_t client)
{
    return client && client->connected ? QM_TRUE : QM_FALSE;
}

int qm_mqtt_client_set_user_context(qm_mqtt_client_handle_t client, void *user_context)
{
    if (!client) {
        return -QM_EINVAL;
    }
    client->config.user_context = user_context;
    return QM_EOK;
}

void *qm_mqtt_client_get_user_context(qm_mqtt_client_handle_t client)
{
    return client ? client->config.user_context : NULL;
}
