#include "iot_control_volume.h"

#include "ljyd_cloud.h"
#include "voice.h"

#include "qm.h"
#include "qm_iot_api.h"
#include "qm_kv.h"
#include "qm_spec_api.h"

/*
 * 对齐 ljyd_ai_lamp 的音量，不接静音。
 *   SIID=2 PIID=1 绝对音量 10..100
 *   SIID=2 PIID=2 1增大 2减小 3最大 4最小
 * 回调挂在 iot_control 的 siid/piid 表上。
 */

#define LOG_TAG "volume"

#define VOLUME_MIN     10
#define VOLUME_MAX     100
#define VOLUME_DEFAULT 80
#define VOLUME_STEP    10

#define KV_VOLUME "volume"

static uint8_t s_volume = VOLUME_DEFAULT;
static uint8_t s_kv_loaded;

static uint8_t clamp_volume(int value)
{
    if (value < VOLUME_MIN) {
        return VOLUME_MIN;
    }
    if (value > VOLUME_MAX) {
        return VOLUME_MAX;
    }
    return (uint8_t)value;
}

static void volume_kv_load(void)
{
    uint8_t volume = VOLUME_DEFAULT;
    int len;

    if (s_kv_loaded) {
        return;
    }
    s_kv_loaded = 1;

    len = (int)sizeof(volume);
    if (qm_kv_get(KV_VOLUME, &volume, &len) == 0) {
        s_volume = clamp_volume((int)volume);
    }
    QM_LOGI(LOG_TAG, "kv load vol=%u", (unsigned)s_volume);
}

static void volume_kv_save(void)
{
    (void)qm_kv_set(KV_VOLUME, &s_volume, (int)sizeof(s_volume), 1);
}

static void volume_apply_hw(void)
{
    if (!voice_audio_ready()) {
        QM_LOGW(LOG_TAG, "audio not ready, skip hw vol=%u", (unsigned)s_volume);
        return;
    }

    /* 0..100。芯片 0..31 的换算在 VB701N 设备里做。 */
    (void)qm_audio_set_volume(s_volume);
    QM_LOGI(LOG_TAG, "hw vol=%u", (unsigned)s_volume);
}

static void shadow_add_number(qm_spec_property_operation_t *op,
                              uint8_t siid, uint16_t piid, int value)
{
    qm_spec_property_t *prop;

    if (op == NULL) {
        return;
    }
    prop = qm_spec_property_creat();
    if (prop == NULL) {
        return;
    }
    qm_spec_property_pack_xiid(prop, siid, piid);
    qm_spec_property_pack_number(prop, value);
    qm_spec_property_add(op, prop);
}

static void volume_shadow_report(void)
{
    qm_spec_property_operation_t *op;
    void *iot;
    int32_t ret;

    iot = ljyd_cloud_get_handle();
    if (iot == NULL) {
        return;
    }
    op = qm_spec_property_operation_creat();
    if (op == NULL) {
        return;
    }
    shadow_add_number(op, IOT_CONTROL_VOLUME_SIID, IOT_CONTROL_VOLUME_PIID, (int)s_volume);
    ret = qm_iot_report(iot, op);
    if (ret != QM_EOK) {
        qm_spec_property_operation_delete(op);
        QM_LOGW(LOG_TAG, "shadow report failed ret=%d", (int)ret);
        return;
    }
    QM_LOGI(LOG_TAG, "shadow report vol=%u", (unsigned)s_volume);
}

static void volume_commit(uint8_t volume)
{
    s_volume = clamp_volume((int)volume);
    s_kv_loaded = 1;
    volume_kv_save();
    volume_apply_hw();
    volume_shadow_report();
    QM_LOGI(LOG_TAG, "set vol=%u", (unsigned)s_volume);
}

static int unpack_number(qm_spec_property_t *property, int *value)
{
    if (property == NULL || value == NULL) {
        return 0;
    }
    return qm_spec_property_unpack_number(property, value) == QM_EOK;
}

int iot_control_volume_set_value(qm_spec_property_t *property)
{
    int value = 0;

    if (!unpack_number(property, &value)) {
        return -QM_EINVAL;
    }
    QM_LOGI(LOG_TAG, "VALUE set=%d", value);
    volume_commit(clamp_volume(value));
    return QM_EOK;
}

int iot_control_volume_set_adjust(qm_spec_property_t *property)
{
    int value = 0;
    uint8_t next;

    if (!unpack_number(property, &value)) {
        return -QM_EINVAL;
    }
    volume_kv_load();
    QM_LOGI(LOG_TAG, "ADJUST cmd=%d cur=%u", value, (unsigned)s_volume);

    switch (value) {
    case 1:
        next = (s_volume + VOLUME_STEP > VOLUME_MAX) ?
               VOLUME_MAX : (uint8_t)(s_volume + VOLUME_STEP);
        break;
    case 2:
        next = (s_volume < VOLUME_MIN + VOLUME_STEP) ?
               VOLUME_MIN : (uint8_t)(s_volume - VOLUME_STEP);
        break;
    case 3:
        next = VOLUME_MAX;
        break;
    case 4:
        next = VOLUME_MIN;
        break;
    default:
        QM_LOGW(LOG_TAG, "ADJUST unknown=%d", value);
        return QM_EOK;
    }
    volume_commit(next);
    return QM_EOK;
}
