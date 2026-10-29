
#ifndef _IOT_EXPORT_MQTT_H_
#define _IOT_EXPORT_MQTT_H_
 
#include "qm_types.h"


/* From mqtt_client.h */
typedef enum {
    IOT_MQTT_QOS0 = 0,
    IOT_MQTT_QOS1,
    IOT_MQTT_QOS2
} iot_mqtt_qos_t;

typedef enum {

    /* Undefined event */
    IOT_MQTT_EVENT_UNDEF = 0,

    /* MQTT disconnect event */
    IOT_MQTT_EVENT_DISCONNECT = 1,

    /* MQTT reconnect event */
    IOT_MQTT_EVENT_RECONNECT = 2,

    /* A ACK to the specific subscribe which specify by packet-id be received */
    IOT_MQTT_EVENT_SUBCRIBE_SUCCESS = 3,

    /* No ACK to the specific subscribe which specify by packet-id be received in timeout period */
    IOT_MQTT_EVENT_SUBCRIBE_TIMEOUT = 4,

    /* A failed ACK to the specific subscribe which specify by packet-id be received*/
    IOT_MQTT_EVENT_SUBCRIBE_NACK = 5,

    /* A ACK to the specific unsubscribe which specify by packet-id be received */
    IOT_MQTT_EVENT_UNSUBCRIBE_SUCCESS = 6,

    /* No ACK to the specific unsubscribe which specify by packet-id be received in timeout period */
    IOT_MQTT_EVENT_UNSUBCRIBE_TIMEOUT = 7,

    /* A failed ACK to the specific unsubscribe which specify by packet-id be received*/
    IOT_MQTT_EVENT_UNSUBCRIBE_NACK = 8,

    /* A ACK to the specific publish which specify by packet-id be received */
    IOT_MQTT_EVENT_PUBLISH_SUCCESS = 9,

    /* No ACK to the specific publish which specify by packet-id be received in timeout period */
    IOT_MQTT_EVENT_PUBLISH_TIMEOUT = 10,

    /* A failed ACK to the specific publish which specify by packet-id be received*/
    IOT_MQTT_EVENT_PUBLISH_NACK = 11,

    /* MQTT packet published from MQTT remote broker be received */
    IOT_MQTT_EVENT_PUBLISH_RECVEIVED = 12,

    /* MQTT packet buffer overflow which the remaining space less than to receive byte */
    IOT_MQTT_EVENT_BUFFER_OVERFLOW = 13,
} iot_mqtt_event_type_t;

/* topic information */
typedef struct {
    uint16_t        packet_id;
    uint8_t         qos;
    uint8_t         dup;
    uint8_t         retain;
    uint16_t        topic_len;
    uint16_t        payload_len;
    char     *ptopic;
    char     *payload;
} iot_mqtt_topic_info_t, *iot_mqtt_topic_info_pt;

typedef struct {

    /* Specify the event type */
    iot_mqtt_event_type_t  event_type;

    /*
     * Specify the detail event information. @msg means different to different event types:
     *
     * 1) IOT_MQTT_EVENT_UNKNOWN,
     *    IOT_MQTT_EVENT_DISCONNECT,
     *    IOT_MQTT_EVENT_RECONNECT :
     *      Its data type is string and the value is detail information.
     *
     * 2) IOT_MQTT_EVENT_SUBCRIBE_SUCCESS,
     *    IOT_MQTT_EVENT_SUBCRIBE_TIMEOUT,
     *    IOT_MQTT_EVENT_SUBCRIBE_NACK,
     *    IOT_MQTT_EVENT_UNSUBCRIBE_SUCCESS,
     *    IOT_MQTT_EVENT_UNSUBCRIBE_TIMEOUT,
     *    IOT_MQTT_EVENT_UNSUBCRIBE_NACK
     *    IOT_MQTT_EVENT_PUBLISH_SUCCESS,
     *    IOT_MQTT_EVENT_PUBLISH_TIMEOUT,
     *    IOT_MQTT_EVENT_PUBLISH_NACK :
     *      Its data type is @uint32_t and the value is MQTT packet identifier.
     *
     * 3) IOT_MQTT_EVENT_PUBLISH_RECVEIVED:
     *      Its data type is @iot_mqtt_packet_info_t and see detail at the declare of this type.
     *
     * */
    void *msg;
} iot_mqtt_event_msg_t, *iot_mqtt_event_msg_pt;


