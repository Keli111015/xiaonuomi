/** @file test_ring_queue.c @brief Host regressions for variable-length records. */

#include "qm_ring_queue.h"
#include "qm_string.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Host tests provide a qm_memcpy stand-in; firmware targets link the QUMI
 * string utility. / Host 测试提供 qm_memcpy 替身；固件目标链接 QUMI string 工具。 */
void *qm_memcpy(void *destination, const void *source, uint32_t bytes)
{
    return memcpy(destination, source, bytes);
}

#define MODEL_MAX_RECORDS 16U
#define MODEL_MAX_PAYLOAD 64U

typedef struct {
    uint32_t size;
    uint8_t data[MODEL_MAX_PAYLOAD];
} model_record_t;

typedef struct {
    model_record_t records[MODEL_MAX_RECORDS];
    uint32_t count;
} model_queue_t;

static uint32_t advance_offset(uint32_t offset, uint32_t bytes,
                               uint32_t capacity)
{
    uint32_t tail_bytes = capacity - offset;

    if (bytes < tail_bytes) return offset + bytes;
    return bytes - tail_bytes;
}

static uint32_t model_used_bytes(const model_queue_t *model)
{
    uint32_t used = 0U;
    uint32_t index;

    for (index = 0U; index < model->count; ++index) {
        used += QM_RING_QUEUE_RECORD_HEADER_BYTES + model->records[index].size;
    }
    return used;
}

static void model_discard(model_queue_t *model)
{
    assert(model->count != 0U);
    if (model->count > 1U) {
        memmove(&model->records[0], &model->records[1],
                (model->count - 1U) * sizeof(model->records[0]));
    }
    model->count--;
}

static int model_push(model_queue_t *model, uint32_t capacity,
                      const uint8_t *data, uint32_t size,
                      qm_bool overwrite)
{
    uint32_t bytes;

    if (size > capacity - QM_RING_QUEUE_RECORD_HEADER_BYTES) {
        return -QM_EINVAL;
    }
    bytes = QM_RING_QUEUE_RECORD_HEADER_BYTES + size;
    if (!overwrite && bytes > capacity - model_used_bytes(model)) {
        return -QM_EFULL;
    }
    while (overwrite && bytes > capacity - model_used_bytes(model)) {
        model_discard(model);
    }

    assert(model->count < MODEL_MAX_RECORDS);
    model->records[model->count].size = size;
    if (size != 0U) {
        memcpy(model->records[model->count].data, data, size);
    }
    model->count++;
    return QM_EOK;
}

static void assert_state(const qm_ring_queue_t *queue, uint32_t capacity,
                         uint32_t read_offset, uint32_t write_offset,
                         uint32_t used_bytes)
{
    assert(queue->capacity_bytes == capacity);
    assert(queue->read_offset == read_offset);
    assert(queue->write_offset == write_offset);
    assert(queue->used_bytes == used_bytes);
    assert(queue->write_offset ==
           advance_offset(queue->read_offset, queue->used_bytes,
                          queue->capacity_bytes));
}

static void assert_model_matches(const qm_ring_queue_t *queue,
                                 const model_queue_t *model)
{
    uint32_t size = UINT32_MAX;

    assert(queue->used_bytes == model_used_bytes(model));
    assert(queue->write_offset ==
           advance_offset(queue->read_offset, queue->used_bytes,
                          queue->capacity_bytes));
    if (model->count == 0U) {
        assert(qm_ring_queue_peek_size(queue, &size) == -QM_EEMPTY);
    } else {
        assert(qm_ring_queue_peek_size(queue, &size) == QM_EOK);
        assert(size == model->records[0].size);
    }
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}

