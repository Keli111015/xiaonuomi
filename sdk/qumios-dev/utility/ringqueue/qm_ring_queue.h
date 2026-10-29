/**
 * @file qm_ring_queue.h
 * @brief Caller-owned variable-length record ring queue. /
 *        调用方持有存储的可变长度记录环形队列。
 */

#ifndef QM_RING_QUEUE_H
#define QM_RING_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_errno.h"
#include "qm_types.h"

/**
 * @brief Bytes consumed by the internal length header of every record. /
 *        每条记录内部长度头占用的字节数。
 */
#define QM_RING_QUEUE_RECORD_HEADER_BYTES ((uint32_t)sizeof(uint32_t))

/**
 * @brief Variable-length record FIFO state. / 可变长度记录 FIFO 状态。
 * @details
 * Each record is stored internally as a private uint32_t payload length
 * followed by the payload. The header and payload may wrap at the end of the
 * backing storage. The queue copies records and does not expose internal
 * pointers. A segmented push can copy multiple input ranges into one record.
 *
 * 每条记录内部保存为私有的 uint32_t payload 长度和 payload；长度头和 payload
 * 都可能跨越后备存储尾部。队列采用复制语义，不暴露内部指针。分段入队可以将多个输入
 * 区间复制为一条记录。
 *
 * The caller owns storage and must serialize every operation on one queue.
 * The queue does not allocate, wait, or provide synchronization. The queue
 * control block and storage must not overlap. A nonzero input or output
 * payload range, and every size output, must not overlap either of them.
 * A nonzero pop payload range must also not overlap its size output.
 * After initialization, callers must not modify queue fields or storage bytes
 * that contain unconsumed records; the API manages those bytes until pop,
 * discard, or reset.
 *
 * 后备存储由调用方持有；同一队列的全部操作必须由调用方串行化。队列不分配内存、
 * 不等待，也不提供同步。queue 控制块不得与 storage 重叠；非零输入或输出 payload
 * 区间和全部 size 输出均不得与二者重叠；非零 pop payload 输出也不得与其 size 输出
 * 重叠。初始化后，调用方不得修改 queue 字段或含有未消费记录的 storage 字节；这些字节
 * 在 pop、discard 或 reset 前由 API 管理。
 */
typedef struct {
    /** @brief Caller-owned backing storage. / 调用方持有的后备存储。 */
    uint8_t *storage;
    /** @brief Total backing storage capacity in bytes. / 后备存储总容量，单位为字节。 */
    uint32_t capacity_bytes;
    /** @brief Offset of the oldest record. / 最旧记录的偏移量。 */
    uint32_t read_offset;
    /** @brief Offset where the next record is written. / 下一条记录的写入偏移量。 */
    uint32_t write_offset;
    /** @brief Bytes occupied by complete records, including length headers. /
     *         完整记录占用的字节数，包含长度头。 */
    uint32_t used_bytes;
} qm_ring_queue_t;

/**
 * @brief One input range for a segmented record push. /
 *        分段记录入队时的一个输入区间。
 * @details Segment boundaries are not stored in the queue; segments are
 *          concatenated in array order into one payload. /
 *          队列不会保存分段边界；各 segment 按数组顺序拼接为一个 payload。
 */
typedef struct {
    /** @brief Input bytes; NULL is allowed only when size is zero. /
     *         输入字节；仅在 size 为零时允许为 NULL。 */
    const void *data;
    /** @brief Input range length in bytes. / 输入区间长度，单位为字节。 */
    uint32_t size;
} qm_ring_queue_segment_t;

/**
 * @brief Initialize a queue over caller-owned storage. /
 *        使用调用方持有的存储初始化队列。
 * @param[out] queue Queue state owned by the caller. / 调用方持有的队列状态。
 * @param[in] storage Writable backing storage; must not overlap queue and
 *                     must remain valid while the
 *                     queue is initialized. /
 *                     可写后备存储；不得与 queue 重叠，队列处于初始化状态期间必须保持
 *                     有效。
 * @param[in] capacity_bytes Storage capacity in bytes; must be at least
 *                           QM_RING_QUEUE_RECORD_HEADER_BYTES. /
 *                           存储容量，单位为字节；必须至少为
 *                           QM_RING_QUEUE_RECORD_HEADER_BYTES。
 * @return QM_EOK on success; -QM_EINVAL for invalid or overlapping arguments. /
 *         成功返回 QM_EOK；参数无效返回 -QM_EINVAL。
 * @details A successful reinitialization discards all existing records. The
 *          function does not clear the backing storage. A failed call leaves
 *          queue unchanged.
 *
 *          成功重新初始化会丢弃现有全部记录，但不会清零后备存储。调用失败时 queue
 *          保持不变。
 */
int qm_ring_queue_init(qm_ring_queue_t *queue,
                       void *storage,
                       uint32_t capacity_bytes);

