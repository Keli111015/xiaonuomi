#ifndef _IOT_MQTT_CLIENT_H_
#define _IOT_MQTT_CLIENT_H_
#if defined(__cplusplus)
extern "C" {
#endif

#include "qm.h"
#include "util_net.h"
#include "qm_utils_timer.h"
#include "qm_kernel.h"
#include "MQTTPacket/MQTTPacket.h"
#include "qm_utils_list.h"
#include "iot_mqtt.h"

#ifndef IOT_MC_SUB_NUM_MAX
/* maximum number of successful subscribe */
#define IOT_MC_SUB_NUM_MAX                     (20)
#endif

#ifndef CONFIG_IOT_MQTT_WRITE_TIMEOUT
/* maximum number of successful subscribe */
#define CONFIG_IOT_MQTT_WRITE_TIMEOUT          (100)
#endif

#ifndef IOT_MC_SUB_REQUEST_NUM_MAX
/* maximum number of simultaneously invoke subscribe request */
#define IOT_MC_SUB_REQUEST_NUM_MAX             (30)
#endif

/* maximum republish elements in list */
#define IOT_MC_REPUB_NUM_MAX                   (8)

/* MQTT client version number */
#define IOT_MC_MQTT_VERSION                    (4)

/* maximum length of topic name in byte */
#define IOT_MC_TOPIC_NAME_MAX_LEN              (128)

/* maximum MQTT packet-id */
#define IOT_MC_PACKET_ID_MAX                   (65535)

/* Minimum interval of MQTT reconnect in millisecond */
#define IOT_MC_RECONNECT_INTERVAL_MIN_MS       (1000)

/* Maximum interval of MQTT reconnect in millisecond */
#define IOT_MC_RECONNECT_INTERVAL_MAX_MS       (60000)

/* Minimum timeout interval of MQTT request in millisecond */
#define IOT_MC_REQUEST_TIMEOUT_MIN_MS          (500)

/* Maximum timeout interval of MQTT request in millisecond */
#define IOT_MC_REQUEST_TIMEOUT_MAX_MS          (5000)

/* Default timeout interval of MQTT request in millisecond */
#define IOT_MC_REQUEST_TIMEOUT_DEFAULT_MS      (2000)

/* Max times of keepalive which has been send and did not received response package */
#define IOT_MC_KEEPALIVE_PROBE_MAX             (3)


typedef enum {
    IOT_MC_CONNECTION_ACCEPTED = 0,
    IOT_MC_CONNECTION_REFUSED_UNACCEPTABLE_PROTOCOL_VERSION = 1,
    IOT_MC_CONNECTION_REFUSED_IDENTIFIER_REJECTED = 2,
    IOT_MC_CONNECTION_REFUSED_SERVER_UNAVAILABLE = 3,
    IOT_MC_CONNECTION_REFUSED_BAD_USERDATA = 4,
    IOT_MC_CONNECTION_REFUSED_NOT_AUTHORIZED = 5
} iot_mc_connect_ack_code_t;


/* State of MQTT client */
typedef enum {
    IOT_MC_STATE_INVALID = 0,                    /* MQTT in invalid state */
    IOT_MC_STATE_INITIALIZED = 1,                /* MQTT in initializing state */
    IOT_MC_STATE_CONNECTED = 2,                  /* MQTT in connected state */
    IOT_MC_STATE_DISCONNECTED = 3,               /* MQTT in disconnected state */
    IOT_MC_STATE_DISCONNECTED_RECONNECTING = 4,  /* MQTT in reconnecting state */
} iot_mc_state_t;


typedef enum MQTT_NODE_STATE {
    IOT_MC_NODE_STATE_NORMANL = 0,
    IOT_MC_NODE_STATE_INVALID,
} iot_mc_node_t;


/* Handle structure of subscribed topic */
typedef struct {
    const char *topic_filter;
    iot_mqtt_event_handle_t handle;
} iot_mc_topic_handle_t;


/* Information structure of subscribed topic */
typedef struct SUBSCRIBE_INFO {
    enum msgTypes           type;            /* type, (sub or unsub) */
    qm_utils_time_t         sub_start_time;  /* start time of subscribe request */
    iot_mc_node_t           node_state;      /* state of this node */
    uint16_t                msg_id;          /* packet id of subscribe(unsubcribe) */
    int                     count;           /* count of topic subscribed(unsubcribed)*/
    iot_mc_topic_handle_t   *handler;      /* handle of topic subscribed(unsubcribed) */
    uint16_t                len;             /* length of subscribe message */
    unsigned char           *buf;            /* subscribe message */
} iot_mc_subsribe_info_t, *iot_mc_subsribe_info_pt;


/* Information structure of published topic */
typedef struct REPUBLISH_INFO {
    qm_utils_time_t         pub_start_time;     /* start time of publish request */
    iot_mc_node_t           node_state;         /* state of this node */
    uint16_t                msg_id;             /* packet id of publish */
    uint32_t                len;                /* length of publish message */
    unsigned char           *buf;               /* publish message */
    iot_mqtt_event_handle_t handle_event;       /* event handle */
} iot_mc_pub_info_t, *iot_mc_pub_info_pt;


/* Reconnected parameter of MQTT client */
typedef struct {
    qm_utils_time_t         reconnect_next_time;         /* the next time point of reconnect */
    uint32_t                reconnect_time_interval_ms;  /* time interval of this reconnect */
} iot_mc_reconnect_param_t;

/* structure of MQTT client */
typedef struct client {
    qm_mutex_t                      lock_generic;                            /* generic lock */
    int                             yield_timeout;
    uint32_t                        packet_id;                               /* packet id */
    uint32_t                        request_timeout_ms;                      /* request timeout in millisecond */
    uint32_t                        buf_size_send;                           /* send buffer size in byte */
    uint32_t                        buf_size_read;                           /* read buffer size in byte */
    uint8_t                         keepalive_probes;                        /* keepalive probes */
    char                            *buf_send;                                /* pointer of send buffer */
    char                            *buf_read;                                /* pointer of read buffer */
    iot_mc_topic_handle_t           sub_handle[IOT_MC_SUB_NUM_MAX];           /* array of subscribe handle */
    util_network_pt                 ipstack;                                 /* network parameter */
    qm_utils_time_t                 next_ping_time;                          /* next ping time */
    int                             ping_mark;                               /* flag of ping */
    iot_mc_state_t                  client_state;                            /* state of MQTT client */
    iot_mc_reconnect_param_t        reconnect_param;                         /* reconnect parameter */
    MQTTPacket_connectData          connect_data;                            /* connection parameter */
    uint32_t                        req_sub_id;                              /* request id of subscribe */
    uint32_t                        rsp_sub_id;                              /* response id of subscribe */
    int                             is_sub_timeout;
    uint32_t                        req_pub_id;                              /* request id of publish */
    uint32_t                        rsp_pub_id;                              /* response id of publish */
    int                             is_pub_timeout;
    qm_list_t                       *list_pub_wait_ack;                      /* list of wait publish ack */
    qm_list_t                       *list_sub_wait_ack;                      /* list of subscribe or unsubscribe ack */
    qm_mutex_t                      lock_list_pub;                           /* lock of list of wait publish ack */
    qm_mutex_t                      lock_list_sub;                           /* lock of list of subscribe or unsubscribe ack */
    qm_mutex_t                      lock_write_buf;                          /* lock of write */
    qm_sem_t                        yield_wait;                              /* timed wait between failed yield cycles */
    iot_mqtt_event_handle_t         handle_event;                            /* event handle */
    int (*mqtt_auth)(void);
    int (*mqtt_up_process)(char *topic, iot_mqtt_topic_info_pt topic_msg);  /* process function before mqtt publish */
    int (*mqtt_down_process)(iot_mqtt_topic_info_pt topic_msg);             /* process function while received mqtt publish */
} iot_mc_client_t, *iot_mc_client_pt;

typedef enum {
    TOPIC_NAME_TYPE = 0,
    TOPIC_FILTER_TYPE
} iot_mc_topic_type_t;


#if defined(__cplusplus)
}
#endif
#endif  /* #ifndef _IOT_MQTT_CLIENT_H_ */
