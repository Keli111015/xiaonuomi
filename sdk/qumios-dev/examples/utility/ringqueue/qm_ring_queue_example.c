/**
 * @file qm_ring_queue_example.c
 * @brief Standalone variable-length record ring queue example. /
 *        独立的可变长度记录环形队列示例。
 *
 * @details Build this source as one example application. It defines
 *          qm_application_start(), so do not link it with another example
 *          source that defines the same entry point. 将本文件作为一个示例应用构建。
 *          它定义 qm_application_start()，不可与定义相同入口的其他示例源文件同时链接。
 */

#include "qm_config.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_log.h"
#include "qm_ring_queue.h"
#include "qm_string.h"

#define LOG_TAG "qm_ring_queue_example"
#define QM_RING_QUEUE_EXAMPLE_STORAGE_BYTES 32U
#define QM_RING_QUEUE_EXAMPLE_OUTPUT_BYTES 16U

static uint8_t s_storage[QM_RING_QUEUE_EXAMPLE_STORAGE_BYTES];
static uint8_t s_output[QM_RING_QUEUE_EXAMPLE_OUTPUT_BYTES];
static qm_ring_queue_t s_queue;

static const uint8_t s_first[] = { 'f', 'i', 'r', 's', 't' };
static const uint8_t s_second[] = { 's', 'e', 'c', 'o', 'n', 'd' };
static const uint8_t s_third[] = { 't', 'h', 'i', 'r', 'd' };
static const uint8_t s_latest[] = { 'l', 'a', 't', 'e', 's', 't' };

static const uint8_t *const s_expected_payloads[] = {
    s_second,
    s_third,
    s_latest,
};

static const uint32_t s_expected_sizes[] = {
    sizeof(s_second),
    sizeof(s_third),
    sizeof(s_latest),
};

void qm_application_start(void)
{
    uint32_t index;
    uint32_t expected_size;
    uint32_t actual_size;
    int ret;

    ret = qm_ring_queue_init(&s_queue, s_storage, sizeof(s_storage));
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "init failed: %d", ret);
        return;
    }

    ret = qm_ring_queue_push(&s_queue, s_first, sizeof(s_first));
    if (ret == QM_EOK) {
        ret = qm_ring_queue_push(&s_queue, s_second, sizeof(s_second));
    }
    if (ret == QM_EOK) {
        ret = qm_ring_queue_push(&s_queue, s_third, sizeof(s_third));
    }
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "initial push failed: %d", ret);
        return;
    }

    /* Strict push retains the queue unchanged when the complete record does
     * not fit. / 严格入队在完整记录放不下时保持队列不变。 */
    ret = qm_ring_queue_push(&s_queue, s_latest, sizeof(s_latest));
    if (ret != -QM_EFULL) {
        QM_LOGE(LOG_TAG, "strict push returned: %d", ret);
        return;
    }

    /* Overwrite mode evicts only the oldest complete record here, retaining
     * second, third, and latest in FIFO order. / 覆盖模式在此只淘汰最旧完整记录，
     * 按 FIFO 顺序保留 second、third 和 latest。 */
    ret = qm_ring_queue_push_overwrite(&s_queue, s_latest, sizeof(s_latest));
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "overwrite push failed: %d", ret);
        return;
    }

    for (index = 0U; index < QM_ARRAY_SIZE(s_expected_sizes); ++index) {
        expected_size = 0U;
        ret = qm_ring_queue_peek_size(&s_queue, &expected_size);
        if (ret != QM_EOK || expected_size != s_expected_sizes[index]) {
            QM_LOGE(LOG_TAG, "unexpected head at %u: ret=%d size=%u",
                    (unsigned int)index, ret, (unsigned int)expected_size);
            return;
        }

        actual_size = 0U;
        ret = qm_ring_queue_pop(&s_queue, s_output, sizeof(s_output),
                                &actual_size);
        if (ret != QM_EOK || actual_size != expected_size ||
            qm_memcmp(s_output, s_expected_payloads[index], actual_size) != 0) {
            QM_LOGE(LOG_TAG, "unexpected payload at %u: ret=%d size=%u",
                    (unsigned int)index, ret, (unsigned int)actual_size);
            return;
        }

        QM_LOGI(LOG_TAG, "record %u size=%u", (unsigned int)index,
                (unsigned int)actual_size);
    }

    ret = qm_ring_queue_peek_size(&s_queue, &expected_size);
    if (ret != -QM_EEMPTY) {
        QM_LOGE(LOG_TAG, "queue should be empty: %d", ret);
        return;
    }

    QM_LOGI(LOG_TAG, "ring queue example complete");
}
