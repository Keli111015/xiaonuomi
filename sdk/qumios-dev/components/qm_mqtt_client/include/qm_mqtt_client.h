#ifndef QM_MQTT_CLIENT_H
#define QM_MQTT_CLIENT_H

#include "qm_errno.h"
#include "qm_types.h"
#include "qm_mqtt_client_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct qm_mqtt_client *qm_mqtt_client_handle_t;

typedef enum {
    QM_MQTT_QOS0 = 0,
    QM_MQTT_QOS1 = 1,
} qm_mqtt_qos_t;

typedef enum {
    QM_MQTT_EVENT_CONNECTED = 0,
    QM_MQTT_EVENT_DISCONNECTED,
    QM_MQTT_EVENT_SUBSCRIBED,
    QM_MQTT_EVENT_UNSUBSCRIBED,
    QM_MQTT_EVENT_PUBLISHED,
    QM_MQTT_EVENT_DATA,
    QM_MQTT_EVENT_ERROR,
} qm_mqtt_event_id_t;

typedef enum {
    QM_MQTT_ERROR_NONE = 0,
    QM_MQTT_ERROR_TRANSPORT,
    QM_MQTT_ERROR_CONNECTION_REFUSED,
    QM_MQTT_ERROR_PROTOCOL,
    QM_MQTT_ERROR_TIMEOUT,
    QM_MQTT_ERROR_BUFFER,
} qm_mqtt_error_type_t;

typedef struct {
    const char *server_crt;
    int server_crt_len;
    const char *client_crt;
    int client_crt_len;
    const char *client_key;
    int client_key_len;
} qm_mqtt_tls_config_t;

typedef struct {
    qm_mqtt_event_id_t event_id;
    qm_mqtt_client_handle_t client;
    void *user_context;
    const char *topic;
    uint16_t topic_len;
    const char *data;
    uint32_t data_len;
    uint8_t qos;
    uint8_t retain;
    uint8_t dup;
    uint16_t packet_id;
    uint32_t total_data_len;
    uint32_t current_data_offset;
    qm_bool session_present;
    qm_mqtt_error_type_t error_type;
    int error_code;
} qm_mqtt_event_t;

typedef void (*qm_mqtt_event_cb_t)(const qm_mqtt_event_t *event);

typedef struct {
    const char *host;
    uint16_t port;
    const qm_mqtt_tls_config_t *tls;
} qm_mqtt_broker_config_t;

typedef struct {
    const char *client_id;
    const char *username;
    const char *password;
} qm_mqtt_credentials_config_t;

typedef struct {
    qm_bool clean_session;
    uint32_t keepalive_interval_ms;
} qm_mqtt_session_config_t;

typedef struct {
    uint32_t timeout_ms;
} qm_mqtt_network_config_t;

typedef struct {
    uint32_t rx_size;
    uint32_t tx_size;
} qm_mqtt_buffer_config_t;

typedef struct {
    int stack_size;
    int priority;
} qm_mqtt_task_config_t;

typedef struct {
    qm_mqtt_broker_config_t broker;
    qm_mqtt_credentials_config_t credentials;
    qm_mqtt_session_config_t session;
    qm_mqtt_network_config_t network;
    qm_mqtt_buffer_config_t buffer;
    qm_mqtt_task_config_t task;
    void *user_context;
    qm_mqtt_event_cb_t on_event;
} qm_mqtt_client_config_t;

qm_mqtt_client_handle_t qm_mqtt_client_init(const qm_mqtt_client_config_t *config);
int qm_mqtt_client_start(qm_mqtt_client_handle_t client);
int qm_mqtt_client_stop(qm_mqtt_client_handle_t client);
int qm_mqtt_client_destroy(qm_mqtt_client_handle_t client);

int qm_mqtt_client_subscribe(qm_mqtt_client_handle_t client, const char *topic, qm_mqtt_qos_t qos);
int qm_mqtt_client_unsubscribe(qm_mqtt_client_handle_t client, const char *topic);

int qm_mqtt_client_publish(qm_mqtt_client_handle_t client, const char *topic, const void *payload,
                           uint32_t payload_len, qm_mqtt_qos_t qos, qm_bool retain);

qm_bool qm_mqtt_client_is_connected(qm_mqtt_client_handle_t client);

int qm_mqtt_client_set_user_context(qm_mqtt_client_handle_t client, void *user_context);
void *qm_mqtt_client_get_user_context(qm_mqtt_client_handle_t client);

#ifdef __cplusplus
}
#endif

#endif
