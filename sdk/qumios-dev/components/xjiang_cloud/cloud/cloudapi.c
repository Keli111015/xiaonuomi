#include "cloudapi.h"
#include "cloud_common.h"
#include "qm_utils_list.h"
#include "qm_iot_import.h"
#include "iot_cloud_debug.h"
#include "product.h"
#include "qm_utils_string.h"
#include "qm_utils_base64.h"

#define MQTT_REPORT_FORMAT          "{\"devid\":%d,\"method\":\"%s\",\"id\":\"%s\",\"version\":\"1.0\",\"params\":%s}"
#define MQTT_GENERIC_REPLY_FORMAT   "{\"devid\":%d,\"method\":\"%s\",\"id\":\"%s\",\"version\":\"1.0\",\"code\":%d}"
#define MQTT_RAW_FORMAT             "{\"devid\":%d,\"id\":\"%s\",\"params\":\"%s\"}"
#define MQTT_RAW_REPLAY_FORMAT      "{\"devid\":%d,\"id\":\"%s\",\"code\":%d}"
#define MQTT_TOKEN_REPORT_FORMAT    "{\"devid\":%d,\"method\":\"%s\",\"id\":\"%s\",\"version\":\"1.0\",\"params\":{\"token\":\"%s\"}}"
#define MQTT_GENERIC_EVENT_FORMAT   "{\"devid\":%d,\"method\":\"%s\",\"id\":\"%s\",\"version\":\"1.0\"}"


#define MAX_JSON_DATA_LEN    (128)

static cloud_context_t *g_cloud_context = NULL;

const char* sdk_version_get(void)
{
    return SDK_VERSION;
}


char *app_version_get(void)
{
    cloud_context_t *cloud_context = NULL;
    cloud_context = cloud_context_get();
    if(cloud_context == NULL){
        return NULL;
    }
    return cloud_context->product_info.version;
}

char *mcu_version_get(void)
{
    cloud_context_t *cloud_context = NULL;
    cloud_context = cloud_context_get();
    if(cloud_context == NULL){
        return NULL;
    }
    return cloud_context->product_info.mcu_ver;
}

int32_t mcu_version_set(char *version)
{
    cloud_context_t *cloud_context = NULL;
    if(version == NULL){
        return PARAM_ERR;
    }
    if(strlen(version) > DEVICE_VERSION_LEN){
        return PARAM_ERR;
    }

    cloud_context = cloud_context_get();
    if(cloud_context == NULL){
        return NO_INIT_ERR;
    }

    memcpy(cloud_context->product_info.mcu_ver, version, strlen(version));

    return NO_ERR;
}

int32_t app_version_set(char *version)
{
    cloud_context_t *cloud_context = NULL;
    if(version == NULL){
        return PARAM_ERR;
    }
    if(strlen(version) > DEVICE_VERSION_LEN){
        return PARAM_ERR;
    }

    cloud_context = cloud_context_get();
    if(cloud_context == NULL){
        return NO_INIT_ERR;
    }
    memcpy(cloud_context->product_info.version, version, strlen(version));

    return NO_ERR;
}


void *cloud_context_get(void)
{
    return g_cloud_context;
}

void cloud_context_set(cloud_context_t *context)
{
   g_cloud_context = context;
}

uint16_t packet_id_get(void)
{
    uint16_t packet_id = 0;
    cloud_context_t *cloud_context = NULL;
    cloud_context = cloud_context_get();
    hal_mutex_lock(cloud_context->lock);
    cloud_context->packet_id  = (cloud_context->packet_id == PACKET_ID_MAX) ? 1 : cloud_context->packet_id + 1;
    hal_mutex_unlock(cloud_context->lock);
    return cloud_context->packet_id;
}


static int32_t _iot_slave_open(iot_dev_meta_info_t *meta_info)
{
    return NO_ERR;
}

