#include "qm.h"
#include "util_net.h"
#include "qm_utils_list.h"
#include "qm_utils_timer.h"
#include "MQTTPacket/MQTTPacket.h"
#include "iot_mqtt_client.h"
#include "iot_mqtt_debug.h"
#include "iot_mqtt.h"
#include "iot_mqtt_errno.h"
#include "iot_mqtt_client.h"
#include <string.h>

static int iot_mc_send_packet(iot_mc_client_t *c, char *buf, int length, qm_utils_time_t *timer);
static iot_mc_state_t iot_mc_get_client_state(iot_mc_client_t *pClient);
static void iot_mc_set_client_state(iot_mc_client_t *pClient, iot_mc_state_t newState);
static int iot_mc_keepalive_sub(iot_mc_client_t *pClient);
static void iot_mc_disconnect_callback(iot_mc_client_t *pClient) ;
static int iot_mc_check_state_normal(iot_mc_client_t *c);
static int iot_mc_handle_reconnect(iot_mc_client_t *pClient);
static void iot_mc_reconnect_callback(iot_mc_client_t *pClient, int code);
static int iot_mc_push_pubInfo_to(iot_mc_client_t *c, int len, unsigned short msgId, iot_mqtt_event_handle_t *handle_event, qm_list_node_t **node);
static int g_iot_mqtt_last_error = SUCCESS_RETURN;
static int iot_mc_push_subInfo_to(iot_mc_client_t *c, int len, unsigned short msgId, enum msgTypes type,
                                   iot_mqtt_topic_t *topic_list, int count,
                                   qm_list_node_t **node);
static int iot_mc_check_handle_is_identical(iot_mc_topic_handle_t *messageHandlers1,
        iot_mc_topic_handle_t *messageHandler2);


/* check rule whether is valid or not */
static int iot_mc_check_rule(char *iterm, iot_mc_topic_type_t type)
{
    int i = 0;
    int len = 0;

    if (NULL == iterm) {
        mqtt_log_err("iterm is NULL");
        return FAIL_RETURN;
    }

    len = strlen(iterm);

    for (i = 0; i < len; i++) {
        if (TOPIC_FILTER_TYPE == type) {
            if ('+' == iterm[i] || '#' == iterm[i]) {
                if (1 != len) {
                    mqtt_log_err("the character # and + is error");
                    return FAIL_RETURN;
                }
            }
        } else {
            if ('+' == iterm[i] || '#' == iterm[i]) {
                mqtt_log_err("has character # and + is error");
                return FAIL_RETURN;
            }
        }

        if (iterm[i] < 32 || iterm[i] >= 127) {
            return FAIL_RETURN;
        }
    }
    return SUCCESS_RETURN;
}


/* Check topic name */
/* 0, topic name is valid; NOT 0, topic name is invalid */
static int iot_mc_check_topic(const char *topicName, iot_mc_topic_type_t type)
{
    int mask = 0;
    char *delim = "/";
    char *iterm = NULL;
    char topicString[IOT_MC_TOPIC_NAME_MAX_LEN];
    if (NULL == topicName) {
        return FAIL_RETURN;
    }

    if (strlen(topicName) > IOT_MC_TOPIC_NAME_MAX_LEN) {
        mqtt_log_err("len of topicName exceeds 64");
        return FAIL_RETURN;
    }

    memset(topicString, 0x0, IOT_MC_TOPIC_NAME_MAX_LEN);
    strncpy(topicString, topicName, IOT_MC_TOPIC_NAME_MAX_LEN - 1);

    iterm = strtok(topicString, delim);

    if (SUCCESS_RETURN != iot_mc_check_rule(iterm, type)) {
        mqtt_log_err("run iot_check_rule error");
        return FAIL_RETURN;
    }

    for (;;) {
        iterm = strtok(NULL, delim);

        if (iterm == NULL) {
            break;
        }

        /* The character '#' is not in the last */
        if (1 == mask) {
            mqtt_log_err("the character # is error");
            return FAIL_RETURN;
        }

        if (SUCCESS_RETURN != iot_mc_check_rule(iterm, type)) {
            mqtt_log_err("run iot_check_rule error");
            return FAIL_RETURN;
        }

        if (iterm[0] == '#') {
            mask = 1;
        }
    }

    return SUCCESS_RETURN;
}


/* Send keepalive packet */
static int MQTTKeepalive(iot_mc_client_t *pClient)
{
    int len = 0;
    int rc = 0;
    /* there is no ping outstanding - send ping packet */
    qm_utils_time_t timer;

    if (!pClient) {
        return FAIL_RETURN;
    }

    qm_utils_time_init(&timer);
    qm_utils_time_countdown_ms(&timer, 1000);

    qm_mutex_lock(&pClient->lock_write_buf, QM_WAIT_FOREVER);
    len = qm_MQTTSerialize_pingreq((unsigned char *)pClient->buf_send, pClient->buf_size_send);
    if (len <= 0) {
        qm_mutex_unlock(&pClient->lock_write_buf);
        mqtt_log_err("Serialize ping request is error");
        return MQTT_PING_PACKET_ERROR;
    }

    rc = iot_mc_send_packet(pClient, pClient->buf_send, len, &timer);
    if (SUCCESS_RETURN != rc) {
        qm_mutex_unlock(&pClient->lock_write_buf);
        /* ping outstanding, then close socket unsubscribe topic and handle callback function */
        mqtt_log_err("ping outstanding is error,result = %d", rc);
        return MQTT_NETWORK_ERROR;
    }
    qm_mutex_unlock(&pClient->lock_write_buf);

    return SUCCESS_RETURN;
}


/* MQTT send connect packet */
static int MQTTConnect(iot_mc_client_t *pClient)
{
    MQTTPacket_connectData *pConnectParams;
    qm_utils_time_t connectTimer;
    int len = 0;

    if (!pClient) {
        return FAIL_RETURN;
    }

    pConnectParams = &pClient->connect_data;
    qm_mutex_lock(&pClient->lock_write_buf, QM_WAIT_FOREVER);
    if ((len = qm_MQTTSerialize_connect((unsigned char *)pClient->buf_send, pClient->buf_size_send, pConnectParams)) <= 0) {
        qm_mutex_unlock(&pClient->lock_write_buf);
        mqtt_log_err("Serialize connect packet failed,len = %d", len);
        return MQTT_CONNECT_PACKET_ERROR;
    }

    /* send the connect packet */
    qm_utils_time_init(&connectTimer);
    qm_utils_time_countdown_ms(&connectTimer, pClient->request_timeout_ms);
    if ((iot_mc_send_packet(pClient, pClient->buf_send, len, &connectTimer)) != SUCCESS_RETURN) {
        qm_mutex_unlock(&pClient->lock_write_buf);
        mqtt_log_err("send connect packet failed, host=%s, port=%u, timeout_ms=%u",
                     pClient->ipstack && pClient->ipstack->pHostAddress ? pClient->ipstack->pHostAddress : "",
                     pClient->ipstack ? pClient->ipstack->port : 0,
                     (unsigned int)pClient->request_timeout_ms);
        return MQTT_NETWORK_ERROR;
    }
    qm_mutex_unlock(&pClient->lock_write_buf);

    return SUCCESS_RETURN;
}


/* MQTT send publish packet */
static int MQTTPublish(iot_mc_client_t *c, const char *topicName, iot_mqtt_topic_info_pt topic_msg, iot_mqtt_event_handle_t *handle_event)

{
    qm_list_node_t *node = NULL;
    qm_utils_time_t timer;
    MQTTString topic = MQTTString_initializer;
    int len = 0;
    int send_ret;

    if (!c || !topicName || !topic_msg) {
        return FAIL_RETURN;
    }

    topic.cstring = (char *)topicName;
    qm_utils_time_init(&timer);
    qm_utils_time_countdown_ms(&timer, CONFIG_IOT_MQTT_WRITE_TIMEOUT);

    qm_mutex_lock(&c->lock_write_buf, QM_WAIT_FOREVER);
    len = qm_MQTTSerialize_publish((unsigned char *)c->buf_send,
                                c->buf_size_send,
                                0,
                                topic_msg->qos,
                                topic_msg->retain,
                                topic_msg->packet_id,
                                topic,
                                (unsigned char *)topic_msg->payload,
                                topic_msg->payload_len);
    if (len <= 0) {
        qm_mutex_unlock(&c->lock_write_buf);
        mqtt_log_err("qm_MQTTSerialize_publish is error, len=%d, buf_size=%u, payloadlen=%u",
                len,
                c->buf_size_send,
                topic_msg->payload_len);
        return MQTT_PUBLISH_PACKET_ERROR;
    }

    /* If the QOS >1, push the information into list of wait publish ACK */
    if (topic_msg->qos > IOT_MQTT_QOS0) {
        /* push into list */
        if (SUCCESS_RETURN != iot_mc_push_pubInfo_to(c, len, topic_msg->packet_id, handle_event, &node)) {
            mqtt_log_err("push publish into to pubInfolist failed!");
            qm_mutex_unlock(&c->lock_write_buf);
            return MQTT_PUSH_TO_LIST_ERROR;
        }
    }

    /* send the publish packet */
    send_ret = iot_mc_send_packet(c, c->buf_send, len, &timer);
    if (send_ret != SUCCESS_RETURN) {
        if (topic_msg->qos > IOT_MQTT_QOS0) {
            /* If failed, remove from list */
            qm_mutex_lock(&c->lock_list_pub, QM_WAIT_FOREVER);
            qm_list_remove(c->list_pub_wait_ack, node);
            qm_list_node_destroy(node);
            qm_mutex_unlock(&c->lock_list_pub);
        }

        qm_mutex_unlock(&c->lock_write_buf);
        return MQTT_NETWORK_ERROR;
    }

    qm_mutex_unlock(&c->lock_write_buf);
    return SUCCESS_RETURN;
}


/* MQTT send publish ACK */
static int MQTTPuback(iot_mc_client_t *c, unsigned int msgId, enum msgTypes type)
{
    int rc = 0;
    int len = 0;
    qm_utils_time_t timer;

    if (!c) {
        return FAIL_RETURN;
    }

    qm_utils_time_init(&timer);
    qm_utils_time_countdown_ms(&timer, c->request_timeout_ms);

    qm_mutex_lock(&c->lock_write_buf, QM_WAIT_FOREVER);
    if (type == PUBACK) {
        len = qm_MQTTSerialize_ack((unsigned char *)c->buf_send, c->buf_size_send, PUBACK, 0, msgId);
    } else if (type == PUBREC) {
        len = qm_MQTTSerialize_ack((unsigned char *)c->buf_send, c->buf_size_send, PUBREC, 0, msgId);
    } else if (type == PUBREL) {
        len = qm_MQTTSerialize_ack((unsigned char *)c->buf_send, c->buf_size_send, PUBREL, 0, msgId);
    } else {
        qm_mutex_unlock(&c->lock_write_buf);
        return MQTT_PUBLISH_ACK_TYPE_ERROR;
    }

    if (len <= 0) {
        qm_mutex_unlock(&c->lock_write_buf);
        return MQTT_PUBLISH_ACK_PACKET_ERROR;
    }

    rc = iot_mc_send_packet(c, c->buf_send, len, &timer);
    if (rc != SUCCESS_RETURN) {
        qm_mutex_unlock(&c->lock_write_buf);
        return MQTT_NETWORK_ERROR;
    }

    qm_mutex_unlock(&c->lock_write_buf);
    return SUCCESS_RETURN;
}