static void test_init_zero_record_and_reset(void)
{
    uint8_t storage[QM_RING_QUEUE_RECORD_HEADER_BYTES];
    qm_ring_queue_t queue = {0};
    qm_ring_queue_t before = queue;
    uint32_t size = UINT32_MAX;

    assert(qm_ring_queue_init(NULL, storage, sizeof(storage)) == -QM_EINVAL);
    assert(qm_ring_queue_init(&queue, NULL, sizeof(storage)) == -QM_EINVAL);
    assert(memcmp(&queue, &before, sizeof(queue)) == 0);
    assert(qm_ring_queue_init(&queue, storage, sizeof(storage) - 1U) ==
           -QM_EINVAL);
    assert(memcmp(&queue, &before, sizeof(queue)) == 0);
    assert(qm_ring_queue_init(&queue, &queue, sizeof(storage)) == -QM_EINVAL);
    assert(memcmp(&queue, &before, sizeof(queue)) == 0);

    assert(qm_ring_queue_init(&queue, storage, sizeof(storage)) == QM_EOK);
    assert_state(&queue, sizeof(storage), 0U, 0U, 0U);
    assert(qm_ring_queue_push(&queue, NULL, 0U) == QM_EOK);
    before = queue;
    assert(qm_ring_queue_init(&queue, storage, sizeof(storage) - 1U) ==
           -QM_EINVAL);
    assert(memcmp(&queue, &before, sizeof(queue)) == 0);
    assert_state(&queue, sizeof(storage), 0U, 0U,
                 QM_RING_QUEUE_RECORD_HEADER_BYTES);
    assert(qm_ring_queue_peek_size(&queue, &size) == QM_EOK);
    assert(size == 0U);
    assert(qm_ring_queue_pop(&queue, NULL, 0U, &size) == QM_EOK);
    assert(size == 0U);
    assert_state(&queue, sizeof(storage), 0U, 0U, 0U);

    assert(qm_ring_queue_push(&queue, NULL, 0U) == QM_EOK);
    queue.read_offset = queue.capacity_bytes;
    assert(qm_ring_queue_reset(&queue) == QM_EOK);
    assert_state(&queue, sizeof(storage), 0U, 0U, 0U);
}

static void test_fifo_full_and_short_destination(void)
{
    uint8_t storage[20] = {0};
    uint8_t payload[] = {1U, 2U, 3U, 4U};
    uint8_t full_payload[sizeof(storage) -
                         QM_RING_QUEUE_RECORD_HEADER_BYTES];
    uint8_t output[sizeof(payload)] = {0};
    qm_ring_queue_t queue = {0};
    qm_ring_queue_t before;
    uint8_t storage_before[sizeof(storage)];
    uint32_t size = UINT32_MAX;

    assert(qm_ring_queue_init(&queue, storage, sizeof(storage)) == QM_EOK);
    assert(qm_ring_queue_push(&queue, payload, sizeof(payload)) == QM_EOK);
    before = queue;
    size = UINT32_MAX;
    assert(qm_ring_queue_pop(&queue, NULL, 0U, &size) == -QM_EINVAL);
    assert(size == UINT32_MAX);
    assert(memcmp(&queue, &before, sizeof(queue)) == 0);
    before = queue;
    memcpy(storage_before, storage, sizeof(storage));
    assert(qm_ring_queue_pop(&queue, output, sizeof(output) - 1U, &size) ==
           -QM_EINVAL);
    assert(size == sizeof(payload));
    assert(memcmp(&queue, &before, sizeof(queue)) == 0);
    assert(memcmp(storage, storage_before, sizeof(storage)) == 0);
    assert(qm_ring_queue_pop(&queue, output, sizeof(output), &size) == QM_EOK);
    assert(size == sizeof(payload));
    assert(memcmp(output, payload, sizeof(payload)) == 0);

    memset(full_payload, 0xA5, sizeof(full_payload));
    assert(qm_ring_queue_reset(&queue) == QM_EOK);
    assert(qm_ring_queue_push(&queue, full_payload, sizeof(full_payload)) ==
           QM_EOK);
    assert_state(&queue, sizeof(storage), 0U, 0U, sizeof(storage));
    before = queue;
    memcpy(storage_before, storage, sizeof(storage));
    assert(qm_ring_queue_push(&queue, NULL, 0U) == -QM_EFULL);
    assert(memcmp(&queue, &before, sizeof(queue)) == 0);
    assert(memcmp(storage, storage_before, sizeof(storage)) == 0);
    assert(qm_ring_queue_push(&queue, payload, UINT32_MAX) == -QM_EINVAL);
    assert(memcmp(&queue, &before, sizeof(queue)) == 0);
    assert(memcmp(storage, storage_before, sizeof(storage)) == 0);
    assert(qm_ring_queue_discard(&queue) == QM_EOK);
    assert_state(&queue, sizeof(storage), 0U, 0U, 0U);
}