static int32_t _iot_master_open(iot_dev_meta_info_t *meta_info)
{
    cloud_context_t *cloud_context = NULL;
    char pid_str[PRODUCT_ID_LEN+1] = {0};

    cloud_context = cloud_context_get();
    if(cloud_context == NULL){
        cloud_context = (cloud_context_t*)hal_malloc(sizeof(cloud_context_t));
        if(cloud_context == NULL){
            return ALLOC_MEM_ERR;
        }

        memset(cloud_context, 0, sizeof(cloud_context_t));
        g_cloud_context = cloud_context;
    }

    cloud_context->product_info.product_id = meta_info->product_id;
    memcpy(cloud_context->product_info.device_id, meta_info->device_id, strlen(meta_info->device_id));
    memcpy(cloud_context->product_info.device_secret, meta_info->device_secret, strlen(meta_info->device_secret));

    cloud_context->loop_timeout = NORMAL_LOOP_TIME;

    cloud_context->lock = hal_mutex_create();
    cloud_context->push_list = qm_list_new();
    cloud_context->push_ack_list = qm_list_new();

    return NO_ERR;
}

int32_t cloud_open(iot_dev_type_t dev_type, iot_dev_meta_info_t *meta_info)
{
    int32_t res = 0;

    if (dev_type < 0 || dev_type >= IOT_DEV_TYPE_MAX || meta_info == NULL) {
        return PARAM_ERR;
    }

    switch (dev_type) {
        case IOT_DEV_TYPE_MASTER: {
            res = _iot_master_open(meta_info);
            if(res == NO_ERR){
                res = IOT_MASTER_DEVID;
            }
        }
        break;
        case IOT_DEV_TYPE_SLAVE: {
#ifdef DEVICE_GATEWAY_SUPPORT
            res = _iot_slave_open(meta_info);
#else
            res = DEVICE_TYPE_ERR;
#endif
        }
        break;
        default: {
            res = DEVICE_TYPE_ERR;
        }
        break;
    }

    return res;       
}

int32_t cloud_connect(void)
{
    int rc = 0;
    cloud_context_t *cloud_context=NULL;

    cloud_context = cloud_context_get();
    if(cloud_context == NULL){
        return NO_INIT_ERR;
    }

    rc = mqtt_client(cloud_context);
    if(rc < 0){
        return NETWORK_ERR;
    }

    return NO_ERR;
}


