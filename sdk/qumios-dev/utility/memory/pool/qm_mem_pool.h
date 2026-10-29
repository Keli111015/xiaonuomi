/**
 * @file qm_mem_pool.h
 * @brief Caller-owned fixed-block and multi-size Pool allocator. / 调用方持有的固定块与多规格 Pool 分配器。
 */

#ifndef QM_MEM_POOL_H
#define QM_MEM_POOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_config.h"
#include "qm_errno.h"
#include "qm_types.h"

#ifndef CONFIG_QM_MEM_POOL_SUPPORT
#define CONFIG_QM_MEM_POOL_SUPPORT 0
#endif

#ifndef CONFIG_QM_MEM_POOL_ALIGNMENT
#define CONFIG_QM_MEM_POOL_ALIGNMENT 4
#endif

/**
 * @brief Configured Pool payload alignment in bytes. / 配置的 Pool payload 对齐字节数。
 * @details CONFIG_QM_MEM_POOL_ALIGNMENT selects 4 (default), 8, 16, 32, or 64.
 *          All instances and callers must use the same build configuration;
 *          do not override this macro per translation unit. Select a value
 *          that meets the stored object's alignment requirement. Pool derives
 *          an aligned inner interval from raw storage during initialization.
 *          CONFIG_QM_MEM_POOL_ALIGNMENT 可选 4（默认）、8、16、32、64。全部实例和
 *          调用方必须使用同一构建配置，不能在单个编译单元中覆盖此宏。配置值须满足
 *          所存对象的对齐要求；Pool 会在初始化时从原始存储推导内部对齐区间。
 */
#define QM_MEM_POOL_ALIGNMENT CONFIG_QM_MEM_POOL_ALIGNMENT

#if CONFIG_QM_MEM_POOL_SUPPORT
#if (QM_MEM_POOL_ALIGNMENT < 4) || (QM_MEM_POOL_ALIGNMENT > 64) || \
    ((QM_MEM_POOL_ALIGNMENT & (QM_MEM_POOL_ALIGNMENT - 1)) != 0)
#error "Pool alignment must be 4, 8, 16, 32, or 64"
#endif
#endif

/**
 * @brief Raw storage bytes that guarantee block_count Pool slots. / 保证 block_count 个 Pool 槽位的原始存储字节数。
 * @details Metadata is padded before the payload and the slot span is rounded
 *          to QM_MEM_POOL_ALIGNMENT. The extra QM_MEM_POOL_ALIGNMENT - 1 bytes
 *          preserve that span after the raw
 *          interval is rounded inward at both ends. Arguments must not
 *          overflow size_t; qm_mem_pool_init performs checked runtime
 *          validation. payload 前的元数据会补齐，槽位跨度也按 QM_MEM_POOL_ALIGNMENT
 *          向上对齐；增加的 QM_MEM_POOL_ALIGNMENT - 1 个字节可保证原始区间两端
 *          向内收敛后仍保留
 *          该跨度。参数不得使 size_t 溢出；qm_mem_pool_init 会执行带溢出检查的
 *          运行时校验。
 */
#define QM_MEM_POOL_STORAGE_SIZE(payload_size, block_count) \
    ((size_t)(block_count) * \
     (((size_t)(payload_size) + \
       (((2U * sizeof(uint32_t)) + \
         ((size_t)QM_MEM_POOL_ALIGNMENT - 1U)) & \
        ~((size_t)QM_MEM_POOL_ALIGNMENT - 1U)) + \
       ((size_t)QM_MEM_POOL_ALIGNMENT - 1U)) & \
      ~((size_t)QM_MEM_POOL_ALIGNMENT - 1U)) + \
     ((size_t)QM_MEM_POOL_ALIGNMENT - 1U))

/**
 * @brief Caller-owned backing storage for one ordered Pool size class. / 一个有序 Pool 规格的调用方后备存储。
 */
typedef struct {
    /** @brief Raw writable storage start; it may have any address alignment, and the Pool writes slot metadata after internally normalizing it. / 原始可写存储起始地址；地址可以不满足对齐要求，Pool 会在内部规范化后写入槽位元数据。 */
    void *storage;
    /** @brief Raw storage bytes; use QM_MEM_POOL_STORAGE_SIZE(block_size, block_count) to guarantee the configured slots after normalization, and do not overlap another active allocator. / 原始存储字节数；使用 QM_MEM_POOL_STORAGE_SIZE(block_size, block_count) 保证规范化后仍可容纳配置槽位，且不得与其他活跃分配器重叠。 */
    size_t storage_size;
    /** @brief Usable payload capacity of each block in bytes. / 每个块可用的 payload 容量，单位字节。 */
    size_t block_size;
    /** @brief Number of slots in this class. / 此规格中的槽位数量。 */
    uint32_t block_count;
} qm_mem_pool_class_config_t;

