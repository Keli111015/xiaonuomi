/**
 * @file qm_ring_queue.c
 * @brief Caller-owned variable-length record ring queue. /
 *        调用方持有存储的可变长度记录环形队列。
 */

#include "qm_ring_queue.h"
#include "qm_string.h"

static uint32_t advance_offset(uint32_t offset, uint32_t bytes,
                               uint32_t capacity)
{
    uint32_t tail_bytes = capacity - offset;

    if (bytes < tail_bytes) return offset + bytes;
    return bytes - tail_bytes;
}

static qm_bool range_end(const void *start, uintmax_t bytes, uintptr_t *end)
{
    uintptr_t begin;

    if (start == NULL || end == NULL || bytes == 0U) {
        return QM_FALSE;
    }

    begin = (uintptr_t)start;
    if (bytes > (uintmax_t)UINTPTR_MAX - (uintmax_t)begin) {
        return QM_FALSE;
    }

    *end = begin + (uintptr_t)bytes;
    return QM_TRUE;
}

static qm_bool ranges_overlap(const void *left, uintmax_t left_bytes,
                              const void *right, uintmax_t right_bytes)
{
    uintptr_t left_end;
    uintptr_t right_end;

    if (left_bytes == 0U || right_bytes == 0U) return QM_FALSE;
    if (!range_end(left, left_bytes, &left_end) ||
        !range_end(right, right_bytes, &right_end)) {
        return QM_TRUE;
    }

    return (uintptr_t)left < right_end && (uintptr_t)right < left_end;
}

static qm_bool queue_base_valid(const qm_ring_queue_t *queue)
{
    return queue != NULL && queue->storage != NULL &&
           queue->capacity_bytes >= QM_RING_QUEUE_RECORD_HEADER_BYTES &&
           !ranges_overlap(queue, sizeof(*queue), queue->storage,
                           queue->capacity_bytes);
}

static qm_bool queue_valid(const qm_ring_queue_t *queue)
{
    if (!queue_base_valid(queue) ||
        queue->read_offset >= queue->capacity_bytes ||
        queue->write_offset >= queue->capacity_bytes ||
        queue->used_bytes > queue->capacity_bytes) {
        return QM_FALSE;
    }

    return queue->write_offset ==
           advance_offset(queue->read_offset, queue->used_bytes,
                          queue->capacity_bytes);
}

static qm_bool payload_range_valid(const qm_ring_queue_t *queue,
                                   const void *data, uint32_t bytes)
{
    if (bytes == 0U) return QM_TRUE;

    return data != NULL &&
           !ranges_overlap(queue, sizeof(*queue), data, bytes) &&
           !ranges_overlap(queue->storage, queue->capacity_bytes,
                           data, bytes);
}

static qm_bool size_output_valid(const qm_ring_queue_t *queue,
                                 const uint32_t *size)
{
    return size != NULL &&
           !ranges_overlap(queue, sizeof(*queue), size, sizeof(*size)) &&
           !ranges_overlap(queue->storage, queue->capacity_bytes,
                           size, sizeof(*size));
}

/* A record can cross the physical storage end, so every header and payload
 * copy is split at most once. / 记录可跨越物理存储尾部，因此长度头和 payload 的
 * 每次复制至多拆分一次。 */
static void copy_to_storage(qm_ring_queue_t *queue, uint32_t offset,
                            const void *source, uint32_t bytes)
{
    uint32_t first_bytes;

    if (bytes == 0U) return;
    first_bytes = queue->capacity_bytes - offset;
    if (first_bytes > bytes) first_bytes = bytes;
    qm_memcpy(queue->storage + offset, source, first_bytes);
    if (bytes > first_bytes) {
        qm_memcpy(queue->storage, (const uint8_t *)source + first_bytes,
                  bytes - first_bytes);
    }
}

static void copy_from_storage(const qm_ring_queue_t *queue, uint32_t offset,
                              void *destination, uint32_t bytes)
{
    uint32_t first_bytes;

    if (bytes == 0U) return;
    first_bytes = queue->capacity_bytes - offset;
    if (first_bytes > bytes) first_bytes = bytes;
    qm_memcpy(destination, queue->storage + offset, first_bytes);
    if (bytes > first_bytes) {
        qm_memcpy((uint8_t *)destination + first_bytes, queue->storage,
                  bytes - first_bytes);
    }
}

static int record_bytes(const qm_ring_queue_t *queue, uint32_t payload_size,
                        uint32_t *bytes)
{
    if (payload_size > queue->capacity_bytes -
                       QM_RING_QUEUE_RECORD_HEADER_BYTES) {
        return -QM_EINVAL;
    }

    *bytes = QM_RING_QUEUE_RECORD_HEADER_BYTES + payload_size;
    return QM_EOK;
}