/**
 * @brief It define a datatype of function pointer.
 *        This type of function will be called when a related event occur.
 *
 * @param pcontext : The program context.
 * @param pclient : The MQTT client.
 * @param msg : The event message.
 *
 * @return none
 */
typedef void (*iot_mqtt_event_handle_func_fpt)(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg);


/* The structure of MQTT event handle */
typedef struct {
    iot_mqtt_event_handle_func_fpt     h_fp;
    void                               *pcontext;
} iot_mqtt_event_handle_t, *iot_mqtt_event_handle_pt;


/**
 * Topic definition struct
 */
typedef struct topic_t {
    const char                     *topic_filter;  /*!< Topic filter  to subscribe */
    iot_mqtt_qos_t                 qos;            /*!< Max QoS level of the subscription */
    void                           *pcontext;
    iot_mqtt_event_handle_func_fpt topic_handle_func;
} iot_mqtt_topic_t;


/* The structure of MQTT initial parameter */
typedef struct {

    uint16_t                    port;                   /* Specify MQTT broker port */
    const char                 *host;                   /* Specify MQTT broker host */
    const char                 *client_id;              /* Specify MQTT connection client id*/
    const char                 *username;               /* Specify MQTT user name */
    const char                 *password;               /* Specify MQTT password */

    const char                  *server_crt;            /*server root certificate*/
    const char                  *client_crt;             /*Client certificate*/
    const char                  *client_key;             /*Client certificate's private key*/

    uint8_t                     clean_session;            /* Specify MQTT clean session or not*/
    uint32_t                    request_timeout_ms;       /* Specify timeout of a MQTT request in millisecond */
    uint32_t                    keepalive_interval_ms;    /* Specify MQTT keep-alive interval in millisecond */

    char                       *pwrite_buf;               /* Specify write-buffer */
    uint32_t                    write_buf_size;           /* Specify size of write-buffer in byte */
    char                       *pread_buf;                /* Specify read-buffer */
    uint32_t                    read_buf_size;            /* Specify size of read-buffer in byte */

    iot_mqtt_event_handle_t    handle_event;              /* Specify MQTT event handle */

} iot_mqtt_param_t, *iot_mqtt_param_pt;

/** @defgroup group_api api
 *  @{
 */

/** @defgroup group_api_mqtt mqtt
 *  @{
 */

/**
 * @brief Construct the MQTT client
 *        This function initialize the data structures, establish MQTT connection.
 *
 * @param [in] params: specify the MQTT client parameter.
 *
 * @retval     NULL : Construct failed.
 * @retval NOT_NULL : The handle of MQTT client.
 * @see None.
 */
void *iot_mqtt_client_construct(iot_mqtt_param_t *params);
int iot_mqtt_client_last_error(void);
/**
 * @brief Deconstruct the MQTT client
 *        This function disconnect MQTT connection and release the related resource.
 *
 * @param [in] phandle: pointer of handle, specify the MQTT client.
 *
 * @retval  0 : Deconstruct success.
 * @retval <0 : Deconstruct failed.
 * @see None.
 */
int iot_mqtt_client_destroy(void **phandle);

/**
 * @brief Handle MQTT packet from remote server and process timeout request
 *        which include the MQTT subscribe, unsubscribe, publish(QOS >= 1), reconnect, etc..
 *
 * @param [in] handle: specify the MQTT client.
 * @param [in] timeout_ms: specify the timeout in millisecond in this loop.
 *
 * @return status.
 * @see None.
 */