/**
 * @brief Discard all records and retain the backing storage. /
 *        丢弃全部记录并保留后备存储。
 * @param[in,out] queue Initialized queue state. / 已初始化的队列状态。
 * @return QM_EOK on success; -QM_EINVAL when the queue/storage binding is
 *         invalid. /
 *         成功返回 QM_EOK；queue/storage 绑定无效返回 -QM_EINVAL。
 * @details Reset needs only a valid queue/storage binding, not valid offsets
 *          or record headers, so it can recover a corrupted queue state. It
 *          does not clear backing storage.
 *
 *          reset 只要求 queue/storage 绑定有效，不要求偏移量或记录头有效，因此可用于
 *          恢复损坏的队列状态；它不会清零后备存储。
 */
int qm_ring_queue_reset(qm_ring_queue_t *queue);

/**
 * @brief Copy one record into the queue. / 将一条记录复制到队列。
 * @param[in,out] queue Initialized queue state. / 已初始化的队列状态。
 * @param[in] data Payload to copy; may be NULL only when size is zero and must
 *                 not overlap queue or storage for a nonzero size. /
 *                 要复制的 payload；仅在 size 为零时允许为 NULL；非零时不得与 queue
 *                 或 storage 重叠。
 * @param[in] size Payload length in bytes. / payload 长度，单位为字节。
 * @return QM_EOK on success; -QM_EFULL when current free space is insufficient;
 *         -QM_EINVAL for invalid arguments or a record that cannot fit even in
 *         an empty queue. /
 *         成功返回 QM_EOK；当前剩余空间不足返回 -QM_EFULL；参数无效或单条记录即使
 *         在空队列中也无法容纳返回 -QM_EINVAL。
 * @details A zero-length record is valid and occupies one internal length
 *          header. Failure leaves queue state and storage unchanged.
 *
 *          零长度记录有效，仍占用一个内部长度头；失败时 queue 状态和 storage 保持不变。
 */
int qm_ring_queue_push(qm_ring_queue_t *queue,
                       const void *data,
                       uint32_t size);

/**
 * @brief Copy multiple input ranges as one record. /
 *        将多个输入区间复制为一条记录。
 * @param[in,out] queue Initialized queue state. / 已初始化的队列状态。
 * @param[in] segments Input ranges in payload order. May be NULL only when
 *                     segment_count is zero. /
 *                     按 payload 顺序排列的输入区间；仅在 segment_count 为零时允许为 NULL。
 * @param[in] segment_count Number of input ranges. Zero creates a zero-length
 *                          record. / 输入区间数量；零表示入队一条零长度记录。
 * @return QM_EOK on success; -QM_EFULL when the complete record does not fit;
 *         -QM_EINVAL for invalid ranges, overflow, overlap, or an oversized
 *         record. / 成功返回 QM_EOK；完整记录空间不足返回 -QM_EFULL；区间无效、溢出、
 *         重叠或记录过大返回 -QM_EINVAL。
 * @details All ranges are validated before queue state or storage changes.
 *          The queue stores one private length header followed by all segment
 *          bytes. / 所有区间会在修改 queue 或 storage 前完成校验。队列只保存一个私有
 *          长度头，后面紧跟全部 segment 字节。
 */
int qm_ring_queue_push_segments(qm_ring_queue_t *queue,
                                const qm_ring_queue_segment_t *segments,
                                uint32_t segment_count);

/**
 * @brief Copy multiple input ranges as one record, evicting old records. /
 *        将多个输入区间复制为一条记录，必要时淘汰旧记录。
 * @param[in,out] queue Initialized queue state. / 已初始化的队列状态。
 * @param[in] segments Input ranges in payload order. May be NULL only when
 *                     segment_count is zero. /
 *                     按 payload 顺序排列的输入区间；仅在 segment_count 为零时允许为 NULL。
 * @param[in] segment_count Number of input ranges. Zero creates a zero-length
 *                          record. / 输入区间数量；零表示入队一条零长度记录。
 * @return QM_EOK on success; -QM_EINVAL for invalid ranges, overflow, or an
 *         oversized record; -QM_EIO if an old record header is malformed. /
 *         成功返回 QM_EOK；区间无效、溢出或记录过大返回 -QM_EINVAL；旧记录头损坏返回
 *         -QM_EIO。
 * @details Complete old records are evicted only after all input ranges and
 *          total size have been validated. Failure leaves the live queue and
 *          storage unchanged. / 所有输入区间和总长度校验通过后才会淘汰旧记录。失败时
 *          当前 queue 和 storage 保持不变。
 */
int qm_ring_queue_push_segments_overwrite(
    qm_ring_queue_t *queue,
    const qm_ring_queue_segment_t *segments,
    uint32_t segment_count);

