/**
 * @file qm_mem_heap.c
 * @brief Caller-owned variable-size Heap allocator. / 调用方持有的变长 Heap 分配器。
 */

#include "qm_mem_heap.h"
#include "qm_string.h"

#if CONFIG_QM_MEM_HEAP_SUPPORT

#define QM_MEM_HEAP_STATE_ACTIVE 0x4d484541U
#define QM_MEM_HEAP_BLOCK_ALLOCATED ((size_t)1U)
#define QM_MEM_HEAP_BLOCK_SIZE_MASK (~QM_MEM_HEAP_BLOCK_ALLOCATED)
#define QM_MEM_HEAP_BLOCK_MAGIC_FREE 0x48465245U
#define QM_MEM_HEAP_BLOCK_MAGIC_ALLOCATED 0x48414c4fU
#define QM_MEM_HEAP_BLOCK_MAGIC_SENTINEL 0x48454e44U

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

typedef struct qm_mem_heap_block {
    size_t size;
    struct qm_mem_heap_block *next_free;
    uint32_t magic;
} qm_mem_heap_block_t;

#if defined(_MSC_VER)
#define QM_MEM_HEAP_ALIGNOF(type) __alignof(type)
#elif defined(__GNUC__) || defined(__clang__)
#define QM_MEM_HEAP_ALIGNOF(type) __alignof__(type)
#else
#define QM_MEM_HEAP_ALIGNOF(type) \
    offsetof(struct { char prefix; type value; }, value)
#endif

#define QM_MEM_HEAP_BLOCK_NATIVE_ALIGNMENT \
    ((size_t)QM_MEM_HEAP_ALIGNOF(qm_mem_heap_block_t))
#define QM_MEM_HEAP_BLOCK_ALIGNMENT \
    ((QM_MEM_HEAP_BLOCK_NATIVE_ALIGNMENT > (size_t)QM_MEM_HEAP_ALIGNMENT) ? \
     QM_MEM_HEAP_BLOCK_NATIVE_ALIGNMENT : (size_t)QM_MEM_HEAP_ALIGNMENT)
#define QM_MEM_HEAP_ALIGN_UP_INTERNAL(value) \
    (((size_t)(value) + (QM_MEM_HEAP_BLOCK_ALIGNMENT - 1U)) & \
     ~(QM_MEM_HEAP_BLOCK_ALIGNMENT - 1U))
#define QM_MEM_HEAP_BLOCK_OVERHEAD \
    QM_MEM_HEAP_ALIGN_UP_INTERNAL(sizeof(qm_mem_heap_block_t))
#define QM_MEM_HEAP_MIN_BLOCK_SIZE \
    (QM_MEM_HEAP_BLOCK_OVERHEAD + QM_MEM_HEAP_BLOCK_ALIGNMENT)
#define QM_MEM_HEAP_MIN_REGION_SIZE_INTERNAL \
    ((2U * QM_MEM_HEAP_BLOCK_OVERHEAD) + QM_MEM_HEAP_BLOCK_ALIGNMENT)

typedef char heap_block_alignment_must_be_power_of_two[
    ((QM_MEM_HEAP_BLOCK_NATIVE_ALIGNMENT != 0U) &&
     ((QM_MEM_HEAP_BLOCK_NATIVE_ALIGNMENT &
       (QM_MEM_HEAP_BLOCK_NATIVE_ALIGNMENT - 1U)) == 0U) &&
     (QM_MEM_HEAP_BLOCK_ALIGNMENT != 0U) &&
     ((QM_MEM_HEAP_BLOCK_ALIGNMENT & (QM_MEM_HEAP_BLOCK_ALIGNMENT - 1U)) == 0U)) ?
        1 : -1];

/* qm_string uses uint32_t lengths; retain this allocator's size_t contract.
 * qm_string 使用 uint32_t 长度；保留本分配器的 size_t 契约。 */
static void heap_copy(void *destination, const void *source, size_t size)
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

static void heap_clear(void *destination, size_t size)
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

static int heap_size_add(size_t left, size_t right, size_t *result)
{
    if (result == NULL || left > SIZE_MAX - right) {
        return 0;
    }

    *result = left + right;
    return 1;
}

