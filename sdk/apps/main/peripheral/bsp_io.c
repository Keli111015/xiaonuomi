#include "bsp_io.h"
#include "ljyd_cloud.h"
#include "ljyd_provision.h"

#include "qm_log.h"
#include "qm_gpio.h"

#define LOG_TAG "io"

#define BSP_RESET_GPIO        14
#define BSP_RESET_HOLD_MS     3000
#define BSP_RESET_POLL_MS     100
#define BSP_RESET_TASK_STACK  2048
#define BSP_RESET_TASK_PRIO   5

static uint8_t g_resetting;

static void reset_provision(void)
{
    if (g_resetting) {
        return;
    }
    g_resetting = 1;

    if (ljyd_cloud_request_reset() == QM_EOK) {
        QM_LOGI(LOG_TAG, "gpio%d held %u ms, request unbind",
                BSP_RESET_GPIO, (unsigned)BSP_RESET_HOLD_MS);
        return;
    }

    QM_LOGI(LOG_TAG, "gpio%d held %u ms, cloud not up, reboot",
            BSP_RESET_GPIO, (unsigned)BSP_RESET_HOLD_MS);
    ljyd_provision_clear_and_reboot();
}

static void reset_task(void *arg)
{
    qm_gpio_dev_t gpio = {
        .port = BSP_RESET_GPIO,
        .config = {
            .mode = QM_GPIO_MODE_INPUT,
            .pull_en = QM_GPIO_PULLUP_ONLY,
        },
    };
    uint32_t low_ms = 0;

    (void)arg;
    if (qm_gpio_init(&gpio) != QM_EOK) {
        QM_LOGE(LOG_TAG, "reset gpio%d init fail", BSP_RESET_GPIO);
        qm_task_exit(NULL);
        return;
    }

    while (1) {
        if (qm_gpio_get_level(&gpio) == 0) {
            if (low_ms == 0) {
                QM_LOGI(LOG_TAG, "gpio%d low, hold %u ms to reset provision",
                        BSP_RESET_GPIO, (unsigned)BSP_RESET_HOLD_MS);
            }
            low_ms += BSP_RESET_POLL_MS;
            if (low_ms >= BSP_RESET_HOLD_MS) {
                reset_provision();
            }
        } else {
            low_ms = 0;
        }
        qm_msleep(BSP_RESET_POLL_MS);
    }
}

static qm_err_t reset_init(void)
{
    static qm_task_t task;

    if (qm_task_new(&task, "prov_rst", reset_task, NULL,
                    BSP_RESET_TASK_STACK, BSP_RESET_TASK_PRIO) != 0) {
        QM_LOGE(LOG_TAG, "reset task create fail");
        return -QM_EIO;
    }
    return QM_EOK;
}

qm_err_t bsp_io_init(void)
{
    return reset_init();
}
