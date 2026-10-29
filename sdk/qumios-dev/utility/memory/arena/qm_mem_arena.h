/**
 * @file qm_mem_arena.h
 * @brief Caller-owned linear Arena allocator. / 调用方持有的线性 Arena 分配器。
 */

#ifndef QM_MEM_ARENA_H
#define QM_MEM_ARENA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_config.h"
#include "qm_errno.h"
#include "qm_types.h"

#ifndef CONFIG_QM_MEM_ARENA_SUPPORT
#define CONFIG_QM_MEM_ARENA_SUPPORT 0
#endif

#ifndef CONFIG_QM_MEM_ARENA_ALIGNMENT
#define CONFIG_QM_MEM_ARENA_ALIGNMENT 4
#endif

/**
 * @brief Configured Arena payload alignment in bytes. / 配置的 Arena payload 对齐字节数。
 * @details CONFIG_QM_MEM_ARENA_ALIGNMENT selects 4 (default), 8, 16, 32, or 64.
 *          All instances and callers must use the same build configuration;
 *          do not override this macro per translation unit. Select a value
 *          that meets the stored object's alignment requirement. The Arena
 *          normalizes an unaligned caller buffer during initialization.
 *          CONFIG_QM_MEM_ARENA_ALIGNMENT 可选 4（默认）、8、16、32、64。全部实例和
 *          调用方必须使用同一构建配置，不能在单个编译单元中覆盖此宏。配置值须满足
 *          所存对象的对齐要求；Arena 会在初始化时规范化未对齐的调用方缓冲区。
 */
#define QM_MEM_ARENA_ALIGNMENT CONFIG_QM_MEM_ARENA_ALIGNMENT

#if CONFIG_QM_MEM_ARENA_SUPPORT
#if (QM_MEM_ARENA_ALIGNMENT < 4) || (QM_MEM_ARENA_ALIGNMENT > 64) || \
    ((QM_MEM_ARENA_ALIGNMENT & (QM_MEM_ARENA_ALIGNMENT - 1)) != 0)
#error "Arena alignment must be 4, 8, 16, 32, or 64"
#endif
#endif

/**
 * @brief Snapshot of an active Arena. / 活跃 Arena 的统计快照。
 */
typedef struct {
    /** @brief Usable capacity after internal buffer alignment in bytes. / 内部规范化缓冲区对齐后的可用容量，单位字节。 */
    size_t capacity_bytes;
    /** @brief Bytes currently consumed by the linear cursor. / 当前被线性游标占用的字节数。 */
    size_t used_bytes;
    /** @brief Largest observed cursor value. / 历史最大游标值。 */
    size_t peak_used_bytes;
    /** @brief Bytes after the current cursor. / 当前游标之后的剩余字节数。 */
    size_t free_bytes;
    /** @brief Current mark generation used to reject stale marks. / 用于拒绝过期标记的当前 generation。 */
    uint32_t generation;
} qm_mem_arena_stats_t;

/**
 * @brief Caller-owned Arena state. / 调用方持有的 Arena 状态。
 * @details Zero-initialize before first use and do not modify fields while active.
 *          The caller serializes every operation on one Arena instance.
 *          首次使用前必须清零；活跃期间不得由调用方修改字段。同一 Arena 实例的全部操作
 *          由调用方串行化。
 */
typedef struct qm_mem_arena {
    /** @brief Borrowed writable aligned subrange normalized from the caller buffer; allocations remain valid only while this lifetime remains active. / 从调用方缓冲区规范化得到的被借用可写对齐子区间；分配结果仅在本生命周期保持活跃时有效。 */
    uint8_t *buffer;
    /** @brief Usable normalized backing capacity in bytes. / 规范化后的可用后备容量，单位字节。 */
    size_t size;
    /** @brief Current next-allocation offset. / 当前下一次分配的偏移量。 */
    size_t offset;
    /** @brief Largest observed offset. / 历史最大偏移量。 */
    size_t peak_used;
    /** @brief Mark generation; reset and reinitialization invalidate earlier marks. / 标记 generation；reset 和重新初始化会使旧标记失效。 */
    uint32_t generation;
    /** @brief Private lifecycle state; managed only by the Arena API. / 私有生命周期状态；仅由 Arena API 管理。 */
    uint32_t internal_state;
} qm_mem_arena_t;

/**
 * @brief Cursor bookmark for scoped rewind. / 用于作用域回退的游标书签。
 */
typedef struct {
    /** @brief Arena instance that produced this mark. / 生成此标记的 Arena 实例。 */
    const qm_mem_arena_t *arena;
    /** @brief Generation captured with the mark. / 随标记保存的 generation。 */
    uint32_t generation;
    /** @brief Cursor offset captured with the mark. / 随标记保存的游标偏移量。 */
    size_t offset;
} qm_mem_arena_mark_t;