static int heap_size_multiply(size_t left, size_t right, size_t *result)
{
    if (result == NULL || (left != 0U && right > SIZE_MAX / left)) {
        return 0;
    }

    *result = left * right;
    return 1;
}

static int heap_align_up(size_t value, size_t *result)
{
    if (result == NULL ||
        value > SIZE_MAX - (QM_MEM_HEAP_BLOCK_ALIGNMENT - 1U)) {
        return 0;
    }

    *result = (value + (QM_MEM_HEAP_BLOCK_ALIGNMENT - 1U)) &
              ~(QM_MEM_HEAP_BLOCK_ALIGNMENT - 1U);
    return 1;
}

static int heap_range_end(const void *start, size_t size, uintptr_t *end)
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

static int heap_normalize_region(const qm_mem_heap_region_t *region,
                                 uint8_t **normalized_start,
                                 size_t *normalized_size)
{
    uintptr_t raw_start;
    uintptr_t raw_end;
    uintptr_t aligned_start;
    uintptr_t aligned_end;
    uintptr_t alignment_mask =
        (uintptr_t)(QM_MEM_HEAP_BLOCK_ALIGNMENT - 1U);

    if (region == NULL || region->start == NULL ||
        normalized_start == NULL || normalized_size == NULL ||
        !heap_range_end(region->start, region->size, &raw_end)) {
        return 0;
    }

    raw_start = (uintptr_t)region->start;
    if (raw_start > UINTPTR_MAX - alignment_mask) {
        return 0;
    }

    /* Meet the configured payload alignment, while aligning private block headers
     * to their native ABI requirement before any direct field access.
     * 满足配置的 payload 对齐，同时在直接访问私有块头字段前按 ABI 原生要求对齐。 */
    aligned_start = (raw_start + alignment_mask) & ~alignment_mask;
    aligned_end = raw_end & ~alignment_mask;
    if (aligned_start >= aligned_end) {
        return 0;
    }

    *normalized_start = (uint8_t *)aligned_start;
    *normalized_size = (size_t)(aligned_end - aligned_start);
    return 1;
}

static int heap_ranges_overlap(const void *left, size_t left_size,
                               const void *right, size_t right_size)
{
    uintptr_t left_end;
    uintptr_t right_end;

    if (!heap_range_end(left, left_size, &left_end) ||
        !heap_range_end(right, right_size, &right_end)) {
        return 1;
    }

    return (uintptr_t)left < right_end && (uintptr_t)right < left_end;
}

static int heap_is_active(const qm_mem_heap_t *heap)
{
    return heap != NULL && heap->internal_state == QM_MEM_HEAP_STATE_ACTIVE;
}

static int heap_can_initialize(const qm_mem_heap_t *heap)
{
    return heap != NULL && heap->internal_state == 0U;
}

static size_t heap_block_size(const qm_mem_heap_block_t *block)
{
    return block->size & QM_MEM_HEAP_BLOCK_SIZE_MASK;
}

static size_t heap_block_payload_size(const qm_mem_heap_block_t *block)
{
    return heap_block_size(block) - QM_MEM_HEAP_BLOCK_OVERHEAD;
}

static int heap_block_is_free(const qm_mem_heap_block_t *block)
{
    return block->magic == QM_MEM_HEAP_BLOCK_MAGIC_FREE &&
           (block->size & QM_MEM_HEAP_BLOCK_ALLOCATED) == 0U;
}

static int heap_block_is_allocated(const qm_mem_heap_block_t *block)
{
    return block->magic == QM_MEM_HEAP_BLOCK_MAGIC_ALLOCATED &&
           (block->size & QM_MEM_HEAP_BLOCK_ALLOCATED) != 0U;
}

static qm_mem_heap_block_t *heap_next_block(qm_mem_heap_block_t *block)
{
    return (qm_mem_heap_block_t *)((uint8_t *)block + heap_block_size(block));
}

static void heap_record_failure(qm_mem_heap_t *heap)
{
    if (heap->allocation_failures != UINT32_MAX) {
        ++heap->allocation_failures;
    }
}