static void test_header_and_payload_wrap(void)
{
    uint8_t storage[16] = {0};
    uint8_t first[10];
    uint8_t header_wrap[] = {21U, 22U, 23U, 24U, 25U};
    uint8_t payload_wrap[] = {31U, 32U, 33U, 34U, 35U, 36U};
    uint8_t output[10] = {0};
    qm_ring_queue_t queue = {0};
    uint32_t size;
    uint32_t index;

    for (index = 0U; index < sizeof(first); ++index) first[index] = (uint8_t)index;
    assert(qm_ring_queue_init(&queue, storage, sizeof(storage)) == QM_EOK);

    assert(qm_ring_queue_push(&queue, first, sizeof(first)) == QM_EOK);
    assert(qm_ring_queue_pop(&queue, output, sizeof(output), &size) == QM_EOK);
    assert(size == sizeof(first));
    assert(memcmp(output, first, sizeof(first)) == 0);
    assert_state(&queue, sizeof(storage), 14U, 14U, 0U);

    assert(qm_ring_queue_push(&queue, header_wrap, sizeof(header_wrap)) == QM_EOK);
    assert(qm_ring_queue_pop(&queue, output, sizeof(output), &size) == QM_EOK);
    assert(size == sizeof(header_wrap));
    assert(memcmp(output, header_wrap, sizeof(header_wrap)) == 0);
    assert_state(&queue, sizeof(storage), 7U, 7U, 0U);

    assert(qm_ring_queue_push(&queue, payload_wrap, sizeof(payload_wrap)) == QM_EOK);
    assert(qm_ring_queue_pop(&queue, output, sizeof(output), &size) == QM_EOK);
    assert(size == sizeof(payload_wrap));
    assert(memcmp(output, payload_wrap, sizeof(payload_wrap)) == 0);
    assert_state(&queue, sizeof(storage), 1U, 1U, 0U);
}

static void test_overwrite_and_transactional_failures(void)
{
    uint8_t storage[30] = {0};
    uint8_t first[] = {1U, 1U, 1U, 1U};
    uint8_t second[] = {2U, 2U, 2U, 2U};
    uint8_t third[] = {3U, 3U, 3U, 3U};
    uint8_t latest[11];
    uint8_t oversized[sizeof(storage) -
                      QM_RING_QUEUE_RECORD_HEADER_BYTES + 1U] = {0};
    uint8_t transactional[19];
    uint8_t output[11] = {0};
    qm_ring_queue_t queue = {0};
    qm_ring_queue_t before;
    uint8_t storage_before[sizeof(storage)];
    uint32_t size;
    uint32_t index;

    for (index = 0U; index < sizeof(latest); ++index) latest[index] = (uint8_t)(40U + index);
    for (index = 0U; index < sizeof(transactional); ++index) {
        transactional[index] = (uint8_t)(70U + index);
    }
    assert(qm_ring_queue_init(&queue, storage, sizeof(storage)) == QM_EOK);
    assert(qm_ring_queue_push(&queue, first, sizeof(first)) == QM_EOK);
    assert(qm_ring_queue_push(&queue, second, sizeof(second)) == QM_EOK);
    assert(qm_ring_queue_push(&queue, third, sizeof(third)) == QM_EOK);
    assert(qm_ring_queue_push_overwrite(&queue, latest, sizeof(latest)) == QM_EOK);
    assert(qm_ring_queue_pop(&queue, output, sizeof(output), &size) == QM_EOK);
    assert(size == sizeof(third));
    assert(memcmp(output, third, sizeof(third)) == 0);
    assert(qm_ring_queue_pop(&queue, output, sizeof(output), &size) == QM_EOK);
    assert(size == sizeof(latest));
    assert(memcmp(output, latest, sizeof(latest)) == 0);

    assert(qm_ring_queue_reset(&queue) == QM_EOK);
    assert(qm_ring_queue_push(&queue, first, sizeof(first)) == QM_EOK);
    before = queue;
    memcpy(storage_before, storage, sizeof(storage));
    assert(qm_ring_queue_push_overwrite(&queue, NULL, 1U) == -QM_EINVAL);
    assert(memcmp(&queue, &before, sizeof(queue)) == 0);
    assert(memcmp(storage, storage_before, sizeof(storage)) == 0);
    assert(qm_ring_queue_push_overwrite(&queue, oversized,
                                        sizeof(oversized)) ==
           -QM_EINVAL);
    assert(memcmp(&queue, &before, sizeof(queue)) == 0);
    assert(memcmp(storage, storage_before, sizeof(storage)) == 0);

    assert(qm_ring_queue_reset(&queue) == QM_EOK);
    assert(qm_ring_queue_push(&queue, first, sizeof(first)) == QM_EOK);
    assert(qm_ring_queue_push(&queue, second, sizeof(second)) == QM_EOK);
    {
        uint32_t malformed_size = sizeof(storage) -
                                  QM_RING_QUEUE_RECORD_HEADER_BYTES + 1U;

        memcpy(storage + QM_RING_QUEUE_RECORD_HEADER_BYTES + sizeof(first),
               &malformed_size, sizeof(malformed_size));
    }
    before = queue;
    memcpy(storage_before, storage, sizeof(storage));
    assert(qm_ring_queue_push_overwrite(&queue, transactional,
                                        sizeof(transactional)) == -QM_EIO);
    assert(memcmp(&queue, &before, sizeof(queue)) == 0);
    assert(memcmp(storage, storage_before, sizeof(storage)) == 0);
}