/* MQTT send subscribe packet */
static int MQTTSubscribe(iot_mc_client_t *c, unsigned int msgId, iot_mqtt_topic_t *topic_list, int count)
{
    int size = 0;
    iot_err_t ret = SUCCESS_RETURN;
    int len = 0, i = 0;
    qm_utils_time_t timer;
    MQTTString *topic = NULL;
    int *requestedQoSs = NULL;
    qm_list_node_t *node = NULL;

    if (!c || !topic_list || !count) {
        return FAIL_RETURN;
    }
    size = sizeof(MQTTString) * count + sizeof(int) * count;
    topic = (MQTTString*)qm_malloc(size);
    if(topic == NULL){
        return ERROR_NO_MEM;
    }
    memset(topic, 0, size);
    requestedQoSs = (int*)((uint8_t*)topic + sizeof(MQTTString) * count);
    for(i = 0; i < count; i++){
        topic[i].cstring = (char *)topic_list[i].topic_filter;
        requestedQoSs[i] = topic_list[i].qos;
    }
    
    qm_utils_time_init(&timer);
    qm_utils_time_countdown_ms(&timer, c->request_timeout_ms);

    qm_mutex_lock(&c->lock_write_buf, QM_WAIT_FOREVER);

    len = qm_MQTTSerialize_subscribe((unsigned char *)c->buf_send, c->buf_size_send, 0, (unsigned short)msgId, count, topic,
                                  requestedQoSs);
    if (len <= 0) {
        qm_mutex_unlock(&c->lock_write_buf);
        ret = MQTT_SUBSCRIBE_PACKET_ERROR;
        goto __exit;
    }
    /*
     * NOTE: It prefer to push the element into list and then remove it when send failed,
     *       because some of extreme cases
     * */

    /* push the element to list of wait subscribe ACK */
    if (SUCCESS_RETURN != iot_mc_push_subInfo_to(c, len, msgId, SUBSCRIBE, topic_list, count, &node)) {
        mqtt_log_err("push publish into to pubInfolist failed!");
        qm_mutex_unlock(&c->lock_write_buf);
        ret = MQTT_PUSH_TO_LIST_ERROR;
        goto __exit;
    }

    if ((iot_mc_send_packet(c, c->buf_send, len, &timer)) != SUCCESS_RETURN) { /* send the subscribe packet */
        /* If send failed, remove it */
        qm_mutex_lock(&c->lock_list_sub, QM_WAIT_FOREVER);
        qm_list_remove(c->list_sub_wait_ack, node);
        qm_list_node_destroy(node);
        qm_mutex_unlock(&c->lock_list_sub);
        qm_mutex_unlock(&c->lock_write_buf);
        mqtt_log_err("run sendPacket error!");
        ret = MQTT_NETWORK_ERROR;
        goto __exit;
    }

    qm_mutex_unlock(&c->lock_write_buf);
    ret = SUCCESS_RETURN;

__exit:
    if(topic){
        qm_free(topic);
        topic = NULL;
    }
    return ret; 
}

/* MQTT send unsubscribe packet */
static int MQTTUnsubscribe(iot_mc_client_t *c, unsigned int msgId, const char *topic_filter[], int count)
{
    qm_utils_time_t timer;
    MQTTString *topic = NULL;
    int len = 0, i = 0;
    iot_err_t ret = SUCCESS_RETURN;
    iot_mqtt_topic_t *topic_list = NULL;
    /* push into list */
    qm_list_node_t *node = NULL;

    if (!c || !topic_filter || !count) {
        return FAIL_RETURN;
    }
    topic = (MQTTString*)qm_malloc(sizeof(MQTTString) * count);
    if(topic == NULL){
        ret = ERROR_NO_MEM;
        goto __exit;
    }
    memset(topic, 0, sizeof(MQTTString) * count);
    topic_list = (iot_mqtt_topic_t*)qm_malloc(sizeof(iot_mqtt_topic_t) * count);
    if(topic_list == NULL){
        ret = ERROR_NO_MEM;
        goto __exit;
    }
    memset(topic_list, 0, sizeof(iot_mqtt_topic_t) * count);
    
    for(i = 0; i < count; i++){
        topic[i].cstring = (char *)topic_filter[i];
        topic_list[i].topic_filter = (char*)topic_filter[i];
    }

    qm_utils_time_init(&timer);
    qm_utils_time_countdown_ms(&timer, c->request_timeout_ms);

    qm_mutex_lock(&c->lock_write_buf, QM_WAIT_FOREVER);

    if ((len = qm_MQTTSerialize_unsubscribe((unsigned char *)c->buf_send, c->buf_size_send, 0, (unsigned short)msgId, count,
                                         topic)) <= 0) {
        qm_mutex_unlock(&c->lock_write_buf);
        ret = MQTT_UNSUBSCRIBE_PACKET_ERROR;
        goto __exit;
    }

    if (SUCCESS_RETURN != iot_mc_push_subInfo_to(c, len, msgId, UNSUBSCRIBE, topic_list, count, &node)) {
        mqtt_log_err("push publish into to pubInfolist failed!");
        qm_mutex_unlock(&c->lock_write_buf);
        ret = MQTT_PUSH_TO_LIST_ERROR;
        goto __exit;
    }

    if ((iot_mc_send_packet(c, c->buf_send, len, &timer)) != SUCCESS_RETURN) { /* send the subscribe packet */
        /* remove from list */
        qm_mutex_lock(&c->lock_list_sub, QM_WAIT_FOREVER);
        qm_list_remove(c->list_sub_wait_ack, node);
        qm_list_node_destroy(node);
        qm_mutex_unlock(&c->lock_list_sub);
        qm_mutex_unlock(&c->lock_write_buf);
        ret = MQTT_NETWORK_ERROR;
        goto __exit;
    }

    qm_mutex_unlock(&c->lock_write_buf);

    ret = SUCCESS_RETURN;
__exit:
    if(topic){
        qm_free(topic);
        topic = NULL;
    }
    if(topic_list){
        qm_free(topic_list);
        topic_list = NULL;
    }
    return ret;
}


/* MQTT send disconnect packet */
static int MQTTDisconnect(iot_mc_client_t *c)
{
    int rc = FAIL_RETURN;
    qm_utils_time_t timer;     /* we might wait for incomplete incoming publishes to complete */

    if (!c) {
        return FAIL_RETURN;
    }

    qm_mutex_lock(&c->lock_write_buf, QM_WAIT_FOREVER);

    int len = qm_MQTTSerialize_disconnect((unsigned char *)c->buf_send, c->buf_size_send);
    qm_utils_time_init(&timer);
    qm_utils_time_countdown_ms(&timer, c->request_timeout_ms);

    if (len > 0) {
        rc = iot_mc_send_packet(c, c->buf_send, len, &timer);           /* send the disconnect packet */
    }

    qm_mutex_unlock(&c->lock_write_buf);

    return rc;
}

/* remove the list element specified by @msgId from list of wait publish ACK */
/* return: 0, success; NOT 0, fail; */
static int iot_mc_mask_pubInfo_from(iot_mc_client_t *c, uint16_t msgId, iot_mc_pub_info_t **pub_info)
{
    qm_list_iterator_t *iter;
    qm_list_node_t *node = NULL;
    iot_mc_pub_info_t *repubInfo = NULL;
    if (!c) {
        return FAIL_RETURN;
    }

    qm_mutex_lock(&c->lock_list_pub, QM_WAIT_FOREVER);
    if (c->list_pub_wait_ack->len) {

        if (NULL == (iter = qm_list_iterator_new(c->list_pub_wait_ack, LIST_TAIL))) {
            qm_mutex_unlock(&c->lock_list_pub);
            return SUCCESS_RETURN;
        }

        for (;;) {
            node = qm_list_iterator_next(iter);

            if (NULL == node) {
                break;
            }

            repubInfo = (iot_mc_pub_info_t *) node->val;
            if (NULL == repubInfo) {
                mqtt_log_err("node's value is invalid!");
                continue;
            }

            if (repubInfo->msg_id == msgId) {
                repubInfo->node_state = IOT_MC_NODE_STATE_INVALID; /* mark as invalid node */
                *pub_info = repubInfo;
                break;
            }
        }

        qm_list_iterator_destroy(iter);
    }
    qm_mutex_unlock(&c->lock_list_pub);

    return SUCCESS_RETURN;
}


/* push the wait element into list of wait publish ACK */
/* return: 0, success; NOT 0, fail; */
static int iot_mc_push_pubInfo_to(iot_mc_client_t *c, int len, unsigned short msgId, iot_mqtt_event_handle_t *handle_event, qm_list_node_t **node)
{
    if (!c || !node) {
        mqtt_log_err("the param of c is error!");
        return FAIL_RETURN;
    }

    if ((len < 0) || (len > c->buf_size_send)) {
        mqtt_log_err("the param of len is error!");
        return FAIL_RETURN;
    }

    qm_mutex_lock(&c->lock_list_pub, QM_WAIT_FOREVER);

    if (c->list_pub_wait_ack->len >= IOT_MC_REPUB_NUM_MAX) {
        qm_mutex_unlock(&c->lock_list_pub);
        mqtt_log_err("more than %u elements in republish list. List overflow!", c->list_pub_wait_ack->len);
        return FAIL_RETURN;
    }

    iot_mc_pub_info_t *repubInfo = (iot_mc_pub_info_t *)qm_malloc(sizeof(iot_mc_pub_info_t) + len);
    if (NULL == repubInfo) {
        qm_mutex_unlock(&c->lock_list_pub);
        mqtt_log_err("run iot_memory_malloc is error!");
        return FAIL_RETURN;
    }
    memset(repubInfo, 0, sizeof(iot_mc_pub_info_t) + len);
    if(handle_event){
        memcpy(&repubInfo->handle_event, handle_event, sizeof(iot_mqtt_event_handle_t));
    }
    repubInfo->node_state = IOT_MC_NODE_STATE_NORMANL;
    repubInfo->msg_id = msgId;
    repubInfo->len = len;
    qm_utils_time_start(&repubInfo->pub_start_time);
    repubInfo->buf = (unsigned char *)repubInfo + sizeof(iot_mc_pub_info_t);

    memcpy(repubInfo->buf, c->buf_send, len);

    *node = qm_list_node_new(repubInfo);
    if (NULL == *node) {
        qm_mutex_unlock(&c->lock_list_pub);
        mqtt_log_err("run qm_list_node_new is error!");
        return FAIL_RETURN;
    }

    qm_list_rpush(c->list_pub_wait_ack, *node);

    qm_mutex_unlock(&c->lock_list_pub);

    return SUCCESS_RETURN;
}


/* push the wait element into list of wait subscribe(unsubscribe) ACK */
/* return: 0, success; NOT 0, fail; */
static int iot_mc_push_subInfo_to(iot_mc_client_t *c, int len, unsigned short msgId, enum msgTypes type,
                                   iot_mqtt_topic_t *topic_list, int count,
                                   qm_list_node_t **node)
{
    int i = 0, size = 0;
    iot_mc_topic_handle_t *mc_topic_handle = NULL; 
    if (!c || !topic_list || !count || !node) {
        return FAIL_RETURN;
    }

    qm_mutex_lock(&c->lock_list_sub, QM_WAIT_FOREVER);

    if (c->list_sub_wait_ack->len >= IOT_MC_SUB_REQUEST_NUM_MAX) {
        qm_mutex_unlock(&c->lock_list_sub);
        mqtt_log_err("number of subInfo more than max!,size = %d", c->list_sub_wait_ack->len);
        return FAIL_RETURN;
    }

    size = sizeof(iot_mc_subsribe_info_t) + sizeof(iot_mc_topic_handle_t) * count + len;
    iot_mc_subsribe_info_t *subInfo = (iot_mc_subsribe_info_t *)qm_malloc(size);
    if (NULL == subInfo) {
        qm_mutex_unlock(&c->lock_list_sub);
        mqtt_log_err("run iot_memory_malloc is error!");
        return FAIL_RETURN;
    }
    memset(subInfo, 0, size);

    mc_topic_handle = (iot_mc_topic_handle_t*)((uint8_t*)subInfo + sizeof(iot_mc_subsribe_info_t));
    for(i = 0; i < count; i++){
        mc_topic_handle[i].topic_filter = (char *)topic_list[i].topic_filter;
        mc_topic_handle[i].handle.h_fp = topic_list[i].topic_handle_func;
        mc_topic_handle[i].handle.pcontext = topic_list[i].pcontext;
    }
    
    subInfo->node_state = IOT_MC_NODE_STATE_NORMANL;
    subInfo->msg_id = msgId;
    subInfo->len = len;
    qm_utils_time_start(&subInfo->sub_start_time);

    subInfo->type = type;
    subInfo->handler = mc_topic_handle;
    subInfo->count = count;
    subInfo->buf = (uint8_t*)subInfo + sizeof(iot_mc_subsribe_info_t) + sizeof(iot_mc_topic_handle_t) * count;

    memcpy(subInfo->buf, c->buf_send, len);

    *node = qm_list_node_new(subInfo);
    if (NULL == *node) {
        qm_mutex_unlock(&c->lock_list_sub);
        mqtt_log_err("run qm_list_node_new is error!");
        return FAIL_RETURN;
    }

    qm_list_rpush(c->list_sub_wait_ack, *node);

    qm_mutex_unlock(&c->lock_list_sub);

    return SUCCESS_RETURN;
}


