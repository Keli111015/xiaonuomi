# QUMI Memory Utilities

Languages: [English](README.md) | [中文](README_CN.md)

This document is the single design and usage entry for the explicit memory
utilities in `utility/memory/`. The public headers are the normative reference
for exact type members and Doxygen parameter details. This component does not
change the QMOS allocation adapter or the behavior of `qm_malloc()`,
`qm_calloc()`, `qm_realloc()`, and `qm_free()`.

## 1. Scope

The component adds caller-owned allocators for memory with a known owner and
known backing storage.

| Utility | Model | Use when |
| --- | --- | --- |
| `qm_mem_heap` | Variable-size allocator over one or more caller regions | An isolated subsystem needs variable-size objects and explicit release. |
| `qm_mem_pool` | Fixed blocks with one or more size classes | Object sizes are bounded and reuse matters more than variable capacity. |
| `qm_mem_arena` | Linear allocation with rewind or bulk reset | Temporary data has one synchronous owner and a common end of life. |

`region_count == 1` gives `qm_mem_heap` heap_4-like behavior. A count greater
than one gives heap_5-like behavior. These are behavioral references only;
QUMI neither exposes FreeRTOS allocator symbols nor depends on FreeRTOS.

`qm_mem_pool` supports one or more block sizes through its class configuration.

The component does not allocate backing storage, fall back to another
allocator, compact live objects, add internal locking, or migrate existing
callers of the QMOS allocation adapter.

### 1.1 Design Basis

| Utility | Industry model | QUMI boundary |
| --- | --- | --- |
| Heap | FreeRTOS `heap_4` / `heap_5`: coalescing variable blocks in one or more regions | Caller owns the instance and regions; the Heap has no internal lock or RTOS dependency. |
| Pool | Fixed-block slab / memory-slab allocator | One API handles one or many ordered size classes; no fallback to Heap. |
| Arena | Bump-pointer allocator with bulk reset | `mark`, `rewind`, and generation checks make the owner's lifetime boundary explicit. |

## 2. Common Contract

### 2.1 Public Files

| Utility | Public header | Main public API |
| --- | --- | --- |
| Heap | [heap/qm_mem_heap.h](heap/qm_mem_heap.h) | `qm_mem_heap_init`, `alloc`, `calloc`, `realloc`, `release`, `get_stats`, `deinit` |
| Pool | [pool/qm_mem_pool.h](pool/qm_mem_pool.h) | `qm_mem_pool_init`, `alloc`, `release`, `get_stats`, `get_class_stats`, `deinit` |
| Arena | [arena/qm_mem_arena.h](arena/qm_mem_arena.h) | `qm_mem_arena_init`, `alloc`, `mark`, `rewind`, `reset`, `get_stats`, `deinit` |

Include the header for the allocator being used. General allocation APIs such
as `qm_malloc()` and `qm_free()` are declared in `qmos/qm_kernel.h` and supplied
by the platform adaptation layer; they do not depend on these utilities.

### 2.2 Storage, Alignment, and Ownership

Every instance object, metadata array, and backing buffer belongs to the
caller. An instance is a public fixed-size state struct, so it may be static
or embedded. Its fields are implementation state; after zero initialization,
the caller changes it only through the documented APIs.

Each header defines a component-local minimum payload alignment:
`QM_MEM_HEAP_ALIGNMENT`, `QM_MEM_POOL_ALIGNMENT`, and
`QM_MEM_ARENA_ALIGNMENT`. Each value comes from its corresponding compile-time
configuration and defaults to 4 bytes. All `init()` APIs accept an ordinary
writable raw byte range. Pool and Arena use their configured alignment as `A`;
Heap uses the greater of its configured value and its private block header's
native alignment. For this internal alignment `A`, each utility manages
`[align_up(raw_begin, A), align_down(raw_end, A))`. Bytes excluded at either
edge remain caller-owned and are not reported as allocator capacity. Heap may
therefore return more strictly aligned pointers and retain fewer bytes on an
ABI with stricter metadata alignment.

