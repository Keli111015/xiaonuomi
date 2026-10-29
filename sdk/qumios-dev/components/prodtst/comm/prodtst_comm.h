#ifndef PRODTST_COMM_H
#define PRODTST_COMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "prodtst_common.h"
#include "qm_config.h" 

#ifndef CONFIG_PRODTST_OTA_SUPPORT
#define CONFIG_PRODTST_OTA_SUPPORT   1
#endif

#ifndef CONFIG_PRODTST_MQTT_HOST
#define CONFIG_PRODTST_MQTT_HOST "prodtest-emq.xjiangiot.com" 
#endif

#ifndef CONFIG_PRODTST_MQTT_TEST_HOST
#define CONFIG_PRODTST_MQTT_TEST_HOST "218.0.1.222" 
#endif

#ifndef CONFIG_PRODTST_MQTT_PORT
#define CONFIG_PRODTST_MQTT_PORT   21883 
#endif

#define PRODTST_VERSION_MAX_LEN         (8)
#define PRODTST_PRODUCT_ID_MAX_LEN      (8)
#define PRODTST_DEVICE_ID_MAX_LEN       (16)
#define PRODTST_DEVICE_SECRET_MAX_LEN   (32)

typedef enum{
    PRODTST_COMM_EVENT_LOGIN,
    PRODTST_COMM_EVENT_LOGOUT,
    PRODTST_COMM_EVENT_OTA_START,
    PRODTST_COMM_EVENT_OTA_SUCCESS,
    PRODTST_COMM_EVENT_OTA_FAIL
}prodtst_comm_event_t;

typedef struct{
    void (*event_notify)(prodtst_comm_event_t event);
    void (*msg_notify)(prodtst_comm_msg_type_t msg_type, char *data, int len);
}prodtst_comm_hooks_t;


void *prodtst_comm_init(char *version, char *product_id, char *device_id, char *device_secret, prodtst_comm_hooks_t *hooks);
int prodtst_comm_send(void *handle, prodtst_comm_msg_type_t msg_type, char *data, int len);
int prodtst_comm_deinit(void *handle);

#ifdef __cplusplus
}
#endif


#endif /* PRODTST_COMM_H */