int iot_mqtt_client_yield(void *handle, int timeout_ms);

/**
 * @brief check whether MQTT connection is established or not.
 *
 * @param [in] handle: specify the MQTT client.
 *
 * @retval true  : MQTT in normal state.
 * @retval false : MQTT in abnormal state.
 * @see None.
 */
int iot_mqtt_client_check_state_normal(void *handle);

/**
 * @brief Subscribe MQTT topic.
 *
 * @param [in] handle: specify the MQTT client.
 * @param [in] topic_filter: specify the topic filter.
 * @param [in] qos: specify the MQTT Requested QoS.
 * @param [in] topic_handle_func: specify the topic handle callback-function.
 * @param [in] pcontext: specify context. When call 'topic_handle_func', it will be passed back.
 *
 * @retval -1  : Subscribe failed.
 * @retval >0 : Subscribe successful.
          The value is a unique ID of this request.
          The ID will be passed back when callback 'iot_mqtt_param_t:handle_event'.
 * @see None.
 */
int iot_mqtt_client_subscribe(void *handle,
                       const char *topic_filter,
                       iot_mqtt_qos_t qos,
                       iot_mqtt_event_handle_func_fpt topic_handle_func,
                       void *pcontext);


/**
 * @brief Subscribe MQTT topic wait ack.
 *
 * @param [in] handle: specify the MQTT client.
 * @param [in] topic_filter: specify the topic filter.
 * @param [in] qos: specify the MQTT Requested QoS.
 * @param [in] topic_handle_func: specify the topic handle callback-function.
 * @param [in] pcontext: specify context. When call 'topic_handle_func', it will be passed back.
 *
 * @retval -1  : Subscribe failed.
 * @retval =0 : Subscribe successful.
 * @see None.
 */
int iot_mqtt_client_subscribe_wait_ack(void *handle,
                       const char *topic_filter,
                       iot_mqtt_qos_t qos,
                       iot_mqtt_event_handle_func_fpt topic_handle_func,
                       void *pcontext);

/**
 * @brief Subscribe the client to a list of defined topics with defined qos
 *
 *
 * @param client    *MQTT* client handle
 * @param topic_list List of topics to subscribe
 * @param count count of topic_list
 *
 * @retval -1  : Subscribe failed.
 * @retval >0 : Subscribe successful.
          The value is a unique ID of this request.
          The ID will be passed back when callback 'iot_mqtt_param_t:handle_event'.
 * @see None.
 */
int iot_mqtt_client_multi_subscribe(void *handle, iot_mqtt_topic_t *topic_list, int count);

/**
 * @brief Subscribe the client to a list of defined topics with defined qos
 *
 *
 * @param client    *MQTT* client handle
 * @param topic_list List of topics to subscribe
 * @param count count of topic_list
 *
 * @retval -1  : Subscribe failed.
 * @retval >0 : Subscribe successful.
          The value is a unique ID of this request.
          The ID will be passed back when callback 'iot_mqtt_param_t:handle_event'.
 * @see None.
 */
int iot_mqtt_client_multi_subscribe_wait_ack(void *handle, iot_mqtt_topic_t *topic_list, int count);

/**
 * @brief Unsubscribe MQTT topic.
 *
 * @param [in] handle: specify the MQTT client.
 * @param [in] topic_filter: specify the topic filter.
 *
 * @retval -1  : Unsubscribe failed.
 * @retval >=0 : Unsubscribe successful.
          The value is a unique ID of this request.
          The ID will be passed back when callback 'iot_mqtt_param_t:handle_event'.
 * @see None.
 */
int iot_mqtt_client_unsubscribe(void *handle, const char *topic_filter);

/**
 * @brief Unsubscribe the client to a list of defined topics with defined qos
 *
 * @param [in] handle: specify the MQTT client.
 * @param [in] topic_filter: specify the topic filter.
 * @param count count of topic_list
 *
 * @retval -1  : Unsubscribe failed.
 * @retval >=0 : Unsubscribe successful.
          The value is a unique ID of this request.
          The ID will be passed back when callback 'iot_mqtt_param_t:handle_event'.
 * @see None.
 */