Payload starts and block strides follow this alignment. A 4-byte setting does
not guarantee the alignment of objects requiring 8 or more bytes; select a
value that meets the stored objects' requirements. Increasing alignment may
increase padding and reduce usable capacity for the same backing buffer.

There is no shared memory-utility header or common public alignment type.

The caller uses a normal `uint8_t[]`; no alignment union, compiler attribute,
or pre-rounded address is required. The instance object, borrowed metadata
arrays, and raw backing ranges must not overlap.

For Pool storage, use `QM_MEM_POOL_STORAGE_SIZE(block_size, block_count)`. It
reserves the raw-storage margin needed for the configured slots to remain
available after Pool normalizes the range. It is the only public Pool capacity
calculation helper.

### 2.3 Borrowing and Concurrency

| Utility | Borrowed input | Borrowing ends |
| --- | --- | --- |
| Heap | Region descriptor array and every region byte range | Successful `qm_mem_heap_deinit()` |
| Pool | Class array, class-state array, and every class storage range | Successful `qm_mem_pool_deinit()` |
| Arena | Arena byte range | Successful `qm_mem_arena_deinit()` |

The outer `qm_mem_pool_config_t` is read only by `qm_mem_pool_init()` and may
be a stack variable. Its `classes` and `class_states` members are borrowed for
the active Pool lifetime.

No API is ISR-safe. The caller serializes all Heap and Pool APIs, including
`init`, statistics, and `deinit`. An Arena has one task-context owner that
serializes allocation, marks, rewind, reset, statistics, and deinitialization.

### 2.4 State and Errors

All instances are zero-initialized before their first `init`.

An inactive instance becomes active only after a successful `init`. A failed
`init` leaves an inactive instance unchanged and reusable. Calling `init` on
an active instance returns `-QM_EBUSY`. Heap and Pool deinitialization returns
`-QM_EBUSY` while allocations remain active. Other non-allocation APIs on an
inactive instance return `-QM_EINIT`.

Heap and Pool may be initialized after a successful `deinit`. An Arena is
initialized after `deinit` without clearing its state because its retained
generation invalidates marks from the previous lifetime.

| Condition | Result |
| --- | --- |
| Bad pointer, raw range, ordering, Pool policy value, overflow, foreign pointer, double release, invalid mark, or null statistics output | `-QM_EINVAL` |
| Operation requiring an active instance | `-QM_EINIT` |
| `init` on an active instance; Heap or Pool `deinit` with active allocations | `-QM_EBUSY` |
| Arena reset or reinitialization would wrap its generation | `-QM_EFULL` |
| Successful management API | `QM_EOK` |

`alloc`, `calloc`, Pool allocation, and Arena allocation return `NULL` for a
zero-size request, inactive instance, overflow, or exhausted storage. A failed
`qm_mem_heap_realloc()` leaves the original allocation valid. Statistics expose
allocation failures for an active Heap or Pool.

## 3. Heap

### 3.1 API Surface

| Header | Public types | Operations |
| --- | --- | --- |
| [heap/qm_mem_heap.h](heap/qm_mem_heap.h) | `qm_mem_heap_t`, `qm_mem_heap_region_t`, `qm_mem_heap_stats_t` | Initialize an ordered region table; allocate, zero-allocate, resize, release, read statistics, and deinitialize. |

Each raw region is `[start, start + size)`. Initialization requires a non-null,
strictly ascending, non-overlapping raw region table. The Heap validates the raw
ownership boundary, derives an aligned inner region, and requires that retained
region to hold a first block and its sentinel. It validates without sorting or
copying the table and borrows the descriptor array until deinitialization.

The implementation writes block metadata inside each region. It uses an
address-ordered first-fit free list, splits sufficiently large blocks, and
coalesces adjacent free blocks only within one declared region. Adjacent regions
remain separate; one allocation never spans a region boundary.

