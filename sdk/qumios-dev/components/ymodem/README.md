# YMODEM UART Firmware Update Component

[中文说明](README_CN.md)

## 1. Document purpose

This document defines the implementation for transferring a firmware image from a PC to a running ESP32/QUMI application over UART. It specifies protocol processing, OTA storage callbacks, session lifecycle, error handling, and application integration requirements.

## 2. Scope and constraints

The component is for application-level UART updates. The PC sends an ESP App image with YMODEM-CRC; the device receives the image and writes it to an OTA partition. Espressif ROM download mode and the protocol used by `esptool` are outside this scope.

The file must be an App image compatible with the target OTA partition. A bootloader, partition table, or merged image must be handled by the product's established flashing procedure and must not be sent as an ordinary App image.

The upgrade UART is exclusive to the session. The caller must stop application traffic and logs on that UART and clear its RX buffer before starting the session.

## 3. Design goals and principles

- Keep protocol processing independent of the UART and OTA platform implementations. Generic code uses QUMI abstractions.
- Write data only after block-number, complement, and CRC-16/XMODEM validation succeeds.
- Keep storage commit and transfer confirmation ordered: send the final ACK only after `finish` succeeds.
- Release an uncommitted storage session when transfer, storage, or user cancellation fails.
- Use synchronous callbacks. The caller owns callback contexts and their lifetimes.

## 4. Architecture and responsibilities

```text
PC YMODEM tool
        | UART byte stream
        v
ymodem: framing, CRC, ACK/NAK, retransmission, termination handshake
        | ymodem_config_t
        v
Application adapter: UART read/write, OTA begin/write/finish/abort
        |
        v
QUMI OTA / firmware-storage backend
```

| Module | Responsibility |
| --- | --- |
| PC YMODEM tool | Establish the sender session and send block 0, data blocks, and EOT. |
| `ymodem` | Send the CRC request; parse SOH/STX frames; validate block numbers, complements, and CRC; perform ACK/NAK, retries, and termination handshake. |
| Application adapter | Provide UART byte-stream and OTA storage callbacks and write validated data to the target OTA partition. |
| Upgrade entry | Own the UART, clear RX, call `ymodem_receive`, and decide when to reboot according to product policy. |

The protocol core does not depend directly on `qm_uart_dev_t`, ESP-IDF, or
`qm_ota_*`; the application supplies platform callbacks through
`ymodem_config_t`. A UART/OTA adapter example is provided in
`examples/ymodem_uart_example.c`.

## 5. Transfer procedure

1. The receiver sends `C` to request CRC-16/XMODEM mode.
2. It receives and validates block 0, then parses the file name and decimal file size.
3. After block 0 validation, it calls `storage.begin`; on success it sends ACK and `C`.
4. It receives SOH 128-byte or STX 1024-byte data blocks in sequence and calls `storage.write` after validation.
5. Only bytes within the advertised file size are passed to `storage.write`; protocol padding in the final block is discarded.
6. After EOT, it performs the EOT/ACK/empty-block-0 termination handshake.
7. After validating the empty block 0, it calls `storage.finish` to commit the image and sends the final ACK after success.

## 6. YMODEM protocol processing

- SOH 128-byte and STX 1024-byte frames are supported.
- The block number, its complement, and the big-endian CRC-16/XMODEM value are validated.
- Data blocks must arrive in sequence. A bad CRC, unexpected sequence, or frame timeout consumes a retry for that frame.
- A duplicate of the last accepted data block is ACKed without another `storage.write` call.
- Block numbers are 8-bit values; block 0 after block 255 is a valid data block.
- The block-0 name is treated as a basename. It must contain printable characters and must not contain `/` or `\\`; file-name and image-size limits are configurable.
- The receiver accepts senders that wait for a direct ACK after the first EOT and senders that repeat EOT before the final empty block 0.

The standard termination sequence is:

```text
EOT -> NAK -> EOT -> ACK -> C -> empty block 0 -> finish -> ACK
```