/**
 * @brief Copy a record, overwriting the oldest records when necessary. /
 *        复制一条记录；空间不足时覆盖最旧记录。
 * @param[in,out] queue Initialized queue state. / 已初始化的队列状态。
 * @param[in] data Payload to copy; may be NULL only when size is zero and must
 *                 not overlap queue or storage for a nonzero size. /
 *                 要复制的 payload；仅在 size 为零时允许为 NULL；非零时不得与 queue
 *                 或 storage 重叠。
 * @param[in] size Payload length in bytes. / payload 长度，单位为字节。
 * @return QM_EOK on success; -QM_EINVAL for invalid arguments or a record that
 *         cannot fit in an empty queue; -QM_EIO if a record header examined
 *         while evicting old records is malformed. /
 *         成功返回 QM_EOK；参数无效或单条记录无法在空队列中容纳返回 -QM_EINVAL；在
 *         淘汰旧记录时读取到的记录头损坏返回 -QM_EIO。
 * @details When space is insufficient, complete oldest records are discarded
 *          until the new record fits. The input record is validated before any
 *          old record is discarded. This is the standard overwrite mode: data
 *          loss is intentional and the newest record is retained.
 *
 *          空间不足时，完整丢弃一条或多条最旧记录，直到新记录可以完整写入。输入记录会
 *          在丢弃旧记录前先验证。这是标准 overwrite mode：丢数据是有意行为，优先保留
 *          最新记录。失败时 queue 状态和 storage 保持不变。
 */
int qm_ring_queue_push_overwrite(qm_ring_queue_t *queue,
                                 const void *data,
                                 uint32_t size);

/**
 * @brief Read the oldest record length without consuming it. /
 *        读取最旧记录的长度但不消费记录。
 * @param[in] queue Initialized queue state. / 已初始化的队列状态。
 * @param[out] size Required payload capacity in bytes; must not overlap queue
 *                  or storage. / 所需 payload 容量，单位为字节；不得与 queue 或
 *                  storage 重叠。
 * @return QM_EOK on success; -QM_EEMPTY when the queue is empty;
 *         -QM_EINVAL for invalid arguments; -QM_EIO if the oldest record
 *         header is malformed. /
 *         成功返回 QM_EOK；队列为空返回 -QM_EEMPTY；参数无效返回 -QM_EINVAL；最旧记录头
 *         损坏返回 -QM_EIO。
 */
int qm_ring_queue_peek_size(const qm_ring_queue_t *queue, uint32_t *size);

/**
 * @brief Copy and consume the oldest record. / 复制并消费最旧记录。
 * @param[in,out] queue Initialized queue state. / 已初始化的队列状态。
 * @param[out] data Destination for the payload; any nonzero destination range
 *                  must not overlap queue, storage, or size. / payload 输出
 *                  缓冲区；任意非零输出区间不得与 queue、storage 或 size 重叠。
 * @param[in] capacity_bytes Destination capacity in bytes. When it is too
 *                            small, no payload byte is accessed and the data
 *                            range checked is this declared destination range. /
 *                            输出缓冲区容量，单位为字节。容量不足时不会访问 payload，
 *                            data 仅校验该声明的输出区间。
 * @param[out] size Actual payload length, or required length when a valid
 *                  destination is too small; must not overlap queue or storage.
 *                  / 实际 payload 长度；有效输出缓冲区容量不足时返回所需长度；不得与
 *                  queue 或 storage 重叠。
 * @return QM_EOK on success; -QM_EEMPTY when the queue is empty;
 *         -QM_EINVAL when size is NULL, data is NULL for a nonempty record,
 *         capacity_bytes is too small, or arguments are invalid; -QM_EIO if the
 *         oldest record header is malformed. A too-small destination does not
 *         consume the record. In that specific too-small-destination case,
 *         size receives the required length; other failures leave size
 *         unchanged.
 *         成功返回 QM_EOK；队列为空返回 -QM_EEMPTY；size 为空、非空记录的 data 为空、
 *         capacity_bytes 不足或参数无效返回 -QM_EINVAL；最旧记录头损坏返回 -QM_EIO。
 *         输出容量不足时不会消费记录，并将所需长度写入 size；其他失败不会修改 size。
 */
int qm_ring_queue_pop(qm_ring_queue_t *queue,
                      void *data,
                      uint32_t capacity_bytes,
                      uint32_t *size);

/**
 * @brief Consume the oldest record without copying its payload. /
 *        不复制 payload，直接消费最旧记录。
 * @param[in,out] queue Initialized queue state. / 已初始化的队列状态。
 * @return QM_EOK on success; -QM_EEMPTY when the queue is empty;
 *         -QM_EINVAL for an invalid queue; -QM_EIO if the oldest record header
 *         is malformed. /
 *         成功返回 QM_EOK；队列为空返回 -QM_EEMPTY；队列无效返回 -QM_EINVAL；最旧记录头
 *         损坏返回 -QM_EIO。
 */
int qm_ring_queue_discard(qm_ring_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* QM_RING_QUEUE_H */
