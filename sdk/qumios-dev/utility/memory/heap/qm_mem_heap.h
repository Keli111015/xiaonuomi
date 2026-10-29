/**
 * @file qm_mem_heap.h
 * @brief Caller-owned variable-size Heap allocator. / 调用方持有的变长 Heap 分配器。
 */

#ifndef QM_MEM_HEAP_H
#define QM_MEM_HEAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_config.h"
#include "qm_errno.h"
#include "qm_types.h"

#ifndef CONFIG_QM_MEM_HEAP_SUPPORT
#define CONFIG_QM_MEM_HEAP_SUPPORT 0
#endif

#ifndef CONFIG_QM_MEM_HEAP_ALIGNMENT
#define CONFIG_QM_MEM_HEAP_ALIGNMENT 4
#endif

/**
 * @brief Minimum guaranteed Heap payload alignment in bytes. / Heap payload 的最小保证字节对齐。
 *
 * @details CONFIG_QM_MEM_HEAP_ALIGNMENT selects 4 (default), 8, 16, 32, or 64.
 *          All instances and callers must use the same build configuration;
 *          do not override this macro per translation unit. Select a value
 *          that meets the stored object's alignment requirement. The Heap
 *          may use stricter alignment for its metadata and discard additional
 *          leading or trailing Region bytes.
 *          CONFIG_QM_MEM_HEAP_ALIGNMENT 可选 4（默认）、8、16、32、64。全部实例和
 *          调用方必须使用同一构建配置，不能在单个编译单元中覆盖此宏。配置值须满足
 *          所存对象的对齐要求；Heap 可能为元数据采用更严格的对齐并额外舍弃区域边缘字节。
 */
#define QM_MEM_HEAP_ALIGNMENT CONFIG_QM_MEM_HEAP_ALIGNMENT

#if CONFIG_QM_MEM_HEAP_SUPPORT
#if (QM_MEM_HEAP_ALIGNMENT < 4) || (QM_MEM_HEAP_ALIGNMENT > 64) || \
    ((QM_MEM_HEAP_ALIGNMENT & (QM_MEM_HEAP_ALIGNMENT - 1)) != 0)
#error "Heap alignment must be 4, 8, 16, 32, or 64"
#endif
#endif

/**
 * @brief One caller-owned contiguous Heap byte range. / 一个由调用方持有的连续 Heap 字节区域。
 */
typedef struct {
    /** @brief Raw writable first byte of the Region; the Heap internally aligns the managed subrange, writes block metadata there, and borrows this storage while active. / Region 的原始可写首地址；Heap 会在内部对齐其管理子区、在其中写入块元数据，并在活跃期间借用该存储。 */
    void *start;
    /** @brief Raw Region capacity in bytes; after internal normalization it must leave room for Heap metadata and one aligned payload. The Heap may discard leading and trailing bytes to form its privately aligned managed subrange. It must not overlap another active allocator. / 原始 Region 容量，单位字节；经内部收敛后必须能容纳 Heap 元数据和一个对齐的 payload。Heap 可能舍弃前导和尾部字节以形成其按私有对齐收敛的管理子区；不得与其他活跃分配器重叠。 */
    size_t size;
} qm_mem_heap_region_t;

/**
 * @brief Snapshot of an active Heap. / 活跃 Heap 的统计快照。
 */
typedef struct {
    /** @brief Number of configured independent regions. / 已配置的独立区域数量。 */
    uint32_t region_count;
    /** @brief Number of currently allocated payloads. / 当前已分配 payload 数量。 */
    uint32_t active_allocations;
    /** @brief Cumulative failed allocation attempts since initialization. / 自初始化以来累计失败的分配次数。 */
    uint32_t allocation_failures;
    /** @brief Allocator-managed bytes after the region sentinel is reserved, including block headers. / 扣除区域尾哨兵后的分配器管理字节数，包含块头。 */
    size_t usable_bytes;
    /** @brief Allocator-managed bytes in allocated blocks, including block headers. / 已分配块占用的分配器管理字节数，包含块头。 */
    size_t used_bytes;
    /** @brief Allocator-managed bytes in free blocks, including block headers. / 空闲块中的分配器管理字节数，包含块头。 */
    size_t free_bytes;
    /** @brief Lowest observed free_bytes value since initialization. / 自初始化以来观测到的最小 free_bytes。 */
    size_t minimum_free_bytes;
    /** @brief Largest currently allocatable contiguous payload. / 当前可分配的最大连续 payload。 */
    size_t largest_free_block;
} qm_mem_heap_stats_t;

/**
 * @brief Caller-owned Heap state. / 调用方持有的 Heap 状态。
 *
 * @details Zero-initialize before first use and do not modify fields while active.
 *          The caller serializes every operation on one Heap instance.
 *          首次使用前必须清零；活跃期间不得由调用方修改字段。同一 Heap 实例的全部操作
 *          由调用方串行化。
 */
typedef struct {
    /** @brief Borrowed region descriptor array; descriptors and their ranges stay unchanged while active. / 被借用的区域描述符数组；活跃期间描述符和其区域范围必须保持不变。 */
    const qm_mem_heap_region_t *regions;
    /** @brief Private free-list head; managed only by the Heap API. / 私有空闲链表头；仅由 Heap API 管理。 */
    void *free_list;
    /** @brief Total managed-block capacity, including block headers. / 管理块总容量，包含块头。 */
    size_t usable_bytes;
    /** @brief Current allocated managed-block bytes. / 当前已分配的管理块字节数。 */
    size_t used_bytes;
    /** @brief Historical minimum free managed-block bytes. / 历史最小空闲管理块字节数。 */
    size_t minimum_free_bytes;
    /** @brief Number of borrowed region descriptors. / 被借用的区域描述符数量。 */
    uint32_t region_count;
    /** @brief Current live allocation count. / 当前存活分配数量。 */
    uint32_t active_allocations;
    /** @brief Cumulative failed allocation attempts. / 累计失败的分配次数。 */
    uint32_t allocation_failures;
    /** @brief Private lifecycle state; managed only by the Heap API. / 私有生命周期状态；仅由 Heap API 管理。 */
    uint32_t internal_state;
} qm_mem_heap_t;