`storage.finish` is synchronous and should complete within the PC sender's
handshake timeout. If OTA verification or flash operations take longer, verify
the selected PC tool's timeout behavior on the target board.

## 7. Storage and OTA lifecycle

All four storage callbacks are required:

| Callback | Invocation | Contract |
| --- | --- | --- |
| `begin` | After block 0 validation and before the first ACK | Prepare the target and enter the write state. |
| `write` | After each valid data block | Consume only valid image bytes synchronously. |
| `finish` | After empty block 0 validation and before the final ACK | Commit the image; do not reboot in the callback. |
| `abort` | After `begin` succeeds and before `finish` succeeds | Release the uncommitted target; do not switch the boot partition. Called at most once per session. |

If `finish` succeeds, `abort` is not called even when the final UART ACK fails.
The storage adapter must make `abort` safe on write errors, timeouts, and local
cancellation.

If `begin` fails, it must release any resources acquired by that call; the
component does not call `abort` in this case. `write` returns `QM_EOK` only
after synchronously writing all `length` bytes, or a negative error on failure.

## 8. API call sequence

The public interface is defined in [include/ymodem.h](include/ymodem.h):

1. Populate `ymodem_config_t`.
2. Call `ymodem_create()`.
3. Call `ymodem_receive()` from the task that owns the upgrade UART.
4. After `QM_EOK`, `storage.finish` has committed the image; reboot according to product policy.
5. Call `ymodem_destroy()`.

Only one receive session may run on a handle at a time. The caller must
serialize `ymodem_receive`, callbacks, `ymodem_destroy`, UART access, and all
callback contexts. `ymodem_destroy` applies to an idle handle and does not
terminate an active session.

### 8.1 Data structures

| Structure | Members | Purpose and constraints |
| --- | --- | --- |
| `ymodem_file_info_t` | `name`, `total_size` | File name and advertised byte count for storage preparation and commit checks. Zero length is allowed by the protocol core; the storage backend decides whether to accept it. Metadata and name are borrowed during callbacks and must be copied if retained. |
| `ymodem_storage_t` | `begin`, `write`, `finish`, `abort`, `user` | Four required storage operations and their shared context. |
| `ymodem_progress_t` | `transferred`, `total_size` | Successfully written image bytes and advertised total bytes, for application percentage or byte-count displays. |
| `ymodem_config_t` | Transport, storage, timeouts, size limit, and progress settings | Copied at creation. Defaults are resolved in the internal copy without modifying the caller's configuration. |

`ymodem_config_t` member contracts:

| Member | Contract |
| --- | --- |
| `read`, `write`, `transport_user` | Both transport callbacks are required and share the transport context. |
| `storage` | Storage operations with their own `storage.user`. |
| `handshake_timeout_ms` | Initial and final frame wait; supported overrides are 1..`UINT32_MAX / 2` ms. |
| `packet_timeout_ms` | Data frame wait, including the start byte and remaining frame; the same override range applies. |
| `retry_max` | Maximum retries after the first attempt; nonzero overrides are 1..255. |
| `max_file_size` | Maximum image bytes; set according to storage target capacity. |
| `progress`, `progress_user` | Optional progress callback and separate application context; `progress_user` is ignored when `progress` is NULL. |

Zero selects the Kconfig default for both timeouts, retry count, and maximum
image size. `retry_max = 0` does not disable retries; `max_file_size = 0` does
not remove the size limit. Timeouts exceeding the timer range cause
`ymodem_create()` to return NULL. The three context pointers serve transport,
storage, and application notifications separately. They may be NULL; referenced
objects must remain valid until the handle is destroyed.

### 8.2 Progress callback

`ymodem_progress_fn` receives `const ymodem_progress_t *progress` and the
configured `progress_user`. It runs after each successful `storage.write` and
before that block's ACK. Duplicate blocks, failed writes, and zero-length files
produce no progress callback. The snapshot is valid only during the callback;
notifications to another task must copy its fields instead of retaining the pointer.