static void test_segmented_push(void)
{
    uint8_t storage[20] = {0};
    uint8_t prefix[] = {1U, 2U, 3U, 4U, 5U};
    uint8_t filler[] = {6U, 7U, 8U, 9U, 10U};
    uint8_t first_part[] = {10U, 11U, 12U};
    uint8_t second_part[] = {13U, 14U, 15U, 16U, 17U};
    uint8_t expected[] = {10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U};
    uint8_t output[sizeof(expected)] = {0};
    uint8_t full_payload[sizeof(storage) -
                         QM_RING_QUEUE_RECORD_HEADER_BYTES] = {0};
    uint8_t old_record[] = {21U, 22U, 23U, 24U};
    uint8_t latest_first[] = {31U, 32U};
    uint8_t latest_second[] = {33U, 34U, 35U};
    uint8_t bad_data[] = {41U};
    qm_ring_queue_segment_t segments[] = {
        {first_part, sizeof(first_part)},
        {second_part, sizeof(second_part)},
    };
    qm_ring_queue_segment_t latest_segments[] = {
        {latest_first, sizeof(latest_first)},
        {latest_second, sizeof(latest_second)},
    };
    qm_ring_queue_segment_t bad_segments[] = {
        {NULL, sizeof(bad_data)},
    };
    qm_ring_queue_t queue = {0};
    qm_ring_queue_t before;
    uint8_t storage_before[sizeof(storage)];
    uint32_t size;

    assert(qm_ring_queue_init(&queue, storage, sizeof(storage)) == QM_EOK);
    assert(qm_ring_queue_push(&queue, prefix, sizeof(prefix)) == QM_EOK);
    assert(qm_ring_queue_pop(&queue, output, sizeof(output), &size) == QM_EOK);
    assert(qm_ring_queue_push(&queue, filler, sizeof(filler)) == QM_EOK);
    assert(qm_ring_queue_pop(&queue, output, sizeof(output), &size) == QM_EOK);

    /* The header and payload both wrap at the end of storage. */
    assert(queue.write_offset == 18U);
    assert(qm_ring_queue_push_segments(&queue, segments,
                                       sizeof(segments) / sizeof(segments[0])) ==
           QM_EOK);
    assert(qm_ring_queue_pop(&queue, output, sizeof(output), &size) == QM_EOK);
    assert(size == sizeof(expected));
    assert(memcmp(output, expected, sizeof(expected)) == 0);

    /* Zero segments are a valid zero-length record. */
    assert(qm_ring_queue_push_segments(&queue, NULL, 0U) == QM_EOK);
    assert(qm_ring_queue_pop(&queue, NULL, 0U, &size) == QM_EOK);
    assert(size == 0U);

    /* Invalid segments are rejected transactionally. */
    before = queue;
    memcpy(storage_before, storage, sizeof(storage));
    assert(qm_ring_queue_push_segments(&queue, bad_segments,
                                       sizeof(bad_segments) /
                                       sizeof(bad_segments[0])) == -QM_EINVAL);
    assert(memcmp(&queue, &before, sizeof(queue)) == 0);
    assert(memcmp(storage, storage_before, sizeof(storage)) == 0);

    /* Strict and overwrite segmented pushes retain the existing semantics. */
    assert(qm_ring_queue_reset(&queue) == QM_EOK);
    assert(qm_ring_queue_push(&queue, old_record, sizeof(old_record)) ==
           QM_EOK);
    assert(qm_ring_queue_push(&queue, old_record, sizeof(old_record)) ==
           QM_EOK);
    assert(qm_ring_queue_push_segments_overwrite(
               &queue, latest_segments,
               sizeof(latest_segments) / sizeof(latest_segments[0])) ==
           QM_EOK);
    assert(qm_ring_queue_pop(&queue, output, sizeof(output), &size) == QM_EOK);
    assert(size == sizeof(old_record));
    assert(qm_ring_queue_pop(&queue, output, sizeof(output), &size) == QM_EOK);
    assert(size == sizeof(latest_first) + sizeof(latest_second));
    assert(memcmp(output, latest_first, sizeof(latest_first)) == 0);
    assert(memcmp(output + sizeof(latest_first), latest_second,
                  sizeof(latest_second)) == 0);

    /* A complete record is still checked before strict insertion. */
    assert(qm_ring_queue_reset(&queue) == QM_EOK);
    assert(qm_ring_queue_push(&queue, full_payload, sizeof(full_payload)) ==
           QM_EOK);
    before = queue;
    assert(qm_ring_queue_push_segments(&queue, NULL, 0U) == -QM_EFULL);
    assert(memcmp(&queue, &before, sizeof(queue)) == 0);
}