/* remove the list element specified by @msgId from list of wait subscribe(unsubscribe) ACK */
/* and return message handle by @messageHandler */
/* return: 0, success; NOT 0, fail; */
static int iot_mc_mask_subInfo_from(iot_mc_client_t *c, unsigned int msgId, iot_mc_topic_handle_t **topic_list, int *count)
{
    qm_list_iterator_t *iter;
    qm_list_node_t *node = NULL;
    iot_mc_subsribe_info_t *subInfo = NULL;

    if (!c || !topic_list || !count) {
        return FAIL_RETURN;
    }

    qm_mutex_lock(&c->lock_list_sub, QM_WAIT_FOREVER);
    if (c->list_sub_wait_ack->len) {

        if (NULL == (iter = qm_list_iterator_new(c->list_sub_wait_ack, LIST_TAIL))) {
            qm_mutex_unlock(&c->lock_list_sub);
            return SUCCESS_RETURN;
        }

        for (;;) {
            node = qm_list_iterator_next(iter);
            if (NULL == node) {
                break;
            }
            subInfo = (iot_mc_subsribe_info_t *) node->val;
            if (NULL == subInfo) {
                mqtt_log_err("node's value is invalid!");
                continue;
            }

            if (subInfo->msg_id == msgId) {
                *topic_list = subInfo->handler; /* return handle */
                *count = subInfo->count;
                subInfo->node_state = IOT_MC_NODE_STATE_INVALID; /* mark as invalid node */
            }
        }

        qm_list_iterator_destroy(iter);
    }
    qm_mutex_unlock(&c->lock_list_sub);

    return SUCCESS_RETURN;
}


/* get next packet-id */
static int iot_mc_get_next_packetid(iot_mc_client_t *c)
{
    unsigned int id = 0;

    if (!c) {
        return FAIL_RETURN;
    }

    qm_mutex_lock(&c->lock_generic, QM_WAIT_FOREVER);
    c->packet_id = (c->packet_id == IOT_MC_PACKET_ID_MAX) ? 1 : c->packet_id + 1;
    id = c->packet_id;
    qm_mutex_unlock(&c->lock_generic);

    return id;
}


/* send packet */
static int iot_mc_send_packet(iot_mc_client_t *c, char *buf, int length, qm_utils_time_t *time)
{
    int rc = FAIL_RETURN;
    int sent = 0;

    if (!c || !buf || !time) {
        return rc;
    }

    while (sent < length && !qm_utils_time_is_expired(time)) {
        rc = c->ipstack->write(c->ipstack, &buf[sent], length, qm_utils_time_left(time));
        if (rc < 0) { /* there was an error writing the data */
            break;
        }
        sent += rc;
    }

    if (sent == length) {
        rc = SUCCESS_RETURN;
    } else {
        rc = MQTT_NETWORK_ERROR;
    }
    return rc;
}


/* decode packet */
static int iot_mc_decode_packet(iot_mc_client_t *c, int *value, int timeout)
{
    char i;
    int multiplier = 1;
    int len = 0;
    const int MAX_NO_OF_REMAINING_LENGTH_BYTES = 4;

    if (!c || !value) {
        return FAIL_RETURN;
    }

    *value = 0;
    do {
        int rc = MQTTPACKET_READ_ERROR;

        if (++len > MAX_NO_OF_REMAINING_LENGTH_BYTES) {
            return MQTTPACKET_READ_ERROR; /* bad data */
        }

        rc = c->ipstack->read(c->ipstack, &i, 1, timeout);
        if (rc != 1) {
            return MQTT_NETWORK_ERROR;
        }

        *value += (i & 127) * multiplier;
        multiplier *= 128;
    } while ((i & 128) != 0);

    return len;
}


/* read packet */
static int iot_mc_read_exact(iot_mc_client_t *c, char *buffer, int len, uint32_t timeout_ms)
{
    qm_utils_time_t timer;
    int got = 0;

    if (!c || !buffer || len <= 0) {
        return FAIL_RETURN;
    }

    qm_utils_time_countdown_ms(&timer, timeout_ms);

    while (got < len) {
        int rc = c->ipstack->read(c->ipstack, buffer + got, len - got, qm_utils_time_left(&timer));

        if (rc < 0) {
            return FAIL_RETURN;
        }

        if (rc == 0) {
            if (qm_utils_time_is_expired(&timer)) {
                return FAIL_RETURN;
            }
            qm_msleep(1);
            continue;
        }

        got += rc;
    }

    return SUCCESS_RETURN;
}


static int iot_mc_read_packet(iot_mc_client_t *c, qm_utils_time_t *timer, unsigned int *packet_type)
{
    MQTTHeader header = {0};
    int len = 0;
    int rem_len = 0;
    int rc = 0;

    if (!c || !timer || !packet_type) {
        return FAIL_RETURN;
    }

    /* 1. read the header byte.  This has the packet type in it */
    rc = c->ipstack->read(c->ipstack, c->buf_read, 1, qm_utils_time_left(timer));
    if (0 == rc) { /* timeout */
        *packet_type = 0;
        return SUCCESS_RETURN;
    } else if (1 != rc) {
        mqtt_log_debug("mqtt read error, rc=%d", rc);
        return FAIL_RETURN;
    }

    len = 1;

    /* 2. read the remaining length.  This is variable in itself */
    if ((rc = iot_mc_decode_packet(c, &rem_len, qm_utils_time_left(timer) + c->request_timeout_ms)) < 0) {
        mqtt_log_err("decodePacket error,rc = %d", rc);
        return rc;
    }

    len += qm_MQTTPacket_encode((unsigned char *)c->buf_read + 1,
                             rem_len); /* put the original remaining length back into the buffer */

    /* Check if the received data length exceeds mqtt read buffer length */
    if ((rem_len > 0) && ((rem_len + len) > c->buf_size_read)) {
        mqtt_log_err("mqtt read buffer is too short, mqttReadBufLen : %u, remainDataLen : %d", c->buf_size_read, rem_len);
        int needReadLen = c->buf_size_read - len;
        if (iot_mc_read_exact(c, c->buf_read + len, needReadLen, qm_utils_time_left(timer) + c->request_timeout_ms) != SUCCESS_RETURN) {
            mqtt_log_err("mqtt read error");
            return FAIL_RETURN;
        }

        /* drop data whitch over the length of mqtt buffer */
        int remainDataLen = rem_len - needReadLen;
        char *remainDataBuf = qm_malloc(remainDataLen + 1);
        if (!remainDataBuf) {
            mqtt_log_err("allocate remain buffer failed");
            return FAIL_RETURN;
        }

        if (iot_mc_read_exact(c, remainDataBuf, remainDataLen, qm_utils_time_left(timer) + c->request_timeout_ms) != SUCCESS_RETURN) {
            mqtt_log_err("mqtt read error");
            qm_free(remainDataBuf);
            remainDataBuf = NULL;
            return FAIL_RETURN;
        }

        qm_free(remainDataBuf);
        remainDataBuf = NULL;

        if (NULL != c->handle_event.h_fp) {
            iot_mqtt_event_msg_t msg;

            msg.event_type = IOT_MQTT_EVENT_BUFFER_OVERFLOW;
            msg.msg = "mqtt read buffer is too short";

            c->handle_event.h_fp(c->handle_event.pcontext, c, &msg);
        }

        return SUCCESS_RETURN;

    }

    /* 3. read the rest of the buffer using a callback to supply the rest of the data */
    if (rem_len > 0 && (iot_mc_read_exact(c, c->buf_read + len, rem_len, qm_utils_time_left(timer) + c->request_timeout_ms) != SUCCESS_RETURN)) {
        mqtt_log_err("mqtt read error");
        return FAIL_RETURN;
    }

    header.byte = c->buf_read[0];
    *packet_type = header.bits.type;
    return SUCCESS_RETURN;
}


/* check whether the topic is matched or not */
static char iot_mc_is_topic_matched(char *topicFilter, MQTTString *topicName)
{
    if (!topicFilter || !topicName) {
        return 0;
    }
    char *curf = topicFilter;
    char *curn = topicName->lenstring.data;
    char *curn_end = curn + topicName->lenstring.len;

    while (*curf && curn < curn_end) {
        if (*curn == '/' && *curf != '/') {
            break;
        }

        if (*curf != '+' && *curf != '#' && *curf != *curn) {
            break;
        }

        if (*curf == '+') {
            /* skip until we meet the next separator, or end of string */
            char *nextpos = curn + 1;
            while (nextpos < curn_end && *nextpos != '/') {
                nextpos = ++curn + 1;
            }
        } else if (*curf == '#') {
            curn = curn_end - 1;    /* skip until end of string */
        }
        curf++;
        curn++;
    }

    return (curn == curn_end) && (*curf == '\0');
}


/* deliver message */
static void iot_mc_deliver_message(iot_mc_client_t *c, MQTTString *topicName, iot_mqtt_topic_info_pt topic_msg)
{
    int i, flag_matched = 0;

    if (!c || !topicName || !topic_msg) {
        return;
    }

    topic_msg->ptopic = topicName->lenstring.data;
    topic_msg->topic_len = topicName->lenstring.len;

    /* we have to find the right message handler - indexed by topic */
    qm_mutex_lock(&c->lock_generic, QM_WAIT_FOREVER);
    for (i = 0; i < IOT_MC_SUB_NUM_MAX; ++i) {

        if ((c->sub_handle[i].topic_filter != 0)
            && (qm_MQTTPacket_equals(topicName, (char *)c->sub_handle[i].topic_filter)
                || iot_mc_is_topic_matched((char *)c->sub_handle[i].topic_filter, topicName))) {
            mqtt_log_debug("topic be matched");

            iot_mc_topic_handle_t msg_handle = c->sub_handle[i];
            qm_mutex_unlock(&c->lock_generic);

            if (NULL != msg_handle.handle.h_fp) {
                iot_mqtt_event_msg_t msg;
                msg.event_type = IOT_MQTT_EVENT_PUBLISH_RECVEIVED;
                msg.msg = (void *)topic_msg;

                msg_handle.handle.h_fp(msg_handle.handle.pcontext, c, &msg);
                flag_matched = 1;
            }

            qm_mutex_lock(&c->lock_generic, QM_WAIT_FOREVER);
        }
    }

    qm_mutex_unlock(&c->lock_generic);

    if (0 == flag_matched) {
        mqtt_log_debug("NO matching any topic, call default handle function");

        if (NULL != c->handle_event.h_fp) {
            iot_mqtt_event_msg_t msg;

            msg.event_type = IOT_MQTT_EVENT_PUBLISH_RECVEIVED;
            msg.msg = topic_msg;

            c->handle_event.h_fp(c->handle_event.pcontext, c, &msg);
        }
    }
}


/* handle CONNACK packet received from remote MQTT broker */
static int iot_mc_handle_recv_CONNACK(iot_mc_client_t *c)
{
    int rc = SUCCESS_RETURN;
    unsigned char connack_rc = 255;
    char sessionPresent = 0;

    if (!c) {
        return FAIL_RETURN;
    }

    if (qm_MQTTDeserialize_connack((unsigned char *)&sessionPresent, &connack_rc, (unsigned char *)c->buf_read,
                                c->buf_size_read) != 1) {
        mqtt_log_err("connect ack is error");
        return MQTT_CONNECT_ACK_PACKET_ERROR;
    }

    switch (connack_rc) {
        case IOT_MC_CONNECTION_ACCEPTED:
            rc = SUCCESS_RETURN;
            break;
        case IOT_MC_CONNECTION_REFUSED_UNACCEPTABLE_PROTOCOL_VERSION:
            rc = MQTT_CONANCK_UNACCEPTABLE_PROTOCOL_VERSION_ERROR;
            break;
        case IOT_MC_CONNECTION_REFUSED_IDENTIFIER_REJECTED:
            rc = MQTT_CONNACK_IDENTIFIER_REJECTED_ERROR;
            break;
        case IOT_MC_CONNECTION_REFUSED_SERVER_UNAVAILABLE:
            rc = MQTT_CONNACK_SERVER_UNAVAILABLE_ERROR;
            break;
        case IOT_MC_CONNECTION_REFUSED_BAD_USERDATA:
            rc = MQTT_CONNACK_BAD_USERDATA_ERROR;
            break;
        case IOT_MC_CONNECTION_REFUSED_NOT_AUTHORIZED:
            rc = MQTT_CONNACK_NOT_AUTHORIZED_ERROR;
            break;
        default:
            rc = MQTT_CONNACK_UNKNOWN_ERROR;
            break;
    }

    return rc;
}


