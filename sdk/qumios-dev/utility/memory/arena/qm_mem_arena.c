/**
 * @file qm_mem_arena.c
 * @brief Caller-owned linear Arena allocator. / 调用方持有的线性 Arena 分配器。
 */

#include "qm_mem_arena.h"

#if CONFIG_QM_MEM_ARENA_SUPPORT

/* Private lifecycle sentinels reject uninitialized or corrupted instances.
 * 私有生命周期哨兵用于拒绝未初始化或已损坏的实例。 */
#define QM_MEM_ARENA_STATE_ACTIVE   0x4d415241U
#define QM_MEM_ARENA_STATE_INACTIVE 0x4d415249U

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

static int arena_size_add(size_t left, size_t right, size_t *result)
{
    if (result == NULL || left > SIZE_MAX - right) {
        return 0;
    }

    *result = left + right;
    return 1;
}

/* Use checked size arithmetic for the configured allocation boundary.
 * 使用受检的 size 算术处理配置的分配对齐边界。 */
static int arena_align_up(size_t value, size_t *result)
{
    if (result == NULL ||
        value > SIZE_MAX - ((size_t)QM_MEM_ARENA_ALIGNMENT - 1U)) {
        return 0;
    }

    *result = (value + ((size_t)QM_MEM_ARENA_ALIGNMENT - 1U)) &
              ~((size_t)QM_MEM_ARENA_ALIGNMENT - 1U);
    return 1;
}

static int arena_range_end(const void *start, size_t size, uintptr_t *end)
{
    uintptr_t begin;

    if (start == NULL || end == NULL || size == 0U) {
        return 0;
    }

    begin = (uintptr_t)start;
    if ((uintmax_t)size > (uintmax_t)UINTPTR_MAX - (uintmax_t)begin) {
        return 0;
    }

    *end = begin + (uintptr_t)size;
    return 1;
}

static int arena_ranges_overlap(const void *left, size_t left_size,
                                const void *right, size_t right_size)
{
    uintptr_t left_end;
    uintptr_t right_end;

    if (!arena_range_end(left, left_size, &left_end) ||
        !arena_range_end(right, right_size, &right_end)) {
        return 1;
    }

    return (uintptr_t)left < right_end && (uintptr_t)right < left_end;
}

static int arena_normalize_buffer(void *buffer, size_t raw_size,
                                  uint8_t **normalized_buffer,
                                  size_t *normalized_size)
{
    uintptr_t raw_begin;
    uintptr_t raw_end;
    uintptr_t aligned_begin;
    uintptr_t aligned_end;
    uintptr_t alignment_mask;

    if (buffer == NULL || normalized_buffer == NULL ||
        normalized_size == NULL ||
        !arena_range_end(buffer, raw_size, &raw_end)) {
        return 0;
    }

    raw_begin = (uintptr_t)buffer;
    alignment_mask = (uintptr_t)(QM_MEM_ARENA_ALIGNMENT - 1U);
    if (raw_begin > UINTPTR_MAX - alignment_mask) {
        return 0;
    }

    /* Keep the Arena-managed interval aligned at both ends. The discarded
     * edge bytes stay owned by the caller. 将 Arena 管理区间的两端都收敛到
     * 对齐边界；被排除的边缘字节仍由调用方持有。 */
    aligned_begin = (raw_begin + alignment_mask) & ~alignment_mask;
    aligned_end = raw_end & ~alignment_mask;
    if (aligned_begin >= aligned_end ||
        (uintmax_t)(aligned_end - aligned_begin) > (uintmax_t)SIZE_MAX) {
        return 0;
    }

    *normalized_buffer = (uint8_t *)aligned_begin;
    *normalized_size = (size_t)(aligned_end - aligned_begin);
    return 1;
}

static int arena_is_active(const qm_mem_arena_t *arena)
{
    return arena != NULL && arena->internal_state == QM_MEM_ARENA_STATE_ACTIVE;
}

static int arena_can_initialize(const qm_mem_arena_t *arena)
{
    return arena->internal_state == 0U ||
           arena->internal_state == QM_MEM_ARENA_STATE_INACTIVE;
}