static void test_corruption_and_overlap_rejection(void)
{
    union {
        uint32_t alignment;
        uint8_t bytes[32];
    } storage = {0};
    union {
        uint32_t alignment;
        uint8_t bytes[8];
    } output = {0};
    uint8_t payload[] = {8U, 7U, 6U, 5U};
    qm_ring_queue_t queue = {0};
    qm_ring_queue_t before;
    uint32_t malformed_size = 29U;
    uint32_t size = UINT32_MAX;

    assert(qm_ring_queue_init(&queue, storage.bytes, sizeof(storage.bytes)) ==
           QM_EOK);
    assert(qm_ring_queue_push(&queue, &queue, 1U) == -QM_EINVAL);
    assert(qm_ring_queue_push(&queue, storage.bytes, 1U) == -QM_EINVAL);
    assert(qm_ring_queue_push(&queue, payload, sizeof(payload)) == QM_EOK);
    before = queue;
    assert(qm_ring_queue_peek_size(&queue, (uint32_t *)storage.bytes) ==
           -QM_EINVAL);
    assert(qm_ring_queue_peek_size(&queue, &queue.used_bytes) == -QM_EINVAL);
    assert(qm_ring_queue_pop(&queue, storage.bytes, sizeof(payload), &size) ==
           -QM_EINVAL);
    assert(qm_ring_queue_pop(&queue, output.bytes, sizeof(payload),
                             (uint32_t *)output.bytes) == -QM_EINVAL);
    assert(memcmp(&queue, &before, sizeof(queue)) == 0);
    assert(qm_ring_queue_pop(&queue, output.bytes, sizeof(output.bytes), &size) ==
           QM_EOK);
    assert(memcmp(output.bytes, payload, sizeof(payload)) == 0);

    assert(qm_ring_queue_reset(&queue) == QM_EOK);
    assert(qm_ring_queue_push(&queue, payload, sizeof(payload)) == QM_EOK);
    memcpy(storage.bytes, &malformed_size, sizeof(malformed_size));
    before = queue;
    size = UINT32_MAX;
    assert(qm_ring_queue_peek_size(&queue, &size) == -QM_EIO);
    assert(size == UINT32_MAX);
    assert(qm_ring_queue_pop(&queue, output.bytes, sizeof(output.bytes), &size) ==
           -QM_EIO);
    assert(size == UINT32_MAX);
    assert(qm_ring_queue_discard(&queue) == -QM_EIO);
    assert(memcmp(&queue, &before, sizeof(queue)) == 0);

    queue.read_offset = 0U;
    queue.write_offset = 1U;
    queue.used_bytes = 1U;
    assert(qm_ring_queue_peek_size(&queue, &size) == -QM_EIO);
    assert(qm_ring_queue_reset(&queue) == QM_EOK);
}

