/**
 * @file qm_mem_pool.c
 * @brief Caller-owned fixed-block and multi-size Pool allocator. / 调用方持有的固定块与多规格 Pool 分配器。
 */

#include "qm_mem_pool.h"
#include "qm_string.h"

#if CONFIG_QM_MEM_POOL_SUPPORT

#define QM_MEM_POOL_STATE_ACTIVE 0x4d504f4cU

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

#define QM_MEM_POOL_FREE_HEAD_NONE UINT32_MAX
#define QM_MEM_POOL_SLOT_NEXT_OFFSET 0U
#define QM_MEM_POOL_SLOT_ACTIVE_OFFSET ((size_t)sizeof(uint32_t))
#define QM_MEM_POOL_SLOT_OVERHEAD \
    (((2U * sizeof(uint32_t)) + ((size_t)QM_MEM_POOL_ALIGNMENT - 1U)) & \
     ~((size_t)QM_MEM_POOL_ALIGNMENT - 1U))

/* Slots use two serialized uint32_t values rather than a native pointer
 * structure. Pad these words to the configured boundary before the payload;
 * the public storage-size macro uses the same layout.
 * 槽位使用两个序列化的 uint32_t 值，而非本机指针结构。payload 前的元数据按配置
 * 边界补齐；公开存储容量宏使用相同布局。 */

/* qm_string uses uint32_t lengths; retain this allocator's size_t contract.
 * qm_string 使用 uint32_t 长度；保留本分配器的 size_t 契约。 */
static void pool_copy(void *destination, const void *source, size_t size)
{
    uint8_t *output = (uint8_t *)destination;
    const uint8_t *input = (const uint8_t *)source;

    while (size > UINT32_MAX) {
        qm_memcpy(output, input, UINT32_MAX);
        output += UINT32_MAX;
        input += UINT32_MAX;
        size -= UINT32_MAX;
    }

    if (size != 0U) {
        qm_memcpy(output, input, (uint32_t)size);
    }
}

static void pool_clear(void *destination, size_t size)
{
    uint8_t *output = (uint8_t *)destination;

    while (size > UINT32_MAX) {
        qm_memset(output, 0, UINT32_MAX);
        output += UINT32_MAX;
        size -= UINT32_MAX;
    }

    if (size != 0U) {
        qm_memset(output, 0, (uint32_t)size);
    }
}

static int pool_size_add(size_t left, size_t right, size_t *result)
{
    if (result == NULL || left > SIZE_MAX - right) {
        return 0;
    }

    *result = left + right;
    return 1;
}

static int pool_size_multiply(size_t left, size_t right, size_t *result)
{
    if (result == NULL || (left != 0U && right > SIZE_MAX / left)) {
        return 0;
    }

    *result = left * right;
    return 1;
}

static int pool_align_up(size_t value, size_t alignment, size_t *result)
{
    if (result == NULL || alignment == 0U ||
        (alignment & (alignment - 1U)) != 0U ||
        value > SIZE_MAX - (alignment - 1U)) {
        return 0;
    }

    *result = (value + (alignment - 1U)) & ~(alignment - 1U);
    return 1;
}

static int pool_range_end(const void *start, size_t size, uintptr_t *end)
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

static int pool_ranges_overlap(const void *left, size_t left_size,
                               const void *right, size_t right_size)
{
    uintptr_t left_end;
    uintptr_t right_end;

    if (!pool_range_end(left, left_size, &left_end) ||
        !pool_range_end(right, right_size, &right_end)) {
        return 1;
    }

    return (uintptr_t)left < right_end && (uintptr_t)right < left_end;
}