/* handle PUBACK packet received from remote MQTT broker */
static int iot_mc_handle_recv_PUBACK(iot_mc_client_t *c)
{
    unsigned short mypacketid;
    unsigned char dup = 0;
    unsigned char type = 0;
    iot_mqtt_event_msg_t msg;
    iot_mc_pub_info_t *pub_info = NULL;

    if (!c) {
        return FAIL_RETURN;
    }

    if (qm_MQTTDeserialize_ack(&type, &dup, &mypacketid, (unsigned char *)c->buf_read, c->buf_size_read) != 1) {
        return MQTT_PUBLISH_ACK_PACKET_ERROR;
    }

    c->rsp_pub_id = mypacketid;

    (void)iot_mc_mask_pubInfo_from(c, mypacketid, &pub_info);

    if(c->rsp_pub_id == c->req_pub_id){
        return SUCCESS_RETURN;
    }

    if(pub_info && pub_info->handle_event.h_fp){
        msg.event_type = IOT_MQTT_EVENT_PUBLISH_SUCCESS;
        msg.msg = (void *)(uintptr_t)mypacketid;
        pub_info->handle_event.h_fp(pub_info->handle_event.pcontext, c, &msg);
        return SUCCESS_RETURN;
    }

    /* call callback function to notify that PUBLISH is successful */
    if (NULL != c->handle_event.h_fp) {
        msg.event_type = IOT_MQTT_EVENT_PUBLISH_SUCCESS;
        msg.msg = (void *)(uintptr_t)mypacketid;
        c->handle_event.h_fp(c->handle_event.pcontext, c, &msg);
    }

    return SUCCESS_RETURN;
}


/* handle SUBACK packet received from remote MQTT broker */
static int iot_mc_handle_recv_SUBACK(iot_mc_client_t *c)
{
    unsigned short mypacketid;
    int topic_count = 0;
    iot_mc_topic_handle_t *topic_list = NULL;
    int i, j, count = 0;
    int grantedQoS[IOT_MC_SUB_NUM_MAX] = {-1};
    int i_free = -1, flag_dup = 0;

    if (!c) {
        return FAIL_RETURN;
    }

    if (qm_MQTTDeserialize_suback(&mypacketid, IOT_MC_SUB_NUM_MAX, &count, grantedQoS, (unsigned char *)c->buf_read, c->buf_size_read) != 1) {
        mqtt_log_err("Sub ack packet error");
        return MQTT_SUBSCRIBE_ACK_PACKET_ERROR;
    }

    c->rsp_sub_id = mypacketid;
    (void)iot_mc_mask_subInfo_from(c, mypacketid, &topic_list, &topic_count);
    for(i = 0; i < count; i++){
        /* In negative case, grantedQoS will be 0xFFFF FF80, which means -128 */
        if ((uint8_t)grantedQoS[i] == 0x80) {
            mqtt_log_err("MQTT SUBSCRIBE failed, ack code is 0x80");
            if (NULL != c->handle_event.h_fp && c->rsp_sub_id != c->req_sub_id) {
                iot_mqtt_event_msg_t msg;

                msg.event_type = IOT_MQTT_EVENT_SUBCRIBE_NACK;
                msg.msg = (void *)(uintptr_t)mypacketid;
                c->handle_event.h_fp(c->handle_event.pcontext, c, &msg);
            }
            return MQTT_SUBSCRIBE_ACK_FAILURE;
        }
    }

    mqtt_log_debug("suback: %d, topic count: %d", count, topic_count);


    if ((NULL == topic_list) || (0 == topic_count)) {
        mqtt_log_err("mqtt subscribe info not found");

        //todo: 新增平台ack次数为0时，返回失败回调
        if (NULL != c->handle_event.h_fp && c->rsp_sub_id != c->req_sub_id) {
            iot_mqtt_event_msg_t msg;

            msg.event_type = IOT_MQTT_EVENT_SUBCRIBE_NACK;
            msg.msg = (void *)(uintptr_t)mypacketid;
            c->handle_event.h_fp(c->handle_event.pcontext, c, &msg);
        }
        return MQTT_SUB_INFO_NOT_FOUND_ERROR;
    }

    qm_mutex_lock(&c->lock_generic, QM_WAIT_FOREVER);

    for(j = 0; j < count; j++){
        i_free = -1;
        flag_dup = 0;
        for (i = 0; i < IOT_MC_SUB_NUM_MAX; ++i) {
            /* If subscribe the same topic and callback function, then ignore */
            if ((NULL != c->sub_handle[i].topic_filter)) {
                if (0 == iot_mc_check_handle_is_identical(&c->sub_handle[i], &topic_list[j])) {
                    /* if subscribe a identical topic and relate callback function, then ignore this subscribe */
                    flag_dup = 1;
                    mqtt_log_err("There is a identical topic and related handle in list!");
                    break;
                }
            } else {
                if (-1 == i_free) {
                    i_free = i; /* record available element */
                }
            }
        }

        if (0 == flag_dup) {
            if (-1 == i_free) {
                mqtt_log_err("NOT more @sub_handle space!");
                qm_mutex_unlock(&c->lock_generic);
                return FAIL_RETURN;
            } else {
                c->sub_handle[i_free].topic_filter = topic_list[j].topic_filter;
                c->sub_handle[i_free].handle.h_fp = topic_list[j].handle.h_fp;
                c->sub_handle[i_free].handle.pcontext = topic_list[j].handle.pcontext;
            }
        }
    }

    qm_mutex_unlock(&c->lock_generic);

    /* call callback function to notify that SUBSCRIBE is successful */
    if (NULL != c->handle_event.h_fp && c->rsp_sub_id != c->req_sub_id) {
        iot_mqtt_event_msg_t msg;
        msg.event_type = IOT_MQTT_EVENT_SUBCRIBE_SUCCESS;
        msg.msg = (void *)(uintptr_t)mypacketid;
        c->handle_event.h_fp(c->handle_event.pcontext, c, &msg);
    }

    return SUCCESS_RETURN;
}


/* handle PUBLISH packet received from remote MQTT broker */
static int iot_mc_handle_recv_PUBLISH(iot_mc_client_t *c)
{
    int result = 0;
    MQTTString topicName;
    iot_mqtt_topic_info_t topic_msg;
    int qos = 0;
    int payload_len = 0;

    if (!c) {
        return FAIL_RETURN;
    }

    memset(&topic_msg, 0x0, sizeof(iot_mqtt_topic_info_t));
    memset(&topicName, 0x0, sizeof(MQTTString));

    if (1 != qm_MQTTDeserialize_publish((unsigned char *)&topic_msg.dup,
                                     (int *)&qos,
                                     (unsigned char *)&topic_msg.retain,
                                     (unsigned short *)&topic_msg.packet_id,
                                     &topicName,
                                     (unsigned char **)&topic_msg.payload,
                                     (int *)&payload_len,
                                     (unsigned char *)c->buf_read,
                                     c->buf_size_read)) {
        return MQTT_PUBLISH_PACKET_ERROR;
    
    }
    topic_msg.qos = (unsigned char)qos;
    topic_msg.payload_len = (unsigned short)payload_len;

    /* payload decrypt by id2_aes */
    if (c->mqtt_down_process) {
        c->mqtt_down_process(&topic_msg);
    }

    mqtt_log_debug("%20s : %08d", "Packet Ident", topic_msg.packet_id);
    mqtt_log_debug("%20s : %d", "Topic Length", topicName.lenstring.len);
    mqtt_log_debug("%20s : %.*s",
              "Topic Name",
              topicName.lenstring.len,
              topicName.lenstring.data);
    mqtt_log_debug("%20s : %d / %d", "Payload Len/Room",
              topic_msg.payload_len,
              c->buf_read + c->buf_size_read - topic_msg.payload);
    mqtt_log_debug("%20s : %d", "Receive Buflen", c->buf_size_read);

#if defined(INSPECT_MQTT_FLOW)
    mqtt_log_debug("%20s : %p", "Payload Buffer", topic_msg.payload);
    mqtt_log_debug("%20s : %p", "Receive Buffer", c->buf_read);
    HEXDUMP_DEBUG(topic_msg.payload, topic_msg.payload_len);
#endif

    topic_msg.ptopic = NULL;
    topic_msg.topic_len = 0;

    mqtt_log_debug("delivering msg ...");

    iot_mc_deliver_message(c, &topicName, &topic_msg);

    if (topic_msg.qos == IOT_MQTT_QOS0) {
        return SUCCESS_RETURN;
    } else if (topic_msg.qos == IOT_MQTT_QOS1) {
        result = MQTTPuback(c, topic_msg.packet_id, PUBACK);
    } else if (topic_msg.qos == IOT_MQTT_QOS2) {
        result = MQTTPuback(c, topic_msg.packet_id, PUBREC);
    } else {
        mqtt_log_err("Invalid QOS, QOSvalue = %d", topic_msg.qos);
        return MQTT_PUBLISH_QOS_ERROR;
    }

    return result;
}


/* handle UNSUBACK packet received from remote MQTT broker */
static int iot_mc_handle_recv_UNSUBACK(iot_mc_client_t *c)
{
    int count = 0, j = 0;
    unsigned short i, mypacketid = 0;  /* should be the same as the packetid above */
    iot_mc_topic_handle_t *topic_list = NULL;

    if (!c) {
        return FAIL_RETURN;
    }

    if (qm_MQTTDeserialize_unsuback(&mypacketid, (unsigned char *)c->buf_read, c->buf_size_read) != 1) {

        return MQTT_UNSUBSCRIBE_ACK_PACKET_ERROR;
    }

    (void)iot_mc_mask_subInfo_from(c, mypacketid, &topic_list, &count);

    if ((NULL == topic_list) || (0 == count)) {
        return MQTT_SUB_INFO_NOT_FOUND_ERROR;
    }

    /* Remove from message handler array */
    qm_mutex_lock(&c->lock_generic, QM_WAIT_FOREVER);

    for(j = 0; j < count; j++){
        for (i = 0; i < IOT_MC_SUB_NUM_MAX; ++i) {
            if ((c->sub_handle[i].topic_filter != NULL)
                && (0 == iot_mc_check_handle_is_identical(&c->sub_handle[i], &topic_list[j]))) {
                memset(&c->sub_handle[i], 0, sizeof(iot_mc_topic_handle_t));

                /* NOTE: in case of more than one register(subscribe) with different callback function,
                *       so we must keep continuously searching related message handle */
            }
        } 
    }

    if (NULL != c->handle_event.h_fp) {
        iot_mqtt_event_msg_t msg;
        msg.event_type = IOT_MQTT_EVENT_UNSUBCRIBE_SUCCESS;
        msg.msg = (void *)(uintptr_t)mypacketid;

        c->handle_event.h_fp(c->handle_event.pcontext, c, &msg);
    }

    qm_mutex_unlock(&c->lock_generic);
    return SUCCESS_RETURN;
}


/* wait CONNACK packet from remote MQTT broker */
static int iot_mc_wait_CONNACK(iot_mc_client_t *c)
{

    unsigned char       wait_connack = 0;
    unsigned int        packetType = 0;
    int                 rc = 0;
    qm_utils_time_t         timer;

    if (!c) {
        return FAIL_RETURN;
    }

    qm_utils_time_init(&timer);
    qm_utils_time_countdown_ms(&timer, c->request_timeout_ms);

    do {
        /* read the socket, see what work is due */
        rc = iot_mc_read_packet(c, &timer, &packetType);
        if (rc != SUCCESS_RETURN) {
            mqtt_log_err("readPacket error,result = %d", rc);
            return MQTT_NETWORK_ERROR;
        }

       //todo:统一为IOT_MC_SUB_REQUEST_NUM_MAX宏
        if (++wait_connack > IOT_MC_SUB_REQUEST_NUM_MAX) {
            mqtt_log_err("wait connack exceeds maximum of %d", IOT_MC_SUB_REQUEST_NUM_MAX);
            return MQTT_NETWORK_ERROR;
        }
    } while (packetType != CONNACK);

    rc = iot_mc_handle_recv_CONNACK(c);
    if (SUCCESS_RETURN != rc) {
        mqtt_log_err("recvConnackProc error,result = %d", rc);
    }

    return rc;

}