`qm_mem_heap_release()` accepts only the exact payload start of an active
allocation. Provided allocator metadata has not already been corrupted by an
out-of-bounds write, it rejects interior pointers, pointers from another Heap,
and repeated releases.

Heap statistics are snapshots. `usable_bytes`, `used_bytes`, `free_bytes`, and
`minimum_free_bytes` use managed-block accounting and include internal block
headers, so `used_bytes + free_bytes == usable_bytes`. `largest_free_block` is
payload capacity and excludes its internal header; use it rather than total
`free_bytes` to decide whether a contiguous request can succeed.

The Heap has no deterministic allocation-time guarantee. A use case with a
measured bounded-latency requirement should use a Pool or a separately reviewed
allocator.

## 4. Pool

### 4.1 API Surface

| Header | Public types | Operations and capacity helper |
| --- | --- | --- |
| [pool/qm_mem_pool.h](pool/qm_mem_pool.h) | `qm_mem_pool_t`, `qm_mem_pool_config_t`, `qm_mem_pool_class_config_t`, `qm_mem_pool_class_state_t`, `qm_mem_pool_stats_t`, `qm_mem_pool_class_stats_t` | Initialize one or more ordered classes; allocate, release, read aggregate or per-class statistics, and deinitialize. `QM_MEM_POOL_STORAGE_SIZE(block_size, block_count)` sizes raw class storage. |

`classes` and `class_states` each contain at least `class_count` elements. The
component cannot verify physical array lengths, so they are caller
preconditions. Initialization validates all classes before it changes the state
array, then manages that state array for the active lifetime.

Each class has non-null raw storage, non-zero `block_size` and `block_count`,
and a strictly increasing `block_size` relative to the previous class. Raw
class ranges do not overlap. Pool normalizes every raw range at both ends and
requires the retained storage to hold every configured slot. Use
`QM_MEM_POOL_STORAGE_SIZE(block_size, block_count)` when declaring storage to
preserve all configured slots without external alignment. Extra retained bytes
are not used.

Each slot stores two `uint32_t` metadata values (8 bytes) before its payload.
The payload offset and slot stride include padding for the configured alignment:

```text
A = QM_MEM_POOL_ALIGNMENT
payload_offset = align_up(8, A)
slot_stride = align_up(payload_offset + block_size, A)
raw_storage_size = slot_stride * block_count + A - 1
```

`QM_MEM_POOL_STORAGE_SIZE()` follows this formula. Its arguments must not
overflow `size_t`; initialization also checks the capacity calculations for
overflow. `block_size` remains the usable payload size and excludes metadata
and padding.

Allocation starts with the smallest class that can hold the request.
`allow_larger == QM_FALSE` means a full selected class fails immediately.
`allow_larger == QM_TRUE` permits a search through larger fitting classes.
There is never a Heap fallback.

After class selection, allocation and release use that class's free list.
Release validates an exact active payload pointer and rejects interior, foreign,
and repeated pointers under the same metadata-integrity assumption as Heap.
`qm_mem_pool_get_class_stats()` returns `-QM_EINVAL` for an out-of-range
`class_index`; its reported `block_size` is usable payload capacity.

## 5. Arena

### 5.1 API Surface

| Header | Public types | Operations |
| --- | --- | --- |
| [arena/qm_mem_arena.h](arena/qm_mem_arena.h) | `qm_mem_arena_t`, `qm_mem_arena_mark_t`, `qm_mem_arena_stats_t` | Initialize, allocate with `qm_mem_arena_alloc(arena, size)`, mark, rewind, reset, read statistics, and deinitialize. |

Arena advances one cursor through its caller buffer. Every successful
`qm_mem_arena_alloc(arena, size)` result has
`QM_MEM_ARENA_ALIGNMENT` alignment; callers do not pass a separate alignment
value. There is no per-object release. `qm_mem_arena_reset()` makes the whole
usable buffer available again while the Arena stays active.