static int pool_normalize_storage(void *storage, size_t storage_size,
                                  size_t required_size, void **storage_start)
{
    uintptr_t raw_start;
    uintptr_t raw_end;
    uintptr_t aligned_start;
    uintptr_t aligned_end;

    if (!pool_range_end(storage, storage_size, &raw_end)) {
        return 0;
    }

    raw_start = (uintptr_t)storage;
    if (raw_start > UINTPTR_MAX - (QM_MEM_POOL_ALIGNMENT - 1U)) {
        return 0;
    }

    /* Keep the raw configuration unchanged and use only its aligned inner
     * interval for slots. 保持原始配置不变，仅将其内部对齐区间用于槽位。 */
    aligned_start = (raw_start + (QM_MEM_POOL_ALIGNMENT - 1U)) &
                    ~(uintptr_t)(QM_MEM_POOL_ALIGNMENT - 1U);
    aligned_end = raw_end & ~(uintptr_t)(QM_MEM_POOL_ALIGNMENT - 1U);
    if (aligned_start < raw_start || aligned_start >= aligned_end ||
        (uintmax_t)required_size > (uintmax_t)(aligned_end - aligned_start)) {
        return 0;
    }

    if (storage_start != NULL) {
        *storage_start = (void *)aligned_start;
    }
    return 1;
}

static uint8_t *pool_slot_at(const qm_mem_pool_class_state_t *state,
                             uint32_t slot_index)
{
    return (uint8_t *)state->storage_start +
           ((size_t)slot_index * state->slot_stride);
}

static uint32_t pool_slot_get_next(const uint8_t *slot)
{
    uint32_t next_index;

    pool_copy(&next_index, slot + QM_MEM_POOL_SLOT_NEXT_OFFSET,
              sizeof(next_index));
    return next_index;
}

static void pool_slot_set_next(uint8_t *slot, uint32_t next_index)
{
    pool_copy(slot + QM_MEM_POOL_SLOT_NEXT_OFFSET, &next_index,
              sizeof(next_index));
}

static uint32_t pool_slot_get_active(const uint8_t *slot)
{
    uint32_t active;

    pool_copy(&active, slot + QM_MEM_POOL_SLOT_ACTIVE_OFFSET, sizeof(active));
    return active;
}

static void pool_slot_set_active(uint8_t *slot, uint32_t active)
{
    pool_copy(slot + QM_MEM_POOL_SLOT_ACTIVE_OFFSET, &active, sizeof(active));
}

static void pool_increment_counter(uint32_t *counter)
{
    if (*counter != UINT32_MAX) {
        ++(*counter);
    }
}

static int pool_is_active(const qm_mem_pool_t *pool)
{
    return pool != NULL && pool->internal_state == QM_MEM_POOL_STATE_ACTIVE;
}

static int pool_can_initialize(const qm_mem_pool_t *pool)
{
    return pool->internal_state == 0U;
}

static int pool_get_slot_stride(size_t block_size, size_t *slot_stride)
{
    size_t slot_size;

    return pool_size_add(block_size, QM_MEM_POOL_SLOT_OVERHEAD, &slot_size) &&
           pool_align_up(slot_size, QM_MEM_POOL_ALIGNMENT, slot_stride);
}

