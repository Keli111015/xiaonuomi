/**
 * @file qm_mem_arena_example.c
 * @brief Standalone caller-owned Arena example. / 独立的调用方持有 Arena 示例。
 *
 * @details Build this source as one example application after enabling
 *          CONFIG_QM_MEM_ARENA_SUPPORT. Do not link it with the other memory
 *          examples because each defines qm_application_start. 启用
 *          CONFIG_QM_MEM_ARENA_SUPPORT 后将本文件作为一个示例应用构建；不要与
 *          其他内存示例同时链接，因为每个示例均定义 qm_application_start。
 */

#include "qm_config.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "qm_mem_arena.h"

#define LOG_TAG "qm_mem_arena_example"
#define QM_MEM_ARENA_EXAMPLE_BYTES 256U

#if CONFIG_QM_MEM_ARENA_SUPPORT
/* Arena normalizes this raw byte buffer during initialization.
 * Arena 会在初始化时规范化这个原始字节缓冲区。 */
static uint8_t s_arena_storage[QM_MEM_ARENA_EXAMPLE_BYTES];

static qm_mem_arena_t s_arena;

void qm_application_start(void)
{
    qm_mem_arena_mark_t mark;
    qm_mem_arena_stats_t stats;
    void *persistent;
    void *temporary;
    int ret;

    ret = qm_mem_arena_init(&s_arena, s_arena_storage,
                            sizeof(s_arena_storage));
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "init failed: %d", ret);
        return;
    }

    persistent = qm_mem_arena_alloc(&s_arena, 32U);
    mark = qm_mem_arena_mark(&s_arena);
    temporary = qm_mem_arena_alloc(&s_arena, 48U);
    if (persistent == NULL || temporary == NULL) {
        QM_LOGE(LOG_TAG, "allocation failed");
    } else if (qm_mem_arena_rewind(&s_arena, mark) != QM_EOK) {
        QM_LOGE(LOG_TAG, "rewind failed");
    } else if (qm_mem_arena_get_stats(&s_arena, &stats) == QM_EOK) {
        QM_LOGI(LOG_TAG, "used=%u peak=%u",
                (unsigned int)stats.used_bytes,
                (unsigned int)stats.peak_used_bytes);
    }

    ret = qm_mem_arena_deinit(&s_arena);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "deinit failed: %d", ret);
    }
}
#else
void qm_application_start(void)
{
    QM_LOGE(LOG_TAG, "enable CONFIG_QM_MEM_ARENA_SUPPORT first");
}
#endif
