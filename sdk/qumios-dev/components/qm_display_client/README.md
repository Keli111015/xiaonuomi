# QUMI Display Client

`qm_display_client` is the master-side client for the display controller
protocol V1.0. It owns big-endian framing, CRC-16/CCITT-FALSE, stream resynchronization,
sequence matching, timeout retransmission, command encoding, response decoding,
the local state of the two logical screens, and DISPLAY_COMPLETE event handling.

The normative protocol text is [PROTOCOL.md](PROTOCOL.md). This client follows
that V1.0 document; the in-repository server remains a separate display/asset
test counterpart and is not an OTA implementation.

The name deliberately uses `client`: the QUMI host initiates every request and
the remote display controller is the server. This component is not a panel
driver and does not configure UART pins, USB endpoints, image decoders, RAM, or
Flash on the display controller.

## Architecture

```text
Application
  |  GET_FW_VERSION / DISPLAY / SCREEN_CONTROL / ASSET_* / OTA_*
  v
qm_display_client
  |  frame + sequence + retry + CRC + stream parser
  v
read/write byte-stream callbacks
  |  application-selected adapter
  v
QUMI UART HAL / USB transport  <---->  remote display controller
```

The transport callbacks are borrowed for the lifetime of the client and return
the number of bytes transferred. Reads may return any positive fragment size;
the parser does not rely on lower-layer packet boundaries. A return value of 0
means timeout and a negative value means transport failure.

The client allocates one maximum-size transmit frame and one receive frame at
creation, then reuses them for every request and asset fragment.

## Integration

Enable `CONFIG_QM_DISPLAY_CLIENT_SUPPORT`. Compile and expose:

- `components/qm_display_client/src/qm_display_client.c`
- `components/qm_display_client/include`
- `utility/crc/qm_crc.c` and `utility/crc`
- `utility/timer/qm_utils_timer.c` and `utility/timer`

Targets with explicit source lists must add these paths to their active build
file. The component Kconfig selects `QM_CRC` and `QM_TIMER` and configures the
ASSET_DATA size through `CONFIG_QM_DISPLAY_CLIENT_ASSET_CHUNK_SIZE` (default 512).

## Usage

```c
static int32_t display_write(void *user, const uint8_t *data,
                             uint32_t length, uint32_t timeout_ms)
{
    return qm_uart_write((qm_uart_dev_t *)user, data, length, timeout_ms);
}

static int32_t display_read(void *user, uint8_t *data,
                            uint32_t length, uint32_t timeout_ms)
{
    return qm_uart_read((qm_uart_dev_t *)user, data, length, timeout_ms);
}

qm_display_client_config_t config = {
    .write = display_write,
    .read = display_read,
    .user = &uart,
};
qm_display_client_handle_t client;
qm_display_fw_info_t fw;
qm_display_result_t result;

client = qm_display_client_create(&config);
if (client != NULL) {
    qm_err_t ret = qm_display_client_get_fw_version(client, &fw, &result);

    if (ret == QM_EOK && result.status == QM_DISPLAY_STATUS_OK) {
        /* fw.major, fw.minor, and fw.patch are valid. */
    }
    qm_display_client_destroy(client);
}
```

`qm_display_client_create()` returns `NULL` when its configuration is invalid or
memory allocation fails. For request APIs, a return value of `QM_EOK` means a
valid matching response was received. Always check `result.status` for the
remote outcome. Local transport, timeout, and malformed-response failures are
returned as negative QUMI errors.
For `BUSY`, the client waits for the configured fixed interval and starts a new
transaction with a new sequence. Response timeouts reuse the original sequence
and set `RETRY` as required by the protocol.

Use `qm_display_client_set_screen_power()` to enable or disable display output
without discarding the current image. Use `qm_display_client_set_brightness()`
for a 0..100 product-mapped brightness value; brightness zero does not power
the screen off.

`qm_display_client_upload_asset()` owns the complete `ASSET_BEGIN`, repeated
`ASSET_DATA`, and `ASSET_END` transaction. The caller supplies metadata and an
offset reader, which may consume a file, Flash partition, network cache, or live
download stream. Short reads are combined into the configured protocol chunk.

The complete `total_size` must be known before `ASSET_BEGIN`, for example from
an HTTP Content-Length or asset manifest. The client computes CRC-16/CCITT-FALSE
incrementally while forwarding the bytes and submits the final value in
`ASSET_END`; the complete asset does not need to be buffered or read twice. An
OK result means the new asset was validated and completed. `BUSY` retries and
bounded `OFFSET_ERROR` restarts are handled by the client.

Call `qm_display_client_poll()` from the task that owns the transport read side
while the client is idle. It acknowledges DISPLAY_COMPLETE before invoking the
configured callback. During a synchronous request, events are handled
internally. `qm_display_client_upload_ota()` streams the protocol OTA commands;
set `verify_version` and `expected_version` to confirm a lost OTA_END response.

## Boundary

The client is synchronous and permits one request at a time. Serialize access
outside the component when multiple tasks share a handle. It does not retain
asset or firmware data, automatically read files, configure the physical link,
or implement the remote controller.

The counterpart implementation and direct client/server integration test live
under `components/qm_display_server`.

The runnable UART client example is in `examples/qm_display`.