/* MQTT cycle to handle packet from remote broker */
static int iot_mc_cycle(iot_mc_client_t *c, qm_utils_time_t *timer)
{
    unsigned int packetType;
    int rc = SUCCESS_RETURN;

    if (!c) {
        return FAIL_RETURN;
    }

    iot_mc_state_t state = iot_mc_get_client_state(c);
    if (state != IOT_MC_STATE_CONNECTED) {
        //mqtt_log_debug("state = %d", state);
        return MQTT_STATE_ERROR;
    }

    if (IOT_MC_KEEPALIVE_PROBE_MAX < c->keepalive_probes) {
        iot_mc_set_client_state(c, IOT_MC_STATE_DISCONNECTED);
        c->keepalive_probes = 0;
        mqtt_log_debug("keepalive_probes more than %u, disconnected\n", IOT_MC_KEEPALIVE_PROBE_MAX);
    }

    /* read the socket, see what work is due */
    rc = iot_mc_read_packet(c, timer, &packetType);
    if (rc != SUCCESS_RETURN) {
        iot_mc_set_client_state(c, IOT_MC_STATE_DISCONNECTED);
        mqtt_log_debug("readPacket error,result = %d", rc);
        return MQTT_NETWORK_ERROR;
    }

    if (MQTT_CPT_RESERVED == packetType) {
        /* mqtt_log_debug("wait data timeout"); */
        return SUCCESS_RETURN;
    }

    /* clear ping mark when any data received from MQTT broker */
    qm_mutex_lock(&c->lock_generic, QM_WAIT_FOREVER);
    c->ping_mark = 0;
    c->keepalive_probes = 0;
    qm_mutex_unlock(&c->lock_generic);

    switch (packetType) {
        case CONNACK: {
            mqtt_log_debug("CONNACK");
            break;
        }
        case PUBACK: {
            mqtt_log_debug("PUBACK");
            rc = iot_mc_handle_recv_PUBACK(c);
            if (SUCCESS_RETURN != rc) {
                mqtt_log_err("recvPubackProc error,result = %d", rc);
            }

            break;
        }
        case SUBACK: {
            mqtt_log_debug("SUBACK");
            rc = iot_mc_handle_recv_SUBACK(c);
            if (SUCCESS_RETURN != rc) {
                mqtt_log_err("recvSubAckProc error,result = %d", rc);
            }
            break;
        }
        case PUBLISH: {
            mqtt_log_debug("PUBLISH");
            /* HEXDUMP_DEBUG(c->buf_read, 32); */

            rc = iot_mc_handle_recv_PUBLISH(c);
            if (SUCCESS_RETURN != rc) {
                mqtt_log_err("recvPublishProc error,result = %d", rc);
            }
            break;
        }
        case UNSUBACK: {
            rc = iot_mc_handle_recv_UNSUBACK(c);
            if (SUCCESS_RETURN != rc) {
                mqtt_log_err("recvUnsubAckProc error,result = %d", rc);
            }
            break;
        }
        case PINGRESP: {
            rc = SUCCESS_RETURN;
            mqtt_log_info("receive ping response!");
            /* update to next time sending MQTT keep-alive */
            qm_utils_time_countdown_ms(&c->next_ping_time, c->connect_data.keepAliveInterval * 1000);
            break;
        }
        default:
            mqtt_log_err("INVALID TYPE");
            return FAIL_RETURN;
    }

    return rc;
}


/* check MQTT client is in normal state */
/* 0, in abnormal state; 1, in normal state */
static int iot_mc_check_state_normal(iot_mc_client_t *c)
{
    if (!c) {
        return 0;
    }

    if (iot_mc_get_client_state(c) == IOT_MC_STATE_CONNECTED) {
        return 1;
    }

    return 0;
}


/* return: 0, identical; NOT 0, different */
static int iot_mc_check_handle_is_identical(iot_mc_topic_handle_t *messageHandlers1,
        iot_mc_topic_handle_t *messageHandler2)
{
    if (!messageHandlers1 || !messageHandler2) {
        return 1;
    }

    int topicNameLen = strlen(messageHandlers1->topic_filter);

    if (topicNameLen != strlen(messageHandler2->topic_filter)) {
        return 1;
    }

    if (0 != strncmp(messageHandlers1->topic_filter, messageHandler2->topic_filter, topicNameLen)) {
        return 1;
    }

    if (messageHandlers1->handle.h_fp != messageHandler2->handle.h_fp) {
        return 1;
    }

    /* context must be identical also */
    if (messageHandlers1->handle.pcontext != messageHandler2->handle.pcontext) {
        return 1;
    }

    return 0;
}


/* subscribe */
static int iot_mc_subscribe(iot_mc_client_t *c, iot_mqtt_topic_t *topic_list, int count)
{
    int i = 0;
    if (NULL == c || NULL == topic_list || !count) {
        return NULL_VALUE_ERROR;
    }
    if(count > IOT_MC_SUB_NUM_MAX){
        return ERROR_NO_SUPPORT;
    }

    int rc = FAIL_RETURN;

    for(i = 0; i < count; i++){
        if (topic_list[i].qos > IOT_MQTT_QOS2) {
            mqtt_log_warning("Invalid qos(%d) out of [%d, %d], using %d",
                        topic_list[i].qos,
                        IOT_MQTT_QOS0, IOT_MQTT_QOS2, IOT_MQTT_QOS0);
            topic_list[i].qos = IOT_MQTT_QOS0;
        }
    }

    if (!iot_mc_check_state_normal(c)) {
        mqtt_log_err("mqtt client state is error,state = %d", iot_mc_get_client_state(c));
        return MQTT_STATE_ERROR;
    }

    for(i = 0; i < count; i++){
        if (0 != iot_mc_check_topic(topic_list[i].topic_filter, TOPIC_FILTER_TYPE)) {
            mqtt_log_err("NO%d topic format is error,topicFilter = %s", i + 1, topic_list[i].topic_filter);
            return MQTT_TOPIC_FORMAT_ERROR;
        }
    }

    unsigned int msgId = iot_mc_get_next_packetid(c);
    rc = MQTTSubscribe(c, msgId, topic_list, count);
    if (rc != SUCCESS_RETURN) {
        if (rc == MQTT_NETWORK_ERROR) {
            iot_mc_set_client_state(c, IOT_MC_STATE_DISCONNECTED);
        }

        mqtt_log_err("run MQTTSubscribe error");
        return rc;
    }
    for(i = 0; i < count; i++){
        mqtt_log_info("mqtt subscribe success,topic= %s", topic_list[i].topic_filter);
    }
    return msgId;
}


/* unsubscribe */
static int iot_mc_unsubscribe(iot_mc_client_t *c, const char *topicFilter[], int count)
{
    int rc = FAIL_RETURN;
    int i = 0;
    unsigned int msgId = iot_mc_get_next_packetid(c);

    if (NULL == c || NULL == topicFilter) {
        return NULL_VALUE_ERROR;
    }
    for(i = 0; i < count; i++){
        if (0 != iot_mc_check_topic(topicFilter[i], TOPIC_FILTER_TYPE)) {
            mqtt_log_err("No%d topic format is error, topicFilter = %s", i + 1, topicFilter);
            return MQTT_TOPIC_FORMAT_ERROR;
        }
    }

    if (!iot_mc_check_state_normal(c)) {
        mqtt_log_err("mqtt client state is error,state = %d", iot_mc_get_client_state(c));
        return MQTT_STATE_ERROR;
    }

    rc = MQTTUnsubscribe(c, msgId, topicFilter, count);
    if (rc != SUCCESS_RETURN) {
        if (rc == MQTT_NETWORK_ERROR) { /* send the subscribe packet */
            iot_mc_set_client_state(c, IOT_MC_STATE_DISCONNECTED);
        }

        mqtt_log_err("run MQTTUnsubscribe error!");
        return rc;
    }

    for(i = 0; i < count; i++){
        mqtt_log_info("mqtt unsubscribe success,topic = %s!", topicFilter[i]);
    }
    return (int)msgId;
}

/* publish */
static int iot_mc_publish(iot_mc_client_t *c, const char *topicName, iot_mqtt_topic_info_pt topic_msg, iot_mqtt_event_handle_t *handle_event)
{
    uint16_t msg_id = 0;
    int rc = FAIL_RETURN;

    if (NULL == c || NULL == topicName || NULL == topic_msg) {
        return NULL_VALUE_ERROR;
    }

    if (0 != iot_mc_check_topic(topicName, TOPIC_NAME_TYPE)) {
        mqtt_log_err("topic format is error,topicFilter = %s", topicName);
        return MQTT_TOPIC_FORMAT_ERROR;
    }

    if (!iot_mc_check_state_normal(c)) {
        mqtt_log_err("mqtt client state is error,state = %d", iot_mc_get_client_state(c));
        return MQTT_STATE_ERROR;
    }

    if (topic_msg->qos == IOT_MQTT_QOS1 || topic_msg->qos == IOT_MQTT_QOS2) {
        msg_id = iot_mc_get_next_packetid(c);
        topic_msg->packet_id = msg_id;
    }

    if (topic_msg->qos == IOT_MQTT_QOS2) {
        mqtt_log_err("MQTTPublish return error,MQTT_QOS2 is now not supported.");
        return MQTT_PUBLISH_QOS_ERROR;
    }
    /* payload encrypt by id2_aes */
    if (c->mqtt_up_process) {
        rc = c->mqtt_up_process((char *)topicName, topic_msg);
    }

#if defined(INSPECT_MQTT_FLOW)
    HEXDUMP_DEBUG(topic_msg->payload, topic_msg->payload_len);
#endif

    rc = MQTTPublish(c, topicName, topic_msg, handle_event);
    if (rc != SUCCESS_RETURN) { /* send the subscribe packet */
        if (rc == MQTT_NETWORK_ERROR) {
            iot_mc_set_client_state(c, IOT_MC_STATE_DISCONNECTED);
        }
        mqtt_log_err("MQTTPublish is error, rc = %d", rc);
        return rc;
    }

    return (int)msg_id;
}


/* get state of MQTT client */
static iot_mc_state_t iot_mc_get_client_state(iot_mc_client_t *pClient)
{


    iot_mc_state_t state;
    qm_mutex_lock(&pClient->lock_generic, QM_WAIT_FOREVER);
    state = pClient->client_state;
    qm_mutex_unlock(&pClient->lock_generic);

    return state;
}


/* set state of MQTT client */
static void iot_mc_set_client_state(iot_mc_client_t *pClient, iot_mc_state_t newState)
{

    qm_mutex_lock(&pClient->lock_generic, QM_WAIT_FOREVER);
    pClient->client_state = newState;
    qm_mutex_unlock(&pClient->lock_generic);
}


/* set MQTT connection parameter */
static int iot_mc_set_connect_params(iot_mc_client_t *pClient, MQTTPacket_connectData *pConnectParams)
{

    if (NULL == pClient || NULL == pConnectParams) {
        return NULL_VALUE_ERROR;
    }

    memcpy(pClient->connect_data.struct_id, pConnectParams->struct_id, 4);
    pClient->connect_data.struct_version = pConnectParams->struct_version;
    pClient->connect_data.MQTTVersion = pConnectParams->MQTTVersion;
    pClient->connect_data.clientID = pConnectParams->clientID;
    pClient->connect_data.cleansession = pConnectParams->cleansession;
    pClient->connect_data.willFlag = pConnectParams->willFlag;
    pClient->connect_data.username = pConnectParams->username;
    pClient->connect_data.password = pConnectParams->password;
    memcpy(pClient->connect_data.will.struct_id, pConnectParams->will.struct_id, 4);
    pClient->connect_data.will.struct_version = pConnectParams->will.struct_version;
    pClient->connect_data.will.topicName = pConnectParams->will.topicName;
    pClient->connect_data.will.message = pConnectParams->will.message;
    pClient->connect_data.will.qos = pConnectParams->will.qos;
    pClient->connect_data.will.retained = pConnectParams->will.retained;

    if (pConnectParams->keepAliveInterval < KEEP_ALIVE_INTERVAL_DEFAULT_MIN) {
        mqtt_log_warning("Input heartbeat interval(%d ms) < Allowed minimum(%d ms)",
                    (pConnectParams->keepAliveInterval * 1000),
                    (KEEP_ALIVE_INTERVAL_DEFAULT_MIN * 1000)
                   );
        mqtt_log_warning("Reset heartbeat interval => %d Millisecond",
                    (KEEP_ALIVE_INTERVAL_DEFAULT_MIN * 1000)
                   );
        pClient->connect_data.keepAliveInterval = KEEP_ALIVE_INTERVAL_DEFAULT_MIN;
    } else if (pConnectParams->keepAliveInterval > KEEP_ALIVE_INTERVAL_DEFAULT_MAX) {
        mqtt_log_warning("Input heartbeat interval(%d ms) > Allowed maximum(%d ms)",
                    (pConnectParams->keepAliveInterval * 1000),
                    (KEEP_ALIVE_INTERVAL_DEFAULT_MAX * 1000)
                   );
        mqtt_log_warning("Reset heartbeat interval => %d Millisecond",
                    (KEEP_ALIVE_INTERVAL_DEFAULT_MAX * 1000)
                   );
        pClient->connect_data.keepAliveInterval = KEEP_ALIVE_INTERVAL_DEFAULT_MAX;
    } else {
        pClient->connect_data.keepAliveInterval = pConnectParams->keepAliveInterval;
    }

    return SUCCESS_RETURN;
}