/**
 * @brief Initialize an Arena over caller-owned storage. / 使用调用方持有的存储初始化 Arena。
 * @param[in,out] arena Caller-owned Arena state that is zero-initialized before first use or inactive after deinitialization. / 调用方持有的 Arena 状态；首次使用前必须清零，反初始化后处于非活跃状态。
 * @param[in] buffer Writable raw backing storage borrowed unchanged until deinitialization; pre-alignment is not required. / 可写原始后备存储；借用至反初始化成功且期间不得改变，不要求预先对齐。
 * @param[in] size Raw backing storage capacity in bytes; must be nonzero. / 原始后备存储容量，单位字节；必须非零。
 * @return QM_EOK on success; -QM_EINVAL for invalid or overlapping storage, or when no usable aligned subrange remains; -QM_EBUSY if active; -QM_EINIT for invalid state; -QM_EFULL if generation cannot advance. / 成功返回 QM_EOK；存储无效、重叠或对齐后没有可用子区间返回 -QM_EINVAL；已活跃返回 -QM_EBUSY；状态无效返回 -QM_EINIT；generation 无法递增时返回 -QM_EFULL。
 * @details The implementation rounds the raw start address upward and the raw end address downward to QM_MEM_ARENA_ALIGNMENT.
 *          Bytes excluded at either edge are not available to Arena allocations or reported by qm_mem_arena_get_stats.
 *          实现会将原始起始地址向上、原始结束地址向下收敛到 QM_MEM_ARENA_ALIGNMENT；两端被排除的字节不能用于 Arena 分配，也不会由 qm_mem_arena_get_stats 统计。
 */
int qm_mem_arena_init(qm_mem_arena_t *arena, void *buffer, size_t size);

/**
 * @brief End Arena use without freeing caller storage. / 结束 Arena 使用，不释放调用方存储。
 * @param[in,out] arena Active Arena state. / 活跃的 Arena 状态。
 * @return QM_EOK on success; -QM_EINIT if inactive. / 成功返回 QM_EOK；未活跃返回 -QM_EINIT。
 */
int qm_mem_arena_deinit(qm_mem_arena_t *arena);

/**
 * @brief Allocate a linear payload from the Arena cursor. / 从 Arena 游标线性分配 payload。
 * @param[in,out] arena Active Arena state. / 活跃的 Arena 状态。
 * @param[in] size Requested payload size in bytes; zero is invalid. / 请求的 payload 大小，单位字节；零无效。
 * @return A QM_MEM_ARENA_ALIGNMENT-aligned payload pointer on success, otherwise NULL. / 成功返回按 QM_MEM_ARENA_ALIGNMENT 对齐的 payload 指针，失败返回 NULL。
 * @details The implementation aligns each allocation internally. Callers do not provide an alignment value.
 *          实现会在内部对齐每次分配；调用方无需提供对齐值。
 */
void *qm_mem_arena_alloc(qm_mem_arena_t *arena, size_t size);

/**
 * @brief Capture the current cursor for a later rewind. / 保存当前游标以供后续回退。
 * @param[in] arena Active Arena state. / 活跃的 Arena 状态。
 * @return A valid mark for an active Arena, or a zero mark when inactive. / 活跃 Arena 返回有效标记，未活跃时返回零标记。
 */
qm_mem_arena_mark_t qm_mem_arena_mark(qm_mem_arena_t *arena);

/**
 * @brief Rewind to a mark from the same current Arena generation. / 回退到同一 Arena 当前 generation 创建的标记。
 * @param[in,out] arena Active Arena state. / 活跃的 Arena 状态。
 * @param[in] mark Mark returned by qm_mem_arena_mark at or before the current cursor. / 在当前游标位置或之前由 qm_mem_arena_mark 返回的标记。
 * @return QM_EOK on success; -QM_EINVAL for a foreign, stale, or forward mark; -QM_EINIT if inactive. / 成功返回 QM_EOK；外来、过期或前向标记返回 -QM_EINVAL；未活跃返回 -QM_EINIT。
 */
int qm_mem_arena_rewind(qm_mem_arena_t *arena,
                        qm_mem_arena_mark_t mark);

/**
 * @brief Make the whole usable backing subrange available again. / 重新提供整个可用后备子区间。
 * @param[in,out] arena Active Arena state. / 活跃的 Arena 状态。
 * @return QM_EOK on success; -QM_EINIT if inactive; -QM_EFULL if generation cannot advance. / 成功返回 QM_EOK；未活跃返回 -QM_EINIT；generation 无法递增时返回 -QM_EFULL。
 * @details Existing allocations and marks become invalid. / 已有分配和标记都会失效。
 */
int qm_mem_arena_reset(qm_mem_arena_t *arena);

/**
 * @brief Read Arena usage statistics. / 读取 Arena 使用统计信息。
 * @param[in] arena Active Arena state. / 活跃的 Arena 状态。
 * @param[out] stats Destination snapshot. / 统计快照输出位置。
 * @return QM_EOK on success; -QM_EINVAL for NULL stats; -QM_EINIT if inactive. / 成功返回 QM_EOK；stats 为 NULL 返回 -QM_EINVAL；未活跃返回 -QM_EINIT。
 */
int qm_mem_arena_get_stats(qm_mem_arena_t *arena,
                           qm_mem_arena_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* QM_MEM_ARENA_H */