static void test_random_reference_model(void)
{
    enum { STORAGE_BYTES = 37, ITERATIONS = 1500 };
    uint8_t storage[STORAGE_BYTES] = {0};
    uint8_t payload[MODEL_MAX_PAYLOAD];
    uint8_t output[MODEL_MAX_PAYLOAD];
    uint32_t capacity;

    for (capacity = QM_RING_QUEUE_RECORD_HEADER_BYTES;
         capacity <= sizeof(storage); ++capacity) {
        qm_ring_queue_t queue = {0};
        model_queue_t model = {0};
        uint32_t random_state = 0x12345678U ^ capacity;
        uint32_t iteration;

        memset(storage, 0, sizeof(storage));
        assert(qm_ring_queue_init(&queue, storage, capacity) == QM_EOK);
        for (iteration = 0U; iteration < ITERATIONS; ++iteration) {
            uint32_t operation = next_random(&random_state) % 6U;
            uint32_t payload_size = next_random(&random_state) % 42U;
            uint32_t index;

            for (index = 0U; index < payload_size; ++index) {
                payload[index] = (uint8_t)next_random(&random_state);
            }

            if (operation == 0U || operation == 1U) {
                qm_ring_queue_t before = queue;
                uint8_t storage_before[sizeof(storage)];
                const uint8_t *data = payload_size == 0U ? NULL : payload;
                int expected;
                int actual;

                memcpy(storage_before, storage, sizeof(storage));
                expected = model_push(&model, capacity, data, payload_size,
                                      operation == 1U ? QM_TRUE : QM_FALSE);
                actual = operation == 1U ?
                         qm_ring_queue_push_overwrite(&queue, data, payload_size) :
                         qm_ring_queue_push(&queue, data, payload_size);
                assert(actual == expected);
                if (actual != QM_EOK) {
                    assert(memcmp(&queue, &before, sizeof(queue)) == 0);
                    assert(memcmp(storage, storage_before, sizeof(storage)) == 0);
                }
            } else if (operation == 2U) {
                uint32_t size = UINT32_MAX;

                if (model.count == 0U) {
                    assert(qm_ring_queue_peek_size(&queue, &size) == -QM_EEMPTY);
                } else {
                    assert(qm_ring_queue_peek_size(&queue, &size) == QM_EOK);
                    assert(size == model.records[0].size);
                }
            } else if (operation == 3U) {
                uint32_t destination_capacity = next_random(&random_state) % 42U;
                uint32_t size = UINT32_MAX;
                void *data = model.count != 0U && model.records[0].size == 0U ?
                             NULL : output;
                int actual = qm_ring_queue_pop(&queue, data,
                                                destination_capacity, &size);

                if (model.count == 0U) {
                    assert(actual == -QM_EEMPTY);
                } else if (destination_capacity < model.records[0].size) {
                    assert(actual == -QM_EINVAL);
                    assert(size == model.records[0].size);
                } else {
                    assert(actual == QM_EOK);
                    assert(size == model.records[0].size);
                    assert(memcmp(output, model.records[0].data, size) == 0);
                    model_discard(&model);
                }
            } else if (operation == 4U) {
                int actual = qm_ring_queue_discard(&queue);

                if (model.count == 0U) {
                    assert(actual == -QM_EEMPTY);
                } else {
                    assert(actual == QM_EOK);
                    model_discard(&model);
                }
            } else {
                assert(qm_ring_queue_reset(&queue) == QM_EOK);
                model.count = 0U;
            }

            assert_model_matches(&queue, &model);
        }
    }
}

int main(void)
{
    test_init_zero_record_and_reset();
    test_fifo_full_and_short_destination();
    test_header_and_payload_wrap();
    test_overwrite_and_transactional_failures();
    test_segmented_push();
    test_corruption_and_overlap_rejection();
    test_random_reference_model();
    puts("Ring queue tests passed");
    return 0;
}