static int heap_validate_regions(const qm_mem_heap_t *heap,
                                 const qm_mem_heap_region_t *regions,
                                 uint32_t region_count,
                                 size_t *usable_bytes)
{
    size_t descriptors_size;
    size_t total_usable = 0U;
    uintptr_t previous_end = 0U;
    uint32_t index;

    if (heap == NULL || regions == NULL || region_count == 0U ||
        usable_bytes == NULL ||
        !heap_size_multiply((size_t)region_count, sizeof(*regions),
                            &descriptors_size) ||
        heap_ranges_overlap(heap, sizeof(*heap), regions, descriptors_size)) {
        return 0;
    }

    /* Raw descriptor ranges remain the ownership and overlap boundary; the
     * Heap derives a smaller aligned range only for its own metadata.
     * 原始描述符范围仍是所有权和重叠校验边界；Heap 仅为自身元数据推导更小的对齐范围。 */
    for (index = 0U; index < region_count; ++index) {
        const qm_mem_heap_region_t *region = &regions[index];
        uint8_t *normalized_start;
        size_t normalized_size;
        size_t first_block_size;
        size_t managed_size;
        uintptr_t region_end;

        if (region->start == NULL ||
            region->size < QM_MEM_HEAP_MIN_REGION_SIZE_INTERNAL ||
            !heap_range_end(region->start, region->size, &region_end) ||
            !heap_normalize_region(region, &normalized_start, &normalized_size) ||
            heap_ranges_overlap(heap, sizeof(*heap), region->start,
                                region->size) ||
            heap_ranges_overlap(regions, descriptors_size, region->start,
                                region->size) ||
            (index > 0U && (uintptr_t)region->start < previous_end)) {
            return 0;
        }

        if (normalized_size < QM_MEM_HEAP_BLOCK_OVERHEAD) {
            return 0;
        }

        first_block_size = normalized_size - QM_MEM_HEAP_BLOCK_OVERHEAD;
        if (first_block_size < QM_MEM_HEAP_MIN_BLOCK_SIZE) {
            return 0;
        }

        managed_size = first_block_size;
        if (!heap_size_add(total_usable, managed_size, &total_usable)) {
            return 0;
        }

        previous_end = region_end;
    }

    *usable_bytes = total_usable;
    return 1;
}

static void heap_insert_free(qm_mem_heap_t *heap, qm_mem_heap_block_t *block)
{
    qm_mem_heap_block_t *previous = NULL;
    qm_mem_heap_block_t *current =
        (qm_mem_heap_block_t *)heap->free_list;

    while (current != NULL && (uintptr_t)current < (uintptr_t)block) {
        previous = current;
        current = current->next_free;
    }

    block->next_free = current;
    if (previous == NULL) {
        heap->free_list = block;
    } else {
        previous->next_free = block;
    }
}

static int heap_remove_free(qm_mem_heap_t *heap, qm_mem_heap_block_t *block)
{
    qm_mem_heap_block_t *previous = NULL;
    qm_mem_heap_block_t *current =
        (qm_mem_heap_block_t *)heap->free_list;

    while (current != NULL) {
        if (current == block) {
            if (previous == NULL) {
                heap->free_list = current->next_free;
            } else {
                previous->next_free = current->next_free;
            }
            block->next_free = NULL;
            return 1;
        }

        previous = current;
        current = current->next_free;
    }

    return 0;
}

static size_t heap_free_bytes(const qm_mem_heap_t *heap)
{
    const qm_mem_heap_block_t *block =
        (const qm_mem_heap_block_t *)heap->free_list;
    size_t total = 0U;

    while (block != NULL) {
        size_t managed_size = heap_block_size(block);

        if (!heap_size_add(total, managed_size, &total)) {
            return SIZE_MAX;
        }

        block = block->next_free;
    }

    return total;
}

static size_t heap_largest_free_block(const qm_mem_heap_t *heap)
{
    const qm_mem_heap_block_t *block =
        (const qm_mem_heap_block_t *)heap->free_list;
    size_t largest = 0U;

    while (block != NULL) {
        size_t payload_size = heap_block_payload_size(block);

        if (payload_size > largest) {
            largest = payload_size;
        }

        block = block->next_free;
    }

    return largest;
}

