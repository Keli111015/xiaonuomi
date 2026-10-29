# QUMI Display Server

`qm_display_server` is the screen-controller side of display protocol V1.0. It
receives an ordered byte stream, validates and dispatches requests, sends one
response per valid request, and maintains the protocol state of both logical
screens.

The authoritative protocol text is
[`../qm_display_client/PROTOCOL.md`](../qm_display_client/PROTOCOL.md). The
server deliberately references that file instead of maintaining a duplicate.

The component owns protocol behavior. Product-specific RAM, Flash, image
validation, decoding, and physical screen switching are supplied through
backend callbacks.

`qm_display_server_create()` returns the server handle directly. It returns
`NULL` when the configuration is invalid or memory allocation fails; destroy
the handle with `qm_display_server_destroy()` when it is no longer needed.

## Architecture

```text
QUMI host / qm_display_client
              |
              | framed byte stream
              v
      read/write callbacks
              |
              v
      qm_display_server
      | big-endian framing and CRC
      | validation and state
      | 32-entry / 5-second transaction cache
      v
 backend callbacks
      |-- temporary RAM / Flash writes
      |-- format validation and atomic commit
      |-- atomic one-screen or two-screen display
      `-- screen power and brightness control
```

`qm_display_server_process()` handles one valid request synchronously. Call it
from the product serial task or event loop. Corrupt frames are discarded while
the call continues searching until its timeout.

## Backend contract

- Before `asset_begin`, the server calls `asset_cancel` for replaced temporary
  tasks; `asset_begin` creates a new task from offset zero.
- `asset_data` must store a dual-screen fragment atomically. Return `OK` only
  after the data is reliable for every selected screen.
- Before `asset_end`, the server verifies the received length and incremental
  CRC. `asset_end` validates the format and performs the requested RAM or Flash
  completion.
- `asset_cancel` releases an unfinished temporary task after replacement or
  verification failure. It should be idempotent.
- `display` loads every item first and returns `OK` only after the complete
  one-screen or two-screen switch succeeds.
- `set_screen_power` must preserve the current image and assets when output is
  disabled; repeating the current state must return `OK`.
- `set_brightness` maps 0..100 to product output without changing screen power;
  repeating the current value must return `OK`.

Callback payload pointers are borrowed and valid only during the callback. The
component validates transfer identity, stop-and-wait offsets, received length,
full-asset CRC, RAM readiness, and display request structure before updating its
protocol state.

## Integration

Enable `CONFIG_QM_DISPLAY_SERVER_SUPPORT`, then add these paths to targets with
explicit build lists:

- `components/qm_display_server/src/qm_display_server.c`
- `components/qm_display_server/include`
- `utility/crc/qm_crc.c` and `utility/crc`
- `utility/timer/qm_utils_timer.c` and `utility/timer`

The Kconfig defaults retain 32 transactions for at least 5 seconds as required
by the protocol. Each live cache entry owns a copy of its request payload, so a
full cache can consume roughly 32 KiB when every entry is a maximum-size data
fragment.

## Validation

`tests/test_client_server.c` connects the real `qm_display_client` and
`qm_display_server` through an in-memory fragmented byte stream. It covers full
asset/display flow, screen power and brightness control, a dropped response and
duplicate transaction replay, and server-side asset CRC rejection.

The server does not create a task, configure UART/USB, own persistent asset
indexes, decode image formats, or directly drive a display peripheral.

The runnable UART server example is in `examples/qm_display`.