int iot_mqtt_client_multi_unsubscribe(void *handle, const char *topic_filter[], int count);

/**
 * @brief Publish message to specific topic.
 *
 * @param [in] handle: specify the MQTT client.
 * @param [in] topic_name: specify the topic name.
 * @param [in] data:      payload string (set to NULL, sending empty payload message)
 * @param [in] len:       data length, if set to 0, length is calculated from payload
 * @param [in] qos:       QoS of publish message
 * @param [in] retain:    retain flag
 *
 * @retval -1 :  Publish failed.
 * @retval >0 :  Publish successful.
        The value is a unique ID of this request.
        The ID will be passed back when callback 'iot_mqtt_param_t:handle_event'.
 * @see None.
 */
int iot_mqtt_client_publish(void *handle, const char *topic_name, const char *data, int len, int qos, int retain);

/**
 * @brief Publish message to specific topic.
 *
 * @param [in] handle: specify the MQTT client.
 * @param [in] topic_name: specify the topic name.
 * @param [in] data:      payload string (set to NULL, sending empty payload message)
 * @param [in] len:       data length, if set to 0, length is calculated from payload
 * @param [in] qos:       QoS of publish message
 * @param [in] retain:    retain flag
 *
 * @retval -1 :  Publish failed.
 * @retval  0 :  Publish successful.
 * @see None.
 */
int iot_mqtt_client_publish_wait_ack(void *handle, const char *topic_name, const char *data, int len, int qos, int retain);

/**
 * @brief Publish message to specific topic and callback notify.
 *
 * @param [in] handle: specify the MQTT client.
 * @param [in] topic_name: specify the topic name.
 * @param [in] data:       payload string (set to NULL, sending empty payload message)
 * @param [in] len:        data length, if set to 0, length is calculated from payload
 * @param [in] qos:        QoS of publish message
 * @param [in] retain:     retain flag
 * @param [in] topic_handle_func: specify the topic handle callback-function.
 * @param [in] pcontext: specify context. When call 'topic_handle_func', it will be passed back.
 *
 * @retval -1 :  Publish failed.
 * @retval >0 :  Publish successful.
 * @see None.
 */
int iot_mqtt_client_publish_with_callback(void *handle, 
                                        const char *topic_name, 
                                        const char *data, 
                                        int len, 
                                        int qos, 
                                        int retain, 
                                        iot_mqtt_event_handle_func_fpt handle_func, 
                                        void *pcontext);


/**
 * @brief Auto subscribe MQTT topic.
 *
 * @param [in] handle: specify the MQTT client.
 * @param [in] topic_filter: specify the topic filter.
 * @param [in] topic_handle_func: specify the topic handle callback-function.
 * @param [in] pcontext: specify context. When call 'topic_handle_func', it will be passed back.
 *
 * @retval -1  : Subscribe failed.
 * @retval =0 : Subscribe successful.
 * @see None.
 */
int iot_mqtt_client_auto_subscribe(void *handle, const char *topic_filter, iot_mqtt_event_handle_func_fpt topic_handle_func, void *pcontext);

/**
 * @brief auto Unsubscribe MQTT topic.
 *
 * @param [in] handle: specify the MQTT client.
 * @param [in] topic_filter: specify the topic filter.
 *
 * @retval -1  : Unsubscribe failed.
 * @retval >=0 : Unsubscribe successful.
          The value is a unique ID of this request.
          The ID will be passed back when callback 'iot_mqtt_param_t:handle_event'.
 * @see None.
 */
int iot_mqtt_client_auto_unsubscribe(void *handle, const char *topic_filter);

/* From mqtt_client.h */
/** @} */ /* end of api_mqtt */

/** @} */ /* end of api */

#endif