static int pool_validate_config(qm_mem_pool_t *pool,
                                const qm_mem_pool_config_t *config)
{
    size_t classes_size;
    size_t states_size;
    uintptr_t range_end;
    uint32_t index;

    if (config == NULL || config->classes == NULL || config->class_states == NULL ||
        config->class_count == 0U ||
        (config->allow_larger != QM_FALSE &&
         config->allow_larger != QM_TRUE) ||
        !pool_size_multiply(config->class_count, sizeof(*config->classes),
                            &classes_size) ||
        !pool_size_multiply(config->class_count, sizeof(*config->class_states),
                            &states_size) ||
        !pool_range_end(config, sizeof(*config), &range_end) ||
        !pool_range_end(config->classes, classes_size, &range_end) ||
        !pool_range_end(config->class_states, states_size, &range_end) ||
        pool_ranges_overlap(pool, sizeof(*pool), config->classes, classes_size) ||
        pool_ranges_overlap(pool, sizeof(*pool), config->class_states, states_size) ||
        pool_ranges_overlap(config, sizeof(*config), config->classes,
                            classes_size) ||
        pool_ranges_overlap(config, sizeof(*config), config->class_states,
                            states_size) ||
        pool_ranges_overlap(config->classes, classes_size,
                            config->class_states, states_size)) {
        return 0;
    }

    for (index = 0U; index < config->class_count; ++index) {
        const qm_mem_pool_class_config_t *current = &config->classes[index];
        size_t slot_stride;
        size_t required_size;
        uint32_t previous_index;

        /* Strict ordering makes first-fit selection deterministic; disjoint
         * storage prevents one block from belonging to two classes. 严格递增
         * 保证 first-fit 选择确定；存储不重叠避免一个块属于两个规格。 */
        if (current->storage == NULL || current->storage_size == 0U ||
            current->block_size == 0U || current->block_count == 0U ||
            !pool_get_slot_stride(current->block_size, &slot_stride) ||
            !pool_size_multiply(slot_stride, current->block_count,
                                &required_size) ||
            !pool_normalize_storage(current->storage, current->storage_size,
                                    required_size, NULL) ||
            pool_ranges_overlap(pool, sizeof(*pool), current->storage,
                                current->storage_size) ||
            pool_ranges_overlap(config->classes, classes_size, current->storage,
                                current->storage_size) ||
            pool_ranges_overlap(config->class_states, states_size,
                                current->storage, current->storage_size) ||
            pool_ranges_overlap(config, sizeof(*config), current->storage,
                                current->storage_size) ||
            (index > 0U &&
             config->classes[index - 1U].block_size >= current->block_size)) {
            return 0;
        }

        for (previous_index = 0U; previous_index < index; ++previous_index) {
            const qm_mem_pool_class_config_t *previous =
                &config->classes[previous_index];

            if (pool_ranges_overlap(previous->storage, previous->storage_size,
                                    current->storage, current->storage_size)) {
                return 0;
            }
        }
    }

    return 1;
}

static void pool_initialize_class(const qm_mem_pool_class_config_t *config,
                                  qm_mem_pool_class_state_t *state)
{
    void *storage_start = NULL;
    size_t slot_stride;
    size_t required_size;
    uint32_t index;

    (void)pool_get_slot_stride(config->block_size, &slot_stride);
    (void)pool_size_multiply(slot_stride, config->block_count, &required_size);
    (void)pool_normalize_storage(config->storage, config->storage_size,
                                 required_size, &storage_start);

    state->storage_start = storage_start;
    state->free_head = QM_MEM_POOL_FREE_HEAD_NONE;
    state->slot_stride = slot_stride;
    state->used_blocks = 0U;
    state->peak_used_blocks = 0U;
    state->allocation_failures = 0U;

    /* Link every caller-provided slot before publishing an active Pool.
     * 在发布活跃 Pool 之前连接所有调用方提供的槽位。 */
    for (index = 0U; index < config->block_count; ++index) {
        uint8_t *slot = pool_slot_at(state, index);

        pool_slot_set_active(slot, 0U);
        pool_slot_set_next(slot, state->free_head);
        state->free_head = index;
    }
}

static int pool_find_first_fitting_class(const qm_mem_pool_t *pool, size_t size,
                                         uint32_t *class_index)
{
    uint32_t index;

    for (index = 0U; index < pool->class_count; ++index) {
        if (pool->classes[index].block_size >= size) {
            *class_index = index;
            return 1;
        }
    }

    return 0;
}