/* Initialize MQTT client */
static int iot_mc_init(iot_mc_client_t *pClient, iot_mqtt_param_t *params)
{
    int rc = FAIL_RETURN;
    iot_mc_state_t mc_state = IOT_MC_STATE_INVALID;

    if ((NULL == pClient) || (NULL == params)) {
        return NULL_VALUE_ERROR;
    }

    memset(pClient, 0x0, sizeof(iot_mc_client_t));

    MQTTPacket_connectData connectdata = MQTTPacket_connectData_initializer;

    connectdata.MQTTVersion = IOT_MC_MQTT_VERSION;
    connectdata.keepAliveInterval = params->keepalive_interval_ms / 1000;

    connectdata.clientID.cstring = (char *)params->client_id;
    connectdata.username.cstring = (char *)params->username;
    connectdata.password.cstring = (char *)params->password;
    connectdata.cleansession = params->clean_session;

    memset(pClient->sub_handle, 0, IOT_MC_SUB_NUM_MAX * sizeof(iot_mc_topic_handle_t));

    pClient->packet_id = 0;
    rc = qm_mutex_new(&pClient->lock_generic);
    if (rc != 0) {
        return FAIL_RETURN;
    }

    rc = qm_mutex_new(&pClient->lock_list_sub);
    if (rc != 0) {
        qm_mutex_free(&pClient->lock_generic);
        return FAIL_RETURN;
    }

    rc = qm_mutex_new(&pClient->lock_list_pub);
    if (rc != 0) {
        qm_mutex_free(&pClient->lock_generic);
        qm_mutex_free(&pClient->lock_list_sub);;
        return FAIL_RETURN;
    }

    if (params->request_timeout_ms < IOT_MC_REQUEST_TIMEOUT_MIN_MS
        || params->request_timeout_ms > IOT_MC_REQUEST_TIMEOUT_MAX_MS) {

        pClient->request_timeout_ms = IOT_MC_REQUEST_TIMEOUT_DEFAULT_MS;
    } else {
        pClient->request_timeout_ms = params->request_timeout_ms;
    }

    pClient->buf_send = params->pwrite_buf;
    pClient->buf_size_send = params->write_buf_size;
    pClient->buf_read = params->pread_buf;
    pClient->buf_size_read = params->read_buf_size;

    pClient->keepalive_probes = 0;

    pClient->handle_event.h_fp = params->handle_event.h_fp;
    pClient->handle_event.pcontext = params->handle_event.pcontext;

    /* Initialize reconnect parameter */
    pClient->reconnect_param.reconnect_time_interval_ms = IOT_MC_RECONNECT_INTERVAL_MIN_MS;

    pClient->list_pub_wait_ack = qm_list_new();
    if (pClient->list_pub_wait_ack) {
        pClient->list_pub_wait_ack->free = qm_free;
    }
    pClient->list_sub_wait_ack = qm_list_new();
    if (pClient->list_sub_wait_ack) {
        pClient->list_sub_wait_ack->free = qm_free;
    }

    qm_mutex_new(&pClient->lock_write_buf);
    rc = qm_sem_new(&pClient->yield_wait, 0);
    if (rc != QM_EOK) {
        goto RETURN;
    }

    /* Initialize MQTT connect parameter */
    rc = iot_mc_set_connect_params(pClient, &connectdata);
    if (SUCCESS_RETURN != rc) {
        mc_state = IOT_MC_STATE_INVALID;
        goto RETURN;
    }

    qm_utils_time_init(&pClient->next_ping_time);
    qm_utils_time_init(&pClient->reconnect_param.reconnect_next_time);

    pClient->ipstack = (util_network_pt)qm_malloc(sizeof(util_network_t));
    if (NULL == pClient->ipstack) {
        mqtt_log_err("allocate Network struct failed");
        rc = FAIL_RETURN;
        goto RETURN;
    }
    memset(pClient->ipstack, 0x0, sizeof(util_network_t));

    rc = util_net_init(pClient->ipstack, params->host, params->port, params->server_crt);
    if (SUCCESS_RETURN != rc) {
        mc_state = IOT_MC_STATE_INVALID;
        goto RETURN;
    }
    if(params->client_crt){
        util_net_set_client_cert_data(pClient->ipstack, params->client_crt, strlen(params->client_crt));
    }
    if(params->client_key){
        util_net_set_client_key_data(pClient->ipstack, params->client_key, strlen(params->client_key));
    }

    mc_state = IOT_MC_STATE_INITIALIZED;
    rc = SUCCESS_RETURN;
    mqtt_log_info("MQTT init success!");

RETURN :
    iot_mc_set_client_state(pClient, mc_state);
    if (rc != SUCCESS_RETURN) {
        if (pClient->list_pub_wait_ack) {
            pClient->list_pub_wait_ack->free(pClient->list_pub_wait_ack);
            pClient->list_pub_wait_ack = NULL;
        }
        if (pClient->list_sub_wait_ack) {
            pClient->list_sub_wait_ack->free(pClient->list_sub_wait_ack);
            pClient->list_sub_wait_ack = NULL;
        }
        if (pClient->ipstack) {
            qm_free(pClient->ipstack);
            pClient->ipstack = NULL;
        }
        if (qm_mutex_is_valid(&pClient->lock_generic)) {
            qm_mutex_free(&pClient->lock_generic);
        }
        if (qm_mutex_is_valid(&pClient->lock_list_sub)) {
            qm_mutex_free(&pClient->lock_list_sub);
        }
        if (qm_mutex_is_valid(&pClient->lock_list_pub)) {
            qm_mutex_free(&pClient->lock_list_pub);
        }
        if (qm_mutex_is_valid(&pClient->lock_write_buf)) {
            qm_mutex_free(&pClient->lock_write_buf);
        }
        if (qm_sem_is_valid(&pClient->yield_wait)) {
            qm_sem_free(&pClient->yield_wait);
        }
    }

    return rc;
}


/* remove node of list of wait subscribe ACK, which is in invalid state or timeout */
static int MQTTSubInfoProc(iot_mc_client_t *pClient)
{
    int rc = SUCCESS_RETURN;
    qm_list_iterator_t *iter;
    qm_list_node_t *node = NULL;
    qm_list_node_t *tempNode = NULL;
    uint16_t packet_id = 0;
    enum msgTypes msg_type;

    if (!pClient) {
        return FAIL_RETURN;
    }

    qm_mutex_lock(&pClient->lock_list_sub, QM_WAIT_FOREVER);
    do {
        if (0 == pClient->list_sub_wait_ack->len) {
            break;
        }

        if (NULL == (iter = qm_list_iterator_new(pClient->list_sub_wait_ack, LIST_TAIL))) {
            mqtt_log_err("new list failed");
            qm_mutex_unlock(&pClient->lock_list_sub);
            return SUCCESS_RETURN;
        }

        for (;;) {
            node = qm_list_iterator_next(iter);

            if (NULL != tempNode) {
                qm_list_remove(pClient->list_sub_wait_ack, tempNode);
                qm_list_node_destroy(tempNode);
                tempNode = NULL;
            }

            if (NULL == node) {
                break; /* end of list */
            }

            iot_mc_subsribe_info_t *subInfo = (iot_mc_subsribe_info_t *) node->val;
            if (NULL == subInfo) {
                mqtt_log_err("node's value is invalid!");
                tempNode = node;
                continue;
            }

            /* remove invalid node */
            if (IOT_MC_NODE_STATE_INVALID == subInfo->node_state) {
                tempNode = node;
                continue;
            }

            if (iot_mc_get_client_state(pClient) != IOT_MC_STATE_CONNECTED) {
                continue;
            }

            /* check the request if timeout or not */
            if (qm_utils_time_spend(&subInfo->sub_start_time) <= (pClient->request_timeout_ms * 2)) {
                /* continue to check the next node */
                continue;
            }

            /* When arrive here, it means timeout to wait ACK */
            packet_id = subInfo->msg_id;
            msg_type = subInfo->type;

             /* Wait MQTT SUBSCRIBE ACK timeout */
            if(pClient->req_sub_id == packet_id){
                pClient->is_sub_timeout = 1;
            }else if (NULL != pClient->handle_event.h_fp) {

                iot_mqtt_event_msg_t msg;

                if (SUBSCRIBE == msg_type) {
                    /* subscribe timeout */
                    msg.event_type = IOT_MQTT_EVENT_SUBCRIBE_TIMEOUT;
                    msg.msg = (void *)(uintptr_t)packet_id;
                } else { /* if (UNSUBSCRIBE == msg_type) */
                    /* unsubscribe timeout */
                    msg.event_type = IOT_MQTT_EVENT_UNSUBCRIBE_TIMEOUT;
                    msg.msg = (void *)(uintptr_t)packet_id;
                }

                pClient->handle_event.h_fp(pClient->handle_event.pcontext, pClient, &msg);
            }

            tempNode = node;
        }

        qm_list_iterator_destroy(iter);

    } while (0);

    qm_mutex_unlock(&pClient->lock_list_sub);

    return rc;
}


static void iot_mc_keepalive(iot_mc_client_t *pClient)
{
    int rc = 0;

    if (!pClient) {
        return;
    }

    /* Periodic sending ping packet to detect whether the network is connected */
    iot_mc_keepalive_sub(pClient);

    iot_mc_state_t currentState = iot_mc_get_client_state(pClient);
    do {
        /* if Exceeds the maximum delay time, then return reconnect timeout */
        if (IOT_MC_STATE_DISCONNECTED_RECONNECTING == currentState) {
            /* Reconnection is successful, Resume regularly ping packets */
            qm_mutex_lock(&pClient->lock_generic, QM_WAIT_FOREVER);
            pClient->ping_mark = 0;
            qm_mutex_unlock(&pClient->lock_generic);
            rc = iot_mc_handle_reconnect(pClient);
            if (SUCCESS_RETURN != rc) {
                if(FAIL_RETURN != rc){
                    mqtt_log_debug("reconnect network fail, rc = %d", rc);
                    iot_mc_reconnect_callback(pClient, rc);
                }
            } else {
                mqtt_log_info("network is reconnected!");
                iot_mc_reconnect_callback(pClient, rc);
                pClient->reconnect_param.reconnect_time_interval_ms = IOT_MC_RECONNECT_INTERVAL_MIN_MS;
            }

            break;
        }

        /* If network suddenly interrupted, stop pinging packet, try to reconnect network immediately */
        if (IOT_MC_STATE_DISCONNECTED == currentState) {
            mqtt_log_err("network is disconnected!");
            iot_mc_disconnect_callback(pClient);

            pClient->reconnect_param.reconnect_time_interval_ms = IOT_MC_RECONNECT_INTERVAL_MIN_MS;
            qm_utils_time_countdown_ms(&(pClient->reconnect_param.reconnect_next_time),
                                    pClient->reconnect_param.reconnect_time_interval_ms);

            pClient->ipstack->disconnect(pClient->ipstack);
            iot_mc_set_client_state(pClient, IOT_MC_STATE_DISCONNECTED_RECONNECTING);
            break;
        }

    } while (0);
}


/* republish */
static int __attribute__((unused)) MQTTRePublish(iot_mc_client_t *c, char *buf, int len)
{
    qm_utils_time_t timer;
    qm_utils_time_init(&timer);
    qm_utils_time_countdown_ms(&timer, c->request_timeout_ms);

    qm_mutex_lock(&c->lock_write_buf, QM_WAIT_FOREVER);

    if (iot_mc_send_packet(c, buf, len, &timer) != SUCCESS_RETURN) {
        qm_mutex_unlock(&c->lock_write_buf);
        return MQTT_NETWORK_ERROR;
    }

    qm_mutex_unlock(&c->lock_write_buf);
    return SUCCESS_RETURN;
}


