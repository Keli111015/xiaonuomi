#ifndef IOT_CONTROL_H
#define IOT_CONTROL_H

#include "qm_spec_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 应用层在 ljyd_cloud_init() 成功后调用。没有云 handle 不能初始化。 */
int iot_control_init(void *iot_handle);

/* 云端设属性。按 siid/piid 查表调回调，没有匹配项则返回。 */
void iot_control_on_set(qm_spec_property_t *property);

#ifdef __cplusplus
}
#endif

#endif
