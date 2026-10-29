/**
 * @file qm_mem_heap_example.c
 * @brief Standalone caller-owned Heap example. / 独立的调用方持有 Heap 示例。
 *
 * @details Build this source as one example application after enabling
 *          CONFIG_QM_MEM_HEAP_SUPPORT. Do not link it with the other memory
 *          examples because each defines qm_application_start. 启用
 *          CONFIG_QM_MEM_HEAP_SUPPORT 后将本文件作为一个示例应用构建；不要与
 *          其他内存示例同时链接，因为每个示例均定义 qm_application_start。
 */

#include "qm_config.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "qm_mem_heap.h"

#define LOG_TAG "qm_mem_heap_example"
#define QM_MEM_HEAP_EXAMPLE_REGION_BYTES 512U

#if CONFIG_QM_MEM_HEAP_SUPPORT
/* The Heap normalizes this ordinary byte buffer internally; callers do not
 * need compiler-specific alignment declarations. Heap 会在内部收敛这个普通字节
 * 缓冲区；调用方无需使用编译器相关的对齐声明。 */
static uint8_t s_heap_storage[QM_MEM_HEAP_EXAMPLE_REGION_BYTES];

static qm_mem_heap_t s_heap;

void qm_application_start(void)
{
    qm_mem_heap_region_t region = {
        .start = s_heap_storage,
        .size = sizeof(s_heap_storage),
    };
    qm_mem_heap_stats_t stats;
    void *first = NULL;
    void *second = NULL;
    int ret;

    ret = qm_mem_heap_init(&s_heap, &region, 1U);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "init failed: %d", ret);
        return;
    }

    first = qm_mem_heap_alloc(&s_heap, 40U);
    second = qm_mem_heap_calloc(&s_heap, 4U, 12U);
    if (first == NULL || second == NULL) {
        QM_LOGE(LOG_TAG, "allocation failed");
    } else if (qm_mem_heap_get_stats(&s_heap, &stats) == QM_EOK) {
        QM_LOGI(LOG_TAG, "usable=%u largest_free=%u",
                (unsigned int)stats.usable_bytes,
                (unsigned int)stats.largest_free_block);
    }

    if (first != NULL) {
        (void)qm_mem_heap_release(&s_heap, first);
    }
    if (second != NULL) {
        (void)qm_mem_heap_release(&s_heap, second);
    }

    ret = qm_mem_heap_deinit(&s_heap);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "deinit failed: %d", ret);
    }
}
#else
void qm_application_start(void)
{
    QM_LOGE(LOG_TAG, "enable CONFIG_QM_MEM_HEAP_SUPPORT first");
}
#endif