/* remove node of list of wait publish ACK, which is in invalid state or timeout */
static int MQTTPubInfoProc(iot_mc_client_t *pClient)
{
    iot_mqtt_event_msg_t msg;
    iot_mc_state_t state = IOT_MC_STATE_INVALID;

    if (!pClient) {
        return FAIL_RETURN;
    }

    qm_mutex_lock(&pClient->lock_list_pub, QM_WAIT_FOREVER);
    do {
        if (0 == pClient->list_pub_wait_ack->len) {
            break;
        }

        qm_list_iterator_t *iter;
        qm_list_node_t *node = NULL;
        qm_list_node_t *tempNode = NULL;

        if (NULL == (iter = qm_list_iterator_new(pClient->list_pub_wait_ack, LIST_TAIL))) {
            mqtt_log_err("new list failed");
            break;
        }

        for (;;) {
            node = qm_list_iterator_next(iter);

            if (NULL != tempNode) {
                qm_list_remove(pClient->list_pub_wait_ack, tempNode);
                qm_list_node_destroy(tempNode);
                tempNode = NULL;
            }

            if (NULL == node) {
                break; /* end of list */
            }

            iot_mc_pub_info_t *repubInfo = (iot_mc_pub_info_t *) node->val;
            if (NULL == repubInfo) {
                mqtt_log_err("node's value is invalid!");
                tempNode = node;
                continue;
            }

            /* remove invalid node */
            if (IOT_MC_NODE_STATE_INVALID == repubInfo->node_state) {
                tempNode = node;
                continue;
            }

            state = iot_mc_get_client_state(pClient);
            if (state != IOT_MC_STATE_CONNECTED) {
                continue;
            }

            /* check the request if timeout or not */
            if (qm_utils_time_spend(&repubInfo->pub_start_time) <= (pClient->request_timeout_ms * 2)) {
                continue;
            }

            mqtt_log_err("publish timeout\r\n!!!");

            /* If wait ACK timeout, republish */

            #if 0
            qm_mutex_unlock(&pClient->lock_list_pub);
            rc = MQTTRePublish(pClient, (char *)repubInfo->buf, repubInfo->len);
            qm_utils_time_start(&repubInfo->pub_start_time);
            qm_mutex_lock(&pClient->lock_list_pub, QM_WAIT_FOREVER);

            if (MQTT_NETWORK_ERROR == rc) {
                iot_mc_set_client_state(pClient, IOT_MC_STATE_DISCONNECTED);
                break;
            }
            #endif

            /* call callback function to notify that PUBLISH is timeout */
            if(pClient->req_pub_id == (uint32_t)repubInfo->msg_id){
                pClient->is_pub_timeout = 1;
            }else if(repubInfo->handle_event.h_fp){
                msg.event_type = IOT_MQTT_EVENT_PUBLISH_TIMEOUT;
                msg.msg = (void *)(uintptr_t)(repubInfo->msg_id);
                repubInfo->handle_event.h_fp(repubInfo->handle_event.pcontext, pClient, &msg);
            }else if (NULL != pClient->handle_event.h_fp) {
                msg.event_type = IOT_MQTT_EVENT_PUBLISH_TIMEOUT;
                msg.msg = (void *)(uintptr_t)(repubInfo->msg_id);
                pClient->handle_event.h_fp(pClient->handle_event.pcontext, pClient, &msg);
            }

            tempNode = node;
        }

        qm_list_iterator_destroy(iter);

    } while (0);

    qm_mutex_unlock(&pClient->lock_list_pub);

    return SUCCESS_RETURN;
}


/* connect */
static int iot_mc_connect(iot_mc_client_t *pClient)
{
    int rc = FAIL_RETURN;

    if (NULL == pClient) {
        return NULL_VALUE_ERROR;
    }

    /* Establish TCP or TLS connection */
    rc = pClient->ipstack->connect(pClient->ipstack);
    if (SUCCESS_RETURN != rc) {
        pClient->ipstack->disconnect(pClient->ipstack);
        mqtt_log_err("TCP or TLS Connection failed");

        if (ERROR_CERTIFICATE_EXPIRED == rc) {
            mqtt_log_err("certificate is expired!");
            return ERROR_CERT_VERIFY_FAIL;
        } else {
            return MQTT_NETWORK_CONNECT_ERROR;
        }
    }

    if(pClient->connect_data.clientID.cstring){
        mqtt_log_debug("start MQTT connection with parameters: clientid=%s",
        pClient->connect_data.clientID.cstring);
    }

    if(pClient->connect_data.username.cstring){
        mqtt_log_debug("start MQTT connection with parameters: username=%s",
        pClient->connect_data.username.cstring);
    }

    if(pClient->connect_data.password.cstring){
        mqtt_log_debug("start MQTT connection with parameters: password=%s",
        pClient->connect_data.password.cstring);
    }

    rc = MQTTConnect(pClient);
    if (rc  != SUCCESS_RETURN) {
        pClient->ipstack->disconnect(pClient->ipstack);
        mqtt_log_err("send connect packet failed rc=%d", rc);
        return  rc;
    }

    if (SUCCESS_RETURN != iot_mc_wait_CONNACK(pClient)) {
        (void)MQTTDisconnect(pClient);
        pClient->ipstack->disconnect(pClient->ipstack);
        mqtt_log_err("wait connect ACK timeout, or receive a ACK indicating error!");
        return MQTT_CONNECT_ERROR;
    }

    iot_mc_set_client_state(pClient, IOT_MC_STATE_CONNECTED);

    qm_utils_time_countdown_ms(&pClient->next_ping_time, pClient->connect_data.keepAliveInterval * 1000);

    mqtt_log_debug("mqtt connect success!");
    return SUCCESS_RETURN;
}


static int iot_mc_attempt_reconnect(iot_mc_client_t *pClient)
{

    int rc;
    /* Ignoring return code. failures expected if network is disconnected */
    rc = iot_mc_connect(pClient);

    if (SUCCESS_RETURN != rc) {
        mqtt_log_err("run iot_mqtt_connect() error!");
        return rc;
    }

    return SUCCESS_RETURN;
}


/* reconnect */
static int iot_mc_handle_reconnect(iot_mc_client_t *pClient)
{
    int             rc = FAIL_RETURN;
    uint32_t        interval_ms = 0;

    if (NULL == pClient) {
        return NULL_VALUE_ERROR;
    }
    if (!qm_utils_time_is_expired(&(pClient->reconnect_param.reconnect_next_time))) {
        /* Timer has not expired. Not time to attempt reconnect yet. Return attempting reconnect */
        return FAIL_RETURN;
    }

    mqtt_log_debug("start reconnect");
    /* REDO AUTH before each reconnection */
    if (NULL != pClient->mqtt_auth && SUCCESS_RETURN != pClient->mqtt_auth()) {
        mqtt_log_err("redo authentication error!");
        return -1;
    }

    rc = iot_mc_attempt_reconnect(pClient);
    if (SUCCESS_RETURN == rc) {
        iot_mc_set_client_state(pClient, IOT_MC_STATE_CONNECTED);
        return SUCCESS_RETURN;
    } else {
        /* if reconnect network failed, then increase currentReconnectWaitInterval */
        /* e.g. init currentReconnectWaitInterval=1s, reconnect failed, then 2s..4s..8s */
        if (IOT_MC_RECONNECT_INTERVAL_MAX_MS > pClient->reconnect_param.reconnect_time_interval_ms) {
            pClient->reconnect_param.reconnect_time_interval_ms *= 2;
        } else {
            pClient->reconnect_param.reconnect_time_interval_ms = IOT_MC_RECONNECT_INTERVAL_MAX_MS;
        }
    }

    interval_ms = pClient->reconnect_param.reconnect_time_interval_ms;
    interval_ms += qm_random_get(pClient->reconnect_param.reconnect_time_interval_ms);
    if (IOT_MC_RECONNECT_INTERVAL_MAX_MS < interval_ms) {
        interval_ms = IOT_MC_RECONNECT_INTERVAL_MAX_MS;
    }
    qm_utils_time_countdown_ms(&(pClient->reconnect_param.reconnect_next_time), interval_ms);

    mqtt_log_err("mqtt reconnect failed rc = %d", rc);

    return rc;
}

static int iot_mc_disconnect(iot_mc_client_t *pClient)
{
    int rc = -1;

    if (NULL == pClient) {
        return NULL_VALUE_ERROR;
    }

    if (iot_mc_check_state_normal(pClient)) {
        rc = MQTTDisconnect(pClient);
        mqtt_log_debug("rc = MQTTDisconnect() = %d", rc);
    }

    /* close tcp/ip socket or free tls resources */
    pClient->ipstack->disconnect(pClient->ipstack);

    iot_mc_set_client_state(pClient, IOT_MC_STATE_INITIALIZED);

    mqtt_log_debug("mqtt disconnect!");
    return SUCCESS_RETURN;
}

static void iot_mc_disconnect_callback(iot_mc_client_t *pClient)
{

    if (NULL != pClient->handle_event.h_fp) {
        iot_mqtt_event_msg_t msg;
        msg.event_type = IOT_MQTT_EVENT_DISCONNECT;
        msg.msg = NULL;

        pClient->handle_event.h_fp(pClient->handle_event.pcontext,
                                   pClient,
                                   &msg);
    }
}


/* release MQTT resource */
static int iot_mc_release(iot_mc_client_t *pClient)
{

    if (NULL == pClient) {
        return NULL_VALUE_ERROR;
    }

    iot_mc_disconnect(pClient);
    iot_mc_set_client_state(pClient, IOT_MC_STATE_INVALID);

    qm_mutex_free(&pClient->lock_generic);
    qm_mutex_free(&pClient->lock_list_sub);
    qm_mutex_free(&pClient->lock_list_pub);
    qm_mutex_free(&pClient->lock_write_buf);
    if (qm_sem_is_valid(&pClient->yield_wait)) {
        qm_sem_free(&pClient->yield_wait);
    }

    qm_list_destroy(pClient->list_pub_wait_ack);
    qm_list_destroy(pClient->list_sub_wait_ack);

    if (NULL != pClient->ipstack) {
        qm_free(pClient->ipstack);
    }

    mqtt_log_debug("mqtt release!");
    return SUCCESS_RETURN;
}


static void iot_mc_reconnect_callback(iot_mc_client_t *pClient, int code)
{

    /* handle callback function */
    if (NULL != pClient->handle_event.h_fp) {
        iot_mqtt_event_msg_t msg;
        msg.event_type = IOT_MQTT_EVENT_RECONNECT;
        msg.msg = (void*)code;

        pClient->handle_event.h_fp(pClient->handle_event.pcontext,
                                   pClient,
                                   &msg);
    }
}

static int iot_mc_keepalive_sub(iot_mc_client_t *pClient)
{

    int rc = SUCCESS_RETURN;

    if (NULL == pClient) {
        return NULL_VALUE_ERROR;
    }
    /* if in disabled state, without having to send ping packets */
    if (!iot_mc_check_state_normal(pClient)) {
        return SUCCESS_RETURN;
    }

    /* if there is no ping_timer timeout, then return success */
    if (!qm_utils_time_is_expired(&pClient->next_ping_time)) {
        return SUCCESS_RETURN;
    }
    rc = MQTTKeepalive(pClient);
    if (SUCCESS_RETURN != rc) {
        if (rc == MQTT_NETWORK_ERROR) {
            iot_mc_set_client_state(pClient, IOT_MC_STATE_DISCONNECTED);
        }
        mqtt_log_err("ping outstanding is error,result = %d", rc);
        return rc;
    }

    mqtt_log_info("send MQTT ping...");

    qm_mutex_lock(&pClient->lock_generic, QM_WAIT_FOREVER);
    pClient->ping_mark = 1;
    pClient->keepalive_probes++;
    qm_mutex_unlock(&pClient->lock_generic);

    /* update to next time sending MQTT keep-alive */
    qm_utils_time_countdown_ms(&pClient->next_ping_time, 2 * pClient->request_timeout_ms);

    return SUCCESS_RETURN;
}


/************************  Public Interface ************************/
void *iot_mqtt_client_construct(iot_mqtt_param_t *params)
{
    int                 err;
    iot_mc_client_t   *pclient;

    g_iot_mqtt_last_error = SUCCESS_RETURN;

    POINTER_SANITY_CHECK(params, NULL);
    POINTER_SANITY_CHECK(params->pwrite_buf, NULL);
    POINTER_SANITY_CHECK(params->pread_buf, NULL);

    STRING_PTR_SANITY_CHECK(params->host, NULL);
    STRING_PTR_SANITY_CHECK(params->client_id, NULL);

    pclient = (iot_mc_client_t *)qm_malloc(sizeof(iot_mc_client_t));
    if (NULL == pclient) {
        mqtt_log_err("not enough memory.");
        g_iot_mqtt_last_error = FAIL_RETURN;
        return NULL;
    }

    err = iot_mc_init(pclient, params);
    if (SUCCESS_RETURN != err) {
        g_iot_mqtt_last_error = err;
        qm_free(pclient);
        return NULL;
    }
    pclient->mqtt_auth = NULL;

    err = iot_mc_connect(pclient);
    if (SUCCESS_RETURN != err) {
        g_iot_mqtt_last_error = err;
        iot_mc_release(pclient);
        qm_free(pclient);
        return NULL;
    }
    return pclient;
}

int iot_mqtt_client_last_error(void)
{
    return g_iot_mqtt_last_error;
}

int iot_mqtt_client_destroy(void **phandler)
{
    POINTER_SANITY_CHECK(phandler, NULL_VALUE_ERROR);
    POINTER_SANITY_CHECK(*phandler, NULL_VALUE_ERROR);

    iot_mc_release((iot_mc_client_t *)(*phandler));
    qm_free(*phandler);
    *phandler = NULL;

    return SUCCESS_RETURN;
}

