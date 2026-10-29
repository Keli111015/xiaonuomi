# QUMI Ring Queue

Language: [English](README.md) | [中文](README_CN.md)

`utility/ringqueue/` is a caller-owned bounded FIFO for complete,
variable-length byte records. It copies a record into queue storage and copies
it out again, so it preserves record boundaries: a strict push accepts the
whole record or accepts none of it.

```text
producer buffer -- copy [length | payload] --> caller-owned ring storage
                                                   |
                                                   +-- copy --> consumer buffer
                                                   |
                              push_overwrite may evict oldest complete records
```

## Record Layout And Capacity

Each record uses this private on-storage layout:

```text
[ uint32_t payload_size ][ payload bytes ... ]
```

The length header and payload can independently wrap at the end of storage.
One record consumes `QM_RING_QUEUE_RECORD_HEADER_BYTES + payload_size` bytes.
The entire supplied capacity is usable; there is no reserved sentinel byte.
`capacity_bytes` must be at least `QM_RING_QUEUE_RECORD_HEADER_BYTES`.

A zero-length record is valid, uses one length header, and can be pushed with
`data == NULL`. It is popped with `data == NULL`, `capacity_bytes == 0`.
The internal header is an implementation detail, not a persistent or
cross-platform serialization format.

## API Contract

| API | Behavior |
| --- | --- |
| `qm_ring_queue_init` | Binds caller storage and creates an empty queue. Reinitializing a quiescent queue discards its records but does not clear storage. |
| `qm_ring_queue_reset` | Discards all records without clearing storage. It can recover malformed offsets or accounting when the queue/storage binding remains valid. |
| `qm_ring_queue_push` | Strict FIFO push. Returns `-QM_EFULL` without changing queue state or storage when the whole record does not fit. |
| `qm_ring_queue_push_segments` | Copies multiple input ranges in order as one complete record; failures leave queue and storage unchanged. |
| `qm_ring_queue_push_overwrite` | Pushes the newest record and evicts as many oldest complete records as necessary. Eviction is preflighted, so an invalid input or inspected malformed header leaves queue state and storage unchanged. |
| `qm_ring_queue_push_segments_overwrite` | Segmented overwrite push; evicts oldest complete records as needed. |
| `qm_ring_queue_peek_size` | Reports the oldest payload size without consuming it. |
| `qm_ring_queue_pop` | Copies and consumes the oldest record. A too-small valid destination returns `-QM_EINVAL`, writes the required size, and retains the record. |
| `qm_ring_queue_discard` | Consumes the oldest record without copying its payload. |

`-QM_EINVAL` covers invalid arguments, forbidden overlap, and a record larger
than an empty queue can hold. `-QM_EEMPTY` means there is no head record.
`-QM_EIO` means the operation inspected a malformed record header; it does not
claim that uninspected later records have been validated.

Except for successful reset/reinitialization and the documented required-size
write from a too-small `pop`, error paths do not mutate queue state or storage.
The implementation's all-or-nothing behavior is not a synchronization
guarantee; callers still serialize access.

### Segmented Push

When several input buffers are already available in one call, the segmented
APIs avoid building a temporary contiguous buffer:

```c
qm_ring_queue_segment_t segments[] = {
    { metadata, metadata_size },
    { payload, payload_size },
};

ret = qm_ring_queue_push_segments_overwrite(
    &queue, segments, sizeof(segments) / sizeof(segments[0]));
```

The queue validates every range, the summed payload length, and available
capacity before writing one length header and one complete payload. Segment
boundaries are not stored; consumers use the normal `peek_size()` and `pop()`
operations to read the concatenated record. Data arriving across multiple
callbacks still needs reassembly by the caller before pushing.

## Ownership And Concurrency

- The caller owns both `qm_ring_queue_t` and writable backing storage, which
  must remain valid for the queue lifetime.
- The queue control block and storage must not overlap. A nonzero input or
  output payload range, and every `size` output, must not overlap either one.
  A nonzero `pop` payload output must also not overlap its `size` output.
- Caller ownership is lifetime ownership, not permission to edit active queue
  contents: after initialization, only this API may change queue fields or
  storage bytes that contain unconsumed records. Use `reset` to abandon a
  corrupted queue.
- Source data is read only during `push` or segmented push; destination data is written only
  during `pop`. Callers may reuse those buffers after the call returns.
- The component performs no allocation, locking, waiting, callback dispatch,
  ISR synchronization, or memory-ordering operation. The caller must serialize
  every operation on a given queue and arrange any wakeup separately.

## QUMI Example

[`examples/utility/ringqueue/qm_ring_queue_example.c`](../../examples/utility/ringqueue/qm_ring_queue_example.c)
is a standalone QUMI application source. It initializes caller-owned storage,
shows a strict `push` rejecting a record with `-QM_EFULL`, then uses
`push_overwrite` and verifies the retained FIFO order with `peek_size` and
`pop`.

The source defines `qm_application_start()`. Build it as the only example
entry source in a target; do not link it with another source that defines that
entry point. The target still needs the ringqueue and QUMI string sources
listed in [Build Integration](#build-integration).

## Minimal Use

```c
#include "qm_ring_queue.h"

static uint8_t queue_storage[512];
static qm_ring_queue_t queue;
static uint8_t receive_buffer[128];

void queue_init(void)
{
    (void)qm_ring_queue_init(&queue, queue_storage, sizeof(queue_storage));
}

int queue_latest(const uint8_t *payload, uint32_t payload_size)
{
    /* The caller holds the queue's external lock, if one is needed. */
    return qm_ring_queue_push_overwrite(&queue, payload, payload_size);
}

int queue_receive(void)
{
    uint32_t required_size;
    uint32_t actual_size;
    int ret;

    ret = qm_ring_queue_peek_size(&queue, &required_size);
    if (ret != QM_EOK) return ret;
    if (required_size > sizeof(receive_buffer)) return -QM_EINVAL;

    return qm_ring_queue_pop(&queue, receive_buffer, sizeof(receive_buffer),
                             &actual_size);
}
```

## Boundary

`qm_ringbuf` is a byte stream FIFO and does not retain variable-record
boundaries. QMOS `qm_queue_t` is an OS queue with its own transport and wait
semantics. `qm_ring_queue` is intentionally neither: it is a nonblocking,
copying record container with caller-provided storage.

This component stores only bytes and their internal length prefix. It does not
carry packet metadata, borrowed pointers, ownership callbacks, or audio
lifecycle policy. Code that needs packet metadata should define its own
complete record format and keep audio-specific synchronization in the audio
layer.

## Build Integration

QUMI firmware targets enumerate utility include paths and source files
explicitly. When a target adopts this component, add `utility/ringqueue` to
its include directories and `utility/ringqueue/qm_ring_queue.c` to its source
list. The implementation uses `qm_memcpy()` from `utility/string/qm_string.c`;
the target must already link that QUMI string utility, or add its include path
and source alongside ringqueue. There is deliberately no global registration
or Kconfig switch: this standalone component does not modify an application or
`qm_audio` until a consumer chooses to use it.
