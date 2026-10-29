#include "qm_provision.h"

#include "qm_errno.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_types.h"

#define LOG_TAG "qm_provision_example"

#ifndef CONFIG_QM_PROVISION_EXAMPLE_PRODUCT_ID
#define CONFIG_QM_PROVISION_EXAMPLE_PRODUCT_ID 0U
#endif

#ifndef CONFIG_QM_PROVISION_EXAMPLE_PRODUCT_SECRET
#define CONFIG_QM_PROVISION_EXAMPLE_PRODUCT_SECRET ""
#endif

#ifndef CONFIG_QM_PROVISION_EXAMPLE_ADV_CID
#define CONFIG_QM_PROVISION_EXAMPLE_ADV_CID ""
#endif

static qm_provision_handle_t s_provision;

static qm_bool provision_example_config_ready(void)
{
    return (CONFIG_QM_PROVISION_EXAMPLE_PRODUCT_ID != 0U &&
            CONFIG_QM_PROVISION_EXAMPLE_PRODUCT_SECRET[0] != '\0');
}

static void provision_example_recv_handler(qm_provision_handle_t handle,
                                           const qm_provision_recv_t *recv,
                                           void *userdata)
{
    (void)handle;
    (void)userdata;

    if (recv == NULL) {
        return;
    }

    QM_LOGI(LOG_TAG,
            "recv ssid=%s password_len=%u dynreg_host=%s timezone=%d audio_uri=%s env_valid=%u env_type=%u",
            recv->ssid,
            (unsigned int)qm_strlen(recv->password),
            recv->dynreg_host,
            (int)recv->timezone,
            recv->audio_uri,
            (unsigned int)recv->env_type_valid,
            (unsigned int)recv->env_type);
}

int qm_provision_example_start(void)
{
    int32_t ret;
    const char *adv_cid = NULL;
    qm_provision_config_t config = {
        .product_id = CONFIG_QM_PROVISION_EXAMPLE_PRODUCT_ID,
        .product_secret = CONFIG_QM_PROVISION_EXAMPLE_PRODUCT_SECRET,
        .adv_cid = NULL,
        .recv_handler = provision_example_recv_handler,
        .userdata = NULL,
    };

    if (s_provision != NULL) {
        return QM_EOK;
    }

    if (!provision_example_config_ready()) {
        QM_LOGE(LOG_TAG, "set product id and product secret before starting");
        return -QM_EINVAL;
    }

    if (CONFIG_QM_PROVISION_EXAMPLE_ADV_CID[0] != '\0') {
        adv_cid = CONFIG_QM_PROVISION_EXAMPLE_ADV_CID;
    }
    config.adv_cid = adv_cid;

    s_provision = qm_provision_create(&config);
    if (s_provision == NULL) {
        QM_LOGE(LOG_TAG, "qm_provision_create failed");
        return -QM_EINVAL;
    }

    ret = qm_provision_start(s_provision);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "qm_provision_start failed ret=%d", (int)ret);
        qm_provision_destroy(s_provision);
        s_provision = NULL;
        return ret;
    }

    QM_LOGI(LOG_TAG, "qm provision example started");
    return QM_EOK;
}

void qm_provision_example_stop(void)
{
    if (s_provision == NULL) {
        return;
    }

    qm_provision_destroy(s_provision);
    s_provision = NULL;
    QM_LOGI(LOG_TAG, "qm provision example stopped");
}

void qm_application_start(void)
{
    (void)qm_provision_example_start();
}
