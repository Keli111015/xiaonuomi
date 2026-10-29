/**
 * @file qm_mem_pool_example.c
 * @brief Standalone caller-owned Pool example. / 独立的调用方持有 Pool 示例。
 *
 * @details Build this source as one example application after enabling
 *          CONFIG_QM_MEM_POOL_SUPPORT. Do not link it with the other memory
 *          examples because each defines qm_application_start. 启用
 *          CONFIG_QM_MEM_POOL_SUPPORT 后将本文件作为一个示例应用构建；不要与
 *          其他内存示例同时链接，因为每个示例均定义 qm_application_start。
 */

#include "qm_config.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "qm_mem_pool.h"

#define LOG_TAG "qm_mem_pool_example"
#define QM_MEM_POOL_EXAMPLE_SMALL_BLOCK_BYTES 32U
#define QM_MEM_POOL_EXAMPLE_SMALL_BLOCK_COUNT 4U
#define QM_MEM_POOL_EXAMPLE_LARGE_BLOCK_BYTES 96U
#define QM_MEM_POOL_EXAMPLE_LARGE_BLOCK_COUNT 2U

#if CONFIG_QM_MEM_POOL_SUPPORT
/* Raw byte arrays deliberately have no alignment declaration. Pool reserves
 * the QM_MEM_POOL_STORAGE_SIZE margin and aligns the raw interval internally.
 * 原始字节数组刻意不声明对齐。Pool 通过 QM_MEM_POOL_STORAGE_SIZE 预留余量，并在
 * 内部收敛原始区间。 */
static uint8_t s_pool_small_storage[
    QM_MEM_POOL_STORAGE_SIZE(QM_MEM_POOL_EXAMPLE_SMALL_BLOCK_BYTES,
                             QM_MEM_POOL_EXAMPLE_SMALL_BLOCK_COUNT)];

static uint8_t s_pool_large_storage[
    QM_MEM_POOL_STORAGE_SIZE(QM_MEM_POOL_EXAMPLE_LARGE_BLOCK_BYTES,
                             QM_MEM_POOL_EXAMPLE_LARGE_BLOCK_COUNT)];

static const qm_mem_pool_class_config_t s_pool_classes[] = {
    {
        .storage = s_pool_small_storage,
        .storage_size = sizeof(s_pool_small_storage),
        .block_size = QM_MEM_POOL_EXAMPLE_SMALL_BLOCK_BYTES,
        .block_count = QM_MEM_POOL_EXAMPLE_SMALL_BLOCK_COUNT,
    },
    {
        .storage = s_pool_large_storage,
        .storage_size = sizeof(s_pool_large_storage),
        .block_size = QM_MEM_POOL_EXAMPLE_LARGE_BLOCK_BYTES,
        .block_count = QM_MEM_POOL_EXAMPLE_LARGE_BLOCK_COUNT,
    },
};

static qm_mem_pool_class_state_t s_pool_class_states[
    QM_ARRAY_SIZE(s_pool_classes)];
static qm_mem_pool_t s_pool;

void qm_application_start(void)
{
    const qm_mem_pool_config_t config = {
        .classes = s_pool_classes,
        .class_states = s_pool_class_states,
        .class_count = QM_ARRAY_SIZE(s_pool_classes),
        .allow_larger = QM_TRUE,
    };
    qm_mem_pool_class_stats_t small_stats;
    qm_mem_pool_class_stats_t large_stats;
    void *small_blocks[QM_MEM_POOL_EXAMPLE_SMALL_BLOCK_COUNT] = { NULL };
    void *fallback = NULL;
    uint32_t index;
    int ret;

    ret = qm_mem_pool_init(&s_pool, &config);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "init failed: %d", ret);
        return;
    }

    for (index = 0U;
         index < QM_MEM_POOL_EXAMPLE_SMALL_BLOCK_COUNT;
         ++index) {
        small_blocks[index] = qm_mem_pool_alloc(&s_pool, 16U);
        if (small_blocks[index] == NULL) {
            QM_LOGE(LOG_TAG, "small allocation %u failed",
                    (unsigned int)index);
            break;
        }
    }

    if (index == QM_MEM_POOL_EXAMPLE_SMALL_BLOCK_COUNT) {
        /* The fifth same-size request must use the larger class because all
         * small slots are active. 第五个相同大小的请求只能使用更大类别，因为
         * 所有小规格槽位都处于活跃状态。 */
        fallback = qm_mem_pool_alloc(&s_pool, 16U);
        if (fallback == NULL) {
            QM_LOGE(LOG_TAG, "larger-class fallback failed");
        } else if (qm_mem_pool_get_class_stats(&s_pool, 0U, &small_stats) != QM_EOK ||
                   qm_mem_pool_get_class_stats(&s_pool, 1U, &large_stats) != QM_EOK ||
                   small_stats.used_blocks != QM_MEM_POOL_EXAMPLE_SMALL_BLOCK_COUNT ||
                   large_stats.used_blocks != 1U) {
            QM_LOGE(LOG_TAG, "larger-class fallback was not observed");
        } else {
            QM_LOGI(LOG_TAG, "small=%u large=%u",
                    (unsigned int)small_stats.used_blocks,
                    (unsigned int)large_stats.used_blocks);
        }
    }

    if (fallback != NULL) {
        (void)qm_mem_pool_release(&s_pool, fallback);
    }
    for (index = 0U;
         index < QM_MEM_POOL_EXAMPLE_SMALL_BLOCK_COUNT;
         ++index) {
        if (small_blocks[index] != NULL) {
            (void)qm_mem_pool_release(&s_pool, small_blocks[index]);
        }
    }

    ret = qm_mem_pool_deinit(&s_pool);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "deinit failed: %d", ret);
    }
}
#else
void qm_application_start(void)
{
    QM_LOGE(LOG_TAG, "enable CONFIG_QM_MEM_POOL_SUPPORT first");
}
#endif
