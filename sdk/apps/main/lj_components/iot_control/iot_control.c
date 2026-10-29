#include "iot_control.h"

#include "iot_control_volume.h"
#include "ljyd_cloud.h"

#include "qm.h"

/*
 * 物模型下行表。新增控制项加一行。
 * 有云不一定有本模块，本模块一定要有云 handle。
 */

typedef int (*iot_control_fn)(qm_spec_property_t *property);

typedef struct {
    uint8_t siid;
    uint16_t piid;
    iot_control_fn action;
} iot_control_entry_t;

static const iot_control_entry_t s_table[] = {
    {IOT_CONTROL_VOLUME_SIID, IOT_CONTROL_VOLUME_PIID, iot_control_volume_set_value},
    {IOT_CONTROL_VOLUME_SIID, IOT_CONTROL_VOLUME_PIID_ADJ, iot_control_volume_set_adjust},
};

int iot_control_init(void *iot_handle)
{
    if (iot_handle == NULL) {
        return -QM_EINVAL;
    }

    ljyd_cloud_set_prop_cb(iot_control_on_set);
    return QM_EOK;
}

void iot_control_on_set(qm_spec_property_t *property)
{
    size_t i;

    if (property == NULL) {
        return;
    }
    for (i = 0; i < QM_ARRAY_SIZE(s_table); i++) {
        if (s_table[i].siid != property->siid ||
            s_table[i].piid != property->piid) {
            continue;
        }
        if (s_table[i].action != NULL) {
            (void)s_table[i].action(property);
        }
        return;
    }
}
