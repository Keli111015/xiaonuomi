# ComTrace Edge Protocol

This component contains two independent capabilities:

- `comtrace_log.h`: CELP/1 device logs over TCP or UDP, with bounded TCP
  queueing and synchronous receive callbacks.
- `comtrace_ota.h`: CEUP/1 firmware update control through the QUMI OTA HAL.

Both share the private `2-byte big-endian length + CBOR map` framing
implementation. `comtrace_discovery.h` is the ComTrace-specific DNS-SD
adapter; generic DNS-SD parsing remains in the `mdns` component.

Applications own sockets and tasks. A send callback borrows each complete frame
only for the duration of the callback. Receive message fields and discovery
endpoints are callback-lifetime data and must be copied when retained.

```text
application
   |                         +-- comtrace_log_t -- CELP/1
   +-- shared frame helpers -+
   |                         +-- comtrace_ota_t -- CEUP/1
   +-- comtrace_discover() -> mdns (generic DNS-SD)
```

The top-level `comtrace.h` is an umbrella include. Products can enable only the
capabilities they use with `QM_COMTRACE_LOG_SUPPORT`,
`QM_COMTRACE_OTA_SUPPORT`, and `QM_COMTRACE_DISCOVERY_SUPPORT`.

## Minimal integration

The application owns the socket and calls the capability from its existing
network task. The send callback is synchronous; call `feed_tcp()` whenever
bytes arrive and call `flush()` when a previously blocked TCP send can retry.

```c
comtrace_log_t *log;
comtrace_log_config_t log_config = {
    .sn = "SN-001",
    .sid = 1,
    .transport = COMTRACE_TRANSPORT_TCP,
    .send = send_frame,
};

comtrace_log_init(&log, &log_config);
comtrace_log_register(log);
comtrace_log_write_text(log, COMTRACE_LOG_DIRECTION_EVENT,
                        COMTRACE_LOG_LEVEL_INFO, "ready", NULL, 0);
/* network receive path: comtrace_log_feed_tcp(log, buffer, length); */
```

OTA is also callback-driven. `comtrace_ota_feed()` can run in the same receive
task and directly uses the QUMI OTA HAL. By default, `update_end` reuses the
chunk buffer to read the image back and verify SHA-256 before activation, and
cancel/disconnect call `qm_ota_abort()`. No ComTrace task is created by
`comtrace_ota_init()`.

Targets with a write-only staging API may enable
`QM_COMTRACE_OTA_STREAM_VERIFY`; ComTrace then hashes every successfully
written chunk and verifies that digest at `update_end` without calling
`qm_ota_read()`. Targets without `qm_ota_abort()` may disable
`QM_COMTRACE_OTA_ABORT_SUPPORT`. On such targets any failure, cancellation, or
disconnect after `qm_ota_start()` is terminal for the current boot and the
application must reboot before accepting another transfer. Both compatibility
options are disabled by default, so existing targets keep read-back
verification and abort behavior.

OTA initialization requires an `authorize` callback. It must authenticate the
current connection or compare `sha256` with trusted release metadata; hashing
only the digest supplied by the peer does not authenticate firmware. The
`reboot` callback runs after the `rebooting` status was sent and may reboot or
schedule a reboot. With no callback, the component calls `qm_reboot()`.

```c
static int authorize_update(const comtrace_ota_request_t *request, void *user)
{
    /* Check a verified release manifest, signature result, or trusted session. */
    return release_is_trusted(request, user) ? QM_EOK : -QM_EINVAL;
}

comtrace_ota_config_t ota_config = {
    .sn = "SN-001",
    .sid = 1,
    .current_version = "1.0.0",
    .product_id = "sensor-a1",
    .max_chunk = 768,
    .max_image_size = 8U * 1024U * 1024U,
    .send = send_frame,
    .authorize = authorize_update,
};
```

The application must call `comtrace_ota_disconnect()` after a connection loss
or transfer timeout. With abort support it clears partial framing, aborts an
uncommitted image, and requires registration on the next connection. Without
abort support it reports a terminal failure and the application must reboot.
Once state reaches `rebooting`,
the image is committed and cancel, error, and disconnect cannot rewrite it as
cancelled or failed. After the new firmware boots and registers, use
`comtrace_ota_report_boot_result()` to report success or rollback.

```text
unregistered -> registering -> idle -> prepared -> transferring -> verifying
                                      |                                |
                                      +---- cancel/disconnect ---------+-> failed/cancelled
                                                                       |
                                                                       +-> rebooting -> reboot
```

Instances are not thread-safe. The application must serialize
`register/feed/disconnect/cancel/deinit` and status callbacks, typically on the
task that owns the OTA connection.