static void heap_update_minimum_free(qm_mem_heap_t *heap)
{
    size_t free_bytes = heap_free_bytes(heap);

    if (free_bytes < heap->minimum_free_bytes) {
        heap->minimum_free_bytes = free_bytes;
    }
}

static int heap_find_payload(qm_mem_heap_t *heap, const void *payload,
                             qm_mem_heap_block_t **found,
                             qm_mem_heap_block_t **previous)
{
    uint32_t region_index;

    if (payload == NULL || found == NULL || previous == NULL) {
        return 0;
    }

    for (region_index = 0U; region_index < heap->region_count;
         ++region_index) {
        const qm_mem_heap_region_t *region = &heap->regions[region_index];
        uint8_t *region_start;
        size_t region_size;
        uintptr_t region_end;
        uintptr_t sentinel_address;
        qm_mem_heap_block_t *block;
        qm_mem_heap_block_t *previous_block;

        /* State retains caller descriptors only; repeat normalization so
         * lookup uses the same managed bounds established at initialization.
         * 状态仅保留调用方描述符；重复收敛以确保查找使用初始化时相同的管理边界。 */
        if (!heap_normalize_region(region, &region_start, &region_size) ||
            region_size < QM_MEM_HEAP_BLOCK_OVERHEAD ||
            !heap_range_end(region_start, region_size, &region_end)) {
            return 0;
        }

        sentinel_address = region_end - QM_MEM_HEAP_BLOCK_OVERHEAD;
        block = (qm_mem_heap_block_t *)region_start;
        previous_block = NULL;

        while ((uintptr_t)block < region_end) {
            uintptr_t block_address = (uintptr_t)block;
            size_t block_size;
            int allocated;

            if (block_address == sentinel_address) {
                if (block->magic != QM_MEM_HEAP_BLOCK_MAGIC_SENTINEL ||
                    block->size != 0U) {
                    return 0;
                }
                break;
            }

            block_size = heap_block_size(block);
            allocated = (block->size & QM_MEM_HEAP_BLOCK_ALLOCATED) != 0U;
            if (block_size < QM_MEM_HEAP_MIN_BLOCK_SIZE ||
                block_size > (size_t)(sentinel_address - block_address) ||
                (block_size & (QM_MEM_HEAP_BLOCK_ALIGNMENT - 1U)) != 0U ||
                (allocated && !heap_block_is_allocated(block)) ||
                (!allocated && !heap_block_is_free(block))) {
                return 0;
            }

            if (payload == (const uint8_t *)block + QM_MEM_HEAP_BLOCK_OVERHEAD) {
                *found = block;
                *previous = previous_block;
                return 1;
            }

            previous_block = block;
            block = (qm_mem_heap_block_t *)((uint8_t *)block + block_size);
        }
    }

    return 0;
}

int qm_mem_heap_init(qm_mem_heap_t *heap,
                     const qm_mem_heap_region_t *regions,
                     uint32_t region_count)
{
    size_t usable_bytes;
    uint32_t index;

    if (heap == NULL || regions == NULL || region_count == 0U) {
        return -QM_EINVAL;
    }

    if (heap_is_active(heap)) {
        return -QM_EBUSY;
    }

    if (!heap_can_initialize(heap)) {
        return -QM_EINIT;
    }

    if (!heap_validate_regions(heap, regions, region_count, &usable_bytes)) {
        return -QM_EINVAL;
    }

    heap->regions = regions;
    heap->free_list = NULL;
    heap->usable_bytes = usable_bytes;
    heap->used_bytes = 0U;
    heap->minimum_free_bytes = usable_bytes;
    heap->region_count = region_count;
    heap->active_allocations = 0U;
    heap->allocation_failures = 0U;

    for (index = 0U; index < region_count; ++index) {
        const qm_mem_heap_region_t *region = &regions[index];
        uint8_t *region_start;
        size_t region_size;
        size_t first_block_size;
        qm_mem_heap_block_t *block;
        qm_mem_heap_block_t *sentinel;

        if (!heap_normalize_region(region, &region_start, &region_size) ||
            region_size < QM_MEM_HEAP_BLOCK_OVERHEAD ||
            region_size - QM_MEM_HEAP_BLOCK_OVERHEAD <
                QM_MEM_HEAP_MIN_BLOCK_SIZE) {
            heap_clear(heap, sizeof(*heap));
            return -QM_EINVAL;
        }

        first_block_size = region_size - QM_MEM_HEAP_BLOCK_OVERHEAD;
        block = (qm_mem_heap_block_t *)region_start;
        sentinel =
            (qm_mem_heap_block_t *)(region_start + first_block_size);

        /* A region-local sentinel stops physical coalescing at its boundary.
         * The original descriptor remains raw and unchanged.
         * 每个区域的本地哨兵会在区域边界处阻止物理块合并；原始描述符保持原样。 */
        block->size = first_block_size;
        block->next_free = NULL;
        block->magic = QM_MEM_HEAP_BLOCK_MAGIC_FREE;
        sentinel->size = 0U;
        sentinel->next_free = NULL;
        sentinel->magic = QM_MEM_HEAP_BLOCK_MAGIC_SENTINEL;
        heap_insert_free(heap, block);
    }

    heap->internal_state = QM_MEM_HEAP_STATE_ACTIVE;
    return QM_EOK;
}