static int32_t payload_creat(int devid, msg_type_t msg_type, char *id, char *method, int code, uint8_t *payload, int payload_len, uint8_t *out, int *out_len)
{

    uint8_t *buf = NULL;
    int32_t err_code = 0;
    uint32_t len = 0;
    uint32_t base64_len = 0;
    uint32_t base64_out_len = 0;

    char *out_buf = (char*)out;

    switch(msg_type){

#ifdef STANDARD_SUPPORT        

        case MSG_TYPE_REPORT:

            if(payload == NULL || payload_len == 0){
                err_code = PARAM_ERR;
                goto exit;
            }
            
            len = MAX_JSON_DATA_LEN + payload_len;
            *out_len = len;
            if(out == NULL){
                goto exit;
            }
            
            if(payload == NULL || payload_len == 0){
                err_code = PARAM_ERR;
                goto exit;
            }

            hal_snprintf(out_buf, len, MQTT_REPORT_FORMAT, devid, "report", id, payload);
            *out_len = strlen(out_buf);
                       
        break;

        case MSG_TYPE_SET_REPLY:
            
            len = MAX_JSON_DATA_LEN;
            *out_len = len;
            if(out == NULL){
                goto exit;
            }
            
            hal_snprintf(out_buf, len, MQTT_GENERIC_REPLY_FORMAT, devid, "set_reply", id, code);
            *out_len = strlen(out_buf);
           
        break;
#endif

#ifdef RAW_SUPPORT
        case MSG_TYPE_RAW_DATA_REPORT:

            if(payload == NULL || payload_len == 0){
                err_code = PARAM_ERR;
                goto exit;
            }

            base64_len = 4* ((payload_len + 2) / 3);
                  
            len = MAX_JSON_DATA_LEN + base64_len;
            *out_len = len;
            if(out == NULL){
                goto exit;
            }
        
            buf = (uint8_t*)hal_malloc(base64_len+1);
            if(buf == NULL){
                err_code = ALLOC_MEM_ERR;
                goto exit;
            }

            qm_utils_base64encode((const uint8_t*)payload, payload_len, base64_len, buf, &base64_out_len);

            buf[base64_len] = '\0';
            
            hal_snprintf(out_buf, len, MQTT_RAW_FORMAT, devid, id, (char*)buf);
            *out_len = strlen(out_buf);
            hal_free(buf);
    
        break;

        case MSG_TYPE_RAW_DATA_REPLY:
            
            len = MAX_JSON_DATA_LEN;
            *out_len = len;
            if(out == NULL){
                goto exit;
            }

            hal_snprintf(out_buf, len, MQTT_RAW_REPLAY_FORMAT, devid, id, code);
            *out_len = strlen(out_buf);

        break;
#endif

        case MSG_TYPE_TOKEN_REPORT:
            
            if(payload == NULL || payload_len == 0){
                err_code = PARAM_ERR;
                goto exit;
            }
            
            len = MAX_JSON_DATA_LEN + payload_len;
            *out_len = len;
            if(out == NULL){
                goto exit;
            }

            hal_snprintf(out_buf, len, MQTT_TOKEN_REPORT_FORMAT, devid, "bind_token", id, payload);
            *out_len = strlen(out_buf);

        break;

        case MSG_TYPE_EVENT_NOTIFY_REPLY:
            
            len = MAX_JSON_DATA_LEN;
            *out_len = len;
            if(out == NULL){
                goto exit;
            }
            
            hal_snprintf(out_buf, len, MQTT_GENERIC_REPLY_FORMAT, devid, method, id, code);
            *out_len = strlen(out_buf);

        break;

        case MSG_TYPE_RESET:
            
            len = MAX_JSON_DATA_LEN;
            *out_len = len;
            if(out == NULL){
                goto exit;
            }
            
            hal_snprintf(out_buf, len, MQTT_GENERIC_EVENT_FORMAT, devid, "reset", id);
            *out_len = strlen(out_buf);
            
        break;

        default:
        break;
    }
    
exit:
    return err_code;
}


/*qm_list_node_t + packet_info_t + payload*/

int32_t cloud_generic_send(int devid, msg_type_t msg_type, char *id, char* method, int code, unsigned char *payload, int payload_len)
{
    int32_t err_code = NO_ERR;
    uint8_t *buf = NULL;
    packet_info_t *packet_info = NULL;
    qm_list_node_t *node = NULL;
    uint8_t *data = NULL;
    char id_str[8] = {0}; 
    int need_len = 0;

    cloud_context_t *cloud_context = cloud_context_get();

    if(msg_type <= MSG_TYPE_MIN || msg_type >= MSG_TYPE_MAX){
        return PARAM_ERR;
    }

    if(cloud_context == NULL){
        return NO_INIT_ERR;
    }

    if(!cloud_context->is_login){
        return DEVICE_LOGOUT;
    }

    err_code = payload_creat(devid, msg_type, id, method, code, payload, payload_len, NULL, &need_len);
    if(err_code !=NO_ERR){
        goto exit;
    }

    buf = (uint8_t*)hal_malloc(sizeof(qm_list_node_t) + sizeof(packet_info_t) + need_len);
    if(buf == NULL){
        err_code = ALLOC_MEM_ERR;
        goto exit;
    }

    node = (qm_list_node_t*)buf;
    packet_info = (packet_info_t*)(buf+sizeof(qm_list_node_t));
    data = (uint8_t*)(buf+sizeof(qm_list_node_t)+sizeof(packet_info_t));

    if(id == NULL){
        packet_info->packet_id = packet_id_get();
        hal_snprintf(id_str, 8, "%d", packet_info->packet_id);
        payload_creat(devid, msg_type, id_str, method, code, payload, payload_len, data, &need_len);
    }else{
        payload_creat(devid, msg_type, id, method, code, payload, payload_len, data, &need_len);
    }

    packet_info->data = data;
    packet_info->len = need_len;
    packet_info->msg_type = msg_type;

    hal_mutex_lock(cloud_context->lock);

    node->val = buf+sizeof(qm_list_node_t);

    qm_list_rpush(cloud_context->push_list, node);
    hal_mutex_unlock(cloud_context->lock);

    return packet_info->packet_id;

 exit:

     if(buf){
         hal_free(buf);
     }

     return err_code;
}