static int pool_find_slot(const qm_mem_pool_t *pool, void *block,
                          uint32_t *class_index, uint32_t *slot_index,
                          uint8_t **slot)
{
    uintptr_t block_address = (uintptr_t)block;
    uint32_t index;

    for (index = 0U; index < pool->class_count; ++index) {
        const qm_mem_pool_class_config_t *config = &pool->classes[index];
        const qm_mem_pool_class_state_t *state = &pool->class_states[index];
        uintptr_t storage_address = (uintptr_t)state->storage_start;
        uintptr_t payload_address;
        size_t slot_bytes;
        uintptr_t slots_end;
        uintptr_t offset;

        if (!pool_size_multiply(state->slot_stride, config->block_count,
                                &slot_bytes) ||
            !pool_range_end(state->storage_start, slot_bytes, &slots_end)) {
            continue;
        }

        payload_address = storage_address + QM_MEM_POOL_SLOT_OVERHEAD;
        if (block_address < payload_address || block_address >= slots_end) {
            continue;
        }

        /* Only the exact payload start is releasable; interior pointers fail
         * the stride check. 只有精确 payload 起始地址可释放；内部指针会因
         * 步长检查失败而被拒绝。 */
        offset = block_address - payload_address;
        if ((offset % state->slot_stride) != 0U) {
            return 0;
        }

        *class_index = index;
        *slot_index = (uint32_t)(offset / state->slot_stride);
        *slot = (uint8_t *)(storage_address + offset);
        return 1;
    }

    return 0;
}

int qm_mem_pool_init(qm_mem_pool_t *pool, const qm_mem_pool_config_t *config)
{
    uint32_t index;

    if (pool == NULL || config == NULL) {
        return -QM_EINVAL;
    }

    if (pool_is_active(pool)) {
        return -QM_EBUSY;
    }

    if (!pool_can_initialize(pool)) {
        return -QM_EINIT;
    }

    /* Validate all borrowed ranges before writing caller-owned class state.
     * 在写入调用方持有的规格状态前校验全部借用区域。 */
    if (!pool_validate_config(pool, config)) {
        return -QM_EINVAL;
    }

    for (index = 0U; index < config->class_count; ++index) {
        pool_initialize_class(&config->classes[index], &config->class_states[index]);
    }

    pool->classes = config->classes;
    pool->class_states = config->class_states;
    pool->class_count = config->class_count;
    pool->allow_larger = config->allow_larger;
    pool->active_blocks = 0U;
    pool->peak_active_blocks = 0U;
    pool->allocation_failures = 0U;
    pool->internal_state = QM_MEM_POOL_STATE_ACTIVE;
    return QM_EOK;
}

int qm_mem_pool_deinit(qm_mem_pool_t *pool)
{
    if (!pool_is_active(pool)) {
        return -QM_EINIT;
    }

    if (pool->active_blocks != 0U) {
        return -QM_EBUSY;
    }

    /* Pool owns no backing storage; clearing the instance ends only borrows.
     * Pool 不拥有后备存储；清零实例只结束借用关系。 */
    pool_clear(pool, sizeof(*pool));
    return QM_EOK;
}