static int read_front(const qm_ring_queue_t *queue, uint32_t *payload_size,
                      uint32_t *bytes)
{
    uint32_t size;
    uint32_t total_bytes;

    if (queue->used_bytes == 0U) return -QM_EEMPTY;
    if (queue->used_bytes < QM_RING_QUEUE_RECORD_HEADER_BYTES) {
        return -QM_EIO;
    }

    copy_from_storage(queue, queue->read_offset, &size,
                      QM_RING_QUEUE_RECORD_HEADER_BYTES);
    if (size > queue->capacity_bytes - QM_RING_QUEUE_RECORD_HEADER_BYTES) {
        return -QM_EIO;
    }

    total_bytes = QM_RING_QUEUE_RECORD_HEADER_BYTES + size;
    if (total_bytes > queue->used_bytes) return -QM_EIO;

    *payload_size = size;
    *bytes = total_bytes;
    return QM_EOK;
}

static int discard_front(qm_ring_queue_t *queue)
{
    uint32_t payload_size;
    uint32_t bytes;
    int ret;

    ret = read_front(queue, &payload_size, &bytes);
    if (ret != QM_EOK) return ret;

    queue->read_offset = advance_offset(queue->read_offset, bytes,
                                        queue->capacity_bytes);
    queue->used_bytes -= bytes;
    return QM_EOK;
}

static void append_record(qm_ring_queue_t *queue, const void *data,
                          uint32_t payload_size, uint32_t bytes)
{
    uint32_t payload_offset;

    copy_to_storage(queue, queue->write_offset, &payload_size,
                    QM_RING_QUEUE_RECORD_HEADER_BYTES);
    payload_offset = advance_offset(queue->write_offset,
                                    QM_RING_QUEUE_RECORD_HEADER_BYTES,
                                    queue->capacity_bytes);
    copy_to_storage(queue, payload_offset, data, payload_size);
    queue->write_offset = advance_offset(queue->write_offset, bytes,
                                         queue->capacity_bytes);
    queue->used_bytes += bytes;
}

static int validate_segments(const qm_ring_queue_t *queue,
                             const qm_ring_queue_segment_t *segments,
                             uint32_t segment_count,
                             uint32_t *total_size)
{
    uintmax_t descriptor_bytes;
    uint32_t total = 0U;
    uint32_t index;

    if (total_size == NULL) return -QM_EINVAL;
    if (segment_count == 0U) {
        *total_size = 0U;
        return QM_EOK;
    }
    if (segments == NULL) return -QM_EINVAL;

    descriptor_bytes = (uintmax_t)segment_count *
                       (uintmax_t)sizeof(*segments);
    if (ranges_overlap(queue, sizeof(*queue), segments, descriptor_bytes) ||
        ranges_overlap(queue->storage, queue->capacity_bytes,
                       segments, descriptor_bytes)) {
        return -QM_EINVAL;
    }

    for (index = 0U; index < segment_count; ++index) {
        if (!payload_range_valid(queue, segments[index].data,
                                 segments[index].size) ||
            segments[index].size > UINT32_MAX - total) {
            return -QM_EINVAL;
        }
        total += segments[index].size;
    }

    *total_size = total;
    return QM_EOK;
}

static void append_segments(qm_ring_queue_t *queue,
                            const qm_ring_queue_segment_t *segments,
                            uint32_t segment_count,
                            uint32_t total_size,
                            uint32_t bytes)
{
    uint32_t offset = queue->write_offset;
    uint32_t index;

    copy_to_storage(queue, offset, &total_size,
                    QM_RING_QUEUE_RECORD_HEADER_BYTES);
    offset = advance_offset(offset, QM_RING_QUEUE_RECORD_HEADER_BYTES,
                            queue->capacity_bytes);
    for (index = 0U; index < segment_count; ++index) {
        copy_to_storage(queue, offset, segments[index].data,
                        segments[index].size);
        offset = advance_offset(offset, segments[index].size,
                                queue->capacity_bytes);
    }
    queue->write_offset = advance_offset(queue->write_offset, bytes,
                                         queue->capacity_bytes);
    queue->used_bytes += bytes;
}

int qm_ring_queue_init(qm_ring_queue_t *queue, void *storage,
                       uint32_t capacity_bytes)
{
    if (queue == NULL || storage == NULL ||
        capacity_bytes < QM_RING_QUEUE_RECORD_HEADER_BYTES ||
        ranges_overlap(queue, sizeof(*queue), storage, capacity_bytes)) {
        return -QM_EINVAL;
    }

    queue->storage = (uint8_t *)storage;
    queue->capacity_bytes = capacity_bytes;
    queue->read_offset = 0U;
    queue->write_offset = 0U;
    queue->used_bytes = 0U;
    return QM_EOK;
}

int qm_ring_queue_reset(qm_ring_queue_t *queue)
{
    if (!queue_base_valid(queue)) return -QM_EINVAL;

    queue->read_offset = 0U;
    queue->write_offset = 0U;
    queue->used_bytes = 0U;
    return QM_EOK;
}