/**
 * @brief Caller-owned mutable runtime state for one Pool class. / 一个 Pool 规格的调用方可变运行状态。
 * @details The Pool API initializes and updates every field while active. / Pool API 在活跃期间初始化并更新所有字段。
 */
typedef struct {
    /** @brief Private normalized first-slot address. / 私有的规范化首槽位地址。 */
    void *storage_start;
    /** @brief Private free-list head index; UINT32_MAX means empty. / 私有空闲链表头索引；UINT32_MAX 表示为空。 */
    uint32_t free_head;
    /** @brief Actual storage bytes per slot, including metadata. / 每个槽位的实际存储字节数，包含元数据。 */
    size_t slot_stride;
    /** @brief Number of allocated slots in this class. / 此规格当前已分配槽位数量。 */
    uint32_t used_blocks;
    /** @brief Highest observed used_blocks value. / 历史最大 used_blocks 值。 */
    uint32_t peak_used_blocks;
    /** @brief Failed allocations charged to this class. / 计入此规格的失败分配次数。 */
    uint32_t allocation_failures;
} qm_mem_pool_class_state_t;

/**
 * @brief Input configuration consumed by qm_mem_pool_init. / qm_mem_pool_init 使用的输入配置。
 */
typedef struct {
    /** @brief Strictly ascending class definitions borrowed unchanged while Pool is active. / 严格递增的规格定义；Pool 活跃期间被借用且不得修改。 */
    const qm_mem_pool_class_config_t *classes;
    /** @brief Writable state entries, one per class, borrowed while Pool is active and managed only by the Pool API. / 每个规格对应一个可写状态项；Pool 活跃期间被借用且仅由 Pool API 管理。 */
    qm_mem_pool_class_state_t *class_states;
    /** @brief Number of entries in classes and class_states. / classes 和 class_states 中的条目数量。 */
    uint32_t class_count;
    /** @brief QM_TRUE permits an exhausted fitting class to use a larger class; QM_FALSE makes exhaustion fail immediately. No other value is valid. / QM_TRUE 允许已耗尽的匹配规格改用更大规格；QM_FALSE 表示耗尽即失败；其他值均无效。 */
    qm_bool allow_larger;
} qm_mem_pool_config_t;

/**
 * @brief Snapshot of an active Pool. / 活跃 Pool 的统计快照。
 */
typedef struct {
    /** @brief Number of active size classes. / 活跃规格数量。 */
    uint32_t class_count;
    /** @brief Total currently allocated blocks. / 当前已分配块总数。 */
    uint32_t active_blocks;
    /** @brief Highest observed active_blocks value. / 历史最大 active_blocks 值。 */
    uint32_t peak_active_blocks;
    /** @brief Cumulative failed allocation attempts. / 累计失败分配次数。 */
    uint32_t allocation_failures;
} qm_mem_pool_stats_t;

/**
 * @brief Snapshot of one active Pool class. / 一个活跃 Pool 规格的统计快照。
 */
typedef struct {
    /** @brief Usable payload capacity of a block. / 一个块的可用 payload 容量。 */
    size_t block_size;
    /** @brief Actual backing bytes per slot, including metadata. / 每个槽位的实际后备字节数，包含元数据。 */
    size_t slot_stride;
    /** @brief Number of configured slots. / 已配置槽位数量。 */
    uint32_t block_count;
    /** @brief Current allocated slots. / 当前已分配槽位数量。 */
    uint32_t used_blocks;
    /** @brief Highest observed allocated slots. / 历史最大已分配槽位数量。 */
    uint32_t peak_used_blocks;
    /** @brief Failed allocations charged to this class. / 计入此规格的失败分配次数。 */
    uint32_t allocation_failures;
} qm_mem_pool_class_stats_t;

/**
 * @brief Caller-owned Pool state. / 调用方持有的 Pool 状态。
 * @details Zero-initialize before first use and do not modify fields while active.
 *          The caller serializes every operation on one Pool instance.
 *          首次使用前必须清零；活跃期间不得由调用方修改字段。同一 Pool 实例的全部操作
 *          由调用方串行化。
 */
typedef struct {
    /** @brief Borrowed class definitions. / 被借用的规格定义。 */
    const qm_mem_pool_class_config_t *classes;
    /** @brief Borrowed mutable class states. / 被借用的可变规格状态。 */
    qm_mem_pool_class_state_t *class_states;
    /** @brief Number of active class definitions. / 活跃规格定义数量。 */
    uint32_t class_count;
    /** @brief Active larger-class fallback policy. / 活跃的大规格回退策略。 */
    qm_bool allow_larger;
    /** @brief Current allocated blocks over all classes. / 所有规格中的当前已分配块数。 */
    uint32_t active_blocks;
    /** @brief Highest observed active block count. / 历史最大活跃块数。 */
    uint32_t peak_active_blocks;
    /** @brief Cumulative failed allocations. / 累计失败分配次数。 */
    uint32_t allocation_failures;
    /** @brief Private lifecycle state; managed only by the Pool API. / 私有生命周期状态；仅由 Pool API 管理。 */
    uint32_t internal_state;
} qm_mem_pool_t;