int qm_mem_heap_deinit(qm_mem_heap_t *heap)
{
    if (!heap_is_active(heap)) {
        return -QM_EINIT;
    }

    if (heap->active_allocations != 0U) {
        return -QM_EBUSY;
    }

    /* Heap owns neither regions nor descriptor storage; this ends borrows.
     * Heap 不拥有区域或描述符存储；此操作只结束借用关系。 */
    heap_clear(heap, sizeof(*heap));
    return QM_EOK;
}

void *qm_mem_heap_alloc(qm_mem_heap_t *heap, size_t size)
{
    qm_mem_heap_block_t *block;
    size_t payload_size;
    size_t requested_block_size;

    if (!heap_is_active(heap)) {
        return NULL;
    }

    if (size == 0U || !heap_align_up(size, &payload_size) ||
        !heap_size_add(QM_MEM_HEAP_BLOCK_OVERHEAD, payload_size,
                       &requested_block_size)) {
        heap_record_failure(heap);
        return NULL;
    }

    block = (qm_mem_heap_block_t *)heap->free_list;
    while (block != NULL && heap_block_size(block) < requested_block_size) {
        block = block->next_free;
    }

    if (block == NULL || heap->active_allocations == UINT32_MAX) {
        heap_record_failure(heap);
        return NULL;
    }

    if (!heap_remove_free(heap, block)) {
        heap_record_failure(heap);
        return NULL;
    }

    if (heap_block_size(block) - requested_block_size >=
        QM_MEM_HEAP_MIN_BLOCK_SIZE) {
        qm_mem_heap_block_t *remainder = (qm_mem_heap_block_t *)
            ((uint8_t *)block + requested_block_size);

        /* Split only when the tail can later hold metadata and one aligned
         * payload. 仅当尾部仍能容纳元数据和一个对齐 payload 时才分裂。 */
        remainder->size = heap_block_size(block) - requested_block_size;
        remainder->next_free = NULL;
        remainder->magic = QM_MEM_HEAP_BLOCK_MAGIC_FREE;
        block->size = requested_block_size | QM_MEM_HEAP_BLOCK_ALLOCATED;
        heap_insert_free(heap, remainder);
    } else {
        block->size = heap_block_size(block) | QM_MEM_HEAP_BLOCK_ALLOCATED;
    }

    block->next_free = NULL;
    block->magic = QM_MEM_HEAP_BLOCK_MAGIC_ALLOCATED;
    heap->used_bytes += heap_block_size(block);
    ++heap->active_allocations;
    heap_update_minimum_free(heap);
    return (uint8_t *)block + QM_MEM_HEAP_BLOCK_OVERHEAD;
}

void *qm_mem_heap_calloc(qm_mem_heap_t *heap, size_t count, size_t size)
{
    size_t total_size;
    void *payload;

    if (!heap_is_active(heap)) {
        return NULL;
    }

    if (count == 0U || size == 0U ||
        !heap_size_multiply(count, size, &total_size)) {
        heap_record_failure(heap);
        return NULL;
    }

    payload = qm_mem_heap_alloc(heap, total_size);
    if (payload != NULL) {
        heap_clear(payload, total_size);
    }

    return payload;
}