int qm_ring_queue_push(qm_ring_queue_t *queue, const void *data,
                       uint32_t size)
{
    uint32_t bytes;
    int ret;

    if (!queue_valid(queue)) {
        return -QM_EINVAL;
    }
    if (!payload_range_valid(queue, data, size)) return -QM_EINVAL;

    ret = record_bytes(queue, size, &bytes);
    if (ret != QM_EOK) return ret;
    if (bytes > queue->capacity_bytes - queue->used_bytes) {
        return -QM_EFULL;
    }

    append_record(queue, data, size, bytes);
    return QM_EOK;
}

int qm_ring_queue_push_segments(qm_ring_queue_t *queue,
                                const qm_ring_queue_segment_t *segments,
                                uint32_t segment_count)
{
    uint32_t total_size;
    uint32_t bytes;
    int ret;

    if (!queue_valid(queue)) return -QM_EINVAL;
    ret = validate_segments(queue, segments, segment_count, &total_size);
    if (ret != QM_EOK) return ret;
    ret = record_bytes(queue, total_size, &bytes);
    if (ret != QM_EOK) return ret;
    if (bytes > queue->capacity_bytes - queue->used_bytes) {
        return -QM_EFULL;
    }

    append_segments(queue, segments, segment_count, total_size, bytes);
    return QM_EOK;
}

int qm_ring_queue_push_segments_overwrite(
    qm_ring_queue_t *queue,
    const qm_ring_queue_segment_t *segments,
    uint32_t segment_count)
{
    qm_ring_queue_t next;
    uint32_t total_size;
    uint32_t bytes;
    int ret;

    if (!queue_valid(queue)) return -QM_EINVAL;
    ret = validate_segments(queue, segments, segment_count, &total_size);
    if (ret != QM_EOK) return ret;
    ret = record_bytes(queue, total_size, &bytes);
    if (ret != QM_EOK) return ret;

    next = *queue;
    while (bytes > next.capacity_bytes - next.used_bytes) {
        ret = discard_front(&next);
        if (ret != QM_EOK) return ret;
    }

    append_segments(&next, segments, segment_count, total_size, bytes);
    *queue = next;
    return QM_EOK;
}

int qm_ring_queue_push_overwrite(qm_ring_queue_t *queue, const void *data,
                                 uint32_t size)
{
    qm_ring_queue_t next;
    uint32_t bytes;
    int ret;

    if (!queue_valid(queue)) {
        return -QM_EINVAL;
    }
    if (!payload_range_valid(queue, data, size)) return -QM_EINVAL;

    ret = record_bytes(queue, size, &bytes);
    if (ret != QM_EOK) return ret;

    /* Preflight eviction on a copy. This keeps an EIO from a later malformed
     * header from partially discarding the live queue. / 在副本上预检淘汰，避免
     * 后续损坏长度头导致 EIO 时只丢弃了一部分真实队列。 */
    next = *queue;
    while (bytes > next.capacity_bytes - next.used_bytes) {
        ret = discard_front(&next);
        if (ret != QM_EOK) return ret;
    }

    append_record(&next, data, size, bytes);
    *queue = next;
    return QM_EOK;
}

int qm_ring_queue_peek_size(const qm_ring_queue_t *queue, uint32_t *size)
{
    uint32_t bytes;
    int ret;

    if (!queue_valid(queue) || !size_output_valid(queue, size)) {
        return -QM_EINVAL;
    }

    ret = read_front(queue, size, &bytes);
    return ret;
}

int qm_ring_queue_pop(qm_ring_queue_t *queue, void *data,
                      uint32_t capacity_bytes, uint32_t *size)
{
    uint32_t payload_size;
    uint32_t bytes;
    uint32_t payload_offset;
    int ret;

    if (!queue_valid(queue) || !size_output_valid(queue, size)) {
        return -QM_EINVAL;
    }

    ret = read_front(queue, &payload_size, &bytes);
    if (ret != QM_EOK) return ret;

    if (payload_size != 0U && data == NULL) return -QM_EINVAL;
    if (capacity_bytes < payload_size) {
        if (!payload_range_valid(queue, data, capacity_bytes) ||
            ranges_overlap(data, capacity_bytes, size, sizeof(*size))) {
            return -QM_EINVAL;
        }
        *size = payload_size;
        return -QM_EINVAL;
    }
    if (!payload_range_valid(queue, data, payload_size) ||
        ranges_overlap(data, payload_size, size, sizeof(*size))) {
        return -QM_EINVAL;
    }

    *size = payload_size;

    payload_offset = advance_offset(queue->read_offset,
                                    QM_RING_QUEUE_RECORD_HEADER_BYTES,
                                    queue->capacity_bytes);
    copy_from_storage(queue, payload_offset, data, payload_size);
    queue->read_offset = advance_offset(queue->read_offset, bytes,
                                        queue->capacity_bytes);
    queue->used_bytes -= bytes;
    return QM_EOK;
}

int qm_ring_queue_discard(qm_ring_queue_t *queue)
{
    if (!queue_valid(queue)) return -QM_EINVAL;
    return discard_front(queue);
}