/**
 * @brief Initialize a Pool from caller storage and class state. / 使用调用方存储和规格状态初始化 Pool。
 * @param[in,out] pool Caller-owned Pool state that is zero-initialized before first use or after successful deinitialization. / 调用方持有的 Pool 状态；首次使用前或反初始化成功后必须清零。
 * @param[in] config Class descriptions and state array; each raw class storage
 *            may be unaligned. Pool rounds the raw interval inward at both
 *            ends; QM_MEM_POOL_STORAGE_SIZE preserves the configured
 *            block_count. allow_larger must be QM_FALSE or QM_TRUE.
 *            Referenced arrays and storage remain borrowed until
 *            deinitialization and must not be changed directly.
 *            规格描述和状态数组；每个规格的原始存储可以未对齐。Pool 会将原始区间
 *            两端向内收敛；QM_MEM_POOL_STORAGE_SIZE 可保留配置的 block_count。
 *            allow_larger 只能为 QM_FALSE 或 QM_TRUE。引用的数组与存储会被借用至
 *            反初始化成功，期间不得直接修改。
 * @return QM_EOK on success; -QM_EINVAL for invalid or overlapping input; -QM_EBUSY if active; -QM_EINIT for invalid state. / 成功返回 QM_EOK；输入无效或重叠返回 -QM_EINVAL；已活跃返回 -QM_EBUSY；状态无效返回 -QM_EINIT。
 */
int qm_mem_pool_init(qm_mem_pool_t *pool,
                     const qm_mem_pool_config_t *config);

/**
 * @brief End Pool use after all blocks are released. / 在释放全部块后结束 Pool 使用。
 * @param[in,out] pool Active Pool state. / 活跃的 Pool 状态。
 * @return QM_EOK on success; -QM_EBUSY if blocks remain; -QM_EINIT if inactive. / 成功返回 QM_EOK；仍有块未释放时返回 -QM_EBUSY；未活跃时返回 -QM_EINIT。
 */
int qm_mem_pool_deinit(qm_mem_pool_t *pool);

/**
 * @brief Allocate a block with at least the requested payload capacity. / 分配 payload 容量至少满足请求的块。
 * @param[in,out] pool Active Pool state. / 活跃的 Pool 状态。
 * @param[in] size Requested payload size in bytes; zero is invalid. / 请求的 payload 大小，单位字节；零无效。
 * @return Payload pointer on success, NULL if inactive, too large, or exhausted. / 成功返回 payload 指针；未活跃、请求过大或耗尽时返回 NULL。
 */
void *qm_mem_pool_alloc(qm_mem_pool_t *pool, size_t size);

/**
 * @brief Release one exact active Pool block. / 释放一个精确且仍活跃的 Pool 块。
 * @param[in,out] pool Active Pool state that owns block. / 持有 block 的活跃 Pool 状态。
 * @param[in] block Exact payload pointer returned by qm_mem_pool_alloc. / qm_mem_pool_alloc 返回的精确 payload 指针。
 * @return QM_EOK on success; -QM_EINVAL for NULL, foreign, interior, or already released blocks; -QM_EINIT if inactive. / 成功返回 QM_EOK；NULL、外来、内部或重复释放的块返回 -QM_EINVAL；未活跃返回 -QM_EINIT。
 */
int qm_mem_pool_release(qm_mem_pool_t *pool, void *block);

/**
 * @brief Read aggregate Pool statistics. / 读取 Pool 汇总统计信息。
 * @param[in] pool Active Pool state. / 活跃的 Pool 状态。
 * @param[out] stats Destination snapshot. / 统计快照输出位置。
 * @return QM_EOK on success; -QM_EINVAL for NULL stats; -QM_EINIT if inactive. / 成功返回 QM_EOK；stats 为 NULL 返回 -QM_EINVAL；未活跃返回 -QM_EINIT。
 */
int qm_mem_pool_get_stats(qm_mem_pool_t *pool,
                          qm_mem_pool_stats_t *stats);

/**
 * @brief Read statistics for one size class. / 读取一个规格的统计信息。
 * @param[in] pool Active Pool state. / 活跃的 Pool 状态。
 * @param[in] class_index Zero-based class index. / 从零开始的规格索引。
 * @param[out] stats Destination snapshot. / 统计快照输出位置。
 * @return QM_EOK on success; -QM_EINVAL for NULL stats or an invalid index; -QM_EINIT if inactive. / 成功返回 QM_EOK；stats 为 NULL 或索引无效时返回 -QM_EINVAL；未活跃返回 -QM_EINIT。
 */
int qm_mem_pool_get_class_stats(qm_mem_pool_t *pool,
                                uint32_t class_index,
                                qm_mem_pool_class_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* QM_MEM_POOL_H */
