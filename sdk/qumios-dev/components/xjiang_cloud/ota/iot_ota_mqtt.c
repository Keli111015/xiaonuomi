
#ifndef __OTA_MQTT_C_H__
#define __OTA_MQTT_C_H__


#include "iot_ota.h"
#include "iot_ota_internal.h"
#include "iot_mqtt.h"
#include "cloud_common.h"

#if CONFIG_OTA_SUPPORT

/* OSC, OTA signal channel */

/* Specify the maximum characters of version */
#define OTA_MQTT_TOPIC_LEN   (64)

typedef struct  {
    void *mqtt;
    unsigned int product_id;
    const char *device_id;
    char topic_upgrade[OTA_MQTT_TOPIC_LEN];
    ota_cb_fpt cb;
    void *context;
}otamqtt_Struct_t, *otamqtt_Struct_pt;


/* Generate topic name according to @ota_topic_type, @product_id, @device_name */
/* and then copy to @buf. */
/* 0, successful; -1, failed */
static int otamqtt_GenTopicName(char *buf, size_t buf_len, const char *ota_topic_type, unsigned int product_id, const char *device_name)
{
    int ret;

    ret = hal_snprintf(buf,
            buf_len,
            "/%u/%s/ota/device/%s",
            product_id,
            device_name,
            ota_topic_type);

    if (ret < 0) {
        OTA_LOG_ERROR("hal_snprintf failed");
        return -1;
    }

    return 0;
}

/* report progress of OTA */
static int otamqtt_Publish(otamqtt_Struct_pt handle, const char *topic_type, int qos, const char *msg)
{
    int ret;
    char topic_name[OTA_MQTT_TOPIC_LEN];
    iot_mqtt_topic_info_t topic_info;

    memset(&topic_info, 0, sizeof(iot_mqtt_topic_info_t));

    if (0 == qos) {
        topic_info.qos = IOT_MQTT_QOS0;
    } else {
        topic_info.qos = IOT_MQTT_QOS1;
    }
    topic_info.payload = (void *)msg;
    topic_info.payload_len = strlen(msg);

    /* inform OTA to topic: "ota/device/progress/$(product_id)/$(device_id)/" */
    ret = otamqtt_GenTopicName(topic_name, OTA_MQTT_TOPIC_LEN, topic_type, handle->product_id, handle->device_id);
    if (ret < 0) {
        OTA_LOG_ERROR("generate topic name of info failed");
        return -1;
    }

    ret = iot_mqtt_publish(handle->mqtt, topic_name, &topic_info);
    if (ret < 0) {
        OTA_LOG_ERROR("publish failed");
        return IOT_OTAE_OSC_FAILED;
    }

    return 0;
}


/* decode JSON string to get firmware information, like firmware version, URL, file size, MD5. */
/* return NONE */
static void otamqtt_UpgrageCb(void *pcontext, void *pclient, iot_mqtt_event_msg_pt msg)
{
    otamqtt_Struct_pt handle = (otamqtt_Struct_pt) pcontext;
    iot_mqtt_topic_info_pt topic_info = (iot_mqtt_topic_info_pt)msg->msg;

    OTA_LOG_DEBUG("topic=%.*s", topic_info->topic_len, topic_info->ptopic);
    OTA_LOG_DEBUG("len=%u, topic_msg=%.*s", topic_info->payload_len, topic_info->payload_len, (char *)topic_info->payload);

    if (NULL != handle->cb) {
        handle->cb(handle->context, topic_info->payload, topic_info->payload_len);
    }
}


void *qm_osc_Init(unsigned int product_id, const char *device_id, void *ch_signal, ota_cb_fpt cb, void *context)
{
    int ret;
    otamqtt_Struct_pt h_osc = NULL;

    if (NULL == (h_osc = OTA_MALLOC(sizeof(otamqtt_Struct_t)))) {
        OTA_LOG_ERROR("allocate for h_osc failed");
        return NULL;
    }

    memset(h_osc, 0, sizeof(otamqtt_Struct_t));

    /* subscribe the OTA topic: "/$(product_id)/$(device_name)/ota/device/upgrade" */
    ret = otamqtt_GenTopicName(h_osc->topic_upgrade, OTA_MQTT_TOPIC_LEN, "upgrade", product_id, device_id);
    if (ret < 0) {
        OTA_LOG_ERROR("generate topic name of upgrade failed");
        goto do_exit;
    }
    
    ret = iot_mqtt_auto_subscribe(ch_signal, h_osc->topic_upgrade, otamqtt_UpgrageCb, h_osc);
        if (ret < 0) {
            OTA_LOG_ERROR("mqtt subscribe failed");
            goto do_exit;
     }

    h_osc->mqtt = ch_signal;
    h_osc->product_id = product_id;
    h_osc->device_id = device_id;
    h_osc->cb = cb;
    h_osc->context = context;

    return h_osc;

do_exit:
    if (NULL != h_osc) {
         OTA_FREE(h_osc);
    }

    return NULL;
}


int qm_osc_Deinit(void *handle)
{
    if (NULL != handle) {
        OTA_FREE(handle);
    }

    return 0;
}

/* report progress of OTA */
int qm_osc_ReportProgress(void *handle, const char *msg)
{
    OTA_LOG_INFO("report firmware version %s", msg);
    return otamqtt_Publish(handle, "progress", 0, msg);
}


/* report version of OTA firmware */
int qm_osc_ReportVersion(void *handle, const char *msg)
{
    OTA_LOG_INFO("report firmware version %s", msg);
    return otamqtt_Publish(handle, "inform", 1, msg);
}

#endif
#endif