int32_t cloud_packet_send(int devid, msg_type_t msg_type, unsigned char* payload, int payload_len)
{
    return cloud_generic_send(devid, msg_type, NULL, NULL, 0, payload, payload_len);
}

int32_t cloud_packet_reply_send(int devid, msg_type_t msg_type, int id, int code, unsigned char* payload, int payload_len)
{
    char id_str[16] = {0};
    hal_snprintf(id_str, 16, "%d", id);
    return cloud_generic_send(devid, msg_type, id_str, NULL, code, payload, payload_len);
}


int32_t cloud_token_report(char* token)
{
    if(token == NULL){
        return PARAM_ERR;
    }
    return cloud_generic_send(IOT_MASTER_DEVID, MSG_TYPE_TOKEN_REPORT, NULL, NULL, 0, (unsigned char*)token, strlen(token));
}


int32_t cloud_device_reset(void)
{
    return cloud_generic_send(IOT_MASTER_DEVID, MSG_TYPE_RESET, NULL, NULL, 0, NULL, 0);
}


int32_t cloud_event_reply(int devid, char* id, char* method, int code, unsigned char* payload, int payload_len)
{
    if(id == NULL && method == NULL){
        return PARAM_ERR;
    }
    return cloud_generic_send(devid, MSG_TYPE_EVENT_NOTIFY_REPLY, id, method, code, payload, payload_len);
}


int32_t push_ack_info_add(cloud_context_t *cloud_context, packet_info_t *push_info)
{
    qm_list_node_t *node = NULL;
    if (!cloud_context || !push_info) {
        return PARAM_ERR;
    }

   hal_mutex_lock(cloud_context->lock);

   node = qm_list_node_new(push_info);
   if(!node)
   {
       hal_mutex_unlock(cloud_context->lock);
       return ALLOC_MEM_ERR;
   }
   qm_list_rpush(cloud_context->push_ack_list, node);

   hal_mutex_unlock(cloud_context->lock);

   return NO_ERR;
}


/* remove node of list of wait publish ACK, which is in invalid state or timeout */
int32_t push_ack_info_remove(cloud_context_t *cloud_context, uint16_t mqtt_packet_id, packet_info_t **push_info)
{
    qm_list_iterator_t *iter=NULL;
    qm_list_node_t *node = NULL;
    qm_list_node_t *tempNode = NULL;
    packet_info_t *push_packet= NULL;

    if (!cloud_context ) {
        return PARAM_ERR;
    }

    hal_mutex_lock(cloud_context->lock);
    if (cloud_context->push_ack_list->len) {

        if (NULL == (iter = qm_list_iterator_new(cloud_context->push_ack_list, LIST_TAIL))) {
            hal_mutex_unlock(cloud_context->lock);
            return ALLOC_MEM_ERR;
        }

        for (;;) {
            node = qm_list_iterator_next(iter);

            if (NULL != tempNode) {
                qm_list_remove(cloud_context->push_ack_list, tempNode);
                qm_list_node_destroy(tempNode);
                tempNode = NULL;
            }

            if (NULL == node) {
               break; /* end of list */
            }

            push_packet = (packet_info_t *) node->val;
            if (NULL == push_packet) {
                             
                tempNode = node;
                continue;
            }

            if (push_packet->mqtt_packet_id == mqtt_packet_id) {

                *push_info=push_packet;
                tempNode = node;
            }
        }

        qm_list_iterator_destroy(iter);
    }
    hal_mutex_unlock(cloud_context->lock);

    return NO_ERR;
}