int qm_mem_arena_init(qm_mem_arena_t *arena, void *buffer, size_t size)
{
    uint8_t *normalized_buffer;
    size_t normalized_size;

    if (arena == NULL) {
        return -QM_EINVAL;
    }

    if (arena_is_active(arena)) {
        return -QM_EBUSY;
    }

    if (!arena_can_initialize(arena)) {
        return -QM_EINIT;
    }

    /* The control object cannot live in its own borrowed allocation range.
     * 控制对象不能位于其自身借用的分配区域中。 */
    if (buffer == NULL || size == 0U ||
        arena_ranges_overlap(arena, sizeof(*arena), buffer, size) ||
        !arena_normalize_buffer(buffer, size, &normalized_buffer,
                                &normalized_size)) {
        return -QM_EINVAL;
    }

    if (arena->generation == UINT32_MAX) {
        return -QM_EFULL;
    }

    arena->buffer = normalized_buffer;
    arena->size = normalized_size;
    arena->offset = 0U;
    arena->peak_used = 0U;
    ++arena->generation;
    arena->internal_state = QM_MEM_ARENA_STATE_ACTIVE;
    return QM_EOK;
}

int qm_mem_arena_deinit(qm_mem_arena_t *arena)
{
    if (!arena_is_active(arena)) {
        return -QM_EINIT;
    }

    /* Preserve generation so marks from the old lifetime cannot become valid
     * after a later initialization. 保留 generation，避免旧生命周期的标记在
     * 后续初始化后重新有效。 */
    arena->buffer = NULL;
    arena->size = 0U;
    arena->offset = 0U;
    arena->peak_used = 0U;
    arena->internal_state = QM_MEM_ARENA_STATE_INACTIVE;
    return QM_EOK;
}

void *qm_mem_arena_alloc(qm_mem_arena_t *arena, size_t size)
{
    size_t aligned_offset;
    size_t end_offset;

    if (!arena_is_active(arena) || size == 0U ||
        !arena_align_up(arena->offset, &aligned_offset) ||
        !arena_size_add(aligned_offset, size, &end_offset) ||
        end_offset > arena->size) {
        return NULL;
    }

    /* Commit only after alignment and capacity checks pass.
     * 只有对齐和容量检查都通过后才提交游标。 */
    arena->offset = end_offset;
    if (arena->offset > arena->peak_used) {
        arena->peak_used = arena->offset;
    }

    return arena->buffer + aligned_offset;
}

qm_mem_arena_mark_t qm_mem_arena_mark(qm_mem_arena_t *arena)
{
    qm_mem_arena_mark_t mark = {0};

    if (!arena_is_active(arena)) {
        return mark;
    }

    mark.arena = arena;
    mark.generation = arena->generation;
    mark.offset = arena->offset;
    return mark;
}

int qm_mem_arena_rewind(qm_mem_arena_t *arena, qm_mem_arena_mark_t mark)
{
    if (!arena_is_active(arena)) {
        return -QM_EINIT;
    }

    if (mark.arena != arena || mark.generation != arena->generation ||
        mark.offset > arena->offset) {
        return -QM_EINVAL;
    }

    arena->offset = mark.offset;
    return QM_EOK;
}

int qm_mem_arena_reset(qm_mem_arena_t *arena)
{
    if (!arena_is_active(arena)) {
        return -QM_EINIT;
    }

    if (arena->generation == UINT32_MAX) {
        return -QM_EFULL;
    }

    /* Advancing generation invalidates every mark from the previous epoch.
     * 递增 generation 会使上一个时期的所有标记失效。 */
    arena->offset = 0U;
    ++arena->generation;
    return QM_EOK;
}

int qm_mem_arena_get_stats(qm_mem_arena_t *arena, qm_mem_arena_stats_t *stats)
{
    if (stats == NULL) {
        return -QM_EINVAL;
    }

    if (!arena_is_active(arena)) {
        return -QM_EINIT;
    }

    stats->capacity_bytes = arena->size;
    stats->used_bytes = arena->offset;
    stats->peak_used_bytes = arena->peak_used;
    stats->free_bytes = arena->size - arena->offset;
    stats->generation = arena->generation;
    return QM_EOK;
}

#endif /* CONFIG_QM_MEM_ARENA_SUPPORT */