/**
 * @brief Initialize a Heap over one or more caller regions. / 使用一个或多个调用方区域初始化 Heap。
 * @param[in,out] heap Caller-owned Heap state that is zero-initialized before first use or after successful deinitialization. / 调用方持有的 Heap 状态；首次使用前或反初始化成功后必须清零。
 * @param[in] regions Sorted, non-overlapping descriptors and raw writable ranges borrowed unchanged until deinitialization. Region starts need not be aligned; the Heap normalizes each range internally. / 按地址排序且互不重叠的描述符和原始可写区域；借用至反初始化成功且期间不得改变。Region 起始地址无需预先对齐；Heap 会在内部收敛每个区域。
 * @param[in] region_count Number of entries in regions; one is heap_4-like and multiple are heap_5-like. / regions 中的条目数；一个区域对应 heap_4 式行为，多个区域对应 heap_5 式行为。
 * @return QM_EOK on success; -QM_EINVAL for invalid layout, -QM_EBUSY if active, -QM_EINIT for invalid state. / 成功返回 QM_EOK；布局无效返回 -QM_EINVAL，已活跃返回 -QM_EBUSY，状态无效返回 -QM_EINIT。
 */
int qm_mem_heap_init(qm_mem_heap_t *heap,
                     const qm_mem_heap_region_t *regions,
                     uint32_t region_count);

/**
 * @brief End Heap use after all allocations are released. / 在释放全部分配后结束 Heap 使用。
 * @param[in,out] heap Active Heap state. / 活跃的 Heap 状态。
 * @return QM_EOK on success; -QM_EBUSY if allocations remain; -QM_EINIT if inactive. / 成功返回 QM_EOK；仍有分配时返回 -QM_EBUSY；未活跃时返回 -QM_EINIT。
 */
int qm_mem_heap_deinit(qm_mem_heap_t *heap);

/**
 * @brief Allocate a variable-size payload. / 分配指定大小的 payload。
 * @param[in,out] heap Active Heap state. / 活跃的 Heap 状态。
 * @param[in] size Requested payload size in bytes; zero is invalid. / 请求的 payload 大小，单位字节；零无效。
 * @return Payload pointer aligned to at least QM_MEM_HEAP_ALIGNMENT on success, otherwise NULL. / 成功返回至少按 QM_MEM_HEAP_ALIGNMENT 对齐的 payload 指针，失败返回 NULL。
 */
void *qm_mem_heap_alloc(qm_mem_heap_t *heap, size_t size);

/**
 * @brief Allocate and clear a payload. / 分配并清零 payload。
 * @param[in,out] heap Active Heap state. / 活跃的 Heap 状态。
 * @param[in] count Number of elements. / 元素数量。
 * @param[in] size Size of each element in bytes. / 每个元素的字节大小。
 * @return Zeroed payload pointer on success, NULL for overflow or allocation failure. / 成功返回清零后的 payload 指针；乘法溢出或分配失败返回 NULL。
 */
void *qm_mem_heap_calloc(qm_mem_heap_t *heap, size_t count, size_t size);

/**
 * @brief Resize a Heap allocation. / 调整 Heap 分配的大小。
 * @param[in,out] heap Active Heap state that owns ptr. / 持有 ptr 的活跃 Heap 状态。
 * @param[in] ptr Exact payload pointer returned by this Heap; NULL behaves as alloc. / 此 Heap 返回的精确 payload 指针；NULL 等同于 alloc。
 * @param[in] size New payload size in bytes; zero releases ptr and returns NULL. / 新 payload 大小，单位字节；零时释放 ptr 并返回 NULL。
 * @return Resized payload pointer on success, otherwise NULL; a failed resize leaves ptr valid. / 成功返回调整后的 payload 指针，失败返回 NULL；失败时 ptr 仍然有效。
 */
void *qm_mem_heap_realloc(qm_mem_heap_t *heap, void *ptr, size_t size);

/**
 * @brief Release one exact active Heap allocation. / 释放一个精确且仍活跃的 Heap 分配。
 * @param[in,out] heap Active Heap state that owns ptr. / 持有 ptr 的活跃 Heap 状态。
 * @param[in] ptr Exact payload pointer returned by qm_mem_heap_alloc, calloc, or realloc. / qm_mem_heap_alloc、calloc 或 realloc 返回的精确 payload 指针。
 * @return QM_EOK on success; -QM_EINVAL for NULL, foreign, interior, or already released pointers; -QM_EINIT if inactive. / 成功返回 QM_EOK；NULL、外来、内部或重复释放指针返回 -QM_EINVAL；未活跃返回 -QM_EINIT。
 */
int qm_mem_heap_release(qm_mem_heap_t *heap, void *ptr);

/**
 * @brief Read Heap allocation statistics. / 读取 Heap 分配统计信息。
 * @param[in] heap Active Heap state. / 活跃的 Heap 状态。
 * @param[out] stats Destination snapshot. / 统计快照输出位置。
 * @return QM_EOK on success; -QM_EINVAL for NULL stats; -QM_EINIT if inactive. / 成功返回 QM_EOK；stats 为 NULL 返回 -QM_EINVAL；未活跃返回 -QM_EINIT。
 */
int qm_mem_heap_get_stats(qm_mem_heap_t *heap,
                          qm_mem_heap_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* QM_MEM_HEAP_H */