The raw buffer is non-null and non-zero but need not be pre-aligned. Arena
stores the aligned inner interval and reports that interval's actual capacity.
`qm_mem_arena_deinit()` ends the buffer borrow and invalidates the instance
without inspecting outstanding pointers or freeing caller storage. An inactive
Arena returns an all-zero mark.

A mark records Arena identity, generation, and offset. `rewind` accepts only a
mark from the active Arena generation at or before the current cursor. A
successful `reset` advances the generation before resetting the cursor,
invalidating every previous mark. Successful reinitialization also advances it.
Generation never wraps.

Arena lifecycle is an owner rule, not a separate manager:

```text
owner initializes Arena
  -> synchronous work allocates temporary objects
  -> data crossing an async boundary is copied to Heap, Pool, or caller storage
  -> owner resets on success, failure, and cancellation
  -> owner deinitializes before releasing the backing buffer
```

No queue item, callback, timer, or another task may retain an Arena pointer
past that owner's `rewind`, `reset`, or `deinit` boundary.

## 6. Configuration

[qm_kconfig](qm_kconfig) selects the compiled utilities and their allocation
alignment. Each alignment option is an integer and appears under its own
utility's enable option.

| Enable option | Alignment option | Default alignment | Valid alignment values (bytes) |
| --- | --- | --- | --- |
| `QM_MEM_HEAP_SUPPORT` | `QM_MEM_HEAP_ALIGNMENT` | 4 | 4, 8, 16, 32, 64 |
| `QM_MEM_POOL_SUPPORT` | `QM_MEM_POOL_ALIGNMENT` | 4 | 4, 8, 16, 32, 64 |
| `QM_MEM_ARENA_SUPPORT` | `QM_MEM_ARENA_ALIGNMENT` | 4 | 4, 8, 16, 32, 64 |

`QM_MEM_HEAP_SUPPORT`, `QM_MEM_POOL_SUPPORT`, and `QM_MEM_ARENA_SUPPORT` are
independent, disabled by default, and may all be enabled. Public
`QM_MEM_*_ALIGNMENT` macros map to the corresponding
`CONFIG_QM_MEM_*_ALIGNMENT` values in `qm_config.h`. If an older configuration
omits an alignment value, the header defaults it to 4. Zero is not an automatic
mode. When the utility is enabled, its header rejects values outside 4 through
64 or values that are not powers of two at compile time.

All instances of a utility share its alignment setting. The implementation and
its callers must use the same `qm_config.h`; do not override alignment in an
individual source file. Allocation APIs and initialization parameters do not
take a runtime alignment value.

Capacity, region count, class count, and storage are supplied through each
instance's initialization arguments. Locking and lifecycle remain caller
responsibilities. A disabled utility has no stub or fallback implementation;
callers compile their use path under its corresponding
`CONFIG_QM_MEM_*_SUPPORT` symbol.

## 7. Examples

The standalone examples are separated by utility:

| Source | Demonstrates |
| --- | --- |
| `examples/utility/memory/qm_mem_heap_example.c` | One-region Heap initialization, allocation, statistics, release, and deinitialization. |
| `examples/utility/memory/qm_mem_pool_example.c` | Two Pool classes; exhausts the small class, then demonstrates larger-class fallback, release, and deinitialization. |
| `examples/utility/memory/qm_mem_arena_example.c` | Arena initialization, mark, allocation, rewind, statistics, and deinitialization. |

Each source is a standalone application and defines `qm_application_start()`.
Build exactly one example source in an example target; do not link the three
sources into the same application. All example backing storage is an ordinary
`uint8_t[]`; the utility normalizes it during initialization.

## 8. Non-Goals

- Replacing or wrapping QMOS allocation APIs.
- Heap-to-Pool or Pool-to-Heap fallback.
- Garbage collection, reference counting, relocation, moving compaction, or a
  generic asynchronous Arena lifetime manager.
- A global backing-buffer size, global maximum region/class count, or global
  lock policy.
- Migrating application, audio, network, Agent, or driver allocation paths.
