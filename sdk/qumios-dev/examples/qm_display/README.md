# QUMI Display Protocol Examples

This directory contains application-facing examples for the two halves of the
display protocol V1.0:

The authoritative protocol text is in
[`components/qm_display_client/PROTOCOL.md`](../../components/qm_display_client/PROTOCOL.md).

- `qm_display_client_example.c` creates a master-side client over `qm_uart` and
  queries the remote firmware version.
- `qm_display_server_example.c` creates a screen-controller server over
  `qm_uart` and exposes one-request processing.

Enable `QM_DISPLAY_CLIENT_EXAMPLE` or `QM_DISPLAY_SERVER_EXAMPLE` in the target
example or application configuration, then add the selected source file and
the matching component source/include paths to targets that use explicit build
lists.

The server example intentionally leaves asset storage, format validation, and
display callbacks to product code. See `components/qm_display_server/README.md`
for the callback contract.