void *qm_mem_pool_alloc(qm_mem_pool_t *pool, size_t size)
{
    qm_mem_pool_class_state_t *state;
    uint8_t *slot;
    uint32_t class_index;
    uint32_t next_index;
    uint32_t fallback_index;

    if (!pool_is_active(pool) || size == 0U ||
        !pool_find_first_fitting_class(pool, size, &class_index)) {
        if (pool_is_active(pool) && size != 0U) {
            pool_increment_counter(&pool->allocation_failures);
        }
        return NULL;
    }

    state = &pool->class_states[class_index];
    /* Policy fallback is limited to a larger configured class with a slot.
     * 策略回退仅限于存在空槽的更大已配置规格。 */
    if (state->free_head == QM_MEM_POOL_FREE_HEAD_NONE &&
        pool->allow_larger == QM_TRUE) {
        for (fallback_index = class_index + 1U;
             fallback_index < pool->class_count; ++fallback_index) {
            if (pool->class_states[fallback_index].free_head !=
                QM_MEM_POOL_FREE_HEAD_NONE) {
                class_index = fallback_index;
                state = &pool->class_states[class_index];
                break;
            }
        }
    }

    if (state->free_head == QM_MEM_POOL_FREE_HEAD_NONE ||
        state->free_head >= pool->classes[class_index].block_count ||
        pool->active_blocks == UINT32_MAX) {
        pool_increment_counter(&pool->allocation_failures);
        pool_increment_counter(&state->allocation_failures);
        return NULL;
    }

    slot = pool_slot_at(state, state->free_head);
    next_index = pool_slot_get_next(slot);
    if (pool_slot_get_active(slot) != 0U ||
        (next_index != QM_MEM_POOL_FREE_HEAD_NONE &&
         next_index >= pool->classes[class_index].block_count)) {
        pool_increment_counter(&pool->allocation_failures);
        pool_increment_counter(&state->allocation_failures);
        return NULL;
    }

    state->free_head = next_index;
    pool_slot_set_next(slot, QM_MEM_POOL_FREE_HEAD_NONE);
    pool_slot_set_active(slot, 1U);
    ++state->used_blocks;
    if (state->used_blocks > state->peak_used_blocks) {
        state->peak_used_blocks = state->used_blocks;
    }

    ++pool->active_blocks;
    if (pool->active_blocks > pool->peak_active_blocks) {
        pool->peak_active_blocks = pool->active_blocks;
    }

    return (uint8_t *)slot + QM_MEM_POOL_SLOT_OVERHEAD;
}

int qm_mem_pool_release(qm_mem_pool_t *pool, void *block)
{
    qm_mem_pool_class_state_t *state;
    uint8_t *slot;
    uint32_t class_index;
    uint32_t slot_index;

    if (block == NULL) {
        return -QM_EINVAL;
    }

    if (!pool_is_active(pool)) {
        return -QM_EINIT;
    }

    if (!pool_find_slot(pool, block, &class_index, &slot_index, &slot) ||
        pool_slot_get_active(slot) == 0U) {
        return -QM_EINVAL;
    }

    state = &pool->class_states[class_index];
    if (state->used_blocks == 0U || pool->active_blocks == 0U ||
        (state->free_head != QM_MEM_POOL_FREE_HEAD_NONE &&
         state->free_head >= pool->classes[class_index].block_count)) {
        return -QM_EINVAL;
    }

    pool_slot_set_active(slot, 0U);
    pool_slot_set_next(slot, state->free_head);
    state->free_head = slot_index;
    --state->used_blocks;
    --pool->active_blocks;
    return QM_EOK;
}

int qm_mem_pool_get_stats(qm_mem_pool_t *pool, qm_mem_pool_stats_t *stats)
{
    if (stats == NULL) {
        return -QM_EINVAL;
    }

    if (!pool_is_active(pool)) {
        return -QM_EINIT;
    }

    stats->class_count = pool->class_count;
    stats->active_blocks = pool->active_blocks;
    stats->peak_active_blocks = pool->peak_active_blocks;
    stats->allocation_failures = pool->allocation_failures;
    return QM_EOK;
}

int qm_mem_pool_get_class_stats(qm_mem_pool_t *pool, uint32_t class_index,
                                qm_mem_pool_class_stats_t *stats)
{
    const qm_mem_pool_class_config_t *config;
    const qm_mem_pool_class_state_t *state;

    if (stats == NULL) {
        return -QM_EINVAL;
    }

    if (!pool_is_active(pool)) {
        return -QM_EINIT;
    }

    if (class_index >= pool->class_count) {
        return -QM_EINVAL;
    }

    config = &pool->classes[class_index];
    state = &pool->class_states[class_index];
    stats->block_size = config->block_size;
    stats->slot_stride = state->slot_stride;
    stats->block_count = config->block_count;
    stats->used_blocks = state->used_blocks;
    stats->peak_used_blocks = state->peak_used_blocks;
    stats->allocation_failures = state->allocation_failures;
    return QM_EOK;
}

#endif /* CONFIG_QM_MEM_POOL_SUPPORT */