The application calculates a percentage in the callback with a 64-bit intermediate:

```c
uint32_t percent = 0U;

if (progress->total_size != 0U) {
    percent = (uint32_t)(((uint64_t)progress->transferred * 100U) /
                         progress->total_size);
}
```

Return `QM_EOK` to continue; any other value requests cancellation.
`transferred == total_size` indicates that data writing is complete. The update
is complete when `ymodem_receive()` returns `QM_EOK`.

## 9. UART transport requirements

- The upgrade UART is exclusive to this session. Application traffic and logs must not share the byte stream.
- `read` and `write` callbacks may perform partial transfers. A positive return value is the number of bytes processed; `0` or `-QM_ETIMEOUT` indicates timeout; another negative value indicates a transport error.
- `packet_timeout_ms` covers one complete data frame read, including the wait for its start byte. At 9600 baud with 8N1, a 1024-byte STX frame takes about 1.1 seconds; allow additional time for transmission and scheduling delays.
- After a partial-frame timeout, the component reads and discards the remaining frame bytes for one `packet_timeout_ms` window before sending NAK. The UART adapter must make the stale tail available during that window or provide an independent RX flush; without a frame boundary, a late stale tail cannot be distinguished from a control byte in a new frame.

## 10. Errors, retries, and cancellation

`ymodem_receive()` returns:

- `QM_EOK`: the image was committed by `finish`.
- `-QM_ETIMEOUT`: a protocol timeout or retry budget was exhausted.
- `-QM_EIO`: a UART transport error or malformed frame.
- `-QM_EINVAL`: invalid configuration, file name, or image size.
- `-QM_EBUSY`: another receive is active on the handle.
- `-QM_EINTR`: the sender sent CAN or the progress callback requested cancellation.
- A negative storage-callback error: propagated unchanged.

Retry accounting is per data block. On failure, the component attempts to send
eight CAN bytes. It does not send a second cancellation sequence after a remote
CAN or after `finish` has committed the image.

The component has no separate logging channel. Status is reported through return
values and the optional progress callback; application logs must not be written
to the YMODEM UART.

## 11. Configuration, build, and verification

Enable `YMODEM_SUPPORT`. It selects `QM_TIMER`, `QM_CRC`, and `QM_STRING` and
generates the following configuration values:

- `YMODEM_HANDSHAKE_TIMEOUT_MS`: block-0 and termination-handshake timeout.
- `YMODEM_PACKET_TIMEOUT_MS`: complete-frame timeout.
- `YMODEM_RETRY_MAX`: maximum retransmissions for one frame.
- `YMODEM_FILE_NAME_MAX_LEN`: maximum block-0 file-name length.
- `YMODEM_MAX_FILE_SIZE`: maximum accepted image size.

The OpenCPU source list adds `src/ymodem.c` and `utility/crc/qm_crc.c` when
`CONFIG_YMODEM_SUPPORT=y`. `examples/ymodem_uart_example.c` is application
integration code and must be added explicitly to the application source list.

Host tests cover SOH/STX, partial reads and writes, partial-frame timeout
recovery, bad CRC, duplicate blocks, block-number wraparound, tail padding,
storage failures, cancellation, termination handshakes, and the finish/abort/ACK
lifecycle. They also check progress byte counts, default configuration, the
configuration copy, and timeout ranges. Target-board verification must additionally cover the selected PC
tool's handshake, complete-frame time at the target baud rate, OTA commit and
failure cleanup, and isolation of application traffic and logs from the upgrade
UART.

## 12. References

- [Tera Term source code overview](https://teratermproject.github.io/manual/4/en/reference/sourcecode.html)
- [X/YMODEM protocol reference](https://android.googlesource.com/platform/external/syslinux/+/refs/heads/oreo-m7-release/com32/libupload/ymodem.txt)