void *qm_mem_heap_realloc(qm_mem_heap_t *heap, void *ptr, size_t size)
{
    qm_mem_heap_block_t *block;
    qm_mem_heap_block_t *previous;
    size_t requested_payload_size;
    size_t copy_size;
    void *replacement;

    if (!heap_is_active(heap)) {
        return NULL;
    }

    if (ptr == NULL) {
        return qm_mem_heap_alloc(heap, size);
    }

    if (size == 0U) {
        (void)qm_mem_heap_release(heap, ptr);
        return NULL;
    }

    if (!heap_align_up(size, &requested_payload_size) ||
        !heap_find_payload(heap, ptr, &block, &previous) ||
        !heap_block_is_allocated(block)) {
        heap_record_failure(heap);
        return NULL;
    }

    if (requested_payload_size <= heap_block_payload_size(block)) {
        return ptr;
    }

    replacement = qm_mem_heap_alloc(heap, size);
    if (replacement == NULL) {
        return NULL;
    }

    copy_size = heap_block_payload_size(block);
    heap_copy(replacement, ptr, copy_size);
    /* Allocation happens before release, so a failed growth keeps ptr valid.
     * 先分配后释放，因此扩容失败时 ptr 保持有效。 */
    if (qm_mem_heap_release(heap, ptr) != QM_EOK) {
        (void)qm_mem_heap_release(heap, replacement);
        return NULL;
    }

    return replacement;
}

int qm_mem_heap_release(qm_mem_heap_t *heap, void *ptr)
{
    qm_mem_heap_block_t *block;
    qm_mem_heap_block_t *previous;
    qm_mem_heap_block_t *next;
    size_t released_block_size;

    if (ptr == NULL) {
        return -QM_EINVAL;
    }

    if (!heap_is_active(heap)) {
        return -QM_EINIT;
    }

    /* Scan physical blocks rather than trusting ptr - header_size. This
     * rejects interior, foreign, and repeated-release pointers. 物理扫描
     * 区块而非直接相信 ptr - header_size，可拒绝内部、外来和重复释放指针。 */
    if (!heap_find_payload(heap, ptr, &block, &previous) ||
        !heap_block_is_allocated(block) || heap->active_allocations == 0U) {
        return -QM_EINVAL;
    }

    released_block_size = heap_block_size(block);
    if (heap->used_bytes < released_block_size) {
        return -QM_EINVAL;
    }

    block->size = heap_block_size(block);
    block->magic = QM_MEM_HEAP_BLOCK_MAGIC_FREE;
    block->next_free = NULL;
    next = heap_next_block(block);

    if (heap_block_is_free(next)) {
        (void)heap_remove_free(heap, next);
        block->size = heap_block_size(block) + heap_block_size(next);
    }

    if (previous != NULL && heap_block_is_free(previous)) {
        (void)heap_remove_free(heap, previous);
        previous->size = heap_block_size(previous) + heap_block_size(block);
        block = previous;
    }

    heap_insert_free(heap, block);
    heap->used_bytes -= released_block_size;
    --heap->active_allocations;
    return QM_EOK;
}

int qm_mem_heap_get_stats(qm_mem_heap_t *heap,
                          qm_mem_heap_stats_t *stats)
{
    if (stats == NULL) {
        return -QM_EINVAL;
    }

    if (!heap_is_active(heap)) {
        return -QM_EINIT;
    }

    stats->region_count = heap->region_count;
    stats->active_allocations = heap->active_allocations;
    stats->allocation_failures = heap->allocation_failures;
    stats->usable_bytes = heap->usable_bytes;
    stats->used_bytes = heap->used_bytes;
    stats->free_bytes = heap_free_bytes(heap);
    stats->minimum_free_bytes = heap->minimum_free_bytes;
    stats->largest_free_block = heap_largest_free_block(heap);
    return QM_EOK;
}

#endif /* CONFIG_QM_MEM_HEAP_SUPPORT */