int iot_mqtt_client_yield(void *handle, int timeout_ms)
{
    int                 rc = SUCCESS_RETURN;
    iot_mc_client_t   *pClient = (iot_mc_client_t *)handle;
    qm_utils_time_t         time;
    unsigned int left_t = 0;

    POINTER_SANITY_CHECK(handle, NULL_VALUE_ERROR);
    if (timeout_ms < 0) {
        mqtt_log_err("Invalid argument, timeout_ms = %d", timeout_ms);
        return -1;
    }
    if (timeout_ms == 0) {
        timeout_ms = 10;
    }

    qm_utils_time_init(&time);
    qm_utils_time_countdown_ms(&time, timeout_ms);

    do {
        if (SUCCESS_RETURN != rc) {
            left_t = qm_utils_time_left(&time);
            //mqtt_log_info("error occur ");
            if (left_t < 20)
                (void)qm_sem_wait(&pClient->yield_wait, left_t);
            else
                (void)qm_sem_wait(&pClient->yield_wait, 20);
        }

        /* Keep MQTT alive or reconnect if connection abort */
        iot_mc_keepalive(pClient);

        /* acquire package in cycle, such as PINGRESP or PUBLISH */
        rc = iot_mc_cycle(pClient, &time);
        if (SUCCESS_RETURN == rc) {
            /* check list of wait publish ACK to remove node that is ACKED or timeout */
            MQTTPubInfoProc(pClient);

            /* check list of wait subscribe(or unsubscribe) ACK to remove node that is ACKED or timeout */
            MQTTSubInfoProc(pClient);
        }

    } while (!qm_utils_time_is_expired(&time));

    return 0;
}

/* check whether MQTT connection is established or not */
int iot_mqtt_check_state_normal(void *handle)
{
    POINTER_SANITY_CHECK(handle, NULL_VALUE_ERROR);
    return iot_mc_check_state_normal((iot_mc_client_t *)handle);
}

int iot_mqtt_client_subscribe(void *handle,
                       const char *topic_filter,
                       iot_mqtt_qos_t qos,
                       iot_mqtt_event_handle_func_fpt topic_handle_func,
                       void *pcontext)
{
    iot_mqtt_topic_t mqtt_topic;

    POINTER_SANITY_CHECK(handle, NULL_VALUE_ERROR);
    POINTER_SANITY_CHECK(topic_handle_func, NULL_VALUE_ERROR);
    STRING_PTR_SANITY_CHECK(topic_filter, NULL_VALUE_ERROR);

    mqtt_topic.topic_filter = topic_filter;
    mqtt_topic.qos = qos;
    mqtt_topic.topic_handle_func = topic_handle_func;
    mqtt_topic.pcontext = pcontext;

    return iot_mc_subscribe((iot_mc_client_t *)handle, &mqtt_topic, 1);
}

int iot_mqtt_client_multi_subscribe(void *handle, iot_mqtt_topic_t *topic_list, int count)
{
    return iot_mc_subscribe((iot_mc_client_t *)handle, topic_list, count);
}

int iot_mqtt_client_generic_subscribe_wait_ack(void *handle, iot_mqtt_topic_t *topic_list, int count)
{
    int ret = SUCCESS_RETURN;
    iot_mc_client_t *c = (iot_mc_client_t *)handle; 

    ret = iot_mc_subscribe((iot_mc_client_t *)handle, topic_list, count);
    if(ret <= 0){
        return ret;
    }
    c->req_sub_id = (uint32_t)ret;

    ret = ERROR_NET_TIMEOUT;
    do{
        iot_mqtt_client_yield(handle, 200);
        if(c->req_sub_id == c->rsp_sub_id){
            ret = SUCCESS_RETURN;
            break;
        }
    }while(!c->is_sub_timeout);

    c->req_sub_id = 0;
    c->rsp_sub_id = 0;
    c->is_sub_timeout = 0;
    return ret;
}

int iot_mqtt_client_subscribe_wait_ack(void *handle,
                       const char *topic_filter,
                       iot_mqtt_qos_t qos,
                       iot_mqtt_event_handle_func_fpt topic_handle_func,
                       void *pcontext)
{
    iot_mqtt_topic_t mqtt_topic;
    POINTER_SANITY_CHECK(handle, NULL_VALUE_ERROR);
    POINTER_SANITY_CHECK(topic_handle_func, NULL_VALUE_ERROR);
    STRING_PTR_SANITY_CHECK(topic_filter, NULL_VALUE_ERROR);
    	
    mqtt_topic.topic_filter = topic_filter;
    mqtt_topic.qos = qos;
    mqtt_topic.topic_handle_func = topic_handle_func;
    mqtt_topic.pcontext = pcontext;

    return iot_mqtt_client_generic_subscribe_wait_ack(handle, &mqtt_topic, 1);
}

int iot_mqtt_client_multi_subscribe_wait_ack(void *handle, iot_mqtt_topic_t *topic_list, int count)
{
    return iot_mqtt_client_generic_subscribe_wait_ack(handle, topic_list, count);
}

int iot_mqtt_client_multi_unsubscribe(void *handle, const char *topic_filter[], int count)
{
    if(handle == NULL || topic_filter == NULL || count == 0){
        return NULL_VALUE_ERROR;
    }
    return iot_mc_unsubscribe((iot_mc_client_t *)handle, topic_filter, count);
}

int iot_mqtt_client_unsubscribe(void *handle, const char *topic_filter)
{
    POINTER_SANITY_CHECK(handle, NULL_VALUE_ERROR);
    STRING_PTR_SANITY_CHECK(topic_filter, NULL_VALUE_ERROR);

    return iot_mc_unsubscribe((iot_mc_client_t *)handle, &topic_filter, 1);
}

int iot_mqtt_client_publish(void *handle, const char *topic_name, const char *data, int len, int qos, int retain)
{
    iot_mqtt_topic_info_t topic_msg = {0};

    POINTER_SANITY_CHECK(handle, NULL_VALUE_ERROR);
    STRING_PTR_SANITY_CHECK(topic_name, NULL_VALUE_ERROR);

    if (qos > IOT_MQTT_QOS2) {
        mqtt_log_warning("Invalid qos(%d) out of [%d, %d], using %d",
                    qos,
                    IOT_MQTT_QOS0, IOT_MQTT_QOS2, IOT_MQTT_QOS0);
        qos = IOT_MQTT_QOS0;
    }

    topic_msg.qos = qos;
    topic_msg.retain = retain;
    topic_msg.ptopic = (char*)topic_name;
    topic_msg.payload = (char*)data;
    topic_msg.topic_len = (uint16_t)strlen(topic_name);
    topic_msg.payload_len = (uint16_t)len;

    return iot_mc_publish((iot_mc_client_t *)handle, topic_name, &topic_msg, NULL);
}

int iot_mqtt_client_publish_wait_ack(void *handle, const char *topic_name, const char *data, int len, int qos, int retain)
{
    int ret;
    iot_mc_client_t *c = (iot_mc_client_t *)handle; 
    iot_mqtt_topic_info_t topic_msg = {0};

    POINTER_SANITY_CHECK(handle, NULL_VALUE_ERROR);
    STRING_PTR_SANITY_CHECK(topic_name, NULL_VALUE_ERROR);

    if (qos > IOT_MQTT_QOS2) {
        mqtt_log_warning("Invalid qos(%d) out of [%d, %d], using %d",
                    qos,
                    IOT_MQTT_QOS0, IOT_MQTT_QOS2, IOT_MQTT_QOS0);
        qos = IOT_MQTT_QOS0;
    }

    topic_msg.qos = qos;
    topic_msg.retain = retain;
    topic_msg.ptopic = (char*)topic_name;
    topic_msg.payload = (char*)data;
    topic_msg.topic_len = (uint16_t)strlen(topic_name);
    topic_msg.payload_len = (uint16_t)len;

    ret = iot_mc_publish((iot_mc_client_t *)handle, topic_name, &topic_msg, NULL);
    if(ret <= 0){
        return ret;
    }
    c->req_pub_id = (uint32_t)ret;

    ret = ERROR_NET_TIMEOUT;

    do{
        iot_mqtt_client_yield(handle, 200);
        if(c->req_pub_id == c->rsp_pub_id){
            ret = SUCCESS_RETURN;
            break;
        }
    }while(!c->is_pub_timeout);

    c->req_pub_id = 0;
    c->rsp_pub_id = 0;
    c->is_pub_timeout = 0;

    return ret;
}

int iot_mqtt_client_publish_with_callback(void *handle, const char *topic_name, const char *data, int len, int qos, int retain, iot_mqtt_event_handle_func_fpt handle_func, void *pcontext)
{
    iot_mqtt_event_handle_t handle_event = {0};
    iot_mqtt_topic_info_t topic_msg = {0};

    POINTER_SANITY_CHECK(handle, NULL_VALUE_ERROR);
    STRING_PTR_SANITY_CHECK(topic_name, NULL_VALUE_ERROR);

    topic_msg.qos = qos;
    topic_msg.retain = retain;
    topic_msg.ptopic = (char*)topic_name;
    topic_msg.payload = (char*)data;
    topic_msg.topic_len = (uint16_t)strlen(topic_name);
    topic_msg.payload_len = (uint16_t)len;

    handle_event.h_fp = handle_func;
    handle_event.pcontext = pcontext;

    return iot_mc_publish((iot_mc_client_t *)handle, topic_name, &topic_msg, &handle_event);
}

/* handle SUBACK packet received from remote MQTT broker */
int iot_mqtt_client_auto_subscribe(void *handle,const char *topic_filter,
                                   iot_mqtt_event_handle_func_fpt topic_handle_func,void *pcontext)
{

    iot_mc_topic_handle_t handler= {topic_filter,{topic_handle_func, pcontext}};
    iot_mc_client_t   *c = (iot_mc_client_t *)handle;
    int i_free = -1, flag_dup = 0;
    int i;
    POINTER_SANITY_CHECK(handle, NULL_VALUE_ERROR);
    POINTER_SANITY_CHECK(topic_handle_func, NULL_VALUE_ERROR);
    STRING_PTR_SANITY_CHECK(topic_filter, NULL_VALUE_ERROR);

     if (0 != iot_mc_check_topic(topic_filter, TOPIC_FILTER_TYPE)) {
          mqtt_log_err("topic format is error,topicFilter = %s", topic_filter);
          return MQTT_TOPIC_FORMAT_ERROR;
     }

    qm_mutex_lock(&c->lock_generic, QM_WAIT_FOREVER);

    for (i = 0; i < IOT_MC_SUB_NUM_MAX; ++i) {
        /* If subscribe the same topic and callback function, then ignore */
        if ((NULL != c->sub_handle[i].topic_filter)) {
            if (0 == iot_mc_check_handle_is_identical(&c->sub_handle[i], &handler)) {
                /* if subscribe a identical topic and relate callback function, then ignore this subscribe */
                flag_dup = 1;
                mqtt_log_err("There is a identical topic and related handle in list!");
                break;
            }
        } else {
            if (-1 == i_free) {
                i_free = i; /* record available element */
            }
        }
    }

    if (0 == flag_dup) {
        if (-1 == i_free) {
            mqtt_log_err("NOT more @sub_handle space!");
            qm_mutex_unlock(&c->lock_generic);
            return FAIL_RETURN;
        } else {
            c->sub_handle[i_free].topic_filter = handler.topic_filter;
            c->sub_handle[i_free].handle.h_fp = handler.handle.h_fp;
            c->sub_handle[i_free].handle.pcontext = handler.handle.pcontext;
        }
    }

    qm_mutex_unlock(&c->lock_generic);

    return SUCCESS_RETURN;
}


int iot_mqtt_client_auto_unsubscribe(void *handle, const char *topic_filter)
{
    int i = 0;
    iot_mc_client_t *c = (iot_mc_client_t*)handle;

    if (!c) {
        return FAIL_RETURN;
    }
    /* Remove from message handler array */
    qm_mutex_lock(&c->lock_generic, QM_WAIT_FOREVER);
    for (i = 0; i < IOT_MC_SUB_NUM_MAX; ++i) {
        if (c->sub_handle[i].topic_filter != NULL && 
            strlen(c->sub_handle[i].topic_filter) == strlen(topic_filter) &&
            0 == strcmp(c->sub_handle[i].topic_filter, topic_filter)) {
            memset(&c->sub_handle[i], 0, sizeof(iot_mc_topic_handle_t));
        }
    }
    qm_mutex_unlock(&c->lock_generic);
    return SUCCESS_RETURN;
}
