#ifndef LJYD_CLOUD_H
#define LJYD_CLOUD_H

#include "qm_spec_api.h"

/* 灵机平台产品身份。换产品改这里。 */
/*
 * 说明：产品身份由各开发同学按自己云平台产品覆盖使用
 */
#include "ljyd_cloud_local.h"
#if !defined(CONFIG_XJIANG_CLOUD_PID) || !defined(CONFIG_XJIANG_CLOUD_SECERT)
#error "Configure your own ljyd_cloud_local.h before building"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LJYD_CLOUD_EVENT_CONNECTED = 0,
    LJYD_CLOUD_EVENT_DISCONNECTED,
} ljyd_cloud_event_t;

typedef void (*ljyd_cloud_event_cb_t)(ljyd_cloud_event_t event, void *iot_handle, void *userdata);
typedef void (*ljyd_cloud_prop_cb_t)(qm_spec_property_t *property);

int ljyd_cloud_init(void);
void *ljyd_cloud_get_handle(void);
void ljyd_cloud_set_event_cb(ljyd_cloud_event_cb_t cb, void *userdata);
/* 属性下发。不设置则云只打日志。iot_control 在自己的 init 里挂上。 */
void ljyd_cloud_set_prop_cb(ljyd_cloud_prop_cb_t cb);
void ljyd_cloud_on_network_up(void);
void ljyd_cloud_on_network_down(void);
/* 返回 QM_EOK 表示云已接手解绑，等 RESET 事件再清配网。 */
int ljyd_cloud_request_reset(void);

#ifdef __cplusplus
}
#endif

#endif
